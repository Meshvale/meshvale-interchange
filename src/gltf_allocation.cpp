// SPDX-License-Identifier: Apache-2.0
#include "gltf_allocation.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <utility>

namespace meshvale::interchange::gltf_detail {
namespace {
struct Header {
  geometry::PayloadLease lease;
  AllocationBackend backend;
  void* block;
  std::size_t request_bytes;
  std::size_t alignment;
};

void* AllocateDefault(std::size_t bytes, std::size_t alignment, void*) {
  return ::operator new(bytes, std::align_val_t(alignment));
}
void DeallocateDefault(void* block, std::size_t, std::size_t alignment,
                       void*) noexcept {
  ::operator delete(block, std::align_val_t(alignment));
}
std::size_t BlockAlignment(std::size_t alignment) {
  if (!std::has_single_bit(alignment))
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  return std::max(alignment, alignof(Header));
}
std::size_t PrefixBytes(std::size_t alignment) {
  const auto remainder = sizeof(Header) % alignment;
  const auto padding = remainder == 0 ? 0 : alignment - remainder;
  if (padding > std::numeric_limits<std::size_t>::max() - sizeof(Header))
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  return sizeof(Header) + padding;
}
}  // namespace

AdmissionFailure::AdmissionFailure(AdmissionCode code) noexcept : code_(code) {}
AdmissionCode AdmissionFailure::Code() const noexcept { return code_; }
const char* AdmissionFailure::what() const noexcept {
  return "glTF allocation admission failed";
}
std::size_t CheckedProduct(std::size_t count, std::size_t size) {
  if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  return count * size;
}
AllocationBackend DefaultAllocationBackend() noexcept {
  return {AllocateDefault, DeallocateDefault, nullptr};
}
std::size_t AllocationRequestBytes(std::size_t bytes, std::size_t alignment) {
  const auto prefix = PrefixBytes(BlockAlignment(alignment));
  if (bytes > std::numeric_limits<std::size_t>::max() - prefix)
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  return prefix + bytes;
}
AllocationContext::AllocationContext() noexcept
    : backend_(DefaultAllocationBackend()) {}
AllocationContext::AllocationContext(const geometry::ExecutionContext& context,
                                     std::stop_token stop,
                                     AllocationBackend backend)
    : context_(context), stop_(stop), backend_(backend) {
  (void)context.WorkerBudget();  // Preserve Geometry's moved-object exception.
  if (!backend_.allocate || !backend_.deallocate)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
}
void* AllocationContext::Allocate(std::size_t bytes,
                                  std::size_t alignment) const {
  if (!context_) throw AdmissionFailure(AdmissionCode::kInvalidObject);
  const auto block_alignment = BlockAlignment(alignment);
  const auto prefix = PrefixBytes(block_alignment);
  const auto request_bytes = AllocationRequestBytes(bytes, alignment);
  auto result = geometry::TryReservePayload(*context_, request_bytes, stop_);
  switch (result.status) {
    case geometry::ReservationStatus::kBudgetExceeded:
      throw AdmissionFailure(AdmissionCode::kBudgetExceeded);
    case geometry::ReservationStatus::kCanceled:
      throw AdmissionFailure(AdmissionCode::kCanceled);
    case geometry::ReservationStatus::kAccepted:
      break;
  }
  void* block =
      backend_.allocate(request_bytes, block_alignment, backend_.state);
  if (!block) throw std::bad_alloc();
  if (stop_.stop_requested()) {
    backend_.deallocate(block, request_bytes, block_alignment, backend_.state);
    throw AdmissionFailure(AdmissionCode::kCanceled);
  }
  auto* payload = static_cast<std::byte*>(block) + prefix;
  auto* header = new (payload - sizeof(Header)) Header{
      std::move(result.lease), backend_, block, request_bytes, block_alignment};
  (void)header;
  return payload;
}
void AllocationContext::Deallocate(void* pointer) noexcept {
  if (!pointer) return;
  auto* header = reinterpret_cast<Header*>(static_cast<std::byte*>(pointer) -
                                           sizeof(Header));
  auto lease = std::move(header->lease);
  const auto backend = header->backend;
  const auto block = header->block;
  const auto request_bytes = header->request_bytes;
  const auto alignment = header->alignment;
  header->~Header();
  backend.deallocate(block, request_bytes, alignment, backend.state);
  lease.Reset();
}
bool AllocationContext::SharesBudget(
    const AllocationContext& other) const noexcept {
  if (!context_ || !other.context_) return !context_ && !other.context_;
  return geometry::SharesExecutionBudget(*context_, *other.context_);
}
}  // namespace meshvale::interchange::gltf_detail
