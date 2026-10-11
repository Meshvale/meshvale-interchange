// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_JSON_SNAPSHOT_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_JSON_SNAPSHOT_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

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
  [[nodiscard]] std::string_view Original() const;
  [[nodiscard]] std::size_t NodeCount() const;
  [[nodiscard]] JsonKind Kind(std::size_t node) const;
  [[nodiscard]] std::optional<std::size_t> Parent(std::size_t node) const;
  [[nodiscard]] std::optional<std::size_t> FirstChild(std::size_t node) const;
  [[nodiscard]] std::optional<std::size_t> NextSibling(std::size_t node) const;
  [[nodiscard]] std::string_view Key(std::size_t node) const;
  [[nodiscard]] std::string_view String(std::size_t node) const;
  [[nodiscard]] std::string_view Number(std::size_t node) const;
  [[nodiscard]] bool Boolean(std::size_t node) const;

 private:
  friend JsonCaptureResult CaptureJsonSnapshot(std::string_view,
                                               const JsonCaptureLimits&,
                                               const AllocationContext&);
  explicit JsonSnapshot(std::shared_ptr<const JsonSnapshotData> data);
  [[nodiscard]] const JsonSnapshotData& Data() const;
  std::shared_ptr<const JsonSnapshotData> data_;
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
