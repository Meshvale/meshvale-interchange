// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <limits>
#include <memory>

#include "gltf_metadata.h"
#include "gltf_parser.h"
#include "gltf_resources.h"
#include "meshvale/interchange/gltf.h"

namespace meshvale::interchange {
namespace {
void Configuration(const GltfReadLimits& limits, const GltfWorkPolicy& policy) {
  const std::array<std::uint64_t, 18> fields = {
      limits.max_json_bytes,
      limits.max_resource_bytes,
      limits.max_owned_source_resource_bytes,
      limits.max_document_nodes,
      limits.max_document_depth,
      limits.max_decoded_string_bytes,
      limits.max_numeric_token_bytes,
      limits.max_decimal_coefficient_digits,
      limits.max_decimal_exponent_digits,
      limits.max_table_rows,
      limits.max_graph_edges,
      limits.max_instances,
      limits.max_accessor_elements,
      limits.max_projection_bytes,
      policy.parallel_min_resource_bytes,
      policy.resource_partition_bytes,
      policy.parallel_min_channel_components,
      policy.channel_partition_elements};
  const auto ceiling =
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
  for (const auto value : fields) {
    if (value == 0)
      gltf_detail::Fail(GltfStatus::kInvalidLimits,
                        GltfDiagnosticCode::kZeroLimit);
    if (value > ceiling)
      gltf_detail::Fail(GltfStatus::kInvalidLimits,
                        GltfDiagnosticCode::kLimitNotRepresentable);
  }
  if (limits.max_document_depth > std::numeric_limits<int>::max() ||
      limits.max_document_nodes > gltf_detail::MaximumJsonCaptureNodes())
    gltf_detail::Fail(GltfStatus::kInvalidLimits,
                      GltfDiagnosticCode::kLimitNotRepresentable);
}
void RawNumbers(gltf_detail::AssetState& state) {
  const auto& document = state.document;
  const auto original = document.Original();
  for (std::size_t node = 0; node < document.NodeCount(); ++node) {
    state.allocation.Checkpoint();
    if (document.Kind(node) != gltf_detail::JsonKind::kNumber) continue;
    const auto lexeme = document.Number(node);
    std::uint64_t coefficient = 0;
    std::uint64_t exponent = 0;
    bool exponent_part = false;
    for (const auto c : lexeme) {
      if (c == 'e' || c == 'E') exponent_part = true;
      if (c >= '0' && c <= '9') {
        if (exponent_part)
          ++exponent;
        else
          ++coefficient;
      }
    }
    if (lexeme.size() > state.limits.max_numeric_token_bytes ||
        coefficient > state.limits.max_decimal_coefficient_digits ||
        exponent > state.limits.max_decimal_exponent_digits)
      gltf_detail::Fail(GltfStatus::kUnsupportedProfile,
                        GltfDiagnosticCode::kLimitExceeded);
    const auto offset =
        static_cast<std::uint64_t>(lexeme.data() - original.data());
    const auto kind = lexeme.find_first_of(".eE") == std::string_view::npos
                          ? GltfJsonNumberKind::kIntegerSpelling
                          : GltfJsonNumberKind::kDecimalSpelling;
    state.raw_numbers.push_back({{offset, lexeme.size()}, kind, lexeme});
  }
}
}  // namespace
GltfReadResult ReadGltfAsset(
    std::span<const std::byte> json,
    std::span<const SuppliedGltfResource> supplied_resources,
    const GltfReadLimits& limits, const GltfWorkPolicy& policy,
    const geometry::ExecutionContext& context, std::stop_token stop) {
  using namespace gltf_detail;
  try {
    Configuration(limits, policy);
    for (const auto& binding : supplied_resources)
      if (binding.kind != GltfResourceKind::kBuffer &&
          binding.kind != GltfResourceKind::kImage)
        Fail(GltfStatus::kInvalidRequest,
             GltfDiagnosticCode::kInvalidBindingKind);
    const AllocationContext allocation(context, stop);
    allocation.Checkpoint();
    if (json.size() > limits.max_owned_source_resource_bytes)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded);
    const JsonCaptureLimits capture_limits{
        static_cast<std::size_t>(limits.max_json_bytes),
        static_cast<std::size_t>(limits.max_document_nodes),
        static_cast<std::size_t>(limits.max_decoded_string_bytes),
        static_cast<int>(limits.max_document_depth)};
    const auto input = std::string_view(
        reinterpret_cast<const char*>(json.data()), json.size());
    auto captured = CaptureJsonSnapshot(input, capture_limits, allocation);
    if (!captured.snapshot) {
      switch (captured.code) {
        case JsonCaptureCode::kCanceled:
          Fail(GltfStatus::kCanceled, GltfDiagnosticCode::kCanceled);
        case JsonCaptureCode::kBudgetExceeded:
          Fail(GltfStatus::kBudgetExceeded, GltfDiagnosticCode::kPayloadBudget);
        case JsonCaptureCode::kUnicodeScalar:
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kUnicodeScalar);
        case JsonCaptureCode::kDuplicateKey:
          Fail(GltfStatus::kMalformed,
               GltfDiagnosticCode::kDecodedDuplicateKey);
        case JsonCaptureCode::kSizeLimit:
          Fail(GltfStatus::kUnsupportedProfile,
               GltfDiagnosticCode::kLimitExceeded);
        default:
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kJsonSyntax);
      }
    }
    auto state = std::allocate_shared<AssetState>(
        Allocator<AssetState>(allocation), context, stop, limits, policy,
        std::move(*captured.snapshot));
    RawNumbers(*state);
    AdmitMetadata(*state);
    BuildGraph(*state);
    CheckDeclaredGraphRelationships(*state);
    AdmitResources(*state, supplied_resources);
    const auto projection = BuildCoreProjection(*state);
    const auto parsed = InspectCoreProjection(
        {projection.data(), projection.size()}, allocation);
    if (!parsed.accepted)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreReference);
    allocation.Checkpoint();
    return {Access::MakeAsset(std::move(state)), AcceptedDiagnostic()};
  } catch (const GltfFailure& failure) {
    return {std::nullopt, failure.Diagnostic()};
  } catch (const AdmissionFailure& failure) {
    switch (failure.Code()) {
      case AdmissionCode::kSizeLimit:
        return {std::nullopt,
                {GltfStatus::kUnsupportedProfile,
                 GltfDiagnosticCode::kLimitExceeded,
                 {},
                 {},
                 {},
                 {},
                 0}};
      case AdmissionCode::kBudgetExceeded:
        return {std::nullopt,
                {GltfStatus::kBudgetExceeded,
                 GltfDiagnosticCode::kPayloadBudget,
                 {},
                 {},
                 {},
                 {},
                 0}};
      case AdmissionCode::kCanceled:
        return {std::nullopt,
                {GltfStatus::kCanceled,
                 GltfDiagnosticCode::kCanceled,
                 {},
                 {},
                 {},
                 {},
                 0}};
      case AdmissionCode::kInvalidObject:
        throw;
    }
    throw;
  }
}
}  // namespace meshvale::interchange
