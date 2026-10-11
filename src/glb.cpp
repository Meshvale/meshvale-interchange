// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/glb.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#include "glb_detail.h"
#include "gltf_allocation.h"

namespace meshvale::interchange {
namespace glb_detail {
namespace {
// Khronos GLB 2 little-endian header and chunk layout.
constexpr std::uint32_t kMagic = 0x46546c67;
constexpr std::uint32_t kVersion = 2;
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
constexpr std::size_t kHeaderBytes = 3 * kWordBytes;
constexpr std::size_t kChunkHeaderBytes = 2 * kWordBytes;
constexpr std::size_t kVersionOffset = kWordBytes;
constexpr std::size_t kLengthOffset = 2 * kWordBytes;
constexpr std::size_t kChunkTypeOffset = kWordBytes;
constexpr unsigned int kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 0xff;
constexpr std::byte kJsonPadding{0x20};
constexpr std::byte kBinPadding{0};

struct ChunkRecord {
  std::uint32_t type;
  std::size_t offset;
  std::size_t size;
};
struct Block {
  void* pointer = nullptr;
  ~Block() noexcept { gltf_detail::AllocationContext::Deallocate(pointer); }
};
struct Worker {
  std::thread thread;
  std::exception_ptr error;
};
class JoinedWorkers {
 public:
  JoinedWorkers(Worker* workers, std::size_t count) noexcept
      : workers_(workers), count_(count) {}
  ~JoinedWorkers() noexcept {
    Join();
    for (std::size_t index = 0; index < count_; ++index)
      std::destroy_at(workers_ + index);
  }
  void Join() noexcept {
    for (std::size_t index = 0; index < count_; ++index)
      if (workers_[index].thread.joinable()) workers_[index].thread.join();
  }

 private:
  Worker* workers_;
  std::size_t count_;
};

std::size_t Add(std::size_t first, std::size_t second) {
  if (second > std::numeric_limits<std::size_t>::max() - first)
    throw gltf_detail::AdmissionFailure(gltf_detail::AdmissionCode::kSizeLimit);
  return first + second;
}
std::size_t Aligned(std::size_t size, std::size_t alignment) {
  const auto remainder = size % alignment;
  return remainder == 0 ? size : Add(size, alignment - remainder);
}
std::uint32_t Word(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < kWordBytes; ++index)
    value |= std::to_integer<std::uint32_t>(bytes[offset + index])
             << (kBitsPerByte * index);
  return value;
}
void PutWord(std::byte* bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < kWordBytes; ++index)
    bytes[offset + index] =
        std::byte((value >> (kBitsPerByte * index)) & kByteMask);
}
bool Options(const GlbLimits& limits, const GlbWorkPolicy& work) {
  constexpr auto kHostExtent =
      static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
  return limits.max_container_bytes != 0 && limits.max_chunks != 0 &&
         limits.max_requested_bytes != 0 && work.parallel_min_bytes != 0 &&
         work.partition_bytes != 0 &&
         limits.max_container_bytes <= kHostExtent &&
         limits.max_chunks <= kHostExtent &&
         limits.max_requested_bytes <= kHostExtent &&
         work.parallel_min_bytes <= kHostExtent &&
         work.partition_bytes <= kHostExtent;
}
bool Order(std::size_t index, std::uint32_t type) {
  if (index == 0) return type == kGlbJsonChunkType;
  if (type == kGlbJsonChunkType) return false;
  return type != kGlbBinChunkType || index == 1;
}
GlbResult Failure(GlbStatus status, std::size_t offset = 0) {
  GlbResult result;
  result.status = status;
  result.error_byte_offset = offset;
  return result;
}
GlbResult Admission(const gltf_detail::AdmissionFailure& failure) {
  using gltf_detail::AdmissionCode;
  switch (failure.Code()) {
    case AdmissionCode::kSizeLimit:
      return Failure(GlbStatus::kLimitExceeded);
    case AdmissionCode::kBudgetExceeded:
      return Failure(GlbStatus::kBudgetExceeded);
    case AdmissionCode::kCanceled:
      return Failure(GlbStatus::kCanceled);
    case AdmissionCode::kInvalidObject:
      return Failure(GlbStatus::kInvalidOptions);
  }
  std::terminate();
}

// Each worker owns disjoint wire-byte intervals. The callback never allocates;
// failures are retained per worker and rethrown only after all joins.
template <class Copy>
std::size_t CopyPartitions(std::size_t bytes, std::size_t output_request,
                           const GlbLimits& limits, const GlbWorkPolicy& work,
                           const geometry::ExecutionContext& context,
                           const gltf_detail::AllocationContext& allocation,
                           Hooks hooks, Copy copy) {
  const auto partitions = bytes / work.partition_bytes +
                          (bytes % work.partition_bytes != 0 ? 1 : 0);
  const auto desired = std::min(partitions, context.WorkerBudget());
  auto lease = bytes >= work.parallel_min_bytes && desired >= 2
                   ? geometry::TryReserveWorkers(context, desired)
                   : geometry::WorkerLease{};
  const auto count = lease.Count();
  if (count == 0) {
    for (std::size_t begin = 0; begin < bytes;) {
      allocation.Checkpoint();
      const auto size = std::min(bytes - begin, work.partition_bytes);
      copy(begin, size);
      begin += size;
    }
    allocation.Checkpoint();
    return 0;
  }
  const auto scratch_bytes = gltf_detail::CheckedProduct(count, sizeof(Worker));
  const auto scratch_request =
      gltf_detail::AllocationRequestBytes(scratch_bytes, alignof(Worker));
  if (Add(output_request, scratch_request) > limits.max_requested_bytes)
    throw gltf_detail::AdmissionFailure(gltf_detail::AdmissionCode::kSizeLimit);
  Block scratch{allocation.Allocate(scratch_bytes, alignof(Worker))};
  auto* workers = static_cast<Worker*>(scratch.pointer);
  for (std::size_t index = 0; index < count; ++index)
    std::construct_at(workers + index);
  JoinedWorkers joined(workers, count);
  for (std::size_t index = 0; index < count; ++index) {
    allocation.Checkpoint();
    if (hooks.before_launch) hooks.before_launch(index, hooks.state);
    workers[index].thread = std::thread([&, index] {
      try {
        for (auto partition = index; partition < partitions;) {
          allocation.Checkpoint();
          const auto begin = partition * work.partition_bytes;
          copy(begin, std::min(bytes - begin, work.partition_bytes));
          if (partitions - partition <= count) break;
          partition += count;
        }
      } catch (...) {
        workers[index].error = std::current_exception();
      }
    });
  }
  joined.Join();
  for (std::size_t index = 0; index < count; ++index)
    if (workers[index].error) std::rethrow_exception(workers[index].error);
  allocation.Checkpoint();
  return count;
}
}  // namespace

