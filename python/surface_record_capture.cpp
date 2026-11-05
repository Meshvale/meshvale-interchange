// SPDX-License-Identifier: Apache-2.0
#include "surface_record_capture.h"

#include <Python.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace meshvale::interchange::surface_python_detail {
namespace nb = nanobind;
namespace geo = geometry;
namespace alloc = gltf_detail;
namespace {
constexpr char kSchemaKey[] = "schema";
constexpr char kPositionsKey[] = "positions";
constexpr char kFaceOffsetsKey[] = "face_offsets";
constexpr char kCornerVerticesKey[] = "corner_vertices";
constexpr char kAttributesKey[] = "attributes";
constexpr char kDomainKey[] = "domain";
constexpr char kNameKey[] = "name";
constexpr char kSemanticKey[] = "semantic";
constexpr char kSetIndexKey[] = "set_index";
constexpr char kComponentsKey[] = "components";
constexpr char kScalarTypeKey[] = "scalar_type";
constexpr char kValuesKey[] = "values";
constexpr char kOffsetsKey[] = "offsets";
constexpr char kPresentKey[] = "present";
constexpr char kMetadataKey[] = "metadata";
constexpr char kVertexDomain[] = "vertex";
constexpr char kFaceDomain[] = "face";
constexpr char kCornerDomain[] = "corner";
constexpr char kFloat32Type[] = "float32";
constexpr char kFloat64Type[] = "float64";
constexpr char kInt32Type[] = "int32";
constexpr char kUint8Type[] = "uint8";
constexpr char kUint16Type[] = "uint16";
constexpr char kUint32Type[] = "uint32";
constexpr char kUint64Type[] = "uint64";
constexpr std::size_t kMinimumCopyWorkers = 2;
constexpr std::string_view kSchema = "meshvale.mesh/1";
constexpr std::size_t kPositionComponents = 3;
constexpr Py_UCS4 kSurrogateFirst = 0xd800, kSurrogateLast = 0xdfff;
constexpr Py_UCS4 kUtf8OneByteMax = 0x7f, kUtf8TwoByteMax = 0x7ff,
                  kUtf8ThreeByteMax = 0xffff, kUnicodeMax = 0x10ffff;
constexpr unsigned kUtf8ContinuationBits = 6;
constexpr unsigned kUtf8ContinuationMask = 0x3f;
constexpr unsigned kUtf8ContinuationPrefix = 0x80;
constexpr unsigned kUtf8TwoPrefix = 0xc0, kUtf8ThreePrefix = 0xe0,
                   kUtf8FourPrefix = 0xf0;

std::uint64_t Sum(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    throw std::overflow_error("surface ingress extent overflow");
  return a + b;
}
std::size_t Host(std::uint64_t n) {
  if (n > std::numeric_limits<std::size_t>::max() ||
      n > static_cast<std::uint64_t>(
              std::numeric_limits<std::ptrdiff_t>::max()) ||
      n > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
    throw std::overflow_error("surface ingress exceeds host extent");
  return static_cast<std::size_t>(n);
}
void Positive(std::uint64_t n) {
  if (!n)
    throw std::invalid_argument("surface ingress requires positive bounds");
  (void)Host(n);
}
template <class T>
void Capacity(std::uint64_t count) {
  const auto n = Host(count);
  if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
    throw std::overflow_error("surface capacity byte extent overflow");
  const auto bytes = Host(n * sizeof(T));
  try {
    (void)Host(alloc::AllocationRequestBytes(bytes, alignof(T)));
  } catch (const alloc::AdmissionFailure& error) {
    if (error.Code() == alloc::AdmissionCode::kSizeLimit &&
        error.Provenance() == alloc::AdmissionProvenance::kUnspecified)
      throw std::overflow_error("surface capacity request extent overflow");
    throw;
  }
}
void First(SharedSurfaceIngress& ingress, RecordIngressField field,
           std::optional<std::uint64_t> row) {
  auto& out = ingress.Observation();
  if (!out.first_field) {
    out.first_field = field;
    out.first_row = row;
  }
}
[[noreturn]] void Type(SharedSurfaceIngress& ingress, RecordIngressField field,
                       std::optional<std::uint64_t> row, const char* message) {
  First(ingress, field, row);
  throw nb::type_error(message);
}
[[noreturn]] void Value(SharedSurfaceIngress& ingress, RecordIngressField field,
                        std::optional<std::uint64_t> row, const char* message) {
  First(ingress, field, row);
  throw nb::value_error(message);
}
struct ScalarFormat {
  std::size_t size;
  std::size_t alignment;
  std::string_view formats;
};
ScalarFormat Format(RecordBufferKind kind) {
  constexpr std::string_view kRawByteFormats = "Bbc";
  constexpr std::string_view kFloat32Formats = "f";
  constexpr std::string_view kFloat64Formats = "d";
  constexpr std::string_view kInt32Formats = "il";
  constexpr std::string_view kUint8Formats = "B";
  constexpr std::string_view kUint16Formats = "H";
  constexpr std::string_view kUint32Formats = "IL";
  constexpr std::string_view kUint64Formats = "QL";
  switch (kind) {
    case RecordBufferKind::kBytes:
      return {1, alignof(std::byte), kRawByteFormats};
    case RecordBufferKind::kFloat32:
      return {sizeof(float), alignof(float), kFloat32Formats};
    case RecordBufferKind::kFloat64:
      return {sizeof(double), alignof(double), kFloat64Formats};
    case RecordBufferKind::kInt32:
      return {sizeof(std::int32_t), alignof(std::int32_t), kInt32Formats};
    case RecordBufferKind::kUint8:
      return {sizeof(std::uint8_t), alignof(std::uint8_t), kUint8Formats};
    case RecordBufferKind::kUint16:
      return {sizeof(std::uint16_t), alignof(std::uint16_t), kUint16Formats};
    case RecordBufferKind::kUint32:
      return {sizeof(std::uint32_t), alignof(std::uint32_t), kUint32Formats};
    case RecordBufferKind::kUint64:
      return {sizeof(std::uint64_t), alignof(std::uint64_t), kUint64Formats};
  }
  throw std::logic_error("invalid surface scalar selector");
}
bool TextEquals(nb::handle value, const char* literal) {
  if (!PyUnicode_CheckExact(value.ptr())) return false;
  const auto comparison =
      PyUnicode_CompareWithASCIIString(value.ptr(), literal);
  if (comparison == -1 && PyErr_Occurred()) throw nb::python_error();
  return comparison == 0;
}
nb::handle Get(nb::handle dict, const char* name) {
  auto* value = PyDict_GetItemString(dict.ptr(), name);
  if (!value) {
    if (PyErr_Occurred()) throw nb::python_error();
    throw nb::value_error("record changed during capture");
  }
  return nb::handle(value);
}
void Dictionary(SharedSurfaceIngress& ingress, nb::handle value,
                std::initializer_list<const char*> fields,
                RecordIngressField field, std::optional<std::uint64_t> row) {
  if (!PyDict_Check(value.ptr()))
    Type(ingress, field, row, "record must be a dictionary");
  if (PyDict_Size(value.ptr()) != static_cast<Py_ssize_t>(fields.size()))
    Value(ingress, field, row, "record has missing or extra keys");
  Py_ssize_t pos = 0;
  PyObject *key = nullptr, *item = nullptr;
  while (PyDict_Next(value.ptr(), &pos, &key, &item)) {
    if (!PyUnicode_CheckExact(key))
      Type(ingress, field, row, "record keys must be built-in strings");
    bool found = false;
    for (const auto* expected : fields)
      found = found || TextEquals(nb::handle(key), expected);
    if (!found) Value(ingress, field, row, "record has missing or extra keys");
  }
}
std::uint32_t Uint32(SharedSurfaceIngress& ingress, nb::handle value,
                     std::uint64_t row) {
  if (!PyLong_CheckExact(value.ptr()))
    Type(ingress, RecordIngressField::kAttributeDescriptor, row,
         "record uint32 must be a built-in integer, not bool");
  const auto number = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) {
    First(ingress, RecordIngressField::kAttributeDescriptor, row);
    throw nb::python_error();
  }
  if (number > std::numeric_limits<std::uint32_t>::max()) {
    First(ingress, RecordIngressField::kAttributeDescriptor, row);
    PyErr_SetString(PyExc_OverflowError, "record integer exceeds uint32");
    throw nb::python_error();
  }
  return static_cast<std::uint32_t>(number);
}
std::size_t Utf8Width(Py_UCS4 cp) {
  if (cp <= kUtf8OneByteMax) return 1;
  if (cp <= kUtf8TwoByteMax) return 2;
  if (cp <= kUtf8ThreeByteMax) return 3;
  return 4;
}
std::size_t CountText(SharedSurfaceIngress& ingress, nb::handle value,
                      RecordIngressField field, std::uint64_t row,
                      std::stop_token stop) {
  if (!PyUnicode_CheckExact(value.ptr()))
    Type(ingress, field, row, "record text must be a built-in string");
  const auto count = PyUnicode_GetLength(value.ptr());
  if (count < 0) throw nb::python_error();
  const auto maximum = ingress.Limits().max_descriptor_bytes;
  const auto previous = ingress.Observation().descriptor_bytes;
  if (static_cast<std::uint64_t>(count) > maximum - previous)
    ingress.Refuse(RecordIngressStatus::kLimitExceeded, field, row);
  std::uint64_t bytes = 0;
  for (Py_ssize_t i = 0; i < count; ++i) {
    const auto cp = PyUnicode_ReadChar(value.ptr(), i);
    if (cp == static_cast<Py_UCS4>(-1) && PyErr_Occurred())
      throw nb::python_error();
    if ((cp >= kSurrogateFirst && cp <= kSurrogateLast) || cp > kUnicodeMax)
      Value(ingress, field, row, "record text must encode as strict UTF-8");
    const auto width = Utf8Width(cp);
    bytes = Sum(bytes, width);
    if (bytes > maximum - previous)
      ingress.Refuse(RecordIngressStatus::kLimitExceeded, field, row);
    ingress.PreflightCheckpoint(stop, width);
  }
  ingress.AdmitDescriptorBytes(bytes, field, row);
  return Host(bytes);
}
std::string_view EncodeText(SharedSurfaceIngress& ingress, nb::handle value,
                            char* arena, std::size_t& offset,
                            std::size_t expected, std::stop_token stop) {
  const auto start = offset;
  auto emit = [&](unsigned byte) {
    ingress.Checkpoint(stop);
    if (offset - start >= expected)
      throw std::logic_error("surface Unicode extent changed");
    arena[offset++] = static_cast<char>(byte);
    ++ingress.Observation().copied_descriptor_bytes;
  };
  const auto count = PyUnicode_GetLength(value.ptr());
  if (count < 0) throw nb::python_error();
  for (Py_ssize_t i = 0; i < count; ++i) {
    const auto cp = PyUnicode_ReadChar(value.ptr(), i);
    if (cp == static_cast<Py_UCS4>(-1) && PyErr_Occurred())
      throw nb::python_error();
    const auto width = Utf8Width(cp);
    if (width == 1)
      emit(cp);
    else {
      const auto prefix = width == 2   ? kUtf8TwoPrefix
                          : width == 3 ? kUtf8ThreePrefix
                                       : kUtf8FourPrefix;
      emit(prefix | (cp >> (kUtf8ContinuationBits * (width - 1))));
      for (auto remaining = width - 1; remaining != 0; --remaining)
        emit(kUtf8ContinuationPrefix |
             ((cp >> (kUtf8ContinuationBits * (remaining - 1))) &
              kUtf8ContinuationMask));
    }
  }
  if (offset - start != expected)
    throw std::logic_error("surface Unicode extent changed");
  return expected ? std::string_view(arena + start, expected)
                  : std::string_view{};
}
template <class T>
std::span<const T> Scalars(const SharedSurfaceIngress& ingress,
                           RecordBufferId id) {
  const auto bytes = ingress.Bytes(id);
  if (bytes.empty()) return {};
  if (bytes.size() % sizeof(T) ||
      reinterpret_cast<std::uintptr_t>(bytes.data()) % alignof(T))
    throw std::logic_error("invalid staged scalar extent/alignment");
  return {reinterpret_cast<const T*>(bytes.data()), bytes.size() / sizeof(T)};
}
}  // namespace

