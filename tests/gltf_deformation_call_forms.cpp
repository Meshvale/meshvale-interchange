// SPDX-License-Identifier: Apache-2.0
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <type_traits>
#include <utility>

#include "meshvale/geometry/materialization.h"
#include "meshvale/interchange/gltf.h"
#include "meshvale/interchange/gltf_deformation.h"
#include "meshvale/interchange/gltf_files.h"
#include "meshvale/interchange/gltf_materialization.h"
#include "meshvale/interchange/gltf_topology_write.h"
#include "meshvale/interchange/gltf_workflow_support.h"

namespace meshvale::interchange {
namespace {
static_assert(GltfNodeRotationSourceView::kQuaternionComponents == 4);
static_assert(
    std::is_same_v<decltype(GltfNodeRotationSourceView::projected_components),
                   std::optional<std::span<const std::uint32_t, 4>>>);

template <class Source, class Policy, class Limits, class Work, class Result>
concept MaterializeGltfPrimitiveCallForms = requires(
    const Source& extraction, const Policy& deformation,
    const Limits& materialization_limits, const Work& materialization_work,
    std::stop_token stop) {
  {
    MaterializeGltfPrimitive(extraction, materialization_limits,
                             materialization_work)
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, materialization_limits, {})
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, {}, materialization_work)
  } -> std::same_as<Result>;
  { MaterializeGltfPrimitive(extraction, {}, {}) } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, materialization_limits,
                             materialization_work, stop)
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, materialization_limits,
                             materialization_work, {})
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, materialization_limits, {}, stop)
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, materialization_limits, {}, {})
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, {}, materialization_work, stop)
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, {}, materialization_work, {})
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, {}, {}, stop)
  } -> std::same_as<Result>;
  { MaterializeGltfPrimitive(extraction, {}, {}, {}) } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, deformation, materialization_limits,
                             materialization_work, stop)
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, deformation, materialization_limits,
                             materialization_work, {})
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, deformation, materialization_limits,
                             {}, stop)
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, deformation, materialization_limits,
                             {}, {})
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, deformation, {}, materialization_work,
                             stop)
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, deformation, {}, materialization_work,
                             {})
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, deformation, {}, {}, stop)
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, deformation, {}, {}, {})
  } -> std::same_as<Result>;
  {
    MaterializeGltfPrimitive(extraction, {}, {}, {}, {})
  } -> std::same_as<Result>;
};
static_assert(
    MaterializeGltfPrimitiveCallForms<
        GltfPrimitiveExtraction, GltfDeformationPolicy,
        GltfMaterializationLimits, geometry::MeshMaterializationWorkPolicy,
        GltfMaterializationResult>);
template <class Source, class Policy, class Limits, class Work>
concept MaterializeGltfPrimitiveWithoutPolicyStop = requires(
    const Source& extraction, const Policy& deformation,
    const Limits& materialization_limits, const Work& materialization_work) {
  MaterializeGltfPrimitive(extraction, deformation, materialization_limits,
                           materialization_work);
};
static_assert(
    !MaterializeGltfPrimitiveWithoutPolicyStop<
        GltfPrimitiveExtraction, GltfDeformationPolicy,
        GltfMaterializationLimits, geometry::MeshMaterializationWorkPolicy>);

template <class Source, class Policy, class Limits, class Work, class Result>
concept WriteGltfTopologySelectionCallForms = requires(
    const Source& request, const Policy& deformation,
    const Limits& write_limits, const Work& write_work, std::stop_token stop) {
  {
    WriteGltfTopologySelection(request, write_limits, write_work)
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, write_limits, {})
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, {}, write_work)
  } -> std::same_as<Result>;
  { WriteGltfTopologySelection(request, {}, {}) } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, write_limits, write_work, stop)
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, write_limits, write_work, {})
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, write_limits, {}, stop)
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, write_limits, {}, {})
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, {}, write_work, stop)
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, {}, write_work, {})
  } -> std::same_as<Result>;
  { WriteGltfTopologySelection(request, {}, {}, stop) } -> std::same_as<Result>;
  { WriteGltfTopologySelection(request, {}, {}, {}) } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, deformation, write_limits, write_work,
                               stop)
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, deformation, write_limits, write_work,
                               {})
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, deformation, write_limits, {}, stop)
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, deformation, write_limits, {}, {})
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, deformation, {}, write_work, stop)
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, deformation, {}, write_work, {})
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, deformation, {}, {}, stop)
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, deformation, {}, {}, {})
  } -> std::same_as<Result>;
  {
    WriteGltfTopologySelection(request, {}, {}, {}, {})
  } -> std::same_as<Result>;
};
static_assert(WriteGltfTopologySelectionCallForms<
              GltfTopologyWriteRequest, GltfDeformationPolicy,
              GltfTopologyWriteLimits, GltfTopologyWriteWorkPolicy,
              GltfTopologyWriteResult>);