struct State {
  std::byte* bytes;
  ChunkRecord* chunks;
  std::size_t size;
  std::size_t count;
};
struct Access {
  static GlbContainer Adopt(State* state) noexcept {
    return GlbContainer(state);
  }
};

namespace {
struct Layout {
  std::size_t records_offset;
  std::size_t bytes_offset;
  std::size_t allocation_bytes;
  std::size_t requested_bytes;
};
Layout Storage(std::size_t bytes, std::size_t count) {
  const auto records_offset = Aligned(sizeof(State), alignof(ChunkRecord));
  const auto bytes_offset = Add(
      records_offset, gltf_detail::CheckedProduct(count, sizeof(ChunkRecord)));
  const auto allocation_bytes = Add(bytes_offset, bytes);
  return {
      records_offset, bytes_offset, allocation_bytes,
      gltf_detail::AllocationRequestBytes(allocation_bytes, alignof(State))};
}
State* Construct(void* block, const Layout& layout, std::size_t bytes,
                 std::size_t count) {
  auto* base = static_cast<std::byte*>(block);
  auto* chunks = reinterpret_cast<ChunkRecord*>(base + layout.records_offset);
  return std::construct_at(
      static_cast<State*>(block),
      State{base + layout.bytes_offset, chunks, bytes, count});
}
}  // namespace