struct SharedSurfaceIngress::BufferSlot {
  Py_buffer source{};
  bool leased = false, released = false;
  RecordBufferKind kind = RecordBufferKind::kBytes;
  RecordIngressField field = RecordIngressField::kRecord;
  std::optional<std::uint64_t> row;
  std::size_t bytes = 0, elements = 0, first_scalar = 0;
  const std::byte* source_bytes = nullptr;
  void* staging = nullptr;
  ~BufferSlot() {
    if (leased) PyBuffer_Release(&source);
    alloc::AllocationContext::Deallocate(staging);
  }
  BufferSlot() = default;
  BufferSlot(const BufferSlot&) = delete;
  BufferSlot& operator=(const BufferSlot&) = delete;
  BufferSlot(BufferSlot&& other) noexcept
      : source(other.source),
        leased(std::exchange(other.leased, false)),
        released(other.released),
        kind(other.kind),
        field(other.field),
        row(other.row),
        bytes(other.bytes),
        elements(other.elements),
        first_scalar(other.first_scalar),
        source_bytes(other.source_bytes),
        staging(std::exchange(other.staging, nullptr)) {
    other.source = {};
    other.source_bytes = nullptr;
  }
};
const char* RecordIngressRefusal::what() const noexcept {
  return "surface record ingress refused";
}
SharedSurfaceIngress::SharedSurfaceIngress(
    const RecordIngressLimits& limits, const RecordIngressWork& work,
    const geo::ExecutionContext& context,
    const alloc::AllocationContext& bridge, RecordIngressObservation& reached)
    : limits_(limits),
      work_(work),
      context_(context),
      bridge_(bridge),
      reached_(reached),
      buffers_(alloc::Allocator<BufferSlot>(bridge)) {
  for (auto value :
       {limits.max_buffer_views, limits.max_buffer_bytes,
        limits.max_total_buffer_bytes, limits.max_attributes,
        limits.max_metadata_entries, limits.max_descriptor_bytes,
        work.cancellation_block_bytes, work.cancellation_block_items})
    Positive(value);
  if (!bridge.HasRequestLimit())
    throw std::invalid_argument(
        "surface bridge must have a complete request cap");
  Capacity<BufferSlot>(limits.max_buffer_views);
  buffers_.resize(Host(limits.max_buffer_views));
}
SharedSurfaceIngress::~SharedSurfaceIngress() {
  // The observation outlives this owner. Partial staging may never reach the
  // state admitted by ReleaseBufferStorage; release its actual live payload
  // here as well, without changing the cumulative allocation/copy facts.
  for (auto& slot : buffers_) {
    if (auto* storage = std::exchange(slot.staging, nullptr)) {
      alloc::AllocationContext::Deallocate(storage);
      reached_.active_staging_payload_bytes -= slot.bytes;
    }
  }
}
const alloc::AllocationContext& SharedSurfaceIngress::Bridge() const noexcept {
  return bridge_;
}
const RecordIngressLimits& SharedSurfaceIngress::Limits() const noexcept {
  return limits_;
}
RecordIngressObservation& SharedSurfaceIngress::Observation() noexcept {
  return reached_;
}
[[noreturn]] void SharedSurfaceIngress::Refuse(
    RecordIngressStatus status, RecordIngressField field,
    std::optional<std::uint64_t> row) {
  First(*this, field, row);
  reached_.status = status;
  throw RecordIngressRefusal();
}
bool SharedSurfaceIngress::ClassifyAllocation(
    const alloc::AdmissionFailure& error, RecordIngressField field,
    std::optional<std::uint64_t> row) {
  if (error.Code() == alloc::AdmissionCode::kBudgetExceeded)
    reached_.status = RecordIngressStatus::kBudgetExceeded;
  else if (error.Code() == alloc::AdmissionCode::kCanceled)
    reached_.status = RecordIngressStatus::kCanceled;
  else if (error.Code() == alloc::AdmissionCode::kSizeLimit &&
           error.Provenance() ==
               alloc::AdmissionProvenance::kOperationRequestLimit)
    reached_.status = RecordIngressStatus::kLimitExceeded;
  else
    return false;
  reached_.allocation_code = error.Code();
  reached_.allocation_provenance = error.Provenance();
  First(*this, field, row);
  return true;
}
void SharedSurfaceIngress::PreflightCheckpoint(std::stop_token stop,
                                               std::uint64_t bytes) {
  ++preflight_items_;
  preflight_bytes_ = Sum(preflight_bytes_, bytes);
  if (preflight_items_ >= work_.cancellation_block_items) {
    ++reached_.item_checkpoints;
    reached_.preflight_stop_observed |= stop.stop_requested();
    preflight_items_ = 0;
  }
  if (preflight_bytes_ >= work_.cancellation_block_bytes) {
    ++reached_.byte_checkpoints;
    reached_.preflight_stop_observed |= stop.stop_requested();
    preflight_bytes_ = 0;
  }
}
void SharedSurfaceIngress::LinkStopToken(std::stop_token stop) {
  if (staging_started_)
    throw std::logic_error("surface stop linkage must precede staging");
  bridge_ = bridge_.WithStopToken(stop);
}
void SharedSurfaceIngress::Checkpoint(std::stop_token stop) {
  ++reached_.byte_checkpoints;
  if (stop.stop_requested())
    Refuse(RecordIngressStatus::kCanceled, RecordIngressField::kWorkspace);
}
void SharedSurfaceIngress::AdmitAttributes(std::uint64_t count) {
  if (count > limits_.max_attributes - reached_.attributes)
    Refuse(RecordIngressStatus::kLimitExceeded,
           RecordIngressField::kAttributes);
  reached_.attributes += count;
}
void SharedSurfaceIngress::AdmitMetadata(std::uint64_t count,
                                         std::uint64_t row) {
  if (count > limits_.max_metadata_entries - reached_.metadata_entries)
    Refuse(RecordIngressStatus::kLimitExceeded, RecordIngressField::kMetadata,
           row);
  reached_.metadata_entries += count;
}
void SharedSurfaceIngress::AdmitDescriptorBytes(
    std::uint64_t count, RecordIngressField field,
    std::optional<std::uint64_t> row) {
  if (count > limits_.max_descriptor_bytes - reached_.descriptor_bytes)
    Refuse(RecordIngressStatus::kLimitExceeded, field, row);
  reached_.descriptor_bytes += count;
}
RecordBufferId SharedSurfaceIngress::Acquire(nb::handle value,
                                             RecordBufferKind kind,
                                             RecordIngressField field,
                                             std::optional<std::uint64_t> row,
                                             std::stop_token stop) {
  if (staging_started_)
    throw std::logic_error("surface buffer acquisition after staging");
  if (count_ == buffers_.size())
    Refuse(RecordIngressStatus::kLimitExceeded, field, row);
  auto& slot = buffers_[count_];
  slot.kind = kind;
  slot.field = field;
  slot.row = row;
  ++reached_.attempted_buffers;
  if (PyObject_GetBuffer(value.ptr(), &slot.source,
                         PyBUF_FORMAT | PyBUF_STRIDES) != 0) {
    First(*this, field, row);
    throw nb::python_error();
  }
  slot.leased = true;
  RecordBufferId id;
  id.owner_ = this;
  id.ordinal_ = count_++;
  ++reached_.buffer_views;
  const auto& b = slot.source;
  const auto format = Format(kind);
  constexpr char kCContiguousOrder = 'C';
  constexpr char kNativeAlignedPrefix = '@';
  constexpr char kNativeStandardPrefix = '=';
  if (b.ndim != 1 || b.suboffsets || !b.shape || b.shape[0] < 0 || b.len < 0 ||
      b.itemsize != static_cast<Py_ssize_t>(format.size) ||
      !PyBuffer_IsContiguous(&b, kCContiguousOrder))
    Value(*this, field, row,
          "buffer must be flat direct contiguous native scalar storage");
  const char* spelling = b.format;
  if (!spelling) Value(*this, field, row, "buffer requires a scalar format");
  if (*spelling == kNativeAlignedPrefix || *spelling == kNativeStandardPrefix)
    ++spelling;
  if (!*spelling || spelling[1] ||
      format.formats.find(*spelling) == std::string_view::npos)
    Value(*this, field, row, "buffer has wrong native scalar format");
  const auto elements = static_cast<std::uint64_t>(b.shape[0]);
  slot.elements = Host(elements);
  if (slot.elements > std::numeric_limits<std::size_t>::max() / format.size)
    throw std::overflow_error("surface buffer byte extent overflow");
  slot.bytes = Host(slot.elements * format.size);
  if (slot.bytes != static_cast<std::uint64_t>(b.len) ||
      (slot.elements && b.strides && b.strides[0] != b.itemsize) ||
      (slot.bytes && !b.buf))
    Value(*this, field, row, "buffer shape/stride/byte extent mismatch");
  (void)Host(slot.bytes);
  if (slot.bytes > limits_.max_buffer_bytes ||
      slot.bytes > limits_.max_total_buffer_bytes - reached_.buffer_bytes)
    Refuse(RecordIngressStatus::kLimitExceeded, field, row);
  const auto address = reinterpret_cast<std::uintptr_t>(b.buf);
  if (slot.bytes > std::numeric_limits<std::uintptr_t>::max() - address)
    Value(*this, field, row, "buffer address extent overflow");
  reached_.buffer_bytes += slot.bytes;
  slot.source_bytes = static_cast<const std::byte*>(b.buf);
  PreflightCheckpoint(stop);
  return id;
}
std::size_t SharedSurfaceIngress::Elements(RecordBufferId id) const {
  if (id.owner_ != this || id.ordinal_ >= count_)
    throw std::logic_error("invalid surface buffer owner");
  return buffers_[id.ordinal_].elements;
}
std::span<const std::byte> SharedSurfaceIngress::Bytes(
    RecordBufferId id) const {
  if (!staged_ || id.owner_ != this || id.ordinal_ >= count_ ||
      buffers_[id.ordinal_].released)
    throw std::logic_error("surface buffer is not captured");
  const auto& b = buffers_[id.ordinal_];
  return {static_cast<const std::byte*>(b.staging), b.bytes};
}
void SharedSurfaceIngress::ReleasePythonSources() {
  for (std::size_t i = 0; i < count_; ++i) {
    auto& slot = buffers_[i];
    if (slot.leased) {
      PyBuffer_Release(&slot.source);
      slot.leased = false;
      slot.source = {};
      slot.source_bytes = nullptr;
    }
  }
}
void SharedSurfaceIngress::ReleaseBufferStorage(RecordBufferId id) {
  if (!staged_ || id.owner_ != this || id.ordinal_ >= count_)
    throw std::logic_error("invalid surface buffer release");
  auto& slot = buffers_[id.ordinal_];
  if (slot.released) throw std::logic_error("surface buffer already released");
  alloc::AllocationContext::Deallocate(slot.staging);
  slot.staging = nullptr;
  slot.released = true;
  reached_.active_staging_payload_bytes -= slot.bytes;
}
void SharedSurfaceIngress::Complete() {
  if (!staged_ || reached_.status)
    throw std::logic_error("surface ingress cannot complete");
  reached_.status = RecordIngressStatus::kCaptured;
}