template <class Source, class Policy, class Limits, class Work>
concept WriteGltfTopologySelectionWithoutPolicyStop = requires(
    const Source& request, const Policy& deformation,
    const Limits& write_limits, const Work& write_work) {
  WriteGltfTopologySelection(request, deformation, write_limits, write_work);
};
static_assert(!WriteGltfTopologySelectionWithoutPolicyStop<
              GltfTopologyWriteRequest, GltfDeformationPolicy,
              GltfTopologyWriteLimits, GltfTopologyWriteWorkPolicy>);

template <class Source, class Policy, class Limits, class Work, class Result>
concept InspectGltfTopologyOutputCallForms = requires(
    const Source& request, const Policy& deformation,
    const Limits& inspection_limits, const Work& inspection_work,
    std::stop_token stop) {
  {
    InspectGltfTopologyOutput(request, inspection_limits, inspection_work)
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, inspection_limits, {})
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, {}, inspection_work)
  } -> std::same_as<Result>;
  { InspectGltfTopologyOutput(request, {}, {}) } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, inspection_limits, inspection_work, stop)
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, inspection_limits, inspection_work, {})
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, inspection_limits, {}, stop)
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, inspection_limits, {}, {})
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, {}, inspection_work, stop)
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, {}, inspection_work, {})
  } -> std::same_as<Result>;
  { InspectGltfTopologyOutput(request, {}, {}, stop) } -> std::same_as<Result>;
  { InspectGltfTopologyOutput(request, {}, {}, {}) } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, deformation, inspection_limits,
                              inspection_work, stop)
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, deformation, inspection_limits,
                              inspection_work, {})
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, deformation, inspection_limits, {}, stop)
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, deformation, inspection_limits, {}, {})
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, deformation, {}, inspection_work, stop)
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, deformation, {}, inspection_work, {})
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, deformation, {}, {}, stop)
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, deformation, {}, {}, {})
  } -> std::same_as<Result>;
  {
    InspectGltfTopologyOutput(request, {}, {}, {}, {})
  } -> std::same_as<Result>;
};
static_assert(InspectGltfTopologyOutputCallForms<
              GltfTopologyOutputInspectionRequest, GltfDeformationPolicy,
              GltfTopologyOutputInspectionLimits,
              GltfTopologyOutputInspectionWorkPolicy,
              GltfTopologyOutputInspectionResult>);
template <class Source, class Policy, class Limits, class Work>
concept InspectGltfTopologyOutputWithoutPolicyStop =
    requires(const Source& request, const Policy& deformation,
             const Limits& inspection_limits, const Work& inspection_work) {
      InspectGltfTopologyOutput(request, deformation, inspection_limits,
                                inspection_work);
    };
static_assert(!InspectGltfTopologyOutputWithoutPolicyStop<
              GltfTopologyOutputInspectionRequest, GltfDeformationPolicy,
              GltfTopologyOutputInspectionLimits,
              GltfTopologyOutputInspectionWorkPolicy>);

static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kNone) == 0);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kZeroLimit) == 1);
static_assert(static_cast<std::uint32_t>(
                  GltfDiagnosticCode::kLimitNotRepresentable) == 2);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kInvalidRequest) ==
              3);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kInvalidBindingKind) == 4);
static_assert(static_cast<std::uint32_t>(
                  GltfDiagnosticCode::kUnsupportedResourceRepresentation) == 5);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kEmbeddedUriSyntax) == 6);
static_assert(static_cast<std::uint32_t>(
                  GltfDiagnosticCode::kEmbeddedMediaMismatch) == 7);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kLimitExceeded) ==
              8);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kSizeOverflow) ==
              9);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kJsonSyntax) ==
              10);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kUnicodeScalar) ==
              11);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kDecodedDuplicateKey) == 12);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kCoreFieldType) ==
              13);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kCoreFieldRequired) == 14);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kExactInteger) ==
              15);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kCoreRange) == 16);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kCoreArrayLength) == 17);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kCoreReference) ==
              18);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kGraphRelationship) == 19);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kBinaryLayout) ==
              20);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kBinaryValue) ==
              21);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kIndexRole) == 22);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kUnsupportedEnum) == 23);
