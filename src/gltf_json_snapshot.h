// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_JSON_SNAPSHOT_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_JSON_SNAPSHOT_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_detail {

// Private retention seam, not glTF metadata/role admission or an installed DOM.
struct JsonCaptureLimits {
  std::size_t source_bytes = 0;
  std::size_t nodes = 0;
  std::size_t decoded_bytes = 0;
  int nesting_depth = 0;
};
enum class JsonKind { kObject, kArray, kString, kNumber, kBool, kNull };
enum class JsonCaptureCode {
  kAccepted,
  kInvalidLimits,
  kMalformed,
  kDuplicateKey,
  kSizeLimit,
  kBudgetExceeded,
  kCanceled,
  kUnicodeScalar
};
[[nodiscard]] std::size_t MaximumJsonCaptureNodes() noexcept;

struct JsonSnapshotData;
class JsonSnapshot;
struct JsonCaptureResult;
[[nodiscard]] JsonCaptureResult CaptureJsonSnapshot(
    std::string_view input, const JsonCaptureLimits& limits,
    const AllocationContext& allocation);

// Copies retain a const shared owner. All views below borrow this owner and
// require at least one snapshot copy to remain alive. Independent copies and
// const access may run concurrently; assigning the same wrapper needs a lock.
class JsonSnapshot {
 public:
  JsonSnapshot(const JsonSnapshot& other) : data_(other.data_) {}
  JsonSnapshot(JsonSnapshot&& other) noexcept : data_(std::move(other.data_)) {}
  JsonSnapshot& operator=(const JsonSnapshot& other) {
    if (this != &other) data_ = other.data_;
    reference_ = nullptr;
    reference_state_ = nullptr;
    return *this;
  }
  JsonSnapshot& operator=(JsonSnapshot&& other) noexcept {
    if (this != &other) data_ = std::move(other.data_);
    reference_ = nullptr;
    reference_state_ = nullptr;
    return *this;
  }
  // Only explicitly classified graph/role/reference traversals call this.
  // DOM access, byte parsing and fixed-width scalar work do not imply R work.
  void ReferenceVisit() const {
    if (reference_) reference_(reference_state_);
  }
  [[nodiscard]] std::string_view Original() const;
  [[nodiscard]] std::size_t NodeCount() const;
  [[nodiscard]] JsonKind Kind(std::size_t node) const;
  [[nodiscard]] std::optional<std::size_t> Parent(std::size_t node) const;
  [[nodiscard]] std::optional<std::size_t> FirstChild(std::size_t node) const;
  [[nodiscard]] std::optional<std::size_t> NextSibling(std::size_t node) const;
  [[nodiscard]] std::optional<std::size_t> FirstReferenceChild(
      std::size_t node) const {
    auto child = FirstChild(node);
    if (child) ReferenceVisit();
    return child;
  }
  [[nodiscard]] std::optional<std::size_t> NextReferenceSibling(
      std::size_t node) const {
    auto sibling = NextSibling(node);
    if (sibling) ReferenceVisit();
    return sibling;
  }
  [[nodiscard]] std::string_view Key(std::size_t node) const;
  [[nodiscard]] std::string_view String(std::size_t node) const;
  [[nodiscard]] std::string_view Number(std::size_t node) const;
  [[nodiscard]] bool Boolean(std::size_t node) const;

 private:
  friend class JsonReferenceScope;
  friend JsonCaptureResult CaptureJsonSnapshot(std::string_view,
                                               const JsonCaptureLimits&,
                                               const AllocationContext&);
  explicit JsonSnapshot(std::shared_ptr<const JsonSnapshotData> data);
  [[nodiscard]] const JsonSnapshotData& Data() const;
  std::shared_ptr<const JsonSnapshotData> data_;
  // A lexical borrow on this wrapper only. Copies and moves never propagate it.
  void (*reference_)(void*) = nullptr;
  void* reference_state_ = nullptr;
};
// Attach only to an unpublished read wrapper or an extraction-local copy.
// The wrapper and callback state outlive this noncopyable/nonmovable scope;
// neither is assigned or moved while it is attached. No callback is retained
// by shared DOM data, and unwinding clears the borrow before publication.
class JsonReferenceScope {
 public:
  JsonReferenceScope(JsonSnapshot& document, void (*visit)(void*), void* state)
      : document_(document),
        previous_(document.reference_),
        previous_state_(document.reference_state_) {
    document_.reference_ = visit;
    document_.reference_state_ = state;
  }
  ~JsonReferenceScope() {
    document_.reference_ = previous_;
    document_.reference_state_ = previous_state_;
  }
  JsonReferenceScope(const JsonReferenceScope&) = delete;
  JsonReferenceScope& operator=(const JsonReferenceScope&) = delete;
  JsonReferenceScope(JsonReferenceScope&&) = delete;
  JsonReferenceScope& operator=(JsonReferenceScope&&) = delete;

 private:
  JsonSnapshot& document_;
  void (*previous_)(void*);
  void* previous_state_;
};
struct JsonCaptureResult {
  JsonCaptureCode code;
  std::optional<JsonSnapshot> snapshot;
};
// Input remains unchanged/alive until this synchronous call returns. Expected
// syntax/limit/budget/cancel failures return no partial snapshot. Actual
// backend allocation/runtime exceptions propagate. No parsed values reach cgltf
// here.

}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_JSON_SNAPSHOT_H_