void SharedSurfaceIngress::StageBuffers(
    const geo::MeshMaterializationWorkPolicy& work, std::stop_token stop,
    const alloc::NativeExecutionOptions& execution) {
  if (staging_started_) throw std::logic_error("surface staging is single-use");
  staging_started_ = true;
  for (auto n : {work.parallel_min_bytes, work.partition_bytes,
                 work.cancellation_block_scalars, work.max_jobs})
    Positive(n);
  struct Job {
    std::size_t first = 0, last = 0;
    std::uint64_t copied = 0, checks = 0;
    bool canceled = false;
    std::exception_ptr failure;
  };
  std::vector<Job, alloc::Allocator<Job>> jobs{alloc::Allocator<Job>(bridge_)};
  std::size_t total_scalars = 0, maximum_width = 1;
  auto field = RecordIngressField::kWorkspace;
  std::optional<std::uint64_t> row;
  try {
    Checkpoint(stop);
    for (std::size_t i = 0; i < count_; ++i) {
      auto& b = buffers_[i];
      field = b.field;
      row = b.row;
      Checkpoint(stop);
      const auto f = Format(b.kind);
      b.first_scalar = total_scalars;
      total_scalars = Host(Sum(total_scalars, b.elements));
      maximum_width = std::max(maximum_width, f.size);
      if (!b.bytes) continue;
      const auto request = alloc::AllocationRequestBytes(b.bytes, f.alignment);
      (void)Host(request);
      b.staging = bridge_.Allocate(b.bytes, f.alignment);
      reached_.allocated_staging_payload_bytes =
          Sum(reached_.allocated_staging_payload_bytes, b.bytes);
      reached_.allocated_staging_request_bytes =
          Sum(reached_.allocated_staging_request_bytes, request);
      reached_.active_staging_payload_bytes =
          Sum(reached_.active_staging_payload_bytes, b.bytes);
      Checkpoint(stop);
    }
    field = RecordIngressField::kWorkspace;
    row.reset();
    if (!total_scalars) {
      staged_ = true;
      return;
    }
    const auto per_job =
        std::max<std::size_t>(1, Host(work.partition_bytes) / maximum_width);
    auto count = total_scalars / per_job + (total_scalars % per_job != 0);
    count = std::min(count, Host(work.max_jobs));
    auto workers = reached_.buffer_bytes >= work.parallel_min_bytes &&
                           count >= kMinimumCopyWorkers
                       ? geo::TryReserveWorkers(context_, count)
                       : geo::WorkerLease();
    if (workers.Count() == 0) count = 1;
    jobs.resize(count);
    const auto quotient = total_scalars / count,
               remainder = total_scalars % count;
    for (std::size_t i = 0; i < count; ++i) {
      jobs[i].first = i * quotient + std::min(i, remainder);
      jobs[i].last = jobs[i].first + quotient + (i < remainder ? 1 : 0);
    }
    struct Copy {
      std::span<BufferSlot> buffers;
      std::span<Job> jobs;
      std::size_t block_bytes;
      std::stop_token stop;
    } copy{std::span<BufferSlot>(buffers_.data(), count_), jobs,
           Host(work_.cancellation_block_bytes), stop};
    const auto operation = [](void* opaque, std::size_t ordinal) noexcept {
      auto& state = *static_cast<Copy*>(opaque);
      auto& job = state.jobs[ordinal];
      try {
        for (const auto& b : state.buffers) {
          const auto end = b.first_scalar + b.elements;
          const auto first = std::max(job.first, b.first_scalar);
          const auto last = std::min(job.last, end);
          if (first >= last) continue;
          const auto width = Format(b.kind).size;
          auto offset = (first - b.first_scalar) * width;
          auto bytes = (last - first) * width;
          const auto* source = b.source_bytes;
          auto* target = static_cast<std::byte*>(b.staging);
          while (bytes) {
            ++job.checks;
            if (state.stop.stop_requested()) {
              job.canceled = true;
              return;
            }
            const auto step = std::min(bytes, state.block_bytes);
            std::memcpy(target + offset, source + offset, step);
            job.copied += step;
            offset += step;
            bytes -= step;
          }
        }
      } catch (...) {
        job.failure = std::current_exception();
      }
    };
    reached_.planned_jobs = Sum(reached_.planned_jobs, count);
    alloc::OperationWorkerStats stats;
    std::exception_ptr launch_failure;
    {
      nb::gil_scoped_release release;
      try {
        if (workers.Count())
          alloc::RunOperationJobs(bridge_, workers, count, operation, &copy,
                                  execution, stats);
        else
          operation(&copy, 0);
      } catch (...) {
        launch_failure = std::current_exception();
      }
    }
    // Actual copy and join facts survive a partial-launch exception.
    reached_.launched_workers = Sum(reached_.launched_workers, stats.launched);
    reached_.joined_workers = Sum(reached_.joined_workers, stats.joined);
    if (!workers.Count()) ++reached_.caller_jobs;
    for (const auto& job : jobs) {
      reached_.copied_buffer_bytes =
          Sum(reached_.copied_buffer_bytes, job.copied);
      reached_.byte_checkpoints = Sum(reached_.byte_checkpoints, job.checks);
    }
    if (launch_failure) std::rethrow_exception(launch_failure);
    for (const auto& job : jobs)
      if (job.failure) std::rethrow_exception(job.failure);
    for (const auto& job : jobs)
      if (job.canceled) Refuse(RecordIngressStatus::kCanceled, field);
    Checkpoint(stop);
    staged_ = true;
  } catch (const alloc::AdmissionFailure& error) {
    if (ClassifyAllocation(error, field, row)) throw RecordIngressRefusal();
    throw;
  }
}