static_assert(static_cast<std::uint32_t>(
                  GltfDiagnosticCode::kUnsupportedNumberRepresentation) == 24);
static_assert(static_cast<std::uint32_t>(
                  GltfDiagnosticCode::kUnsupportedCoreString) == 25);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kRequiredExtension) == 26);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kMissingResource) == 27);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kDuplicateResource) == 28);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kExtraResource) ==
              29);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kResourceExtent) ==
              30);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kCanceled) == 31);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kPayloadBudget) ==
              32);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kInvalidSelection) == 33);
static_assert(static_cast<std::uint32_t>(GltfDiagnosticCode::kForeignOwner) ==
              34);
static_assert(static_cast<std::uint32_t>(
                  GltfDiagnosticCode::kAdmissionProfileMismatch) == 35);
static_assert(
    static_cast<std::uint32_t>(GltfDiagnosticCode::kNumericalProfile) == 36);
static_assert(static_cast<std::uint32_t>(GltfCapabilityReason::kReady) == 0);
static_assert(static_cast<std::uint32_t>(
                  GltfCapabilityReason::kBinaryAdmissionPending) == 1);
static_assert(
    static_cast<std::uint32_t>(GltfCapabilityReason::kPrimitiveMode) == 2);
static_assert(static_cast<std::uint32_t>(GltfCapabilityReason::kSkinBinding) ==
              3);
static_assert(static_cast<std::uint32_t>(
                  GltfCapabilityReason::kJointWeightChannels) == 4);
static_assert(static_cast<std::uint32_t>(GltfCapabilityReason::kMorphBinding) ==
              5);
static_assert(
    static_cast<std::uint32_t>(GltfCapabilityReason::kAnimationBinding) == 6);
static_assert(static_cast<std::uint32_t>(
                  GltfCapabilityReason::kOptionalExtensionEffect) == 7);
static_assert(
    static_cast<std::uint32_t>(GltfCapabilityReason::kUnknownCoreRole) == 8);
static_assert(
    static_cast<std::uint32_t>(GltfCapabilityReason::kUnsupportedEnum) == 9);
static_assert(
    static_cast<std::uint32_t>(GltfCapabilityReason::kInvalidSelection) == 10);
static_assert(static_cast<std::uint32_t>(GltfCapabilityReason::kForeignOwner) ==
              11);
static_assert(static_cast<std::uint32_t>(
                  GltfCapabilityReason::kAdmissionProfileMismatch) == 12);
static_assert(
    static_cast<std::uint32_t>(GltfMaterializationDiagnosticCode::kNone) == 0);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kRequiredFieldZero) == 1);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kConfigurationExtent) ==
              2);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kChannelLimit) == 3);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kBridgeRequestLimit) == 4);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kSetIndexRepresentation) ==
              5);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kSourceDescriptor) == 6);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kRequestBudget) == 7);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kStopRequested) == 8);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kGeometry) == 9);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kStorageIssues) == 10);
static_assert(
    static_cast<std::uint32_t>(
        GltfMaterializationDiagnosticCode::kDeformationPolicyRequired) == 11);
static_assert(static_cast<std::uint32_t>(
                  GltfMaterializationDiagnosticCode::kDeformation) == 12);
