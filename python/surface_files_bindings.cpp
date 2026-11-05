// SPDX-License-Identifier: Apache-2.0
#include "surface_files_bindings.h"

#include <Python.h>
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "../src/gltf_allocation.h"
#include "surface_python_options.h"
#include "surface_python_pipeline.h"
#include "surface_python_publication.h"
#include "surface_python_views.h"
#include "surface_record_capture.h"

namespace meshvale::interchange::surface_python_detail {
namespace nb = nanobind;
namespace geo = geometry;
namespace alloc = gltf_detail;
namespace {
constexpr char kActivePayloadBytes[] = "active_payload_bytes";
constexpr char kActiveStagingPayloadBytes[] = "active_staging_payload_bytes";
constexpr char kActiveTrackedPayloadBytes[] = "active_tracked_payload_bytes";
constexpr char kActiveWorkers[] = "active_workers";
constexpr char kAdaptationExecution[] = "adaptation_execution";
constexpr char kAllocatedPayloadBytes[] = "allocated_payload_bytes";
constexpr char kAllocatedRequestBytes[] = "allocated_request_bytes";
constexpr char kAllocatedStagingPayloadBytes[] =
    "allocated_staging_payload_bytes";
constexpr char kAllocatedStagingRequestBytes[] =
    "allocated_staging_request_bytes";
constexpr char kAllocationCode[] = "allocation_code";
constexpr char kAllocationProvenance[] = "allocation_provenance";
constexpr char kAttemptedBuffers[] = "attempted_buffers";
constexpr char kAttribute[] = "attribute";
constexpr char kAttributes[] = "attributes";
constexpr char kBounded[] = "bounded";
constexpr char kBudgetExceeded[] = "budget-exceeded";
constexpr char kBufferBytes[] = "buffer_bytes";
constexpr char kBufferViews[] = "buffer_views";
constexpr char kBundle[] = "bundle";
constexpr char kByteCheckpoints[] = "byte_checkpoints";
constexpr char kByteOffset[] = "byte_offset";
constexpr char kBytes[] = "bytes";
constexpr char kBytesCompared[] = "bytes_compared";
constexpr char kBytesCopied[] = "bytes_copied";
constexpr char kBytesHashed[] = "bytes_hashed";
constexpr char kBytesRead[] = "bytes_read";
constexpr char kBytesWritten[] = "bytes_written";
constexpr char kCallerJobs[] = "caller_jobs";
constexpr char kCanceled[] = "canceled";
constexpr char kCancellation[] = "cancellation";
constexpr char kCaptured[] = "captured";
constexpr char kCatalog[] = "catalog";
constexpr char kCatalogRead[] = "catalog_read";
constexpr char kCatalogUv[] = "catalog_uv";
constexpr char kCategory[] = "category";
constexpr char kChannels[] = "channels";
constexpr char kClosedFiles[] = "closed_files";
constexpr char kCode[] = "code";
constexpr char kCleanup[] = "cleanup";
constexpr char kCommit[] = "commit";
constexpr char kComplete[] = "complete";
constexpr char kCompletedFiles[] = "completed_files";
constexpr char kCompletedJobs[] = "completed_jobs";
constexpr char kComponent[] = "component";
constexpr char kCopiedBufferBytes[] = "copied_buffer_bytes";
constexpr char kCopiedBytes[] = "copied_bytes";
constexpr char kCopiedDescriptorBytes[] = "copied_descriptor_bytes";
constexpr char kCore[] = "core";
constexpr char kCounts[] = "counts";
constexpr char kDescriptorBytes[] = "descriptor_bytes";
constexpr char kDiagnostic[] = "diagnostic";
constexpr char kElement[] = "element";
constexpr char kEncode[] = "encode";
constexpr char kError[] = "error";
constexpr char kExecution[] = "execution";
constexpr char kFace[] = "face";
constexpr char kFacts[] = "facts";
constexpr char kFinalInventory[] = "final_inventory";
constexpr char kFinalPayloadVerification[] = "final_payload_verification";
constexpr char kFinalReceiptVerification[] = "final_receipt_verification";
constexpr char kFinished[] = "finished";
constexpr char kFirstField[] = "first_field";
constexpr char kFirstRow[] = "first_row";
constexpr char kIndex[] = "index";
constexpr char kIngress[] = "ingress";
constexpr char kInspection[] = "inspection";
constexpr char kInspectionDiagnostic[] = "inspection_diagnostic";
constexpr char kInspectionStatus[] = "inspection_status";
constexpr char kItemCheckpoints[] = "item_checkpoints";
constexpr char kJoinedWorkers[] = "joined_workers";
constexpr char kJson[] = "json";
constexpr char kJsonSpan[] = "json_span";
constexpr char kKind[] = "kind";
constexpr char kLaunchedWorkers[] = "launched_workers";
constexpr char kLength[] = "length";
constexpr char kLimitExceeded[] = "limit-exceeded";
constexpr char kMaterialization[] = "materialization";
constexpr char kMaterials[] = "materials";
constexpr char kMemberOrdinal[] = "member_ordinal";
constexpr char kMeshRecord[] = "mesh_record";
constexpr char kMetadataEntries[] = "metadata_entries";
constexpr char kNewRequestedBytes[] = "new_requested_bytes";
constexpr char kNewRequestedBytesAtReturn[] = "new_requested_bytes_at_return";
constexpr char kNewRequestedPeakBytes[] = "new_requested_peak_bytes";
constexpr char kNotCalled[] = "not-called";
constexpr char kObservedDirectories[] = "observed_directories";
constexpr char kObservedFiles[] = "observed_files";
constexpr char kOffset[] = "offset";
constexpr char kOptions[] = "options";
constexpr char kOutcome[] = "outcome";
constexpr char kOutput[] = "output";
constexpr char kOutputCapture[] = "output_capture";
constexpr char kOutputCorners[] = "output_corners";
constexpr char kOutputFaces[] = "output_faces";
constexpr char kOutputInspection[] = "output_inspection";
constexpr char kOutputMaterialization[] = "output_materialization";
constexpr char kOutputVertices[] = "output_vertices";
constexpr char kPayloadInventory[] = "payload_inventory";
constexpr char kPayloadReread[] = "payload_reread";
constexpr char kPayloadWrite[] = "payload_write";
constexpr char kPeakTrackedPayloadBytes[] = "peak_tracked_payload_bytes";
constexpr char kPeakWorkers[] = "peak_workers";
constexpr char kPhaseStates[] = "phase_states";
constexpr char kPlannedJobs[] = "planned_jobs";
constexpr char kPreflightStopObserved[] = "preflight_stop_observed";
constexpr char kPreset[] = "preset";
constexpr char kProvider[] = "provider";
constexpr char kPublication[] = "publication";
constexpr char kReady[] = "ready";
constexpr char kReceiptPath[] = "receipt_path";
constexpr char kReceiptReread[] = "receipt_reread";
constexpr char kReceiptWrite[] = "receipt_write";
constexpr char kReportImport[] = "report_import";
constexpr char kResourceIndex[] = "resource_index";
constexpr char kResourceKind[] = "resource_kind";
constexpr char kResourceSpan[] = "resource_span";
constexpr char kResources[] = "resources";
constexpr char kReturned[] = "returned";
constexpr char kReturnedBytes[] = "returned_bytes";
constexpr char kRole[] = "role";
constexpr char kRuleId[] = "rule_id";
constexpr char kRunning[] = "running";
constexpr char kSecondaryIndex[] = "secondary_index";
constexpr char kSerialReason[] = "serial_reason";
constexpr char kSourceCapture[] = "source_capture";
constexpr char kSourceCorners[] = "source_corners";
constexpr char kSourceFaces[] = "source_faces";
constexpr char kSourceInspection[] = "source_inspection";
constexpr char kSourceVertices[] = "source_vertices";
constexpr char kStage[] = "stage";
constexpr char kState[] = "state";
constexpr char kStatus[] = "status";
constexpr char kStorageIssue[] = "storage_issue";
constexpr char kStorageValid[] = "storage_valid";
constexpr char kSubject[] = "subject";
constexpr char kSystemError[] = "system_error";
constexpr char kTableIndex[] = "table_index";
constexpr char kTerminalPhase[] = "terminal_phase";
constexpr char kThrew[] = "threw";
constexpr char kTrackedPayloadBudgetBytes[] = "tracked_payload_budget_bytes";
constexpr char kTransferExecution[] = "transfer_execution";
constexpr char kTriangulation[] = "triangulation";
constexpr char kUv[] = "uv";
constexpr char kValue[] = "value";
constexpr char kVerified[] = "verified";
constexpr char kWorkerBudget[] = "worker_budget";
constexpr char kWorkersUsed[] = "workers_used";

nb::str Text(std::string_view value) {
  if (value.size() > static_cast<std::size_t>(PY_SSIZE_T_MAX))
    throw std::overflow_error("surface framing exceeds Python extent");
  return nb::str(value.empty() ? "" : value.data(), value.size());
}
nb::object Frozen(nb::dict value) {
  auto result = nb::steal<nb::object>(PyDictProxy_New(value.ptr()));
  if (!result.is_valid()) throw nb::python_error();
  return result;
}
template <class Enum, std::size_t Count>
nb::str EnumText(Enum value,
                 const std::pair<Enum, std::string_view> (&names)[Count]) {
  for (const auto& [candidate, name] : names)
    if (candidate == value) return Text(name);
  throw std::logic_error("unknown native surface phase enumerator");
}
nb::str Name(geo::MaterializationStatus value) {
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
  return EnumText(value, kNames);
}
nb::str Name(geo::MaterializationDiagnosticCode value) {
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
  return EnumText(value, kNames);
}
nb::str Name(geo::StorageIssueCode value) {
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
  return EnumText(value, kNames);
}
nb::str Name(geo::TriangulationStatus value) {
  constexpr std::pair<geo::TriangulationStatus, std::string_view> kNames[] = {
      {geo::TriangulationStatus::kAccepted, "kAccepted"},
      {geo::TriangulationStatus::kBlocked, "kBlocked"},
      {geo::TriangulationStatus::kCanceled, "kCanceled"},
  };
  return EnumText(value, kNames);
}
nb::str Name(geo::ChargedTriangulationStatus value) {
  constexpr std::pair<geo::ChargedTriangulationStatus, std::string_view>
      kNames[] = {
          {geo::ChargedTriangulationStatus::kAccepted, "kAccepted"},
          {geo::ChargedTriangulationStatus::kInvalidConfiguration,
           "kInvalidConfiguration"},
          {geo::ChargedTriangulationStatus::kLimitExceeded, "kLimitExceeded"},
          {geo::ChargedTriangulationStatus::kBlocked, "kBlocked"},
          {geo::ChargedTriangulationStatus::kUnsupportedAllocationProfile,
           "kUnsupportedAllocationProfile"},
          {geo::ChargedTriangulationStatus::kBudgetExceeded, "kBudgetExceeded"},
          {geo::ChargedTriangulationStatus::kCanceled, "kCanceled"},
          {geo::ChargedTriangulationStatus::kDelegateFailure,
           "kDelegateFailure"},
  };
  return EnumText(value, kNames);
}
nb::str Name(geo::ChargedTriangulationStage value) {
  constexpr std::pair<geo::ChargedTriangulationStage, std::string_view>
      kNames[] = {
          {geo::ChargedTriangulationStage::kConfiguration, "kConfiguration"},
          {geo::ChargedTriangulationStage::kSourceCounts, "kSourceCounts"},
          {geo::ChargedTriangulationStage::kSourceCapture, "kSourceCapture"},
          {geo::ChargedTriangulationStage::kSourceInspection,
           "kSourceInspection"},
          {geo::ChargedTriangulationStage::kTriangulation, "kTriangulation"},
          {geo::ChargedTriangulationStage::kCorrespondence, "kCorrespondence"},
          {geo::ChargedTriangulationStage::kAttributeTransfer,
           "kAttributeTransfer"},
          {geo::ChargedTriangulationStage::kOutputMaterialization,
           "kOutputMaterialization"},
          {geo::ChargedTriangulationStage::kOutputInspection,
           "kOutputInspection"},
          {geo::ChargedTriangulationStage::kComplete, "kComplete"},
  };
  return EnumText(value, kNames);
}
nb::str Name(geo::ChargedTriangulationCode value) {
  constexpr std::pair<geo::ChargedTriangulationCode, std::string_view>
      kNames[] = {
          {geo::ChargedTriangulationCode::kNone, "kNone"},
          {geo::ChargedTriangulationCode::kRequiredFieldZero,
           "kRequiredFieldZero"},
          {geo::ChargedTriangulationCode::kConfigurationExtent,
           "kConfigurationExtent"},
          {geo::ChargedTriangulationCode::kUnsupportedCornerLimit,
           "kUnsupportedCornerLimit"},
          {geo::ChargedTriangulationCode::kSourceCountLimit,
           "kSourceCountLimit"},
          {geo::ChargedTriangulationCode::kOutputCountLimit,
           "kOutputCountLimit"},
          {geo::ChargedTriangulationCode::kMapEntryLimit, "kMapEntryLimit"},
          {geo::ChargedTriangulationCode::kCoreRequestBudget,
           "kCoreRequestBudget"},
          {geo::ChargedTriangulationCode::kBridgeRequestBudget,
           "kBridgeRequestBudget"},
          {geo::ChargedTriangulationCode::kSourceStorage, "kSourceStorage"},
          {geo::ChargedTriangulationCode::kFaceCornerLimit, "kFaceCornerLimit"},
          {geo::ChargedTriangulationCode::kRepeatedPosition,
           "kRepeatedPosition"},
          {geo::ChargedTriangulationCode::kDegenerateFace, "kDegenerateFace"},
          {geo::ChargedTriangulationCode::kNonplanarFace, "kNonplanarFace"},
          {geo::ChargedTriangulationCode::kBacktrackingBoundary,
           "kBacktrackingBoundary"},
          {geo::ChargedTriangulationCode::kSelfIntersection,
           "kSelfIntersection"},
          {geo::ChargedTriangulationCode::kNoGeometricEar, "kNoGeometricEar"},
          {geo::ChargedTriangulationCode::kDegenerateResult,
           "kDegenerateResult"},
          {geo::ChargedTriangulationCode::kSizeOverflow, "kSizeOverflow"},
          {geo::ChargedTriangulationCode::kRequestBudget, "kRequestBudget"},
          {geo::ChargedTriangulationCode::kStopRequested, "kStopRequested"},
          {geo::ChargedTriangulationCode::kMaterialization, "kMaterialization"},
          {geo::ChargedTriangulationCode::kInspection, "kInspection"},
          {geo::ChargedTriangulationCode::kOutputStorage, "kOutputStorage"},
          {geo::ChargedTriangulationCode::kAllocationProfile,
           "kAllocationProfile"},
  };
  return EnumText(value, kNames);
}
nb::str Name(geo::ChargedTriangulationSerialReason value) {
  constexpr std::pair<geo::ChargedTriangulationSerialReason, std::string_view>
      kNames[] = {
          {geo::ChargedTriangulationSerialReason::kNone, "kNone"},
          {geo::ChargedTriangulationSerialReason::kEmptyMesh, "kEmptyMesh"},
          {geo::ChargedTriangulationSerialReason::kWorkerBudgetOne,
           "kWorkerBudgetOne"},
          {geo::ChargedTriangulationSerialReason::kBelowFaceThreshold,
           "kBelowFaceThreshold"},
          {geo::ChargedTriangulationSerialReason::
               kSharedWorkersUnavailableOrFewerThanTwoFaces,
           "kSharedWorkersUnavailableOrFewerThanTwoFaces"},
  };
  return EnumText(value, kNames);
}
nb::str Name(GltfStatus value) {
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
  return EnumText(value, kNames);
}
nb::str Name(GltfDiagnosticCode value) {
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
  return EnumText(value, kNames);
}
nb::str Name(GltfResourceKind value) {
  constexpr std::pair<GltfResourceKind, std::string_view> kNames[] = {
      {GltfResourceKind::kInvalid, "kInvalid"},
      {GltfResourceKind::kBuffer, "kBuffer"},
      {GltfResourceKind::kImage, "kImage"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceStatus value) {
  constexpr std::pair<SurfaceStatus, std::string_view> kNames[] = {
      {SurfaceStatus::kSuccess, "kSuccess"},
      {SurfaceStatus::kInvalidConfiguration, "kInvalidConfiguration"},
      {SurfaceStatus::kInvalidStorage, "kInvalidStorage"},
      {SurfaceStatus::kUnsupported, "kUnsupported"},
      {SurfaceStatus::kLimitExceeded, "kLimitExceeded"},
      {SurfaceStatus::kBudgetExceeded, "kBudgetExceeded"},
      {SurfaceStatus::kCanceled, "kCanceled"},
      {SurfaceStatus::kDelegateFailure, "kDelegateFailure"},
      {SurfaceStatus::kVerificationFailed, "kVerificationFailed"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceStage value) {
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
  return EnumText(value, kNames);
}
nb::str Name(SurfaceCode value) {
  constexpr std::pair<SurfaceCode, std::string_view> kNames[] = {
      {SurfaceCode::kNone, "kNone"},
      {SurfaceCode::kRequiredFieldZero, "kRequiredFieldZero"},
      {SurfaceCode::kConfigurationExtent, "kConfigurationExtent"},
      {SurfaceCode::kUnknownEnum, "kUnknownEnum"},
      {SurfaceCode::kEncodingLimitsMismatch, "kEncodingLimitsMismatch"},
      {SurfaceCode::kInvalidBound, "kInvalidBound"},
      {SurfaceCode::kAllowanceTooSmall, "kAllowanceTooSmall"},
      {SurfaceCode::kForeignLedger, "kForeignLedger"},
      {SurfaceCode::kInvalidSourceStorage, "kInvalidSourceStorage"},
      {SurfaceCode::kSourceCount, "kSourceCount"},
      {SurfaceCode::kTriangleCount, "kTriangleCount"},
      {SurfaceCode::kSourceCorrespondence, "kSourceCorrespondence"},
      {SurfaceCode::kChannelCoverage, "kChannelCoverage"},
      {SurfaceCode::kChannelRole, "kChannelRole"},
      {SurfaceCode::kChannelShape, "kChannelShape"},
      {SurfaceCode::kChannelMetadata, "kChannelMetadata"},
      {SurfaceCode::kBareUnrepresented, "kBareUnrepresented"},
      {SurfaceCode::kLossNotAcknowledged, "kLossNotAcknowledged"},
      {SurfaceCode::kUvMap, "kUvMap"},
      {SurfaceCode::kUvReference, "kUvReference"},
      {SurfaceCode::kNormalAbsent, "kNormalAbsent"},
      {SurfaceCode::kNormalMissing, "kNormalMissing"},
      {SurfaceCode::kNormalNotConstant, "kNormalNotConstant"},
      {SurfaceCode::kNonfinite, "kNonfinite"},
      {SurfaceCode::kMaterialIds, "kMaterialIds"},
      {SurfaceCode::kMaterialBinding, "kMaterialBinding"},
      {SurfaceCode::kCatalogField, "kCatalogField"},
      {SurfaceCode::kCatalogExtension, "kCatalogExtension"},
      {SurfaceCode::kResourceBinding, "kResourceBinding"},
      {SurfaceCode::kResourceMedia, "kResourceMedia"},
      {SurfaceCode::kCountLimit, "kCountLimit"},
      {SurfaceCode::kDescriptorLimit, "kDescriptorLimit"},
      {SurfaceCode::kOutputLimit, "kOutputLimit"},
      {SurfaceCode::kMapLimit, "kMapLimit"},
      {SurfaceCode::kSizeOverflow, "kSizeOverflow"},
      {SurfaceCode::kNumericRange, "kNumericRange"},
      {SurfaceCode::kNonzeroUnderflow, "kNonzeroUnderflow"},
      {SurfaceCode::kExactness, "kExactness"},
      {SurfaceCode::kErrorBound, "kErrorBound"},
      {SurfaceCode::kNewCoincidence, "kNewCoincidence"},
      {SurfaceCode::kCollapsedEdge, "kCollapsedEdge"},
      {SurfaceCode::kCollapsedTriangle, "kCollapsedTriangle"},
      {SurfaceCode::kOrientation, "kOrientation"},
      {SurfaceCode::kRequestBudget, "kRequestBudget"},
      {SurfaceCode::kStopRequested, "kStopRequested"},
      {SurfaceCode::kGeometryDelegate, "kGeometryDelegate"},
      {SurfaceCode::kStlDelegate, "kStlDelegate"},
      {SurfaceCode::kGlbDelegate, "kGlbDelegate"},
      {SurfaceCode::kGltfDelegate, "kGltfDelegate"},
      {SurfaceCode::kMemberInventory, "kMemberInventory"},
      {SurfaceCode::kMemberPath, "kMemberPath"},
      {SurfaceCode::kMemberDigest, "kMemberDigest"},
      {SurfaceCode::kJsonSyntax, "kJsonSyntax"},
      {SurfaceCode::kSchema, "kSchema"},
      {SurfaceCode::kBinarySlice, "kBinarySlice"},
      {SurfaceCode::kCapsuleSource, "kCapsuleSource"},
      {SurfaceCode::kWireShape, "kWireShape"},
      {SurfaceCode::kWireValue, "kWireValue"},
      {SurfaceCode::kWireMap, "kWireMap"},
      {SurfaceCode::kMaterialReference, "kMaterialReference"},
      {SurfaceCode::kResourceBytes, "kResourceBytes"},
      {SurfaceCode::kUnits, "kUnits"},
      {SurfaceCode::kNumericProfile, "kNumericProfile"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceSubject value) {
  constexpr std::pair<SurfaceSubject, std::string_view> kNames[] = {
      {SurfaceSubject::kNone, "kNone"},
      {SurfaceSubject::kVertex, "kVertex"},
      {SurfaceSubject::kFace, "kFace"},
      {SurfaceSubject::kCorner, "kCorner"},
      {SurfaceSubject::kAttribute, "kAttribute"},
      {SurfaceSubject::kScalar, "kScalar"},
      {SurfaceSubject::kMember, "kMember"},
      {SurfaceSubject::kMaterial, "kMaterial"},
      {SurfaceSubject::kTexture, "kTexture"},
      {SurfaceSubject::kImage, "kImage"},
      {SurfaceSubject::kSampler, "kSampler"},
      {SurfaceSubject::kPrimitive, "kPrimitive"},
      {SurfaceSubject::kUvSet, "kUvSet"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceFilePublicationOutcome value) {
  constexpr std::pair<SurfaceFilePublicationOutcome, std::string_view>
      kNames[] = {
          {SurfaceFilePublicationOutcome::kFailed, "kFailed"},
          {SurfaceFilePublicationOutcome::kCanceled, "kCanceled"},
          {SurfaceFilePublicationOutcome::kPublished, "kPublished"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceFileCleanupOutcome value) {
  constexpr std::pair<SurfaceFileCleanupOutcome, std::string_view> kNames[] = {
      {SurfaceFileCleanupOutcome::kNotRequired, "kNotRequired"},
      {SurfaceFileCleanupOutcome::kRemoved, "kRemoved"},
      {SurfaceFileCleanupOutcome::kFailed, "kFailed"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceFileStatus value) {
  constexpr std::pair<SurfaceFileStatus, std::string_view> kNames[] = {
      {SurfaceFileStatus::kSuccess, "kSuccess"},
      {SurfaceFileStatus::kInvalidConfiguration, "kInvalidConfiguration"},
      {SurfaceFileStatus::kInvalidStorage, "kInvalidStorage"},
      {SurfaceFileStatus::kLimitExceeded, "kLimitExceeded"},
      {SurfaceFileStatus::kBudgetExceeded, "kBudgetExceeded"},
      {SurfaceFileStatus::kCanceled, "kCanceled"},
      {SurfaceFileStatus::kUnsupportedProfile, "kUnsupportedProfile"},
      {SurfaceFileStatus::kIoFailure, "kIoFailure"},
      {SurfaceFileStatus::kVerificationFailed, "kVerificationFailed"},
      {SurfaceFileStatus::kDestinationExists, "kDestinationExists"},
      {SurfaceFileStatus::kPublicationFailed, "kPublicationFailed"},
      {SurfaceFileStatus::kCallbackFailure, "kCallbackFailure"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceFileStage value) {
  constexpr std::pair<SurfaceFileStage, std::string_view> kNames[] = {
      {SurfaceFileStage::kConfiguration, "kConfiguration"},
      {SurfaceFileStage::kPathPlanning, "kPathPlanning"},
      {SurfaceFileStage::kStaging, "kStaging"},
      {SurfaceFileStage::kReread, "kReread"},
      {SurfaceFileStage::kInspection, "kInspection"},
      {SurfaceFileStage::kReceipt, "kReceipt"},
      {SurfaceFileStage::kInventory, "kInventory"},
      {SurfaceFileStage::kPublication, "kPublication"},
      {SurfaceFileStage::kComplete, "kComplete"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceFileCode value) {
  constexpr std::pair<SurfaceFileCode, std::string_view> kNames[] = {
      {SurfaceFileCode::kNone, "kNone"},
      {SurfaceFileCode::kRequiredFieldZero, "kRequiredFieldZero"},
      {SurfaceFileCode::kConfigurationExtent, "kConfigurationExtent"},
      {SurfaceFileCode::kUnknownEnum, "kUnknownEnum"},
      {SurfaceFileCode::kEncodingLimitsMismatch, "kEncodingLimitsMismatch"},
      {SurfaceFileCode::kAllowanceTooSmall, "kAllowanceTooSmall"},
      {SurfaceFileCode::kProviderConfiguration, "kProviderConfiguration"},
      {SurfaceFileCode::kDestinationPath, "kDestinationPath"},
      {SurfaceFileCode::kReceiptPath, "kReceiptPath"},
      {SurfaceFileCode::kPathCollision, "kPathCollision"},
      {SurfaceFileCode::kSourceContract, "kSourceContract"},
      {SurfaceFileCode::kCountLimit, "kCountLimit"},
      {SurfaceFileCode::kRequestBudget, "kRequestBudget"},
      {SurfaceFileCode::kStopRequested, "kStopRequested"},
      {SurfaceFileCode::kUnsupportedProfile, "kUnsupportedProfile"},
      {SurfaceFileCode::kDestinationParent, "kDestinationParent"},
      {SurfaceFileCode::kStageCreate, "kStageCreate"},
      {SurfaceFileCode::kStageAttemptsExhausted, "kStageAttemptsExhausted"},
      {SurfaceFileCode::kFileOpen, "kFileOpen"},
      {SurfaceFileCode::kFileRead, "kFileRead"},
      {SurfaceFileCode::kFileWrite, "kFileWrite"},
      {SurfaceFileCode::kFileClose, "kFileClose"},
      {SurfaceFileCode::kEnumeration, "kEnumeration"},
      {SurfaceFileCode::kMemberInventory, "kMemberInventory"},
      {SurfaceFileCode::kFileChanged, "kFileChanged"},
      {SurfaceFileCode::kFileKind, "kFileKind"},
      {SurfaceFileCode::kInspection, "kInspection"},
      {SurfaceFileCode::kCallerReportEncoding, "kCallerReportEncoding"},
      {SurfaceFileCode::kReceiptSize, "kReceiptSize"},
      {SurfaceFileCode::kReceiptBytes, "kReceiptBytes"},
      {SurfaceFileCode::kProvider, "kProvider"},
      {SurfaceFileCode::kDestinationExists, "kDestinationExists"},
      {SurfaceFileCode::kCommitFailed, "kCommitFailed"},
      {SurfaceFileCode::kCommitUnsupported, "kCommitUnsupported"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceFilePhaseOutcome value) {
  constexpr std::pair<SurfaceFilePhaseOutcome, std::string_view> kNames[] = {
      {SurfaceFilePhaseOutcome::kCompleted, "kCompleted"},
      {SurfaceFilePhaseOutcome::kFailed, "kFailed"},
      {SurfaceFilePhaseOutcome::kCanceled, "kCanceled"},
  };
  return EnumText(value, kNames);
}
nb::str Name(SurfaceFileCommitOutcome value) {
  constexpr std::pair<SurfaceFileCommitOutcome, std::string_view> kNames[] = {
      {SurfaceFileCommitOutcome::kCommitted, "kCommitted"},
      {SurfaceFileCommitOutcome::kDestinationExists, "kDestinationExists"},
      {SurfaceFileCommitOutcome::kFailed, "kFailed"},
      {SurfaceFileCommitOutcome::kUnsupported, "kUnsupported"},
  };
  return EnumText(value, kNames);
}
nb::str Name(alloc::AdmissionCode value) {
  constexpr std::pair<alloc::AdmissionCode, std::string_view> kNames[] = {
      {alloc::AdmissionCode::kSizeLimit, "kSizeLimit"},
      {alloc::AdmissionCode::kBudgetExceeded, "kBudgetExceeded"},
      {alloc::AdmissionCode::kCanceled, "kCanceled"},
      {alloc::AdmissionCode::kInvalidObject, "kInvalidObject"},
  };
  return EnumText(value, kNames);
}
nb::str Name(alloc::AdmissionProvenance value) {
  constexpr std::pair<alloc::AdmissionProvenance, std::string_view> kNames[] = {
      {alloc::AdmissionProvenance::kUnspecified, "kUnspecified"},
      {alloc::AdmissionProvenance::kOperationRequestLimit,
       "kOperationRequestLimit"},
  };
  return EnumText(value, kNames);
}
nb::object Frame(const geo::StorageIssue& value);
nb::object Frame(const geo::MaterializationDiagnostic& value);
nb::object Frame(const geo::MaterializationExecutionStats& value);
nb::object Frame(const geo::ChargedTriangulationDiagnostic& value);
nb::object Frame(const geo::ChargedTriangulationCounts& value);
nb::object Frame(const geo::ChargedTriangulationMaterializationFacts& value);
nb::object Frame(const geo::ChargedTriangulationInspectionFacts& value);
nb::object Frame(const geo::ChargedTriangulationCoreFacts& value);
nb::object Frame(const GltfSourceSpan& value);
nb::object Frame(const GltfDiagnostic& value);
nb::object Frame(const SurfaceDiagnostic& value);
nb::object Frame(const SurfaceFileDiagnostic& value);
nb::object Frame(const SurfaceFileCleanupObservation& value);
nb::object Frame(const SurfaceFileExecutionStats& value);
nb::object Frame(const SurfaceFileIoObservation& value);
nb::object Frame(const SurfaceFileInventoryObservation& value);
nb::object Frame(const SurfaceFileInspectionObservation& value);
nb::object Frame(const SurfaceFileProviderObservation& value);
nb::object Frame(const SurfaceFileCommitObservation& value);
nb::object Frame(const SurfaceFileFacts& value);
nb::object Frame(const geo::MeshMaterializationResult& value);
nb::object Frame(const geo::MeshInspectionResult& value);
nb::object Frame(const geo::ChargedTriangulationResult& value);
nb::object Frame(const GltfReadResult& value);
nb::object Frame(const SurfaceEncodeResult& value);
nb::object Frame(const SurfaceInspectionResult& value);
nb::object Frame(const SurfaceFilePublicationResult& value);
nb::object Frame(const std::error_code& value);
template <class T>
  requires std::is_arithmetic_v<T>
nb::object Value(T value) {
  return nb::cast(value);
}
template <class T>
  requires std::is_enum_v<T>
nb::object Value(T value) {
  return Name(value);
}
nb::object Value(const SurfaceFacts& value) { return SurfaceFactsView(value); }
template <class T>
  requires(!std::is_arithmetic_v<T> && !std::is_enum_v<T>)
nb::object Value(const T& value) {
  return Frame(value);
}
template <class T>
nb::object Value(const std::optional<T>& value) {
  if (value) return Value(*value);
  return nb::none();
}
nb::object Frame(const std::error_code& value) {
  nb::dict out;
  out[kValue] = nb::cast(value.value());
  out[kCategory] = Text(value.category().name());
  return Frozen(std::move(out));
}
nb::object Frame(const geo::StorageIssue& value) {
  nb::dict out;
  out[kCode] = Value(value.code);
  out[kAttribute] = Value(value.attribute);
  out[kElement] = Value(value.element);
  return Frozen(std::move(out));
}
nb::object Frame(const geo::MaterializationDiagnostic& value) {
  nb::dict out;
  out[kCode] = Value(value.code);
  out[kAttribute] = Value(value.attribute);
  out[kElement] = Value(value.element);
  out[kStorageIssue] = Value(value.storage_issue);
  return Frozen(std::move(out));
}
nb::object Frame(const geo::MaterializationExecutionStats& value) {
  nb::dict out;
  out[kPlannedJobs] = Value(value.planned_jobs);
  out[kLaunchedWorkers] = Value(value.launched_workers);
  out[kJoinedWorkers] = Value(value.joined_workers);
  out[kCallerJobs] = Value(value.caller_jobs);
  return Frozen(std::move(out));
}
nb::object Frame(const geo::ChargedTriangulationDiagnostic& value) {
  nb::dict out;
  out[kStage] = Value(value.stage);
  out[kCode] = Value(value.code);
  out[kFace] = Value(value.face);
  out[kAttribute] = Value(value.attribute);
  out[kStorageIssue] = Value(value.storage_issue);
  return Frozen(std::move(out));
}
nb::object Frame(const geo::ChargedTriangulationCounts& value) {
  nb::dict out;
  out[kSourceVertices] = Value(value.source_vertices);
  out[kSourceFaces] = Value(value.source_faces);
  out[kSourceCorners] = Value(value.source_corners);
  out[kOutputVertices] = Value(value.output_vertices);
  out[kOutputFaces] = Value(value.output_faces);
  out[kOutputCorners] = Value(value.output_corners);
  return Frozen(std::move(out));
}
nb::object Frame(const geo::ChargedTriangulationMaterializationFacts& value) {
  nb::dict out;
  out[kStatus] = Value(value.status);
  out[kDiagnostic] = Value(value.diagnostic);
  out[kExecution] = Value(value.execution);
  out[kNewRequestedBytesAtReturn] = Value(value.new_requested_bytes_at_return);
  return Frozen(std::move(out));
}
nb::object Frame(const geo::ChargedTriangulationInspectionFacts& value) {
  nb::dict out;
  out[kStatus] = Value(value.status);
  out[kDiagnostic] = Value(value.diagnostic);
  out[kExecution] = Value(value.execution);
  out[kStorageValid] = Value(value.storage_valid);
  out[kNewRequestedBytesAtReturn] = Value(value.new_requested_bytes_at_return);
  return Frozen(std::move(out));
}
nb::object Frame(const geo::ChargedTriangulationCoreFacts& value) {
  nb::dict out;
  out[kStatus] = Value(value.status);
  out[kWorkersUsed] = Value(value.workers_used);
  out[kSerialReason] = Value(value.serial_reason);
  out[kNewRequestedPeakBytes] = Value(value.new_requested_peak_bytes);
  return Frozen(std::move(out));
}
nb::object Frame(const GltfSourceSpan& value) {
  nb::dict out;
  out[kOffset] = Value(value.offset);
  out[kLength] = Value(value.length);
  return Frozen(std::move(out));
}
nb::object Frame(const GltfDiagnostic& value) {
  nb::dict out;
  out[kStatus] = Value(value.status);
  out[kCode] = Value(value.code);
  out[kJsonSpan] = Value(value.json_span);
  out[kResourceKind] = Value(value.resource_kind);
  out[kResourceIndex] = Value(value.resource_index);
  out[kResourceSpan] = Value(value.resource_span);
  out[kRuleId] = Value(value.rule_id);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceDiagnostic& value) {
  nb::dict out;
  out[kStage] = Value(value.stage);
  out[kCode] = Value(value.code);
  out[kSubject] = Value(value.subject);
  out[kIndex] = Value(value.index);
  out[kSecondaryIndex] = Value(value.secondary_index);
  out[kComponent] = Value(value.component);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileDiagnostic& value) {
  nb::dict out;
  out[kStatus] = Value(value.status);
  out[kStage] = Value(value.stage);
  out[kCode] = Value(value.code);
  out[kMemberOrdinal] = Value(value.member_ordinal);
  out[kByteOffset] = Value(value.byte_offset);
  out[kSystemError] = Value(value.system_error);
  out[kInspectionStatus] = Value(value.inspection_status);
  out[kInspectionDiagnostic] = Value(value.inspection_diagnostic);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileCleanupObservation& value) {
  nb::dict out;
  out[kOutcome] = Value(value.outcome);
  out[kError] = Value(value.error);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileExecutionStats& value) {
  nb::dict out;
  out[kPlannedJobs] = Value(value.planned_jobs);
  out[kLaunchedWorkers] = Value(value.launched_workers);
  out[kCallerJobs] = Value(value.caller_jobs);
  out[kJoinedWorkers] = Value(value.joined_workers);
  out[kCompletedJobs] = Value(value.completed_jobs);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileIoObservation& value) {
  nb::dict out;
  out[kOutcome] = Value(value.outcome);
  out[kBytesRead] = Value(value.bytes_read);
  out[kBytesWritten] = Value(value.bytes_written);
  out[kBytesCopied] = Value(value.bytes_copied);
  out[kBytesCompared] = Value(value.bytes_compared);
  out[kBytesHashed] = Value(value.bytes_hashed);
  out[kCompletedFiles] = Value(value.completed_files);
  out[kClosedFiles] = Value(value.closed_files);
  out[kExecution] = Value(value.execution);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileInventoryObservation& value) {
  nb::dict out;
  out[kOutcome] = Value(value.outcome);
  out[kObservedFiles] = Value(value.observed_files);
  out[kObservedDirectories] = Value(value.observed_directories);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileInspectionObservation& value) {
  nb::dict out;
  out[kStatus] = Value(value.status);
  out[kDiagnostic] = Value(value.diagnostic);
  out[kFacts] = Value(value.facts);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileProviderObservation& value) {
  nb::dict out;
  out[kOutcome] = Value(value.outcome);
  out[kReturnedBytes] = Value(value.returned_bytes);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileCommitObservation& value) {
  nb::dict out;
  out[kOutcome] = Value(value.outcome);
  out[kError] = Value(value.error);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFileFacts& value) {
  nb::dict out;
  out[kPayloadWrite] = Value(value.payload_write);
  out[kPayloadInventory] = Value(value.payload_inventory);
  out[kPayloadReread] = Value(value.payload_reread);
  out[kInspection] = Value(value.inspection);
  out[kProvider] = Value(value.provider);
  out[kReportImport] = Value(value.report_import);
  out[kReceiptWrite] = Value(value.receipt_write);
  out[kReceiptReread] = Value(value.receipt_reread);
  out[kFinalInventory] = Value(value.final_inventory);
  out[kFinalPayloadVerification] = Value(value.final_payload_verification);
  out[kFinalReceiptVerification] = Value(value.final_receipt_verification);
  out[kCommit] = Value(value.commit);
  return Frozen(std::move(out));
}
// Configuration transport is admitted here, independently of Python dataclass
// construction. The native option helper revalidates every primitive field.
constexpr char kValuesModule[] = "meshvale_interchange.surface_files";
constexpr char kPresetValues[] = "_values";
constexpr char kPresetFactory[] = "bounded";
constexpr char kOptionsType[] = "SurfaceOptions";
constexpr char kPresetType[] = "SurfacePreset";
constexpr char kChannelType[] = "SurfaceChannel";
constexpr char kUvType[] = "SurfaceUv";
constexpr char kMaterialType[] = "SurfaceMaterial";
constexpr char kCatalogUvType[] = "SurfaceCatalogUv";
constexpr char kCatalogType[] = "SurfaceCatalog";
constexpr char kResourceType[] = "SurfaceResource";
constexpr char kPublicationType[] = "SurfacePublication";

struct ConfigurationTypes {
  nb::module_ module = nb::module_::import_(kValuesModule);
  nb::object options = module.attr(kOptionsType);
  nb::object preset = module.attr(kPresetType);
  nb::object channel = module.attr(kChannelType);
  nb::object uv = module.attr(kUvType);
  nb::object material = module.attr(kMaterialType);
  nb::object catalog_uv = module.attr(kCatalogUvType);
  nb::object catalog = module.attr(kCatalogType);
  nb::object resource = module.attr(kResourceType);
  nb::object publication = module.attr(kPublicationType);
};
void ExactType(nb::handle value, nb::handle type, const char* message) {
  if (!PyType_Check(type.ptr()) ||
      Py_TYPE(value.ptr()) != reinterpret_cast<PyTypeObject*>(type.ptr()))
    throw nb::type_error(message);
}
std::size_t Host(std::uint64_t value) {
  if (value > std::numeric_limits<std::size_t>::max() ||
      value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::ptrdiff_t>::max()) ||
      value > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
    throw std::overflow_error("surface extent exceeds host representation");
  return static_cast<std::size_t>(value);
}
std::uint64_t Index(nb::handle value) {
  if (!PyLong_CheckExact(value.ptr()))
    throw nb::type_error("surface index requires built-in int, not bool");
  const auto result = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  (void)Host(result);
  return result;
}
std::size_t TupleSize(nb::handle value) {
  if (!PyTuple_CheckExact(value.ptr()))
    throw nb::type_error("surface rows require a tuple");
  return static_cast<std::size_t>(PyTuple_GET_SIZE(value.ptr()));
}
nb::handle Item(nb::handle tuple, std::size_t ordinal) {
  return nb::handle(
      PyTuple_GET_ITEM(tuple.ptr(), static_cast<Py_ssize_t>(ordinal)));
}
bool Equal(nb::handle value, const char* text) {
  if (!PyUnicode_CheckExact(value.ptr()))
    throw nb::type_error("surface selectors require built-in str");
  const auto result = PyUnicode_CompareWithASCIIString(value.ptr(), text);
  if (result == -1 && PyErr_Occurred()) throw nb::python_error();
  return result == 0;
}
SurfacePresetProjection Preset(nb::handle value,
                               const ConfigurationTypes& types) {
  auto source = nb::borrow<nb::object>(value);
  if (PyUnicode_CheckExact(value.ptr())) {
    if (!Equal(value, kBounded))
      throw nb::value_error("unknown explicit surface preset");
    source = types.preset.attr(kPresetFactory)();
  }
  ExactType(source, types.preset, "preset must be bounded or SurfacePreset");
  return ReadSurfacePreset(nb::getattr(source, kPresetValues));
}
void ExactRows(nb::handle options, const char* field, nb::handle row_type) {
  auto rows = nb::getattr(options, field);
  const auto count = TupleSize(rows);
  for (std::size_t i = 0; i < count; ++i)
    ExactType(Item(rows, i), row_type,
              "surface row must have its exact frozen type");
}
SurfaceOptionsPlan Options(nb::handle value,
                           const SurfacePresetProjection& preset,
                           const ConfigurationTypes& types) {
  ExactType(value, types.options, "options must be frozen SurfaceOptions");
  // Bound the exact-type admission walk before inspecting any row object.
  // Native validation below still owns all primitive/option policy rules.
  constexpr char kUvErrorRows[] = "max_uv_component_errors";
  const std::array kRowFields{kChannels, kUv, kMaterials, kCatalogUv,
                              kUvErrorRows};
  std::uint64_t admitted_rows = 0;
  for (const auto* field : kRowFields) {
    const auto count = TupleSize(nb::getattr(value, field));
    if (count > preset.ingress_limits.max_option_rows - admitted_rows)
      throw nb::value_error("surface option row cap exceeded");
    admitted_rows += count;
  }
  ExactRows(value, kChannels, types.channel);
  ExactRows(value, kUv, types.uv);
  ExactRows(value, kMaterials, types.material);
  ExactRows(value, kCatalogUv, types.catalog_uv);
  return ValidateSurfaceOptions(value, preset);
}

enum class RunState { kReady, kRunning, kFinished };
enum class PreparationFrame { kReady, kLimit, kBudget, kCount };
constexpr std::size_t kPreparationFrameCount =
    static_cast<std::size_t>(PreparationFrame::kCount);
constexpr char kPreparedTypeName[] = "PreparedSurface";
constexpr char kResultTypeName[] = "SurfaceResult";
constexpr char kExecutionTypeName[] = "SurfaceExecutionContext";
constexpr char kDeliveryTypeName[] = "SurfaceDeliveryError";
constexpr char kResultAttribute[] = "result";
constexpr char kRunMethod[] = "run";
constexpr char kPrepareFunction[] = "_surface_prepare";
constexpr char kSourceInspectionPhase[] = "source-inspection";
constexpr char kCatalogReadPhase[] = "catalog-read";
constexpr char kOutputCapturePhase[] = "output-capture";
constexpr char kPreparationWorkspace[] = "prepare.workspace";
constexpr std::array<const char*,
                     static_cast<std::size_t>(SurfaceNativePhase::kCount)>
    kNativePhaseNames{kMaterialization, kSourceInspectionPhase,
                      kTriangulation,   kCatalogReadPhase,
                      kEncode,          kInspection,
                      kPublication};
constexpr std::size_t kBindingPhaseCount = 2;  // Ingress and output capture.
constexpr std::array<const char*, kNativePhaseNames.size() + kBindingPhaseCount>
    kAllPhaseNames{kIngress,
                   kMaterialization,
                   kSourceInspectionPhase,
                   kTriangulation,
                   kCatalogReadPhase,
                   kEncode,
                   kOutputCapturePhase,
                   kInspection,
                   kPublication};

struct NativeController;
struct DestroyController {
  void operator()(NativeController*) const noexcept;
};
struct ResultShell {
  // This atomic belongs to the precreated interpreter shell: it remains
  // available even if no native control block was admitted during preparation.
  std::atomic<RunState> state{RunState::kReady};
  std::unique_ptr<NativeController, DestroyController> native;
  std::array<nb::object, kPreparationFrameCount> preparation;
  nb::object uncalled_phases;
  nb::object preparation_refusal_phases;
  nb::object delivery_error_type;
  nb::object delivery_message;
  PreparationFrame preparation_frame = PreparationFrame::kReady;
};
struct PreparedShell {
  nb::object result;
};
struct ExecutionShell {
  explicit ExecutionShell(nb::handle preset)
      : context(Preset(preset, ConfigurationTypes{}).execution) {}
  geo::ExecutionContext context;
};
template <class T>
T& Ready(nb::handle value) {
  if (!nb::inst_ready(value))
    throw nb::type_error("surface object is not initialized");
  return *nb::inst_ptr<T>(value);
}
template <class T>
nb::object NewShell() {
  auto object = nb::inst_alloc(nb::type<T>());
  std::construct_at(nb::inst_ptr<T>(object));
  nb::inst_mark_ready(object);
  return object;
}
void Readable(const ResultShell& result) {
  if (result.state.load(std::memory_order_acquire) == RunState::kRunning)
    throw std::runtime_error("surface result is running");
}
nb::object RecordStatus(const std::optional<RecordIngressStatus>& value) {
  if (!value) return nb::none();
  switch (*value) {
    case RecordIngressStatus::kCaptured:
      return Text(kCaptured);
    case RecordIngressStatus::kLimitExceeded:
      return Text(kLimitExceeded);
    case RecordIngressStatus::kBudgetExceeded:
      return Text(kBudgetExceeded);
    case RecordIngressStatus::kCanceled:
      return Text(kCanceled);
  }
  throw std::logic_error("unknown surface ingress status");
}
nb::object RecordField(const std::optional<RecordIngressField>& value) {
  if (!value) return nb::none();
  constexpr std::pair<RecordIngressField, std::string_view> kNames[] = {
      {RecordIngressField::kRecord, "record"},
      {RecordIngressField::kPositions, "positions"},
      {RecordIngressField::kFaceOffsets, "face_offsets"},
      {RecordIngressField::kCornerVertices, "corner_vertices"},
      {RecordIngressField::kAttributes, "attributes"},
      {RecordIngressField::kAttributeDescriptor, "attribute_descriptor"},
      {RecordIngressField::kAttributeValues, "attribute_values"},
      {RecordIngressField::kAttributeOffsets, "attribute_offsets"},
      {RecordIngressField::kAttributePresence, "attribute_presence"},
      {RecordIngressField::kMetadata, "metadata"},
      {RecordIngressField::kCatalogJson, "catalog.json"},
      {RecordIngressField::kCatalogResource, "catalog.resources"},
      {RecordIngressField::kWorkspace, "workspace"}};
  return EnumText(*value, kNames);
}
nb::dict IngressFields(const RecordIngressObservation& value) {
  nb::dict out;
  out[kStatus] = RecordStatus(value.status);
  out[kFirstField] = RecordField(value.first_field);
  out[kFirstRow] = Value(value.first_row);
  out[kAttemptedBuffers] = Value(value.attempted_buffers);
  out[kBufferViews] = Value(value.buffer_views);
  out[kBufferBytes] = Value(value.buffer_bytes);
  out[kAttributes] = Value(value.attributes);
  out[kMetadataEntries] = Value(value.metadata_entries);
  out[kDescriptorBytes] = Value(value.descriptor_bytes);
  out[kAllocatedStagingPayloadBytes] =
      Value(value.allocated_staging_payload_bytes);
  out[kAllocatedStagingRequestBytes] =
      Value(value.allocated_staging_request_bytes);
  out[kActiveStagingPayloadBytes] = Value(value.active_staging_payload_bytes);
  out[kCopiedBufferBytes] = Value(value.copied_buffer_bytes);
  out[kCopiedDescriptorBytes] = Value(value.copied_descriptor_bytes);
  out[kByteCheckpoints] = Value(value.byte_checkpoints);
  out[kItemCheckpoints] = Value(value.item_checkpoints);
  out[kPlannedJobs] = Value(value.planned_jobs);
  out[kLaunchedWorkers] = Value(value.launched_workers);
  out[kJoinedWorkers] = Value(value.joined_workers);
  out[kCallerJobs] = Value(value.caller_jobs);
  out[kPreflightStopObserved] = Value(value.preflight_stop_observed);
  out[kAllocationCode] = Value(value.allocation_code);
  out[kAllocationProvenance] = Value(value.allocation_provenance);
  return out;
}
void InitializeFallbacks(ResultShell& result, nb::handle delivery_error_type) {
  constexpr char kLateDeliveryMessage[] =
      "surface publication completed; inspect the retained result";
  result.delivery_error_type = nb::borrow<nb::object>(delivery_error_type);
  result.delivery_message = Text(kLateDeliveryMessage);
  for (std::size_t i = 0; i < kPreparationFrameCount; ++i) {
    auto frame = IngressFields(RecordIngressObservation{});
    frame[kAllocationCode] = nb::none();
    frame[kAllocationProvenance] = nb::none();
    const auto kind = static_cast<PreparationFrame>(i);
    if (kind != PreparationFrame::kReady) {
      frame[kStatus] = Text(kind == PreparationFrame::kLimit ? kLimitExceeded
                                                             : kBudgetExceeded);
      frame[kFirstField] = Text(kPreparationWorkspace);
      frame[kAllocationCode] =
          Name(kind == PreparationFrame::kLimit
                   ? alloc::AdmissionCode::kSizeLimit
                   : alloc::AdmissionCode::kBudgetExceeded);
      frame[kAllocationProvenance] =
          Name(kind == PreparationFrame::kLimit
                   ? alloc::AdmissionProvenance::kOperationRequestLimit
                   : alloc::AdmissionProvenance::kUnspecified);
    }
    result.preparation[i] = Frozen(std::move(frame));
  }
  nb::dict uncalled, refused;
  for (const auto* phase : kAllPhaseNames) {
    uncalled[phase] = Text(kNotCalled);
    refused[phase] =
        Text(std::string_view(phase) == kIngress ? kReturned : kNotCalled);
  }
  result.uncalled_phases = Frozen(std::move(uncalled));
  result.preparation_refusal_phases = Frozen(std::move(refused));
}

nb::object Frame(const geo::MeshMaterializationResult& value) {
  nb::dict out;
  out[kStatus] = Name(value.status);
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kExecution] = Frame(value.execution);
  out[kNewRequestedBytes] = nb::none();
  if (value.mesh)
    out[kNewRequestedBytes] = Value(value.mesh->NewRequestedBytes());
  return Frozen(std::move(out));
}
nb::object Frame(const geo::MeshInspectionResult& value) {
  nb::dict out;
  out[kStatus] = Name(value.status);
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kExecution] = Frame(value.execution);
  out[kNewRequestedBytes] = nb::none();
  out[kStorageValid] = nb::none();
  if (value.inspection) {
    out[kNewRequestedBytes] = Value(value.inspection->NewRequestedBytes());
    out[kStorageValid] = Value(value.inspection->IsStorageValid());
  }
  return Frozen(std::move(out));
}
nb::object Frame(const geo::ChargedTriangulationResult& value) {
  nb::dict out;
  out[kStatus] = Name(value.status);
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kNewRequestedBytes] = nb::none();
  if (value.triangulation)
    out[kNewRequestedBytes] = Value(value.triangulation->NewRequestedBytes());
  out[kCounts] = Value(value.counts);
  out[kSourceCapture] = Value(value.source_capture);
  out[kSourceInspection] = Value(value.source_inspection);
  out[kCore] = Value(value.core);
  out[kOutputMaterialization] = Value(value.output_materialization);
  out[kOutputInspection] = Value(value.output_inspection);
  out[kAdaptationExecution] = Frame(value.adaptation_execution);
  out[kTransferExecution] = Frame(value.transfer_execution);
  return Frozen(std::move(out));
}
nb::object Frame(const GltfReadResult& value) {
  nb::dict out;
  out[kStatus] = Name(value.diagnostic.status);
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kNewRequestedBytes] = nb::none();
  if (value.asset)
    out[kNewRequestedBytes] = Value(value.asset->NewRequestedBytes());
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceEncodeResult& value) {
  nb::dict out;
  out[kStatus] = Name(value.status);
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kFacts] = Value(value.facts);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceInspectionResult& value) {
  nb::dict out;
  out[kStatus] = Name(value.status);
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kFacts] = Value(value.facts);
  return Frozen(std::move(out));
}
nb::object Frame(const SurfaceFilePublicationResult& value) {
  nb::dict out;
  out[kOutcome] = Name(value.outcome);
  out[kDiagnostic] = Frame(value.diagnostic);
  out[kCleanup] = Frame(value.cleanup);
  out[kFacts] = Frame(value.facts);
  out[kNewRequestedBytes] = nb::none();
  if (value.bundle)
    out[kNewRequestedBytes] = Value(value.bundle->NewRequestedBytes());
  return Frozen(std::move(out));
}
nb::object OutputCapture(const SurfaceNativeResult& value) {
  if (value.delivery_call == SurfaceDeliveryCall::kNotCalled) return nb::none();
  nb::dict out;
  out[kStatus] = nb::none();
  if (value.delivery_call == SurfaceDeliveryCall::kCaptured)
    out[kStatus] = Text(kCaptured);
  else if (value.delivery_call == SurfaceDeliveryCall::kRefused) {
    if (value.delivery_admission == alloc::AdmissionCode::kBudgetExceeded)
      out[kStatus] = Text(kBudgetExceeded);
    else if (value.delivery_admission == alloc::AdmissionCode::kCanceled)
      out[kStatus] = Text(kCanceled);
    else if (value.delivery_admission == alloc::AdmissionCode::kSizeLimit &&
             value.delivery_provenance ==
                 alloc::AdmissionProvenance::kOperationRequestLimit)
      out[kStatus] = Text(kLimitExceeded);
    else
      throw std::logic_error("surface output capture refusal is unclassified");
  }
  out[kAllocationCode] = Value(value.delivery_admission);
  out[kAllocationProvenance] = Value(value.delivery_provenance);
  const auto& reached = value.delivery;
  out[kAllocatedPayloadBytes] = Value(reached.allocated_payload_bytes);
  out[kAllocatedRequestBytes] = Value(reached.allocated_request_bytes);
  out[kActivePayloadBytes] = Value(reached.active_payload_bytes);
  out[kCopiedBytes] = Value(reached.copied_bytes);
  out[kByteCheckpoints] = Value(reached.byte_checkpoints);
  out[kPlannedJobs] = Value(reached.planned_jobs);
  out[kLaunchedWorkers] = Value(reached.launched_workers);
  out[kJoinedWorkers] = Value(reached.joined_workers);
  out[kCallerJobs] = Value(reached.caller_jobs);
  return Frozen(std::move(out));
}
const char* CallState(SurfaceNativeCall value) {
  switch (value) {
    case SurfaceNativeCall::kNotCalled:
      return kNotCalled;
    case SurfaceNativeCall::kReturned:
      return kReturned;
    case SurfaceNativeCall::kThrew:
      return kThrew;
  }
  throw std::logic_error("unknown native surface call state");
}

struct CatalogRow {
  RecordBufferId storage;
  GltfResourceKind kind = GltfResourceKind::kInvalid;
  std::uint64_t table_index = 0;
};
struct CatalogWorkspace {
  CatalogWorkspace(std::size_t maximum, const alloc::AllocationContext& bridge)
      : rows(alloc::Allocator<CatalogRow>(bridge)),
        resources(alloc::Allocator<SuppliedGltfResource>(bridge)) {
    (void)alloc::CheckedProduct(maximum, sizeof(CatalogRow));
    (void)alloc::CheckedProduct(maximum, sizeof(SuppliedGltfResource));
    rows.resize(maximum);
    resources.resize(maximum);
  }
  RecordBufferId json;
  std::vector<CatalogRow, alloc::Allocator<CatalogRow>> rows;
  std::vector<SuppliedGltfResource, alloc::Allocator<SuppliedGltfResource>>
      resources;
  std::size_t count = 0;
  bool present = false;
};
bool IsGltf(SurfaceEncoding encoding) {
  return encoding == SurfaceEncoding::kGltfJson ||
         encoding == SurfaceEncoding::kGlb;
}
bool RequiresCatalog(nb::handle options) {
  constexpr char kMaterialRole[] = "material-id";
  auto rows = nb::getattr(options, kChannels);
  for (std::size_t i = 0; i < TupleSize(rows); ++i)
    if (Equal(nb::getattr(Item(rows, i), kRole), kMaterialRole)) return true;
  return false;
}
struct NativeController {
  NativeController(const geo::ExecutionContext& execution,
                   const alloc::AllocationContext& allocation,
                   const SurfacePresetProjection& selected,
                   const SurfaceOptionsPlan& selected_options,
                   nb::handle source_options, nb::handle source_publication,
                   const std::optional<SurfacePublicationPlan>& path_plan,
                   std::stop_token cancellation)
      : context(execution),
        bridge(allocation),
        preset(selected),
        option_plan(selected_options),
        external(cancellation),
        requires_catalog(RequiresCatalog(source_options)) {
    options.emplace(selected_options, source_options, bridge);
    source.emplace(preset.ingress_limits.record, preset.ingress_work, context,
                   bridge, ingress);
    record.emplace(*source);
    if (IsGltf(option_plan.encoding))
      catalog.emplace(Host(preset.ingress_limits.max_catalog_resource_bindings),
                      bridge);
    if (path_plan) publication.emplace(*path_plan, source_publication, bridge);
  }
  NativeController(const NativeController&) = delete;
  NativeController& operator=(const NativeController&) = delete;

  geo::ExecutionContext context;
  alloc::AllocationContext bridge;
  SurfacePresetProjection preset;
  SurfaceOptionsPlan option_plan;
  std::stop_token external;
  bool requires_catalog;
  // Observation precedes the owner that updates it during cleanup.
  RecordIngressObservation ingress;
  SurfaceNativeCall ingress_call = SurfaceNativeCall::kNotCalled;
  SurfaceNativeResult phases;
  std::optional<OwnedSurfaceOptions> options;
  std::optional<SharedSurfaceIngress> source;
  std::optional<SurfaceRecordCapture> record;
  std::optional<CatalogWorkspace> catalog;
  std::optional<OwnedSurfacePublication> publication;
};
void DestroyController::operator()(NativeController* value) const noexcept {
  if (!value) return;
  std::destroy_at(value);
  alloc::AllocationContext::Deallocate(value);
}
std::unique_ptr<NativeController, DestroyController> CreateController(
    const geo::ExecutionContext& execution,
    const alloc::AllocationContext& bridge,
    const SurfacePresetProjection& preset, const SurfaceOptionsPlan& options,
    nb::handle source_options, nb::handle publication,
    const std::optional<SurfacePublicationPlan>& path_plan,
    std::stop_token external) {
  auto* storage = static_cast<NativeController*>(
      bridge.Allocate(sizeof(NativeController), alignof(NativeController)));
  try {
    return std::unique_ptr<NativeController, DestroyController>(
        std::construct_at(storage, execution, bridge, preset, options,
                          source_options, publication, path_plan, external));
  } catch (...) {
    alloc::AllocationContext::Deallocate(storage);
    throw;
  }
}
void ReleaseSources(NativeController& control) {
  // GIL held. These existing source-release methods perform reference drops,
  // never read new input or call a native delegate.
  if (control.record) control.record->ReleasePythonSources();
  if (control.source) control.source->ReleasePythonSources();
}
void FinishInputs(NativeController& control) noexcept {
  // ReleaseSources has already run under GIL on every path before reaching
  // here. Destruction frees native storage before releasing each charge.
  static_assert(std::is_nothrow_destructible_v<SurfaceRecordCapture>);
  static_assert(std::is_nothrow_destructible_v<SharedSurfaceIngress>);
  control.record.reset();
  control.source.reset();
  control.catalog.reset();
  control.publication.reset();
  control.options.reset();
}
void CatalogShape(nb::handle value, NativeController& control,
                  const ConfigurationTypes& types) {
  if (value.is_none()) {
    if (control.requires_catalog || control.option_plan.materials ||
        control.option_plan.catalog_uv)
      throw nb::value_error(
          "catalog absence requires inactive material association");
    return;
  }
  if (!IsGltf(control.option_plan.encoding))
    throw nb::value_error("STL does not accept a catalog");
  ExactType(value, types.catalog,
            "catalog must be frozen SurfaceCatalog or None");
  auto resources = nb::getattr(value, kResources);
  const auto count = TupleSize(resources);
  if (count > control.catalog->rows.size())
    control.source->Refuse(RecordIngressStatus::kLimitExceeded,
                           RecordIngressField::kCatalogResource);
  for (std::size_t i = 0; i < count; ++i) {
    auto resource = Item(resources, i);
    ExactType(resource, types.resource,
              "catalog row must be frozen SurfaceResource");
    auto kind = nb::getattr(resource, kKind);
    constexpr char kBufferResource[] = "buffer";
    constexpr char kImageResource[] = "image";
    auto& row = control.catalog->rows[i];
    if (Equal(kind, kBufferResource))
      row.kind = GltfResourceKind::kBuffer;
    else if (Equal(kind, kImageResource))
      row.kind = GltfResourceKind::kImage;
    else
      throw nb::value_error("unknown catalog resource kind");
    row.table_index = Index(nb::getattr(resource, kTableIndex));
    control.source->PreflightCheckpoint(control.external);
  }
  control.catalog->count = count;
  control.catalog->present = true;
}
void PreflightCatalog(nb::handle value, NativeController& control) {
  if (value.is_none()) return;
  auto& catalog = *control.catalog;
  catalog.json = control.source->Acquire(
      nb::getattr(value, kJson), RecordBufferKind::kBytes,
      RecordIngressField::kCatalogJson, {}, control.external);
  auto resources = nb::getattr(value, kResources);
  for (std::size_t i = 0; i < catalog.count; ++i)
    catalog.rows[i].storage = control.source->Acquire(
        nb::getattr(Item(resources, i), kBytes), RecordBufferKind::kBytes,
        RecordIngressField::kCatalogResource, i, control.external);
}
std::optional<SurfaceCatalogInput> CapturedCatalog(NativeController& control) {
  if (!control.catalog || !control.catalog->present) return std::nullopt;
  auto& catalog = *control.catalog;
  for (std::size_t i = 0; i < catalog.count; ++i) {
    const auto& row = catalog.rows[i];
    catalog.resources[i] = {row.kind, row.table_index,
                            control.source->Bytes(row.storage)};
  }
  return SurfaceCatalogInput{control.source->Bytes(catalog.json),
                             std::span<const SuppliedGltfResource>(
                                 catalog.resources.data(), catalog.count)};
}
struct RunArguments {
  nb::handle record;
  nb::handle catalog{Py_None};
};
RunArguments Arguments(nb::args args, nb::kwargs kwargs) {
  if (args.size() > 1)
    throw nb::type_error("run accepts one record and keyword-only catalog");
  RunArguments result;
  if (args.size()) result.record = args[0];
  Py_ssize_t position = 0;
  PyObject* key = nullptr;
  PyObject* value = nullptr;
  bool catalog_seen = false;
  while (PyDict_Next(kwargs.ptr(), &position, &key, &value)) {
    if (PyUnicode_CompareWithASCIIString(key, kMeshRecord) == 0) {
      if (result.record.is_valid())
        throw nb::type_error("run got duplicate mesh_record");
      result.record = nb::handle(value);
    } else if (PyUnicode_CompareWithASCIIString(key, kCatalog) == 0) {
      if (catalog_seen) throw nb::type_error("run got duplicate catalog");
      result.catalog = nb::handle(value);
      catalog_seen = true;
    } else
      throw nb::type_error("run got an unknown keyword");
  }
  if (!result.record.is_valid())
    throw nb::type_error("run requires mesh_record");
  return result;
}

bool Published(const ResultShell& result) noexcept {
  if (!result.native) return false;
  const auto& publication = result.native->phases.publication;
  return publication &&
         publication->outcome == SurfaceFilePublicationOutcome::kPublished &&
         publication->bundle.has_value();
}
[[noreturn]] void DeliverPythonError(ResultShell& result,
                                     nb::handle existing_result) {
  nb::python_error original;
  if (!Published(result)) throw original;
  auto error = nb::steal<nb::object>(PyObject_CallFunctionObjArgs(
      result.delivery_error_type.ptr(), result.delivery_message.ptr(),
      static_cast<PyObject*>(nullptr)));
  if (!error.is_valid() || !PyExceptionInstance_Check(error.ptr()) ||
      PyObject_SetAttrString(error.ptr(), kResultAttribute,
                             existing_result.ptr()) != 0) {
    PyErr_Clear();
    original.restore();
    throw nb::python_error();
  }
  auto cause = nb::borrow<nb::object>(original.value());
  PyException_SetCause(error.ptr(), cause.release().ptr());
  PyErr_SetObject(result.delivery_error_type.ptr(), error.ptr());
  throw nb::python_error();
}
[[noreturn]] void Rethrow(ResultShell& result, nb::handle existing_result,
                          const std::exception_ptr& failure) {
  if (!Published(result)) std::rethrow_exception(failure);
  try {
    std::rethrow_exception(failure);
  } catch (nb::python_error& error) {
    error.restore();
  } catch (const std::bad_alloc&) {
    PyErr_NoMemory();
  } catch (const std::overflow_error& error) {
    PyErr_SetString(PyExc_OverflowError, error.what());
  } catch (const std::invalid_argument& error) {
    PyErr_SetString(PyExc_ValueError, error.what());
  } catch (const std::exception& error) {
    PyErr_SetString(PyExc_RuntimeError, error.what());
  } catch (...) {
    PyErr_SetString(PyExc_RuntimeError, "surface result delivery failed");
  }
  DeliverPythonError(result, existing_result);
}
template <class Getter>
nb::object Observe(nb::handle existing_result, Getter getter) {
  auto& result = Ready<ResultShell>(existing_result);
  Readable(result);
  try {
    return getter(result);
  } catch (...) {
    Rethrow(result, existing_result, std::current_exception());
  }
}
nb::object Run(nb::handle prepared, nb::args args, nb::kwargs kwargs) {
  auto& operation = Ready<PreparedShell>(prepared);
  auto& result = Ready<ResultShell>(operation.result);
  auto expected = RunState::kReady;
  // This is deliberately before argument parsing, imports, type lookup, any
  // exporter call or native input access, including preparation refusals.
  if (!result.state.compare_exchange_strong(expected, RunState::kRunning,
                                            std::memory_order_acq_rel))
    throw std::runtime_error("surface operation is single-use");
  auto& control = *result.native;
  std::exception_ptr failure;
  try {
    const auto input = Arguments(args, kwargs);
    const ConfigurationTypes types;
    control.ingress_call = SurfaceNativeCall::kThrew;
    CatalogShape(input.catalog, control, types);
    control.record->Preflight(input.record, control.external);
    PreflightCatalog(input.catalog, control);
    // Neither the initially stopped external source nor a pending Python
    // signal can replace complete representation validation above.
    control.source->LinkStopToken(control.external);
    control.source->StageBuffers(control.preset.mesh_work, control.external);
    control.record->CaptureDescriptors(control.external);
    control.source->Complete();
    control.ingress_call = SurfaceNativeCall::kReturned;
    const auto catalog = CapturedCatalog(control);
    std::optional<SurfacePublicationInput> publication;
    if (control.publication) publication.emplace(control.publication->Input());
    ReleaseSources(control);
    {
      nb::gil_scoped_release release;
      RunCapturedSurface(*control.record, *control.source, *control.options,
                         control.preset, control.context, catalog, publication,
                         control.external, control.phases);
    }
  } catch (const RecordIngressRefusal&) {
    control.ingress_call = SurfaceNativeCall::kReturned;
  } catch (...) {
    failure = std::current_exception();
  }
  // Stage/capture/native helpers have already joined on every return/throw.
  // GIL is held here. Source cleanup must precede publication of final state.
  ReleaseSources(control);
  FinishInputs(control);
  result.state.store(RunState::kFinished, std::memory_order_release);
  if (failure) Rethrow(result, operation.result, failure);
  if (PyErr_Occurred() || PyErr_CheckSignals() < 0)
    DeliverPythonError(result, operation.result);
  // Existing Python object only: no wrapper, proof, frame or owner allocation
  // lies between actual native commit and this caller-visible handoff.
  return nb::borrow<nb::object>(operation.result);
}
SurfaceCancellationReader g_cancellation_reader = nullptr;
nb::object Prepare(nb::handle options, nb::handle preset, nb::handle execution,
                   nb::handle cancellation, nb::handle publication,
                   nb::handle delivery_error_type) {
  const ConfigurationTypes types;
  const auto selected = Preset(preset, types);
  const auto option_plan = Options(options, selected, types);
  std::optional<SurfacePublicationPlan> path_plan;
  if (!publication.is_none()) {
    ExactType(publication, types.publication,
              "publication must be frozen SurfacePublication or None");
    path_plan.emplace(ValidateSurfacePublication(publication, selected,
                                                 option_plan.encoding));
  }
  if (!execution.is_none()) {
    ExactType(execution, nb::type<ExecutionShell>(),
              "execution must be SurfaceExecutionContext or None");
    (void)Ready<ExecutionShell>(execution);
  }
  if (!g_cancellation_reader)
    throw std::logic_error("surface cancellation reader unavailable");
  const auto stop = g_cancellation_reader(cancellation);
  // These objects and all refusal/phase frames exist before the first native
  // context, request seed, control block, path, options or workspace admission.
  auto existing_result = NewShell<ResultShell>();
  auto prepared = NewShell<PreparedShell>();
  auto& operation = Ready<PreparedShell>(prepared);
  operation.result = existing_result;
  auto& result = Ready<ResultShell>(existing_result);
  InitializeFallbacks(result, delivery_error_type);
  const auto context = execution.is_none()
                           ? geo::ExecutionContext(selected.execution)
                           : Ready<ExecutionShell>(execution).context;
  try {
    const alloc::AllocationContext root(context, {});
    const auto bridge = root.WithRequestLimit(
        Host(selected.ingress_limits.max_bridge_new_requested_bytes));
    result.native = CreateController(context, bridge, selected, option_plan,
                                     options, publication, path_plan, stop);
  } catch (const alloc::AdmissionFailure& error) {
    if (error.Code() == alloc::AdmissionCode::kSizeLimit &&
        error.Provenance() ==
            alloc::AdmissionProvenance::kOperationRequestLimit)
      result.preparation_frame = PreparationFrame::kLimit;
    else if (error.Code() == alloc::AdmissionCode::kBudgetExceeded &&
             error.Provenance() == alloc::AdmissionProvenance::kUnspecified)
      result.preparation_frame = PreparationFrame::kBudget;
    else
      throw;
    result.state.store(RunState::kFinished, std::memory_order_release);
  }
  return prepared;
}

nb::object PhaseStates(const ResultShell& result) {
  if (!result.native)
    return result.preparation_frame == PreparationFrame::kReady
               ? result.uncalled_phases
               : result.preparation_refusal_phases;
  const auto& control = *result.native;
  nb::dict states;
  states[kIngress] = Text(CallState(control.ingress_call));
  for (std::size_t i = 0; i < kNativePhaseNames.size(); ++i)
    states[kNativePhaseNames[i]] = Text(CallState(control.phases.calls[i]));
  const auto delivery = control.phases.delivery_call;
  states[kOutputCapturePhase] =
      Text(delivery == SurfaceDeliveryCall::kNotCalled ? kNotCalled
           : delivery == SurfaceDeliveryCall::kThrew   ? kThrew
                                                       : kReturned);
  return Frozen(std::move(states));
}
nb::object TerminalPhase(const ResultShell& result) {
  if (!result.native) {
    if (result.preparation_frame != PreparationFrame::kReady)
      return Text(kIngress);
    return nb::none();
  }
  const auto& control = *result.native;
  const auto& phases = control.phases;
  if (phases.complete) return Text(kComplete);
  if (!phases.started && control.ingress_call != SurfaceNativeCall::kNotCalled)
    return Text(kIngress);
  if (phases.delivery_call == SurfaceDeliveryCall::kThrew ||
      phases.delivery_call == SurfaceDeliveryCall::kRefused)
    return Text(kOutputCapturePhase);
  if (phases.terminal_phase)
    return Text(
        kNativePhaseNames.at(static_cast<std::size_t>(*phases.terminal_phase)));
  return nb::none();
}
nb::object Ingress(const ResultShell& result) {
  if (!result.native) {
    if (result.preparation_frame == PreparationFrame::kReady) return nb::none();
    return result
        .preparation[static_cast<std::size_t>(result.preparation_frame)];
  }
  if (result.native->ingress_call == SurfaceNativeCall::kNotCalled)
    return nb::none();
  return Frozen(IngressFields(result.native->ingress));
}
nb::object NativePhase(const ResultShell& result, SurfaceNativePhase phase) {
  if (!result.native) return nb::none();
  const auto& native = result.native->phases;
  switch (phase) {
    case SurfaceNativePhase::kMaterialization:
      return Value(native.materialization);
    case SurfaceNativePhase::kSourceInspection:
      return Value(native.source_inspection);
    case SurfaceNativePhase::kTriangulation:
      return Value(native.triangulation);
    case SurfaceNativePhase::kCatalogRead:
      return Value(native.catalog_read);
    case SurfaceNativePhase::kEncode:
      return Value(native.encode);
    case SurfaceNativePhase::kInspection:
      return Value(native.inspection);
    case SurfaceNativePhase::kPublication:
      return Value(native.publication);
    case SurfaceNativePhase::kCount:
      break;
  }
  throw std::logic_error("unknown surface result property");
}
nb::object Output(const ResultShell& result) {
  if (result.native && result.native->phases.encode &&
      result.native->phases.encode->output)
    return SurfaceOutputView(*result.native->phases.encode->output);
  return nb::none();
}
nb::object Verified(const ResultShell& result) {
  if (result.native && result.native->phases.inspection &&
      result.native->phases.inspection->inspection)
    return SurfaceInspectionView(*result.native->phases.inspection->inspection);
  return nb::none();
}
nb::object Bundle(const ResultShell& result) {
  if (result.native && result.native->phases.publication &&
      result.native->phases.publication->bundle)
    return SurfacePublishedView(*result.native->phases.publication->bundle);
  return nb::none();
}
const char* StateName(RunState state) {
  switch (state) {
    case RunState::kReady:
      return kReady;
    case RunState::kRunning:
      return kRunning;
    case RunState::kFinished:
      return kFinished;
  }
  throw std::logic_error("unknown surface operation state");
}
}  // namespace

void RegisterSurfaceFiles(nb::module_& module,
                          SurfaceCancellationReader reader) {
  if (!reader)
    throw std::invalid_argument("surface cancellation reader is required");
  g_cancellation_reader = reader;
  RegisterSurfaceViews(module);
  constexpr char kSnapshotFunction[] = "_surface_snapshot_destination";
  constexpr char kAlreadyResolvedArgument[] = "already_resolved";
  module.def(kSnapshotFunction, &SnapshotSurfaceDestination,
             nb::arg(kAlreadyResolvedArgument));

  constexpr char kDeliveryQualifiedName[] =
      "meshvale_interchange._interchange.SurfaceDeliveryError";
  auto delivery_error_type = nb::steal<nb::object>(
      PyErr_NewException(kDeliveryQualifiedName, PyExc_RuntimeError, nullptr));
  if (!delivery_error_type.is_valid()) throw nb::python_error();
  module.attr(kDeliveryTypeName) = delivery_error_type;

  nb::class_<ExecutionShell>(module, kExecutionTypeName, nb::is_final())
      .def(nb::init<nb::handle>(), nb::arg(kPreset) = kBounded)
      .def_prop_ro(kWorkerBudget,
                   [](const ExecutionShell& value) {
                     return value.context.WorkerBudget();
                   })
      .def_prop_ro(kActiveWorkers,
                   [](const ExecutionShell& value) {
                     return value.context.ActiveWorkers();
                   })
      .def_prop_ro(kPeakWorkers,
                   [](const ExecutionShell& value) {
                     return value.context.PeakWorkers();
                   })
      .def_prop_ro(kTrackedPayloadBudgetBytes,
                   [](const ExecutionShell& value) {
                     return value.context.TrackedPayloadBudget();
                   })
      .def_prop_ro(kActiveTrackedPayloadBytes,
                   [](const ExecutionShell& value) {
                     return value.context.ActiveTrackedPayload();
                   })
      .def_prop_ro(kPeakTrackedPayloadBytes, [](const ExecutionShell& value) {
        return value.context.PeakTrackedPayload();
      });

  auto result_type =
      nb::class_<ResultShell>(module, kResultTypeName, nb::is_final());
  result_type
      .def_prop_ro(
          kComplete,
          [](nb::handle self) {
            return Observe(self, [](const ResultShell& result) {
              return Value(result.native && result.native->phases.complete);
            });
          })
      .def_prop_ro(
          kTerminalPhase,
          [](nb::handle self) { return Observe(self, &TerminalPhase); })
      .def_prop_ro(kPhaseStates,
                   [](nb::handle self) { return Observe(self, &PhaseStates); })
      .def_prop_ro(kIngress,
                   [](nb::handle self) { return Observe(self, &Ingress); })
      .def_prop_ro(kOutputCapture,
                   [](nb::handle self) {
                     return Observe(
                         self, [](const ResultShell& result) -> nb::object {
                           if (result.native)
                             return OutputCapture(result.native->phases);
                           return nb::none();
                         });
                   })
      .def_prop_ro(kOutput,
                   [](nb::handle self) { return Observe(self, &Output); })
      .def_prop_ro(kVerified,
                   [](nb::handle self) { return Observe(self, &Verified); })
      .def_prop_ro(kBundle,
                   [](nb::handle self) { return Observe(self, &Bundle); });
  constexpr std::array<const char*, kNativePhaseNames.size()> kNativeProperties{
      kMaterialization, kSourceInspection, kTriangulation, kCatalogRead,
      kEncode,          kInspection,       kPublication};
  for (std::size_t i = 0; i < kNativeProperties.size(); ++i) {
    const auto phase = static_cast<SurfaceNativePhase>(i);
    result_type.def_prop_ro(kNativeProperties[i], [phase](nb::handle self) {
      return Observe(self, [phase](const ResultShell& result) {
        return NativePhase(result, phase);
      });
    });
  }
  nb::class_<PreparedShell>(module, kPreparedTypeName, nb::is_final())
      .def(kRunMethod, &Run)
      .def_prop_ro(kResultAttribute,
                   [](nb::handle self) {
                     const auto& operation = Ready<PreparedShell>(self);
                     Readable(Ready<ResultShell>(operation.result));
                     return nb::borrow<nb::object>(operation.result);
                   })
      .def_prop_ro(kState, [](nb::handle self) {
        const auto& operation = Ready<PreparedShell>(self);
        const auto& result = Ready<ResultShell>(operation.result);
        return Text(StateName(result.state.load(std::memory_order_acquire)));
      });
  module.def(
      kPrepareFunction,
      [delivery_error_type](nb::handle options, nb::handle preset,
                            nb::handle execution, nb::handle cancellation,
                            nb::handle publication) {
        return Prepare(options, preset, execution, cancellation, publication,
                       delivery_error_type);
      },
      nb::arg(kOptions), nb::arg(kPreset), nb::arg(kExecution).none(),
      nb::arg(kCancellation).none(), nb::arg(kPublication).none());
}
}  // namespace meshvale::interchange::surface_python_detail
