// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_ALLOCATION_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_ALLOCATION_H_

#include <meshvale/geometry/execution.h>

#include <cstddef>
#include <exception>
#include <optional>
#include <stop_token>
#include <type_traits>

namespace meshvale::interchange::gltf_detail {

enum class AdmissionCode {
  kSizeLimit,
  kBudgetExceeded,
  kCanceled,
  kInvalidObject
};
class AdmissionFailure : public std::exception {
 public:
  explicit AdmissionFailure(AdmissionCode code) noexcept;
  [[nodiscard]] AdmissionCode Code() const noexcept;
  [[nodiscard]] const char* what() const noexcept override;

 private:
  AdmissionCode code_;
};

[[nodiscard]] std::size_t CheckedProduct(std::size_t count, std::size_t size);

// Private injection seam. Production uses the stateless default backend.
// An injected state must outlive every block allocated through it; callbacks
// must match aligned allocation/free and deallocate must neither throw nor
// grow.
struct AllocationBackend {
  void* (*allocate)(std::size_t bytes, std::size_t alignment, void* state);
  void (*deallocate)(void* block, std::size_t bytes, std::size_t alignment,
                     void* state) noexcept;
  void* state;
};
[[nodiscard]] AllocationBackend DefaultAllocationBackend() noexcept;
[[nodiscard]] std::size_t AllocationRequestBytes(std::size_t bytes,
                                                 std::size_t alignment);

class AllocationContext {
 public:
  // Detached default enables standard rebound container construction without
  // creating an uncharged ledger. Allocation on it fails explicitly.
  AllocationContext() noexcept;
  AllocationContext(const geometry::ExecutionContext& context,
                    std::stop_token stop,
                    AllocationBackend backend = DefaultAllocationBackend());
  [[nodiscard]] void* Allocate(std::size_t bytes, std::size_t alignment) const;
  static void Deallocate(void* pointer) noexcept;
  [[nodiscard]] bool SharesBudget(
      const AllocationContext& other) const noexcept;

 private:
  std::optional<geometry::ExecutionContext> context_;
  std::stop_token stop_;
  AllocationBackend backend_;
};

// Only standard allocator templates live here. Block ownership, arithmetic,
// admission, cleanup and backend implementation are in gltf_allocation.cpp.
template <class T>
class Allocator {
 public:
  using value_type = T;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  using is_always_equal = std::false_type;

  Allocator() noexcept = default;
  explicit Allocator(const AllocationContext& context) : context_(context) {}
  template <class U>
  Allocator(const Allocator<U>& other) : context_(other.Context()) {}
  [[nodiscard]] T* allocate(std::size_t count) {
    return static_cast<T*>(
        context_.Allocate(CheckedProduct(count, sizeof(T)), alignof(T)));
  }
  void deallocate(T* pointer, std::size_t) noexcept {
    AllocationContext::Deallocate(pointer);
  }
  [[nodiscard]] const AllocationContext& Context() const noexcept {
    return context_;
  }
  template <class U>
  [[nodiscard]] bool operator==(const Allocator<U>& other) const noexcept {
    return context_.SharesBudget(other.Context());
  }

 private:
  AllocationContext context_;
};

}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_ALLOCATION_H_