GlbResult Read(std::span<const std::byte> bytes, const GlbLimits& limits,
               const GlbWorkPolicy& work,
               const geometry::ExecutionContext& context, std::stop_token stop,
               Hooks hooks) {
  if (!Options(limits, work)) return Failure(GlbStatus::kInvalidOptions);
  if (bytes.size() < kHeaderBytes) return Failure(GlbStatus::kMalformed);
  if (Word(bytes, 0) != kMagic) return Failure(GlbStatus::kMalformed);
  if (Word(bytes, kVersionOffset) != kVersion)
    return Failure(GlbStatus::kUnsupportedVersion, kVersionOffset);
  if (Word(bytes, kLengthOffset) != bytes.size())
    return Failure(GlbStatus::kMalformed, kLengthOffset);
  if (bytes.size() > limits.max_container_bytes)
    return Failure(GlbStatus::kLimitExceeded);
  std::size_t count = 0;
  std::size_t offset = kHeaderBytes;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < kChunkHeaderBytes)
      return Failure(GlbStatus::kMalformed, offset);
    const auto size = Word(bytes, offset);
    const auto type = Word(bytes, offset + kChunkTypeOffset);
    if (size % kWordBytes != 0 ||
        size > bytes.size() - offset - kChunkHeaderBytes || !Order(count, type))
      return Failure(GlbStatus::kMalformed, offset);
    offset += kChunkHeaderBytes + size;
    ++count;
    if (count > limits.max_chunks) return Failure(GlbStatus::kLimitExceeded);
    if (stop.stop_requested()) return Failure(GlbStatus::kCanceled);
  }
  if (count == 0) return Failure(GlbStatus::kMalformed, kHeaderBytes);
  if (bytes.size() > limits.max_container_bytes || count > limits.max_chunks)
    return Failure(GlbStatus::kLimitExceeded);
  try {
    const auto layout = Storage(bytes.size(), count);
    if (layout.requested_bytes > limits.max_requested_bytes)
      return Failure(GlbStatus::kLimitExceeded);
    gltf_detail::AllocationContext allocation(context, stop, hooks.allocation);
    Block block{allocation.Allocate(layout.allocation_bytes, alignof(State))};
    auto* state = Construct(block.pointer, layout, bytes.size(), count);
    offset = kHeaderBytes;
    for (std::size_t index = 0; index < count; ++index) {
      allocation.Checkpoint();
      const auto size = Word(bytes, offset);
      std::construct_at(state->chunks + index,
                        ChunkRecord{Word(bytes, offset + kChunkTypeOffset),
                                    offset + kChunkHeaderBytes, size});
      offset += kChunkHeaderBytes + size;
    }
    GlbResult result;
    result.workers_used = CopyPartitions(
        bytes.size(), layout.requested_bytes, limits, work, context, allocation,
        hooks, [&](std::size_t begin, std::size_t size) {
          std::memcpy(state->bytes + begin, bytes.data() + begin, size);
        });
    result.container = Access::Adopt(state);
    block.pointer = nullptr;
    result.status = GlbStatus::kSuccess;
    return result;
  } catch (const gltf_detail::AdmissionFailure& failure) {
    return Admission(failure);
  }
}