struct SurfaceRecordCapture::AttributeRow {
  nb::object source, name, semantic;
  geo::AttributeDomain domain = geo::AttributeDomain::vertex;
  std::optional<std::uint32_t> set;
  std::uint32_t components = 0;
  RecordBufferKind kind = RecordBufferKind::kFloat32;
  RecordBufferId values;
  std::optional<RecordBufferId> offsets, present;
  std::size_t name_bytes = 0, semantic_bytes = 0, metadata_first = 0,
              metadata_count = 0;
};
struct SurfaceRecordCapture::MetadataRow {
  nb::object key, value;
  std::size_t key_bytes = 0, value_bytes = 0;
};
SurfaceRecordCapture::SurfaceRecordCapture(SharedSurfaceIngress& ingress)
    : ingress_(ingress),
      attributes_(alloc::Allocator<AttributeRow>(ingress.Bridge())),
      metadata_(alloc::Allocator<MetadataRow>(ingress.Bridge())),
      views_(alloc::Allocator<geo::AttributeMaterializationInput>(
          ingress.Bridge())),
      metadata_views_(
          alloc::Allocator<geo::MetadataEntryView>(ingress.Bridge())) {
  const auto attributes = Host(ingress.Limits().max_attributes);
  const auto metadata = Host(ingress.Limits().max_metadata_entries);
  Capacity<AttributeRow>(attributes);
  Capacity<geo::AttributeMaterializationInput>(attributes);
  Capacity<MetadataRow>(metadata);
  Capacity<geo::MetadataEntryView>(metadata);
  attributes_.resize(attributes);
  views_.resize(attributes);
  metadata_.resize(metadata);
  metadata_views_.resize(metadata);
}
SurfaceRecordCapture::~SurfaceRecordCapture() {
  alloc::AllocationContext::Deallocate(text_);
}
void SurfaceRecordCapture::Preflight(nb::handle record, std::stop_token stop) {
  if (started_)
    throw std::logic_error("surface record preflight is single-use");
  started_ = true;
  Dictionary(ingress_, record,
             {kSchemaKey, kPositionsKey, kFaceOffsetsKey, kCornerVerticesKey,
              kAttributesKey},
             RecordIngressField::kRecord, {});
  if (!PyUnicode_CheckExact(Get(record, kSchemaKey).ptr()))
    Type(ingress_, RecordIngressField::kRecord, {},
         "mesh schema must be a built-in string");
  if (!TextEquals(Get(record, kSchemaKey), kSchema.data()))
    Value(ingress_, RecordIngressField::kRecord, {},
          "unsupported mesh record schema");
  source_ = nb::borrow<nb::object>(record);
  const auto rows = nb::borrow<nb::object>(Get(record, kAttributesKey));
  if (!PyList_Check(rows.ptr()))
    Type(ingress_, RecordIngressField::kAttributes, {},
         "attributes must be a list");
  const auto requested_attributes =
      static_cast<std::size_t>(PyList_Size(rows.ptr()));
  ingress_.AdmitAttributes(requested_attributes);
  attribute_count_ = requested_attributes;
  positions_ =
      ingress_.Acquire(Get(record, kPositionsKey), RecordBufferKind::kFloat64,
                       RecordIngressField::kPositions, {}, stop);
  if (ingress_.Elements(positions_) % kPositionComponents)
    Value(ingress_, RecordIngressField::kPositions, {},
          "positions must contain complete xyz triples");
  face_offsets_ =
      ingress_.Acquire(Get(record, kFaceOffsetsKey), RecordBufferKind::kUint64,
                       RecordIngressField::kFaceOffsets, {}, stop);
  corner_vertices_ = ingress_.Acquire(
      Get(record, kCornerVerticesKey), RecordBufferKind::kUint64,
      RecordIngressField::kCornerVertices, {}, stop);
  for (std::size_t i = 0; i < attribute_count_; ++i) {
    auto& row = attributes_[i];
    row.source = nb::borrow<nb::object>(
        PyList_GetItem(rows.ptr(), static_cast<Py_ssize_t>(i)));
    Dictionary(
        ingress_, row.source,
        {kDomainKey, kNameKey, kSemanticKey, kSetIndexKey, kComponentsKey,
         kScalarTypeKey, kValuesKey, kOffsetsKey, kPresentKey, kMetadataKey},
        RecordIngressField::kAttributeDescriptor, i);
    const auto domain = Get(row.source, kDomainKey);
    if (!PyUnicode_CheckExact(domain.ptr()))
      Type(ingress_, RecordIngressField::kAttributeDescriptor, i,
           "attribute domain must be a built-in string");
    if (TextEquals(domain, kVertexDomain))
      row.domain = geo::AttributeDomain::vertex;
    else if (TextEquals(domain, kFaceDomain))
      row.domain = geo::AttributeDomain::face;
    else if (TextEquals(domain, kCornerDomain))
      row.domain = geo::AttributeDomain::corner;
    else
      Value(ingress_, RecordIngressField::kAttributeDescriptor, i,
            "invalid attribute domain");
    row.name = nb::borrow<nb::object>(Get(row.source, kNameKey));
    row.semantic = nb::borrow<nb::object>(Get(row.source, kSemanticKey));
    row.name_bytes = CountText(
        ingress_, row.name, RecordIngressField::kAttributeDescriptor, i, stop);
    row.semantic_bytes =
        CountText(ingress_, row.semantic,
                  RecordIngressField::kAttributeDescriptor, i, stop);
    text_bytes_ =
        Host(Sum(text_bytes_, Sum(row.name_bytes, row.semantic_bytes)));
    const auto set = Get(row.source, kSetIndexKey);
    if (!set.is_none()) row.set = Uint32(ingress_, set, i);
    row.components = Uint32(ingress_, Get(row.source, kComponentsKey), i);
    const auto type = Get(row.source, kScalarTypeKey);
    if (!PyUnicode_CheckExact(type.ptr()))
      Type(ingress_, RecordIngressField::kAttributeDescriptor, i,
           "attribute scalar type must be a built-in string");
    if (TextEquals(type, kFloat32Type))
      row.kind = RecordBufferKind::kFloat32;
    else if (TextEquals(type, kFloat64Type))
      row.kind = RecordBufferKind::kFloat64;
    else if (TextEquals(type, kInt32Type))
      row.kind = RecordBufferKind::kInt32;
    else if (TextEquals(type, kUint8Type))
      row.kind = RecordBufferKind::kUint8;
    else if (TextEquals(type, kUint16Type))
      row.kind = RecordBufferKind::kUint16;
    else if (TextEquals(type, kUint32Type))
      row.kind = RecordBufferKind::kUint32;
    else if (TextEquals(type, kUint64Type))
      row.kind = RecordBufferKind::kUint64;
    else
      Value(ingress_, RecordIngressField::kAttributeDescriptor, i,
            "unsupported record scalar type");
    row.values =
        ingress_.Acquire(Get(row.source, kValuesKey), row.kind,
                         RecordIngressField::kAttributeValues, i, stop);
    const auto offsets = Get(row.source, kOffsetsKey),
               present = Get(row.source, kPresentKey);
    if (!offsets.is_none())
      row.offsets =
          ingress_.Acquire(offsets, RecordBufferKind::kUint64,
                           RecordIngressField::kAttributeOffsets, i, stop);
    if (!present.is_none())
      row.present =
          ingress_.Acquire(present, RecordBufferKind::kUint8,
                           RecordIngressField::kAttributePresence, i, stop);
    const auto metadata = Get(row.source, kMetadataKey);
    if (!PyDict_Check(metadata.ptr()))
      Type(ingress_, RecordIngressField::kMetadata, i,
           "metadata must be a string dictionary");
    row.metadata_first = metadata_count_;
    row.metadata_count = static_cast<std::size_t>(PyDict_Size(metadata.ptr()));
    ingress_.AdmitMetadata(row.metadata_count, i);
    Py_ssize_t pos = 0;
    PyObject *key = nullptr, *value = nullptr;
    while (PyDict_Next(metadata.ptr(), &pos, &key, &value)) {
      auto& item = metadata_[metadata_count_++];
      item.key = nb::borrow<nb::object>(key);
      item.value = nb::borrow<nb::object>(value);
      item.key_bytes =
          CountText(ingress_, item.key, RecordIngressField::kMetadata, i, stop);
      item.value_bytes = CountText(ingress_, item.value,
                                   RecordIngressField::kMetadata, i, stop);
      text_bytes_ =
          Host(Sum(text_bytes_, Sum(item.key_bytes, item.value_bytes)));
      ingress_.PreflightCheckpoint(stop);
    }
    ingress_.PreflightCheckpoint(stop);
  }
  // Final poll is real even for empty/short records; it is deliberately
  // latched.
  ++ingress_.Observation().item_checkpoints;
  ingress_.Observation().preflight_stop_observed |= stop.stop_requested();
  preflighted_ = true;
}
void SurfaceRecordCapture::CaptureDescriptors(std::stop_token stop) {
  if (!preflighted_ || capture_started_)
    throw std::logic_error("surface record is not ready for capture");
  capture_started_ = true;
  auto field = RecordIngressField::kWorkspace;
  std::optional<std::uint64_t> ordinal;
  try {
    ingress_.Checkpoint(stop);
    // Also establishes that the shared numeric stage has actually completed.
    (void)ingress_.Bytes(positions_);
    if (text_bytes_)
      text_ = ingress_.Bridge().Allocate(text_bytes_, alignof(char));
    std::size_t offset = 0;
    for (std::size_t i = 0; i < attribute_count_; ++i) {
      field = RecordIngressField::kAttributeDescriptor;
      ordinal = i;
      const auto& source = attributes_[i];
      auto& view = views_[i];
      view.domain = source.domain;
      view.components = source.components;
      view.set_index = source.set;
      view.name = EncodeText(ingress_, source.name, static_cast<char*>(text_),
                             offset, source.name_bytes, stop);
      view.semantic =
          EncodeText(ingress_, source.semantic, static_cast<char*>(text_),
                     offset, source.semantic_bytes, stop);
      switch (source.kind) {
        case RecordBufferKind::kFloat32:
          view.values = Scalars<float>(ingress_, source.values);
          break;
        case RecordBufferKind::kFloat64:
          view.values = Scalars<double>(ingress_, source.values);
          break;
        case RecordBufferKind::kInt32:
          view.values = Scalars<std::int32_t>(ingress_, source.values);
          break;
        case RecordBufferKind::kUint8:
          view.values = Scalars<std::uint8_t>(ingress_, source.values);
          break;
        case RecordBufferKind::kUint16:
          view.values = Scalars<std::uint16_t>(ingress_, source.values);
          break;
        case RecordBufferKind::kUint32:
          view.values = Scalars<std::uint32_t>(ingress_, source.values);
          break;
        case RecordBufferKind::kUint64:
          view.values = Scalars<std::uint64_t>(ingress_, source.values);
          break;
        case RecordBufferKind::kBytes:
          throw std::logic_error("raw bytes are not record numeric scalars");
      }
      if (source.offsets)
        view.offsets = Scalars<geo::index_t>(ingress_, *source.offsets);
      if (source.present)
        view.present = Scalars<std::uint8_t>(ingress_, *source.present);
      field = RecordIngressField::kMetadata;
      for (std::size_t j = 0; j < source.metadata_count; ++j) {
        const auto at = source.metadata_first + j;
        const auto& item = metadata_[at];
        auto& target = metadata_views_[at];
        target.key = EncodeText(ingress_, item.key, static_cast<char*>(text_),
                                offset, item.key_bytes, stop);
        target.value =
            EncodeText(ingress_, item.value, static_cast<char*>(text_), offset,
                       item.value_bytes, stop);
      }
      auto range = std::span<geo::MetadataEntryView>(
          metadata_views_.data() + source.metadata_first,
          source.metadata_count);
      std::sort(range.begin(), range.end(), [&](const auto& a, const auto& b) {
        for (std::size_t j = 0; j < std::min(a.key.size(), b.key.size()); ++j) {
          ingress_.Checkpoint(stop);
          const auto first = static_cast<unsigned char>(a.key[j]),
                     second = static_cast<unsigned char>(b.key[j]);
          if (first != second) return first < second;
        }
        return a.key.size() < b.key.size();
      });
      view.metadata = range;
    }
    if (offset != text_bytes_)
      throw std::logic_error("surface descriptor arena extent mismatch");
    ingress_.Checkpoint(stop);
    captured_ = true;
  } catch (const alloc::AdmissionFailure& error) {
    if (ingress_.ClassifyAllocation(error, field, ordinal))
      throw RecordIngressRefusal();
    throw;
  }
}
void SurfaceRecordCapture::ReleasePythonSources() {
  source_ = nb::object();
  for (std::size_t i = 0; i < attribute_count_; ++i) {
    auto& row = attributes_[i];
    row.source = nb::object();
    row.name = nb::object();
    row.semantic = nb::object();
  }
  for (std::size_t i = 0; i < metadata_count_; ++i) {
    metadata_[i].key = nb::object();
    metadata_[i].value = nb::object();
  }
}
geo::MeshMaterializationInput SurfaceRecordCapture::Input() const {
  if (!captured_)
    throw std::logic_error("surface record has no frozen native input");
  return {Scalars<double>(ingress_, positions_),
          geo::PolygonTopologyInput{
              Scalars<geo::index_t>(ingress_, face_offsets_),
              Scalars<geo::index_t>(ingress_, corner_vertices_)},
          std::span<const geo::AttributeMaterializationInput>(
              views_.data(), attribute_count_)};
}
void SurfaceRecordCapture::ReleaseCapturedStorage() {
  if (!captured_)
    throw std::logic_error("surface record has no captured storage");
  if (source_.is_valid())
    throw std::logic_error("release Python record sources under GIL first");
  captured_ = false;
  ingress_.ReleaseBufferStorage(positions_);
  ingress_.ReleaseBufferStorage(face_offsets_);
  ingress_.ReleaseBufferStorage(corner_vertices_);
  for (std::size_t i = 0; i < attribute_count_; ++i) {
    const auto& row = attributes_[i];
    ingress_.ReleaseBufferStorage(row.values);
    if (row.offsets) ingress_.ReleaseBufferStorage(*row.offsets);
    if (row.present) ingress_.ReleaseBufferStorage(*row.present);
  }
  views_.clear();
  metadata_views_.clear();
  alloc::AllocationContext::Deallocate(text_);
  text_ = nullptr;
}
}  // namespace meshvale::interchange::surface_python_detail
