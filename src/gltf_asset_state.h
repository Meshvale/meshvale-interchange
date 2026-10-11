// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_ASSET_STATE_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_ASSET_STATE_H_

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stop_token>
#include <string_view>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_json_snapshot.h"
#include "meshvale/interchange/gltf.h"

namespace meshvale::interchange::gltf_detail {
template <class T>
using OwnedVector = std::vector<T, Allocator<T>>;

struct PrimitiveRecord {
  std::uint64_t mesh_index;
  std::uint64_t primitive_index;
  std::size_t json_node;
  GltfCapabilityReason capability;
};
struct InstanceRecord {
  std::uint64_t scene;
  std::uint64_t node;
  std::uint64_t primitive;
};
struct AssetState {
  AssetState(const geometry::ExecutionContext& context, std::stop_token stop,
             const GltfReadLimits& limits, const GltfWorkPolicy& work_policy,
             JsonSnapshot document);
  geometry::ExecutionContext context;
  AllocationContext allocation;
  GltfReadLimits limits;
  GltfWorkPolicy work_policy;
  JsonSnapshot document;
  OwnedVector<std::uint32_t> guard_rules;
  OwnedVector<OwnedVector<std::byte>> resource_storage;
  OwnedVector<GltfResourceView> resources;
  OwnedVector<GltfRawNumberView> raw_numbers;
  OwnedVector<OwnedVector<std::uint64_t>> children;
  OwnedVector<GltfNodeView> nodes;
  OwnedVector<OwnedVector<std::uint64_t>> roots;
  OwnedVector<GltfSceneView> scenes;
  OwnedVector<PrimitiveRecord> primitives;
  OwnedVector<InstanceRecord> instances;
};

// Fixed inline expected failure; never allocates diagnostic detail.
class GltfFailure : public std::exception {
 public:
  explicit GltfFailure(GltfDiagnostic diagnostic) noexcept;
  [[nodiscard]] const GltfDiagnostic& Diagnostic() const noexcept;
  [[nodiscard]] const char* what() const noexcept override;

 private:
  GltfDiagnostic diagnostic_;
};
[[noreturn]] void Fail(GltfStatus status, GltfDiagnosticCode code,
                       std::optional<GltfSourceSpan> source = std::nullopt,
                       std::uint32_t rule_id = 0);
[[nodiscard]] GltfDiagnostic AcceptedDiagnostic() noexcept;
[[nodiscard]] std::optional<std::size_t> FindMember(
    const JsonSnapshot& document, std::size_t object, std::string_view key);
[[nodiscard]] std::optional<std::size_t> ChildAt(const JsonSnapshot& document,
                                                 std::size_t array,
                                                 std::uint64_t ordinal);
[[nodiscard]] std::uint64_t ChildCount(const JsonSnapshot& document,
                                       std::size_t node);
[[nodiscard]] std::uint64_t ReadExactUnsigned(const JsonSnapshot& document,
                                              std::size_t node);
[[nodiscard]] std::int64_t ReadExactSigned(const JsonSnapshot& document,
                                           std::size_t node);
[[nodiscard]] std::uint32_t ReadFloatBits(const JsonSnapshot& document,
                                          std::size_t node);

struct Access {
  [[nodiscard]] static const std::shared_ptr<const AssetState>& Owner(
      const GltfAsset& asset) noexcept;
  [[nodiscard]] static const std::shared_ptr<const AssetState>& Owner(
      const GltfPrimitiveHandle& primitive) noexcept;
  [[nodiscard]] static const AssetState& State(const GltfAsset& asset);
  [[nodiscard]] static const PrimitiveRecord* PrimitiveRecordFor(
      const GltfAsset& asset, const GltfPrimitiveHandle& primitive);
  [[nodiscard]] static GltfAsset MakeAsset(
      std::shared_ptr<const AssetState> state);
  [[nodiscard]] static GltfPrimitiveHandle MakePrimitive(
      std::shared_ptr<const AssetState> state,
      const PrimitiveRecord& primitive);
  [[nodiscard]] static GltfInstanceHandle MakeInstance(
      std::shared_ptr<const AssetState> state, std::uint64_t ordinal);
  [[nodiscard]] static GltfPrimitiveExtraction MakeExtraction(
      std::shared_ptr<const ExtractionState> state);
};
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_ASSET_STATE_H_
