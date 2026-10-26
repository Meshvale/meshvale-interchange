// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_DIRECT_TRANSLATION_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_DIRECT_TRANSLATION_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_operation_workers.h"
#include "meshvale/interchange/gltf.h"

namespace meshvale::interchange::gltf_detail {
inline constexpr std::size_t kDirectTranslationComponents = 3;

struct DirectTranslationAccessor {
  std::uint64_t accessor_index = 0;
  std::uint64_t view_index = 0;
  std::uint64_t buffer_index = 0;
  std::uint64_t resource_offset = 0;
  std::uint64_t count = 0;
  std::size_t components = 0;
  std::span<const std::byte> bytes;
};
struct DirectTranslationSampler {
  std::uint64_t animation_index = 0;
  std::uint64_t sampler_index = 0;
  std::uint64_t first_channel = 0;
  DirectTranslationAccessor input;
  DirectTranslationAccessor output;
  std::array<std::uint32_t, kDirectTranslationComponents> minimum = {};
  std::array<std::uint32_t, kDirectTranslationComponents> maximum = {};
  bool has_minimum = false;
  bool has_maximum = false;
  std::uint32_t first_time = 0;
  std::uint32_t last_time = 0;
  std::uint32_t declared_first_time = 0;
  std::uint32_t declared_last_time = 0;
  GltfSourceSpan declared_first_span = {};
  GltfSourceSpan declared_last_span = {};
  std::array<GltfSourceSpan, kDirectTranslationComponents> minimum_spans = {};
  std::array<GltfSourceSpan, kDirectTranslationComponents> maximum_spans = {};
};

// Private const ownership is the authority; the public borrowed spans cannot
// create this object. Growth ends before ordinary extraction starts.
struct DirectTranslationState {
  DirectTranslationState(std::shared_ptr<const AssetState> source,
                         PrimitiveRecord selected,
                         const AllocationContext& scope);
  std::shared_ptr<const AssetState> asset;
  PrimitiveRecord primitive;
  AllocationContext allocation;
  OwnedVector<std::uint64_t> users;
  OwnedVector<std::uint8_t> membership;
  OwnedVector<GltfDirectTranslationTrackView> tracks;
  OwnedVector<DirectTranslationSampler> samplers;
  GltfDirectTranslationObservation facts;
};
struct DirectTranslationResult {
  std::shared_ptr<const DirectTranslationState> proof;
  std::optional<GltfDirectTranslationObservation> observation;
  GltfDiagnostic diagnostic;
  // Distinguishes actual request admission from a canonical logical count cap.
  bool request_limit = false;
};

// Validates mandatory new-profile configuration without allocating/entering
// the helper. Static extraction does not call this function.
void ValidateDirectTranslationConfiguration(
    const GltfDirectTranslationLimits& limits,
    const GltfDirectTranslationWorkPolicy& work);

// root must be uncapped. Its exact ledger, backend and stop token are retained
// by the independent validation scope, before accounting seed allocation.
[[nodiscard]] DirectTranslationResult ValidateDirectTranslation(
    std::shared_ptr<const AssetState> asset, const PrimitiveRecord& primitive,
    const GltfDirectTranslationLimits& limits,
    const GltfDirectTranslationWorkPolicy& work, const AllocationContext& root,
    std::size_t owner_allowance, const NativeExecutionOptions* execution,
    const DirectTranslationState* expected = nullptr);

}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_DIRECT_TRANSLATION_H_