GlbResult Write(std::span<const GlbChunk> chunks, const GlbLimits& limits,
                const GlbWorkPolicy& work,
                const geometry::ExecutionContext& context, std::stop_token stop,
                Hooks hooks) {
  if (!Options(limits, work)) return Failure(GlbStatus::kInvalidOptions);
  if (chunks.empty()) return Failure(GlbStatus::kMalformed, kHeaderBytes);
  if (chunks.size() > limits.max_chunks)
    return Failure(GlbStatus::kLimitExceeded);
  try {
    std::size_t bytes = kHeaderBytes;
    for (std::size_t index = 0; index < chunks.size(); ++index) {
      const auto& chunk = chunks[index];
      if (!Order(index, chunk.type))
        return Failure(GlbStatus::kMalformed, bytes);
      const auto known =
          chunk.type == kGlbJsonChunkType || chunk.type == kGlbBinChunkType;
      if (!known && chunk.bytes.size() % kWordBytes != 0)
        return Failure(GlbStatus::kMalformed, bytes);
      bytes = Add(bytes, Add(kChunkHeaderBytes,
                             Aligned(chunk.bytes.size(), kWordBytes)));
      if (bytes > std::numeric_limits<std::uint32_t>::max() ||
          bytes > limits.max_container_bytes)
        return Failure(GlbStatus::kLimitExceeded);
      if (stop.stop_requested()) return Failure(GlbStatus::kCanceled);
    }
    if (bytes > std::numeric_limits<std::uint32_t>::max() ||
        bytes > limits.max_container_bytes || chunks.size() > limits.max_chunks)
      return Failure(GlbStatus::kLimitExceeded);
    const auto layout = Storage(bytes, chunks.size());
    if (layout.requested_bytes > limits.max_requested_bytes)
      return Failure(GlbStatus::kLimitExceeded);
    gltf_detail::AllocationContext allocation(context, stop, hooks.allocation);
    Block block{allocation.Allocate(layout.allocation_bytes, alignof(State))};
    auto* state = Construct(block.pointer, layout, bytes, chunks.size());
    PutWord(state->bytes, 0, kMagic);
    PutWord(state->bytes, kVersionOffset, kVersion);
    PutWord(state->bytes, kLengthOffset, static_cast<std::uint32_t>(bytes));
    std::size_t offset = kHeaderBytes;
    for (std::size_t index = 0; index < chunks.size(); ++index) {
      allocation.Checkpoint();
      const auto& chunk = chunks[index];
      const auto size = Aligned(chunk.bytes.size(), kWordBytes);
      PutWord(state->bytes, offset, static_cast<std::uint32_t>(size));
      PutWord(state->bytes, offset + kChunkTypeOffset, chunk.type);
      offset += kChunkHeaderBytes;
      std::construct_at(state->chunks + index,
                        ChunkRecord{chunk.type, offset, size});
      std::fill(state->bytes + offset + chunk.bytes.size(),
                state->bytes + offset + size,
                chunk.type == kGlbJsonChunkType ? kJsonPadding : kBinPadding);
      offset += size;
    }
    GlbResult result;
    result.workers_used = CopyPartitions(
        bytes, layout.requested_bytes, limits, work, context, allocation, hooks,
        [&](std::size_t begin, std::size_t size) {
          const auto end = begin + size;
          // Locate the first intersecting payload without scanning preceding
          // chunks for each partition; headers and padding are already written.
          auto* record = std::lower_bound(
              state->chunks, state->chunks + state->count, begin,
              [](const ChunkRecord& item, std::size_t boundary) {
                return item.offset + item.size <= boundary;
              });
          for (; record != state->chunks + state->count && record->offset < end;
               ++record) {
            const auto index = static_cast<std::size_t>(record - state->chunks);
            const auto first = std::max(begin, record->offset);
            const auto last =
                std::min(end, record->offset + chunks[index].bytes.size());
            if (first < last)
              std::memcpy(state->bytes + first,
                          chunks[index].bytes.data() + (first - record->offset),
                          last - first);
          }
        });
    result.container = Access::Adopt(state);
    block.pointer = nullptr;
    result.status = GlbStatus::kSuccess;
    return result;
  } catch (const gltf_detail::AdmissionFailure& failure) {
    return Admission(failure);
  }
}
}  // namespace glb_detail

GlbContainer::GlbContainer() noexcept = default;
GlbContainer::GlbContainer(glb_detail::State* state) noexcept : state_(state) {}
GlbContainer::~GlbContainer() noexcept {
  if (state_) std::destroy_at(state_);
  gltf_detail::AllocationContext::Deallocate(state_);
}
GlbContainer::GlbContainer(GlbContainer&& other) noexcept
    : state_(std::exchange(other.state_, nullptr)) {}
GlbContainer& GlbContainer::operator=(GlbContainer&& other) noexcept {
  if (this != &other) {
    if (state_) std::destroy_at(state_);
    gltf_detail::AllocationContext::Deallocate(state_);
    state_ = std::exchange(other.state_, nullptr);
  }
  return *this;
}
GlbContainer::operator bool() const noexcept { return state_ != nullptr; }
std::span<const std::byte> GlbContainer::Bytes() const noexcept {
  return state_ ? std::span<const std::byte>(state_->bytes, state_->size)
                : std::span<const std::byte>{};
}
std::size_t GlbContainer::ChunkCount() const noexcept {
  return state_ ? state_->count : 0;
}
GlbChunk GlbContainer::Chunk(std::size_t index) const {
  if (!state_ || index >= state_->count)
    throw std::out_of_range("GLB chunk index outside container");
  const auto& record = state_->chunks[index];
  return {record.type, {state_->bytes + record.offset, record.size}};
}
GlbResult ReadGlbContainer(std::span<const std::byte> bytes,
                           const GlbLimits& limits, const GlbWorkPolicy& work,
                           const geometry::ExecutionContext& context,
                           std::stop_token stop) {
  return glb_detail::Read(bytes, limits, work, context, stop, {});
}
GlbResult WriteGlbContainer(std::span<const GlbChunk> chunks,
                            const GlbLimits& limits, const GlbWorkPolicy& work,
                            const geometry::ExecutionContext& context,
                            std::stop_token stop) {
  return glb_detail::Write(chunks, limits, work, context, stop, {});
}
}  // namespace meshvale::interchange
