// SPDX-License-Identifier: Apache-2.0
#include "surface_python_views.h"

#include <Python.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace meshvale::interchange::surface_python_detail {
namespace nb = nanobind;
namespace geo = geometry;
namespace {

// Enumerator spelling is converted only here at the Python framing boundary.
// Each typed table is complete for the selected native declaration; unknown
// future enum values fail rather than invent an ordinal/string observation.

std::string_view EnumName(SurfaceStage value) {
  constexpr std::pair<SurfaceStage, std::string_view> kNames[] = {
      {SurfaceStage::kConfiguration, "kConfiguration"},
      {SurfaceStage::kSource, "kSource"},
      {SurfaceStage::kCorrespondence, "kCorrespondence"},
      {SurfaceStage::kDisposition, "kDisposition"},
      {SurfaceStage::kCatalog, "kCatalog"},
      {SurfaceStage::kPlan, "kPlan"},
      {SurfaceStage::kNumeric, "kNumeric"},
      {SurfaceStage::kEncode, "kEncode"},
      {SurfaceStage::kCapture, "kCapture"},
      {SurfaceStage::kContainerRead, "kContainerRead"},
      {SurfaceStage::kAssetRead, "kAssetRead"},
      {SurfaceStage::kPrimitiveRead, "kPrimitiveRead"},
      {SurfaceStage::kCapsuleRead, "kCapsuleRead"},
      {SurfaceStage::kCompare, "kCompare"},
      {SurfaceStage::kComplete, "kComplete"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(SurfaceMemberKind value) {
  constexpr std::pair<SurfaceMemberKind, std::string_view> kNames[] = {
      {SurfaceMemberKind::kGltf, "kGltf"},
      {SurfaceMemberKind::kGlb, "kGlb"},
      {SurfaceMemberKind::kStl, "kStl"},
      {SurfaceMemberKind::kGeometryBuffer, "kGeometryBuffer"},
      {SurfaceMemberKind::kImage, "kImage"},
      {SurfaceMemberKind::kCapsuleJson, "kCapsuleJson"},
      {SurfaceMemberKind::kCapsuleBinary, "kCapsuleBinary"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(SurfaceDelegateKind value) {
  constexpr std::pair<SurfaceDelegateKind, std::string_view> kNames[] = {
      {SurfaceDelegateKind::kDerivedMaterialization, "kDerivedMaterialization"},
      {SurfaceDelegateKind::kStlWrite, "kStlWrite"},
      {SurfaceDelegateKind::kGlbWrite, "kGlbWrite"},
      {SurfaceDelegateKind::kGlbRead, "kGlbRead"},
      {SurfaceDelegateKind::kGltfRead, "kGltfRead"},
      {SurfaceDelegateKind::kGltfExtract, "kGltfExtract"},
      {SurfaceDelegateKind::kStlRead, "kStlRead"},
      {SurfaceDelegateKind::kSourceMaterialFactors, "kSourceMaterialFactors"},
      {SurfaceDelegateKind::kWireMaterialFactors, "kWireMaterialFactors"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(SurfaceDelegateProfileStatus value) {
  constexpr std::pair<SurfaceDelegateProfileStatus, std::string_view> kNames[] =
      {
          {SurfaceDelegateProfileStatus::kEstablishedBaseline,
           "kEstablishedBaseline"},
          {SurfaceDelegateProfileStatus::kUnsupportedRounding,
           "kUnsupportedRounding"},
          {SurfaceDelegateProfileStatus::kUnsupportedFlushState,
           "kUnsupportedFlushState"},
          {SurfaceDelegateProfileStatus::kUnqueryable, "kUnqueryable"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(SurfaceNumericSummary::Kind value) {
  constexpr std::pair<SurfaceNumericSummary::Kind, std::string_view> kNames[] =
      {
          {SurfaceNumericSummary::Kind::kPreparedFields, "kPreparedFields"},
          {SurfaceNumericSummary::Kind::kDecodedWire, "kDecodedWire"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(GlbStatus value) {
  constexpr std::pair<GlbStatus, std::string_view> kNames[] = {
      {GlbStatus::kSuccess, "kSuccess"},
      {GlbStatus::kInvalidOptions, "kInvalidOptions"},
      {GlbStatus::kMalformed, "kMalformed"},
      {GlbStatus::kUnsupportedVersion, "kUnsupportedVersion"},
      {GlbStatus::kLimitExceeded, "kLimitExceeded"},
      {GlbStatus::kBudgetExceeded, "kBudgetExceeded"},
      {GlbStatus::kCanceled, "kCanceled"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(GltfStatus value) {
  constexpr std::pair<GltfStatus, std::string_view> kNames[] = {
      {GltfStatus::kAccepted, "kAccepted"},
      {GltfStatus::kInvalidLimits, "kInvalidLimits"},
      {GltfStatus::kInvalidRequest, "kInvalidRequest"},
      {GltfStatus::kMalformed, "kMalformed"},
      {GltfStatus::kUnsupportedProfile, "kUnsupportedProfile"},
      {GltfStatus::kResourceBindingMismatch, "kResourceBindingMismatch"},
      {GltfStatus::kCanceled, "kCanceled"},
      {GltfStatus::kBudgetExceeded, "kBudgetExceeded"},
      {GltfStatus::kInvalidSelection, "kInvalidSelection"},
      {GltfStatus::kForeignOwner, "kForeignOwner"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(GltfDiagnosticCode value) {
  constexpr std::pair<GltfDiagnosticCode, std::string_view> kNames[] = {
      {GltfDiagnosticCode::kNone, "kNone"},
      {GltfDiagnosticCode::kZeroLimit, "kZeroLimit"},
      {GltfDiagnosticCode::kLimitNotRepresentable, "kLimitNotRepresentable"},
      {GltfDiagnosticCode::kInvalidRequest, "kInvalidRequest"},
      {GltfDiagnosticCode::kInvalidBindingKind, "kInvalidBindingKind"},
      {GltfDiagnosticCode::kUnsupportedResourceRepresentation,
       "kUnsupportedResourceRepresentation"},
      {GltfDiagnosticCode::kEmbeddedUriSyntax, "kEmbeddedUriSyntax"},
      {GltfDiagnosticCode::kEmbeddedMediaMismatch, "kEmbeddedMediaMismatch"},
      {GltfDiagnosticCode::kLimitExceeded, "kLimitExceeded"},
      {GltfDiagnosticCode::kSizeOverflow, "kSizeOverflow"},
      {GltfDiagnosticCode::kJsonSyntax, "kJsonSyntax"},
      {GltfDiagnosticCode::kUnicodeScalar, "kUnicodeScalar"},
      {GltfDiagnosticCode::kDecodedDuplicateKey, "kDecodedDuplicateKey"},
      {GltfDiagnosticCode::kCoreFieldType, "kCoreFieldType"},
      {GltfDiagnosticCode::kCoreFieldRequired, "kCoreFieldRequired"},
      {GltfDiagnosticCode::kExactInteger, "kExactInteger"},
      {GltfDiagnosticCode::kCoreRange, "kCoreRange"},
      {GltfDiagnosticCode::kCoreArrayLength, "kCoreArrayLength"},
      {GltfDiagnosticCode::kCoreReference, "kCoreReference"},
      {GltfDiagnosticCode::kGraphRelationship, "kGraphRelationship"},
      {GltfDiagnosticCode::kBinaryLayout, "kBinaryLayout"},
      {GltfDiagnosticCode::kBinaryValue, "kBinaryValue"},
      {GltfDiagnosticCode::kIndexRole, "kIndexRole"},
      {GltfDiagnosticCode::kUnsupportedEnum, "kUnsupportedEnum"},
      {GltfDiagnosticCode::kUnsupportedNumberRepresentation,
       "kUnsupportedNumberRepresentation"},
      {GltfDiagnosticCode::kUnsupportedCoreString, "kUnsupportedCoreString"},
      {GltfDiagnosticCode::kRequiredExtension, "kRequiredExtension"},
      {GltfDiagnosticCode::kMissingResource, "kMissingResource"},
      {GltfDiagnosticCode::kDuplicateResource, "kDuplicateResource"},
      {GltfDiagnosticCode::kExtraResource, "kExtraResource"},
      {GltfDiagnosticCode::kResourceExtent, "kResourceExtent"},
      {GltfDiagnosticCode::kCanceled, "kCanceled"},
      {GltfDiagnosticCode::kPayloadBudget, "kPayloadBudget"},
      {GltfDiagnosticCode::kInvalidSelection, "kInvalidSelection"},
      {GltfDiagnosticCode::kForeignOwner, "kForeignOwner"},
      {GltfDiagnosticCode::kAdmissionProfileMismatch,
       "kAdmissionProfileMismatch"},
      {GltfDiagnosticCode::kNumericalProfile, "kNumericalProfile"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(GltfResourceKind value) {
  constexpr std::pair<GltfResourceKind, std::string_view> kNames[] = {
      {GltfResourceKind::kInvalid, "kInvalid"},
      {GltfResourceKind::kBuffer, "kBuffer"},
      {GltfResourceKind::kImage, "kImage"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(StlStatus value) {
  constexpr std::pair<StlStatus, std::string_view> kNames[] = {
      {StlStatus::kSuccess, "kSuccess"},
      {StlStatus::kInvalidConfiguration, "kInvalidConfiguration"},
      {StlStatus::kMalformed, "kMalformed"},
      {StlStatus::kUnsupportedVariant, "kUnsupportedVariant"},
      {StlStatus::kUnsupportedNumericConversion,
       "kUnsupportedNumericConversion"},
      {StlStatus::kLimitExceeded, "kLimitExceeded"},
      {StlStatus::kBudgetExceeded, "kBudgetExceeded"},
      {StlStatus::kCanceled, "kCanceled"},
      {StlStatus::kInvalidStorage, "kInvalidStorage"},
      {StlStatus::kGeometryFailure, "kGeometryFailure"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(StlStage value) {
  constexpr std::pair<StlStage, std::string_view> kNames[] = {
      {StlStage::kConfiguration, "kConfiguration"},
      {StlStage::kScan, "kScan"},
      {StlStage::kDecode, "kDecode"},
      {StlStage::kMaterialization, "kMaterialization"},
      {StlStage::kInspection, "kInspection"},
      {StlStage::kValidation, "kValidation"},
      {StlStage::kEncode, "kEncode"},
      {StlStage::kPublication, "kPublication"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(StlDiagnosticCode value) {
  constexpr std::pair<StlDiagnosticCode, std::string_view> kNames[] = {
      {StlDiagnosticCode::kNone, "kNone"},
      {StlDiagnosticCode::kRequiredFieldZero, "kRequiredFieldZero"},
      {StlDiagnosticCode::kConfigurationExtent, "kConfigurationExtent"},
      {StlDiagnosticCode::kEncoding, "kEncoding"},
      {StlDiagnosticCode::kSourceLimit, "kSourceLimit"},
      {StlDiagnosticCode::kFacetLimit, "kFacetLimit"},
      {StlDiagnosticCode::kNameLimit, "kNameLimit"},
      {StlDiagnosticCode::kLineLimit, "kLineLimit"},
      {StlDiagnosticCode::kTokenLimit, "kTokenLimit"},
      {StlDiagnosticCode::kOutputLimit, "kOutputLimit"},
      {StlDiagnosticCode::kSyntax, "kSyntax"},
      {StlDiagnosticCode::kNumericSyntax, "kNumericSyntax"},
      {StlDiagnosticCode::kNumericRange, "kNumericRange"},
      {StlDiagnosticCode::kNonfinite, "kNonfinite"},
      {StlDiagnosticCode::kBinaryExtent, "kBinaryExtent"},
      {StlDiagnosticCode::kTrailingBytes, "kTrailingBytes"},
      {StlDiagnosticCode::kAttributeWord, "kAttributeWord"},
      {StlDiagnosticCode::kRequestBudget, "kRequestBudget"},
      {StlDiagnosticCode::kStopRequested, "kStopRequested"},
      {StlDiagnosticCode::kGeometry, "kGeometry"},
      {StlDiagnosticCode::kStorageIssues, "kStorageIssues"},
      {StlDiagnosticCode::kNotTriangles, "kNotTriangles"},
      {StlDiagnosticCode::kNormalSelection, "kNormalSelection"},
      {StlDiagnosticCode::kUnsupportedAttribute, "kUnsupportedAttribute"},
      {StlDiagnosticCode::kMetadata, "kMetadata"},
      {StlDiagnosticCode::kExpansionAcknowledgment, "kExpansionAcknowledgment"},
      {StlDiagnosticCode::kNumericConversion, "kNumericConversion"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(StlEncoding value) {
  constexpr std::pair<StlEncoding, std::string_view> kNames[] = {
      {StlEncoding::kInvalid, "kInvalid"},
      {StlEncoding::kAscii, "kAscii"},
      {StlEncoding::kBinary, "kBinary"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(StlMetadataPolicy value) {
  constexpr std::pair<StlMetadataPolicy, std::string_view> kNames[] = {
      {StlMetadataPolicy::kExplicitWithoutSource, "kExplicitWithoutSource"},
      {StlMetadataPolicy::kRetainedSourceMetadata, "kRetainedSourceMetadata"},
      {StlMetadataPolicy::kReplacedSourceMetadata, "kReplacedSourceMetadata"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(geo::MaterializationStatus value) {
  constexpr std::pair<geo::MaterializationStatus, std::string_view> kNames[] = {
      {geo::MaterializationStatus::kSuccess, "kSuccess"},
      {geo::MaterializationStatus::kInvalidConfiguration,
       "kInvalidConfiguration"},
      {geo::MaterializationStatus::kInvalidTransport, "kInvalidTransport"},
      {geo::MaterializationStatus::kLimitExceeded, "kLimitExceeded"},
      {geo::MaterializationStatus::kUnsupportedAllocationProfile,
       "kUnsupportedAllocationProfile"},
      {geo::MaterializationStatus::kBudgetExceeded, "kBudgetExceeded"},
      {geo::MaterializationStatus::kCanceled, "kCanceled"},
      {geo::MaterializationStatus::kInvalidSourceStorage,
       "kInvalidSourceStorage"},
      {geo::MaterializationStatus::kInvalidSelection, "kInvalidSelection"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(geo::MaterializationDiagnosticCode value) {
  constexpr std::pair<geo::MaterializationDiagnosticCode, std::string_view>
      kNames[] = {
          {geo::MaterializationDiagnosticCode::kNone, "kNone"},
          {geo::MaterializationDiagnosticCode::kRequiredFieldZero,
           "kRequiredFieldZero"},
          {geo::MaterializationDiagnosticCode::kConfigurationExtent,
           "kConfigurationExtent"},
          {geo::MaterializationDiagnosticCode::kTransportSelector,
           "kTransportSelector"},
          {geo::MaterializationDiagnosticCode::kPositionExtent,
           "kPositionExtent"},
          {geo::MaterializationDiagnosticCode::kTriangleExtent,
           "kTriangleExtent"},
          {geo::MaterializationDiagnosticCode::kStorageExtent,
           "kStorageExtent"},
          {geo::MaterializationDiagnosticCode::kCountLimit, "kCountLimit"},
          {geo::MaterializationDiagnosticCode::kIssueLimit, "kIssueLimit"},
          {geo::MaterializationDiagnosticCode::kAllocationProfile,
           "kAllocationProfile"},
          {geo::MaterializationDiagnosticCode::kRequestBudget,
           "kRequestBudget"},
          {geo::MaterializationDiagnosticCode::kStopRequested,
           "kStopRequested"},
          {geo::MaterializationDiagnosticCode::kSourceStorage,
           "kSourceStorage"},
          {geo::MaterializationDiagnosticCode::kSelectionOrder,
           "kSelectionOrder"},
          {geo::MaterializationDiagnosticCode::kSelectionRange,
           "kSelectionRange"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

std::string_view EnumName(geo::StorageIssueCode value) {
  constexpr std::pair<geo::StorageIssueCode, std::string_view> kNames[] = {
      {geo::StorageIssueCode::kPositionNonfinite, "kPositionNonfinite"},
      {geo::StorageIssueCode::kFaceOffsetsEmpty, "kFaceOffsetsEmpty"},
      {geo::StorageIssueCode::kFaceOffsetsStart, "kFaceOffsetsStart"},
      {geo::StorageIssueCode::kFaceOffsetsEnd, "kFaceOffsetsEnd"},
      {geo::StorageIssueCode::kFaceOffsetRange, "kFaceOffsetRange"},
      {geo::StorageIssueCode::kFaceOffsetOrder, "kFaceOffsetOrder"},
      {geo::StorageIssueCode::kFaceTooShort, "kFaceTooShort"},
      {geo::StorageIssueCode::kCornerVertexRange, "kCornerVertexRange"},
      {geo::StorageIssueCode::kAttributeDuplicateName,
       "kAttributeDuplicateName"},
      {geo::StorageIssueCode::kAttributeDomain, "kAttributeDomain"},
      {geo::StorageIssueCode::kAttributeNameEmpty, "kAttributeNameEmpty"},
      {geo::StorageIssueCode::kAttributeComponentsZero,
       "kAttributeComponentsZero"},
      {geo::StorageIssueCode::kAttributeDenseShape, "kAttributeDenseShape"},
      {geo::StorageIssueCode::kAttributeOffsetsEmpty, "kAttributeOffsetsEmpty"},
      {geo::StorageIssueCode::kAttributeOffsetsCount, "kAttributeOffsetsCount"},
      {geo::StorageIssueCode::kAttributeOffsetsStart, "kAttributeOffsetsStart"},
      {geo::StorageIssueCode::kAttributeOffsetsEnd, "kAttributeOffsetsEnd"},
      {geo::StorageIssueCode::kAttributeOffsetRange, "kAttributeOffsetRange"},
      {geo::StorageIssueCode::kAttributeOffsetOrder, "kAttributeOffsetOrder"},
      {geo::StorageIssueCode::kAttributeOffsetAlignment,
       "kAttributeOffsetAlignment"},
      {geo::StorageIssueCode::kAttributePresenceCount,
       "kAttributePresenceCount"},
      {geo::StorageIssueCode::kAttributePresenceValue,
       "kAttributePresenceValue"},
      {geo::StorageIssueCode::kMetadataDuplicateKey, "kMetadataDuplicateKey"},
  };
  for (const auto& [candidate, name] : kNames)
    if (candidate == value) return name;
  throw std::logic_error("unknown native surface fact enumerator");
}

constexpr char kAttribute[] = "attribute";
constexpr char kAttributeBytes[] = "attribute_bytes";
constexpr char kAttributeOffsets[] = "attribute_offsets";
constexpr char kAttributeScalars[] = "attribute_scalars";
constexpr char kAttributes[] = "attributes";
constexpr char kAuthored[] = "authored";
constexpr char kByteOffset[] = "byte_offset";
constexpr char kBytes[] = "bytes";
constexpr char kCallOrdinal[] = "call_ordinal";
constexpr char kCallerJobs[] = "caller_jobs";
constexpr char kChangedValues[] = "changed_values";
constexpr char kCode[] = "code";
constexpr char kCodec[] = "codec";
constexpr char kCoincidenceComparisons[] = "coincidence_comparisons";
constexpr char kComplete[] = "complete";
constexpr char kComponent[] = "component";
constexpr char kCoreF32[] = "core_f32";
constexpr char kCornerExpansionAcknowledged[] = "corner_expansion_acknowledged";
constexpr char kCornerSources[] = "corner_sources";
constexpr char kCorners[] = "corners";
constexpr char kDazDisabled[] = "daz_disabled";
constexpr char kDelegates[] = "delegates";
constexpr char kDescriptorBytePairs[] = "descriptor_byte_pairs";
constexpr char kDescriptorBytes[] = "descriptor_bytes";
constexpr char kDiagnostic[] = "diagnostic";
constexpr char kElement[] = "element";
constexpr char kEncoding[] = "encoding";
constexpr char kErrorByteOffset[] = "error_byte_offset";
constexpr char kExaminedValues[] = "examined_values";
constexpr char kExaminedWork[] = "examined_work";
constexpr char kExecution[] = "execution";
constexpr char kExponent2[] = "exponent2";
constexpr char kFaceNormalAttribute[] = "face_normal_attribute";
constexpr char kFaceOffsets[] = "face_offsets";
constexpr char kFaceOutputOffsets[] = "face_output_offsets";
constexpr char kFaceSources[] = "face_sources";
constexpr char kFaces[] = "faces";
constexpr char kFacet[] = "facet";
constexpr char kFactorComponent[] = "factor_component";
constexpr char kFacts[] = "facts";
constexpr char kFailure[] = "failure";
constexpr char kFtzDisabled[] = "ftz_disabled";
constexpr char kGeometryDiagnostic[] = "geometry_diagnostic";
constexpr char kGeometryStatus[] = "geometry_status";
constexpr char kImages[] = "images";
constexpr char kInspection[] = "inspection";
constexpr char kJoinedWorkers[] = "joined_workers";
constexpr char kJsonSpan[] = "json_span";
constexpr char kKind[] = "kind";
constexpr char kLaunchedWorkers[] = "launched_workers";
constexpr char kLength[] = "length";
constexpr char kLexicalCalls[] = "lexical_calls";
constexpr char kMagnitude[] = "magnitude";
constexpr char kMapEntries[] = "map_entries";
constexpr char kMaterial[] = "material";
constexpr char kMaterialization[] = "materialization";
constexpr char kMaterials[] = "materials";
constexpr char kMaxAbsoluteError[] = "max_absolute_error";
constexpr char kMemberBytes[] = "member_bytes";
constexpr char kMembers[] = "members";
constexpr char kMetadataEntries[] = "metadata_entries";
constexpr char kMetadataPolicy[] = "metadata_policy";
constexpr char kNearestEven[] = "nearest_even";
constexpr char kNewRequestedBytes[] = "new_requested_bytes";
constexpr char kNumericDelegateProfiles[] = "numeric_delegate_profiles";
constexpr char kObserved[] = "observed";
constexpr char kOffset[] = "offset";
constexpr char kOriginalCounts[] = "original_counts";
constexpr char kOutput[] = "output";
constexpr char kPath[] = "path";
constexpr char kPlannedJobs[] = "planned_jobs";
constexpr char kPositionNumeric[] = "position_numeric";
constexpr char kPresenceBytes[] = "presence_bytes";
constexpr char kPrimitive[] = "primitive";
constexpr char kPrimitives[] = "primitives";
constexpr char kReceipt[] = "receipt";
constexpr char kRelationshipSteps[] = "relationship_steps";
constexpr char kRepresentation[] = "representation";
constexpr char kRequestedBytesAtReturn[] = "requested_bytes_at_return";
constexpr char kResourceIndex[] = "resource_index";
constexpr char kResourceKind[] = "resource_kind";
constexpr char kResourceSpan[] = "resource_span";
constexpr char kRuleId[] = "rule_id";
constexpr char kSamplers[] = "samplers";
constexpr char kSha256[] = "sha256";
constexpr char kSourceAttribute[] = "source_attribute";
constexpr char kSourceCornerOffsets[] = "source_corner_offsets";
constexpr char kSourceCornerWireCorners[] = "source_corner_wire_corners";
constexpr char kSourceEncoding[] = "source_encoding";
constexpr char kSourceFaceCount[] = "source_face_count";
constexpr char kSourceFaceOffsets[] = "source_face_offsets";
constexpr char kSourceFaceWireTriangles[] = "source_face_wire_triangles";
constexpr char kSourceRow[] = "source_row";
constexpr char kSourceScalarPairs[] = "source_scalar_pairs";
constexpr char kSourceVertexCount[] = "source_vertex_count";
constexpr char kSourceVertexOffsets[] = "source_vertex_offsets";
constexpr char kSourceVertexWireMembers[] = "source_vertex_wire_members";
constexpr char kStage[] = "stage";
constexpr char kStatus[] = "status";
constexpr char kStorageIssue[] = "storage_issue";
constexpr char kTextures[] = "textures";
constexpr char kTotal[] = "total";
constexpr char kTriangleCounts[] = "triangle_counts";
constexpr char kTriangles[] = "triangles";
constexpr char kTriangulationMaps[] = "triangulation_maps";
constexpr char kUsedWords[] = "used_words";
constexpr char kUvNumeric[] = "uv_numeric";
constexpr char kVerified[] = "verified";
constexpr char kVertices[] = "vertices";
constexpr char kWireCornerCorners[] = "wire_corner_corners";
constexpr char kWireCornerSources[] = "wire_corner_sources";
constexpr char kWireCounts[] = "wire_counts";
constexpr char kWireMaps[] = "wire_maps";
constexpr char kWireTriangleFaces[] = "wire_triangle_faces";
constexpr char kWireTriangleSources[] = "wire_triangle_sources";
constexpr char kWireVertexSources[] = "wire_vertex_sources";
constexpr char kWorkersUsed[] = "workers_used";
constexpr char kWriteFacts[] = "write_facts";

constexpr char kOutputTypeName[] = "SurfaceOutput";
constexpr char kInspectionTypeName[] = "SurfaceOutputInspection";
constexpr char kPublishedTypeName[] = "SurfacePublishedBundle";
constexpr char kFactsTypeName[] = "SurfaceFacts";
constexpr char kMemberTypeName[] = "SurfaceMember";
constexpr char kReceiptTypeName[] = "SurfaceReceipt";
constexpr char kBufferTypeName[] = "_SurfaceNativeBuffer";
constexpr char kGltfJsonSelector[] = "gltf-json";
constexpr char kGlbSelector[] = "glb";
constexpr char kStlAsciiSelector[] = "stl-ascii";
constexpr char kStlBinarySelector[] = "stl-binary";
constexpr char kStrictBareSelector[] = "strict-bare";
constexpr char kAnnotatedSelector[] = "annotated-bundle-v1";
constexpr char kByteBufferFormat[] = "B";
constexpr char kUint64BufferFormat[] = "Q";
constexpr int kBufferDimensions = 1;

nb::str Text(std::string_view value) {
  if (value.size() > static_cast<std::size_t>(PY_SSIZE_T_MAX))
    throw std::overflow_error("surface text exceeds Python extent");
  return nb::str(value.empty() ? "" : value.data(), value.size());
}
nb::object Frozen(nb::dict value) {
  auto result = nb::steal<nb::object>(PyDictProxy_New(value.ptr()));
  if (!result.is_valid()) throw nb::python_error();
  return result;
}
Py_ssize_t PythonSize(std::size_t value) {
  if (value > static_cast<std::size_t>(PY_SSIZE_T_MAX))
    throw std::overflow_error("surface view exceeds Python extent");
  return static_cast<Py_ssize_t>(value);
}
template <class T, class FrameValue>
nb::object Maybe(const std::optional<T>& value, FrameValue frame) {
  if (value) return frame(*value);
  return nb::none();
}
template <class T>
nb::object MaybeScalar(const std::optional<T>& value) {
  static_assert(std::is_integral_v<T>);
  return Maybe(value, [](T scalar) { return nb::cast(scalar); });
}
template <class T, std::size_t Size>
nb::tuple Words(const std::array<T, Size>& values) {
  static_assert(std::is_integral_v<T>);
  auto tuple = nb::steal<nb::tuple>(PyTuple_New(PythonSize(Size)));
  if (!tuple.is_valid()) throw nb::python_error();
  for (std::size_t i = 0; i < Size; ++i) {
    auto item = nb::cast(values[i]);
    PyTuple_SET_ITEM(tuple.ptr(), PythonSize(i), item.release().ptr());
  }
  return tuple;
}
template <class T, class FrameValue>
nb::tuple Rows(std::span<const T> values, FrameValue frame) {
  auto tuple = nb::steal<nb::tuple>(PyTuple_New(PythonSize(values.size())));
  if (!tuple.is_valid()) throw nb::python_error();
  for (std::size_t i = 0; i < values.size(); ++i) {
    auto item = frame(values[i]);
    PyTuple_SET_ITEM(tuple.ptr(), PythonSize(i), item.release().ptr());
  }
  return tuple;
}

nb::object Frame(const SurfaceMeshCounts& value) {
  nb::dict out;
  out[kVertices] = nb::cast(value.vertices);
  out[kFaces] = nb::cast(value.faces);
  out[kCorners] = nb::cast(value.corners);
  out[kFaceOffsets] = nb::cast(value.face_offsets);
  out[kAttributes] = nb::cast(value.attributes);
  out[kAttributeScalars] = nb::cast(value.attribute_scalars);
  out[kAttributeBytes] = nb::cast(value.attribute_bytes);
  out[kAttributeOffsets] = nb::cast(value.attribute_offsets);
  out[kPresenceBytes] = nb::cast(value.presence_bytes);
  out[kMetadataEntries] = nb::cast(value.metadata_entries);
  out[kDescriptorBytes] = nb::cast(value.descriptor_bytes);
  return Frozen(std::move(out));
}

nb::object Frame(const SurfaceWireCounts& value) {
  nb::dict out;
  out[kPrimitives] = nb::cast(value.primitives);
  out[kVertices] = nb::cast(value.vertices);
  out[kTriangles] = nb::cast(value.triangles);
  out[kCorners] = nb::cast(value.corners);
  out[kMapEntries] = nb::cast(value.map_entries);
  out[kMaterials] = nb::cast(value.materials);
  out[kTextures] = nb::cast(value.textures);
  out[kImages] = nb::cast(value.images);
  out[kSamplers] = nb::cast(value.samplers);
  out[kMembers] = nb::cast(value.members);
  out[kMemberBytes] = nb::cast(value.member_bytes);
  return Frozen(std::move(out));
}

nb::object Frame(const SurfaceWorkCounts& value) {
  nb::dict out;
  out[kSourceScalarPairs] = nb::cast(value.source_scalar_pairs);
  out[kDescriptorBytePairs] = nb::cast(value.descriptor_byte_pairs);
  out[kCoincidenceComparisons] = nb::cast(value.coincidence_comparisons);
  out[kRelationshipSteps] = nb::cast(value.relationship_steps);
  return Frozen(std::move(out));
}

nb::object Frame(const geo::MaterializationExecutionStats& value) {
  nb::dict out;
  out[kPlannedJobs] = nb::cast(value.planned_jobs);
  out[kLaunchedWorkers] = nb::cast(value.launched_workers);
  out[kJoinedWorkers] = nb::cast(value.joined_workers);
  out[kCallerJobs] = nb::cast(value.caller_jobs);
  return Frozen(std::move(out));
}

nb::object Frame(const GltfSourceSpan& value) {
  nb::dict out;
  out[kOffset] = nb::cast(value.offset);
  out[kLength] = nb::cast(value.length);
  return Frozen(std::move(out));
}

nb::object Frame(const StlRequestedBytes& value) {
  nb::dict out;
  out[kCodec] = nb::cast(value.codec);
  out[kMaterialization] = nb::cast(value.materialization);
  out[kInspection] = nb::cast(value.inspection);
  out[kTotal] = nb::cast(value.total);
  return Frozen(std::move(out));
}

nb::object Frame(const geo::StorageIssue& value) {
  nb::dict out;
  out[kCode] = Text(EnumName(value.code));
  out[kAttribute] = MaybeScalar(value.attribute);
  out[kElement] = MaybeScalar(value.element);
  return Frozen(std::move(out));
}
nb::object Frame(const geo::MaterializationDiagnostic& value) {
  nb::dict out;
  out[kCode] = Text(EnumName(value.code));
  out[kAttribute] = MaybeScalar(value.attribute);
  out[kElement] = MaybeScalar(value.element);
  out[kStorageIssue] = Maybe(value.storage_issue,
                             [](const auto& issue) { return Frame(issue); });
  return Frozen(std::move(out));
}
nb::object Frame(const GltfDiagnostic& value) {
  nb::dict out;
  out[kStatus] = Text(EnumName(value.status));
  out[kCode] = Text(EnumName(value.code));
  out[kJsonSpan] =
      Maybe(value.json_span, [](const auto& span) { return Frame(span); });
  out[kResourceKind] = Maybe(value.resource_kind,
                             [](auto kind) { return Text(EnumName(kind)); });
  out[kResourceIndex] = MaybeScalar(value.resource_index);
  out[kResourceSpan] =
      Maybe(value.resource_span, [](const auto& span) { return Frame(span); });
  out[kRuleId] = nb::cast(value.rule_id);
  return Frozen(std::move(out));
}
nb::object Frame(const StlDiagnostic& value) {
  nb::dict out;
  out[kStage] = Text(EnumName(value.stage));
  out[kCode] = Text(EnumName(value.code));
  out[kByteOffset] = MaybeScalar(value.byte_offset);
  out[kFacet] = MaybeScalar(value.facet);
  out[kAttribute] = MaybeScalar(value.attribute);
  out[kGeometryStatus] = Maybe(value.geometry_status, [](auto status) {
    return Text(EnumName(status));
  });
  out[kGeometryDiagnostic] =
      Maybe(value.geometry_diagnostic,
            [](const auto& diagnostic) { return Frame(diagnostic); });
  out[kStorageIssue] = Maybe(value.storage_issue,
                             [](const auto& issue) { return Frame(issue); });
  return Frozen(std::move(out));
}
nb::object Frame(const StlExecution& value) {
  nb::dict out;
  out[kCodec] = Frame(value.codec);
  out[kMaterialization] = Frame(value.materialization);
  out[kInspection] = Frame(value.inspection);
  return Frozen(std::move(out));
}
nb::object Frame(const StlWriteFacts& value) {
  nb::dict out;
  out[kEncoding] = Text(EnumName(value.encoding));
  out[kFaceNormalAttribute] = nb::cast(value.face_normal_attribute);
  out[kSourceVertexCount] = nb::cast(value.source_vertex_count);
  out[kSourceFaceCount] = nb::cast(value.source_face_count);
  out[kCornerExpansionAcknowledged] =
      nb::cast(value.corner_expansion_acknowledged);
  out[kSourceEncoding] = Maybe(value.source_encoding, [](auto encoding) {
    return Text(EnumName(encoding));
  });
  out[kMetadataPolicy] = Text(EnumName(value.metadata_policy));
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceGeometryFacts& value) {
  nb::dict out;
  out[kStatus] = Text(EnumName(value.status));
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kExecution] = Frame(value.execution);
  out[kRequestedBytesAtReturn] = MaybeScalar(value.requested_bytes_at_return);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceGlbFacts& value) {
  nb::dict out;
  out[kStatus] = Text(EnumName(value.status));
  out[kErrorByteOffset] = nb::cast(value.error_byte_offset);
  out[kWorkersUsed] = nb::cast(value.workers_used);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceGltfFacts& value) {
  nb::dict out;
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kRequestedBytesAtReturn] = MaybeScalar(value.requested_bytes_at_return);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceStlFacts& value) {
  nb::dict out;
  out[kStatus] = Text(EnumName(value.status));
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kExecution] = Frame(value.execution);
  out[kRequestedBytesAtReturn] =
      Maybe(value.requested_bytes_at_return,
            [](const auto& bytes) { return Frame(bytes); });
  out[kWriteFacts] =
      Maybe(value.write_facts, [](const auto& facts) { return Frame(facts); });
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceCoreFactorFacts& value) {
  nb::dict out;
  out[kMaterial] = nb::cast(value.material);
  out[kCoreF32] = Words(value.core_f32);
  out[kAuthored] = Words(value.authored);
  out[kObserved] = Words(value.observed);
  out[kLexicalCalls] = nb::cast(value.lexical_calls);
  out[kComplete] = nb::cast(value.complete);
  out[kFailure] = Maybe(
      value.failure, [](const auto& diagnostic) { return Frame(diagnostic); });
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceDelegateView& value) {
  nb::dict out;
  out[kStage] = Text(EnumName(value.stage));
  out[kKind] = Text(EnumName(value.kind));
  out[kCallOrdinal] = nb::cast(value.call_ordinal);
  out[kPrimitive] = MaybeScalar(value.primitive);
  out[kFacts] =
      std::visit([](const auto& facts) { return Frame(facts); }, value.facts);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceDyadicError& value) {
  nb::dict out;
  out[kMagnitude] = Words(value.magnitude);
  out[kUsedWords] = nb::cast(value.used_words);
  out[kExponent2] = nb::cast(value.exponent2);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceNumericSummary& value) {
  nb::dict out;
  out[kKind] = Text(EnumName(value.kind));
  out[kComplete] = nb::cast(value.complete);
  out[kExaminedValues] = nb::cast(value.examined_values);
  out[kChangedValues] = nb::cast(value.changed_values);
  out[kMaxAbsoluteError] = Frame(value.max_absolute_error);
  out[kSourceAttribute] = MaybeScalar(value.source_attribute);
  out[kSourceRow] = MaybeScalar(value.source_row);
  out[kComponent] = MaybeScalar(value.component);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceDelegateProfileFacts& value) {
  nb::dict out;
  out[kStatus] = Text(EnumName(value.status));
  out[kNearestEven] = MaybeScalar(value.nearest_even);
  out[kFtzDisabled] = MaybeScalar(value.ftz_disabled);
  out[kDazDisabled] = MaybeScalar(value.daz_disabled);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceDelegateProfileView& value) {
  nb::dict out;
  out[kStage] = Text(EnumName(value.stage));
  out[kKind] = Text(EnumName(value.kind));
  out[kCallOrdinal] = nb::cast(value.call_ordinal);
  out[kPrimitive] = MaybeScalar(value.primitive);
  out[kFactorComponent] = MaybeScalar(value.factor_component);
  out[kFacts] = Frame(value.facts);
  return Frozen(std::move(out));
}

using StrongOwner = std::variant<SurfaceOutput, SurfaceOutputInspection,
                                 SurfacePublishedBundle>;
enum class BufferKind { kBytes, kUint64 };

// No source payload copy or independent allocation/ledger exists in this
// exporter. Its native owner keeps the supplied immutable pointer alive.
class NativeBuffer {
 public:
  NativeBuffer(StrongOwner owner, const void* data, std::size_t count,
               BufferKind kind)
      : owner_(std::move(owner)),
        data_(data),
        count_(PythonSize(count)),
        item_size_(kind == BufferKind::kBytes ? sizeof(std::byte)
                                              : sizeof(std::uint64_t)),
        kind_(kind) {
    if (count > static_cast<std::size_t>(PY_SSIZE_T_MAX) /
                    static_cast<std::size_t>(item_size_))
      throw std::overflow_error(
          "surface buffer byte extent exceeds Python extent");
    bytes_ =
        static_cast<Py_ssize_t>(count * static_cast<std::size_t>(item_size_));
    if (bytes_ && !data_)
      throw std::logic_error("surface native buffer has no backing");
  }
  int GetBuffer(PyObject* self, Py_buffer* view, int flags) noexcept {
    if (PyBuffer_FillInfo(view, self, const_cast<void*>(data_), bytes_, true,
                          flags) != 0)
      return -1;
    view->itemsize = item_size_;
    view->ndim = kBufferDimensions;
    view->format = (flags & PyBUF_FORMAT)
                       ? const_cast<char*>(kind_ == BufferKind::kBytes
                                               ? kByteBufferFormat
                                               : kUint64BufferFormat)
                       : nullptr;
    view->shape = (flags & PyBUF_ND) ? &count_ : nullptr;
    view->strides =
        ((flags & PyBUF_STRIDES) == PyBUF_STRIDES) ? &item_size_ : nullptr;
    view->suboffsets = nullptr;
    view->internal = nullptr;
    return 0;
  }

 private:
  StrongOwner owner_;
  const void* data_;
  Py_ssize_t count_, item_size_, bytes_ = 0;
  BufferKind kind_;
};
int GetNativeBuffer(PyObject* self, Py_buffer* view, int flags) noexcept {
  const nb::handle object(self);
  if (!nb::inst_ready(object)) {
    PyErr_SetString(PyExc_BufferError,
                    "surface buffer owner is not initialized");
    return -1;
  }
  return nb::inst_ptr<NativeBuffer>(object)->GetBuffer(self, view, flags);
}
nb::object ExportBuffer(StrongOwner owner, const void* data, std::size_t count,
                        BufferKind kind) {
  auto exporter = nb::cast(NativeBuffer(std::move(owner), data, count, kind),
                           nb::rv_policy::move);
  auto view = nb::steal<nb::object>(PyMemoryView_FromObject(exporter.ptr()));
  if (!view.is_valid()) throw nb::python_error();
  return view;
}
nb::object Bytes(StrongOwner owner, std::span<const std::byte> values) {
  return ExportBuffer(std::move(owner), values.data(), values.size(),
                      BufferKind::kBytes);
}
nb::object Map(const SurfaceOutput& owner,
               std::span<const std::uint64_t> values) {
  return ExportBuffer(StrongOwner(owner), values.data(), values.size(),
                      BufferKind::kUint64);
}

struct OutputView {
  SurfaceOutput native;
};
struct InspectionView {
  SurfaceOutputInspection native;
};
struct PublishedView {
  SurfacePublishedBundle native;
};
struct FactsView {
  SurfaceFacts native;
};
struct ReceiptView {
  SurfacePublishedBundle native;
};
using MemberOwner = std::variant<SurfaceOutput, SurfaceOutputInspection>;
struct MemberView {
  MemberOwner native;
  std::size_t ordinal;
};

SurfaceMemberView NativeMember(const MemberView& view) {
  return std::visit(
      [&](const auto& owner) -> SurfaceMemberView {
        const auto members = [&] {
          if constexpr (std::is_same_v<std::decay_t<decltype(owner)>,
                                       SurfaceOutput>)
            return owner.Members();
          else
            return owner.CapturedMembers();
        }();
        if (view.ordinal >= members.size())
          throw std::logic_error("surface member ordinal is unavailable");
        return members[view.ordinal];
      },
      view.native);
}
nb::tuple Members(MemberOwner owner) {
  const auto count = std::visit(
      [](const auto& native) {
        if constexpr (std::is_same_v<std::decay_t<decltype(native)>,
                                     SurfaceOutput>)
          return native.Members().size();
        else
          return native.CapturedMembers().size();
      },
      owner);
  auto tuple = nb::steal<nb::tuple>(PyTuple_New(PythonSize(count)));
  if (!tuple.is_valid()) throw nb::python_error();
  for (std::size_t i = 0; i < count; ++i) {
    auto member = nb::cast(MemberView{owner, i}, nb::rv_policy::move);
    PyTuple_SET_ITEM(tuple.ptr(), PythonSize(i), member.release().ptr());
  }
  return tuple;
}
nb::str Encoding(SurfaceEncoding value) {
  switch (value) {
    case SurfaceEncoding::kGltfJson:
      return Text(kGltfJsonSelector);
    case SurfaceEncoding::kGlb:
      return Text(kGlbSelector);
    case SurfaceEncoding::kStlAscii:
      return Text(kStlAsciiSelector);
    case SurfaceEncoding::kStlBinary:
      return Text(kStlBinarySelector);
    case SurfaceEncoding::kInvalid:
      break;
  }
  throw std::logic_error("surface owner has an invalid encoding");
}
nb::str Representation(SurfaceRepresentation value) {
  switch (value) {
    case SurfaceRepresentation::kStrictBare:
      return Text(kStrictBareSelector);
    case SurfaceRepresentation::kAnnotatedBundleV1:
      return Text(kAnnotatedSelector);
    case SurfaceRepresentation::kInvalid:
      break;
  }
  throw std::logic_error("surface owner has an invalid representation");
}
nb::object WireMaps(const SurfaceOutput& output) {
  nb::dict maps;
  maps[kWireTriangleFaces] = Map(output, output.WireTriangleFaces());
  maps[kWireTriangleSources] = Map(output, output.WireTriangleSources());
  maps[kWireCornerCorners] = Map(output, output.WireCornerCorners());
  maps[kWireCornerSources] = Map(output, output.WireCornerSources());
  maps[kWireVertexSources] = Map(output, output.WireVertexSources());
  maps[kSourceVertexOffsets] = Map(output, output.SourceVertexOffsets());
  maps[kSourceVertexWireMembers] =
      Map(output, output.SourceVertexWireMembers());
  maps[kSourceFaceOffsets] = Map(output, output.SourceFaceOffsets());
  maps[kSourceFaceWireTriangles] =
      Map(output, output.SourceFaceWireTriangles());
  maps[kSourceCornerOffsets] = Map(output, output.SourceCornerOffsets());
  maps[kSourceCornerWireCorners] =
      Map(output, output.SourceCornerWireCorners());
  return Frozen(std::move(maps));
}
nb::object TriangulationMaps(const SurfaceOutput& output) {
  nb::dict maps;
  maps[kFaceSources] = Map(output, output.Triangles().FaceSources());
  maps[kCornerSources] = Map(output, output.Triangles().CornerSources());
  maps[kFaceOutputOffsets] =
      Map(output, output.Triangles().FaceOutputOffsets());
  return Frozen(std::move(maps));
}
}  // namespace

void RegisterSurfaceViews(nb::module_& module) {
  static const PyType_Slot kBufferSlots[] = {
      {Py_bf_getbuffer, reinterpret_cast<void*>(GetNativeBuffer)},
      {0, nullptr}};
  nb::class_<NativeBuffer>(module, kBufferTypeName,
                           nb::type_slots(kBufferSlots), nb::is_final());
  nb::class_<MemberView>(module, kMemberTypeName, nb::is_final())
      .def_prop_ro(
          kPath,
          [](const MemberView& view) { return Text(NativeMember(view).path); })
      .def_prop_ro(kKind,
                   [](const MemberView& view) {
                     return Text(EnumName(NativeMember(view).kind));
                   })
      .def_prop_ro(kBytes, [](const MemberView& view) {
        const auto bytes = NativeMember(view).bytes;
        return std::visit(
            [&](const auto& owner) { return Bytes(StrongOwner(owner), bytes); },
            view.native);
      });
  nb::class_<ReceiptView>(module, kReceiptTypeName, nb::is_final())
      .def_prop_ro(kPath,
                   [](const ReceiptView& view) {
                     return Text(view.native.Receipt().path);
                   })
      .def_prop_ro(kBytes,
                   [](const ReceiptView& view) {
                     return Bytes(StrongOwner(view.native),
                                  view.native.Receipt().bytes);
                   })
      .def_prop_ro(kSha256, [](const ReceiptView& view) {
        const auto receipt = view.native.Receipt();
        return nb::bytes(reinterpret_cast<const char*>(receipt.sha256.data()),
                         receipt.sha256.size());
      });
  nb::class_<FactsView>(module, kFactsTypeName, nb::is_final())
      .def_prop_ro(kOriginalCounts,
                   [](const FactsView& view) {
                     return Maybe(
                         view.native.OriginalCounts(),
                         [](const auto& counts) { return Frame(counts); });
                   })
      .def_prop_ro(kTriangleCounts,
                   [](const FactsView& view) {
                     return Maybe(
                         view.native.TriangleCounts(),
                         [](const auto& counts) { return Frame(counts); });
                   })
      .def_prop_ro(kWireCounts,
                   [](const FactsView& view) {
                     return Maybe(
                         view.native.WireCounts(),
                         [](const auto& counts) { return Frame(counts); });
                   })
      .def_prop_ro(kDelegates,
                   [](const FactsView& view) {
                     return Rows(view.native.Delegates(),
                                 [](const auto& row) { return Frame(row); });
                   })
      .def_prop_ro(kPositionNumeric,
                   [](const FactsView& view) {
                     return Maybe(view.native.PositionNumeric(),
                                  [](const auto& row) { return Frame(row); });
                   })
      .def_prop_ro(kUvNumeric,
                   [](const FactsView& view) {
                     return Rows(view.native.UvNumeric(),
                                 [](const auto& row) { return Frame(row); });
                   })
      .def_prop_ro(
          kExecution,
          [](const FactsView& view) { return Frame(view.native.Execution()); })
      .def_prop_ro(kExaminedWork,
                   [](const FactsView& view) {
                     return Frame(view.native.ExaminedWork());
                   })
      .def_prop_ro(kNumericDelegateProfiles,
                   [](const FactsView& view) {
                     return Rows(view.native.NumericDelegateProfiles(),
                                 [](const auto& row) { return Frame(row); });
                   })
      .def_prop_ro(kNewRequestedBytes, [](const FactsView& view) {
        return view.native.NewRequestedBytes();
      });
  nb::class_<OutputView>(module, kOutputTypeName, nb::is_final())
      .def_prop_ro(kEncoding,
                   [](const OutputView& view) {
                     return Encoding(view.native.Encoding());
                   })
      .def_prop_ro(kRepresentation,
                   [](const OutputView& view) {
                     return Representation(view.native.Representation());
                   })
      .def_prop_ro(kMembers,
                   [](const OutputView& view) {
                     return Members(MemberOwner(view.native));
                   })
      .def_prop_ro(kWireMaps,
                   [](const OutputView& view) { return WireMaps(view.native); })
      .def_prop_ro(
          kTriangulationMaps,
          [](const OutputView& view) { return TriangulationMaps(view.native); })
      .def_prop_ro(kFacts, [](const OutputView& view) {
        return SurfaceFactsView(view.native.Facts());
      });
  nb::class_<InspectionView>(module, kInspectionTypeName, nb::is_final())
      .def_prop_ro(kMembers,
                   [](const InspectionView& view) {
                     return Members(MemberOwner(view.native));
                   })
      .def_prop_ro(kFacts,
                   [](const InspectionView& view) {
                     return SurfaceFactsView(view.native.Facts());
                   })
      .def_prop_ro(kOutput, [](const InspectionView& view) {
        return SurfaceOutputView(view.native.Expected());
      });
  nb::class_<PublishedView>(module, kPublishedTypeName, nb::is_final())
      .def_prop_ro(kVerified,
                   [](const PublishedView& view) {
                     return SurfaceInspectionView(view.native.Inspection());
                   })
      .def_prop_ro(kReceipt, [](const PublishedView& view) {
        return nb::cast(ReceiptView{view.native}, nb::rv_policy::move);
      });
}
nb::object SurfaceOutputView(const SurfaceOutput& output) {
  return nb::cast(OutputView{output}, nb::rv_policy::move);
}
nb::object SurfaceInspectionView(const SurfaceOutputInspection& inspection) {
  return nb::cast(InspectionView{inspection}, nb::rv_policy::move);
}
nb::object SurfacePublishedView(const SurfacePublishedBundle& published) {
  return nb::cast(PublishedView{published}, nb::rv_policy::move);
}
nb::object SurfaceFactsView(const SurfaceFacts& facts) {
  return nb::cast(FactsView{facts}, nb::rv_policy::move);
}

}  // namespace meshvale::interchange::surface_python_detail