static_assert(
    static_cast<std::uint32_t>(GltfTopologyWriteDiagnosticCode::kNone) == 0);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kRequiredFieldZero) == 1);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kConfigurationExtent) == 2);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kCountLimit) == 3);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kWriterRequestLimit) == 4);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kForeignBudget) == 5);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kCandidateStorage) == 6);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kSourceCapability) == 7);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kEmptyChangedSelection) ==
              8);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kFaceMapShape) == 9);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kFaceMapOrder) == 10);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kFaceMapRange) == 11);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kCornerMapShape) == 12);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kCornerMapValue) == 13);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kTriangleShape) == 14);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kTriangleLoop) == 15);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kPositionBits) == 16);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kChannelCount) == 17);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kAttributeDescriptor) == 18);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kAttributeBits) == 19);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kSourceDescriptor) == 20);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kSourceProvenance) == 21);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kLexicalSource) == 22);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kIndexRepresentation) == 23);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kOutputExtent) == 24);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kRequestBudget) == 25);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kStopRequested) == 26);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyWriteDiagnosticCode::kDeformationPolicyRequired) == 27);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyWriteDiagnosticCode::kDeformation) == 28);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kNone) == 0);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kRequiredFieldZero) == 1);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kConfigurationExtent) == 2);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kCountLimit) ==
              3);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kInspectionRequestLimit) ==
    4);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kForeignBudget) ==
              5);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kCandidateStorage) == 6);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kSourceIdentity) == 7);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kReloadIdentity) == 8);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kFaceMapShape) ==
              9);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kFaceMapOrder) ==
              10);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kFaceMapRange) ==
              11);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kCornerMapShape) == 12);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kCornerMapValue) == 13);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kTriangleShape) ==
              14);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kTriangleLoop) ==
              15);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kPositionBits) ==
              16);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kChannelCount) ==
              17);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kAttributeDescriptor) ==
    18);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kAttributeBits) ==
              19);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kEmptyChangedSelection) ==
    20);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kOutputJson) ==
              21);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kLexicalPreservation) ==
    22);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kAppendedBinding) == 23);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kResourceIdentity) == 24);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kResourceBytes) ==
              25);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kChannelDescriptor) == 26);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kChannelBits) ==
              27);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kSelectedTopology) == 28);
static_assert(
    static_cast<std::uint32_t>(
        GltfTopologyOutputInspectionDiagnosticCode::kSourceProvenance) == 29);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kRequestBudget) ==
              30);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kStopRequested) ==
              31);
static_assert(
    static_cast<std::uint32_t>(GltfTopologyOutputInspectionDiagnosticCode::
                                   kDeformationPolicyRequired) == 32);
static_assert(static_cast<std::uint32_t>(
                  GltfTopologyOutputInspectionDiagnosticCode::kDeformation) ==
              33);
static_assert(static_cast<std::uint32_t>(
                  GltfAdmissionProfile::kRetainedCoreMetadataV1) == 0);
static_assert(static_cast<std::uint32_t>(
                  GltfAdmissionProfile::kRetainedCoreDeformationV1) == 1);
static_assert(static_cast<std::uint32_t>(
                  GltfProcessingProfile::kStaticLocalTrianglesV1) == 0);
static_assert(static_cast<std::uint32_t>(
                  GltfProcessingProfile::kDirectTranslationTrianglesV1) == 1);
static_assert(static_cast<std::uint32_t>(
                  GltfProcessingProfile::kDeformationRetainedTrianglesV1) == 2);
static_assert(GltfReadRequest{}.admission_profile ==
              GltfAdmissionProfile::kRetainedCoreMetadataV1);
static_assert(GltfExtractionRequest{}.processing_profile ==
              GltfProcessingProfile::kStaticLocalTrianglesV1);
static_assert(!std::is_default_constructible_v<GltfDeformationProof>);
static_assert(std::is_nothrow_copy_constructible_v<GltfDeformationProof>);
static_assert(std::is_nothrow_copy_assignable_v<GltfDeformationProof>);
static_assert(std::is_nothrow_destructible_v<GltfDeformationProof>);
static_assert(std::same_as<
              decltype(std::declval<const GltfDeformationProof&>().Accessors()),
              std::span<const GltfDeformationAccessorView>>);
static_assert(
    std::same_as<decltype(std::declval<const GltfDeformationProof&>().Policy()),
                 const GltfDeformationPolicy&>);
static_assert(std::same_as<decltype(std::declval<const GltfAsset&>()
                                        .DeformationReadPolicy()),
                           const GltfDeformationPolicy*>);
static_assert(
    std::same_as<
        decltype(std::declval<const GltfPrimitiveExtraction&>().Deformation()),
        const GltfDeformationProof*>);
static_assert(std::same_as<decltype(GltfReadResult{}.deformation),
                           std::optional<GltfDeformationObservation>>);
static_assert(std::is_aggregate_v<GltfFileVerifiedContent>);
static_assert(
    std::same_as<decltype(GltfFileVerifiedContent::staged_deformation),
                 const GltfDeformationObservation*>);
static_assert(requires(const GltfTopologyOutputInspection& inspection,
                       GltfFileFormat format, std::string_view entry,
                       const GlbContainer* container,
                       std::span<const GltfFileView> files) {
  GltfFileVerifiedContent{inspection, format, entry, container, files};
});
}  // namespace
}  // namespace meshvale::interchange
