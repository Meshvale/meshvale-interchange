// SPDX-License-Identifier: Apache-2.0
#include "stl_files_bindings.h"

#include <Python.h>
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/geometry/python/record.h>
#include <meshvale/interchange/stl.h>
#include <meshvale/interchange/stl_files.h>
#include <nanobind/stl/string_view.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
#include <variant>
#include <vector>

#include "../src/gltf_allocation.h"
#include "../src/stl_files_internal.h"

namespace meshvale::interchange::stl_python_detail {
namespace nb = nanobind;
namespace geo = geometry;
namespace io = interchange;
namespace sd = stl_files_detail;
namespace alloc = gltf_detail;
namespace {
namespace wire_key {
constexpr char kAcknowledgeCornerExpansion[] = "acknowledge_corner_expansion";
constexpr char kAdmissionDiagnostic[] = "admission_diagnostic";
constexpr char kApplicationLimits[] = "application_limits";
constexpr char kAsciiName[] = "ascii_name";
constexpr char kAsset[] = "asset";
constexpr char kAttributes[] = "attributes";
constexpr char kBatchExecution[] = "batch_execution";
constexpr char kBinaryHeader[] = "binary_header";
constexpr char kBinaryHeaderHex[] = "binary_header_hex";
constexpr char kBundle[] = "bundle";
constexpr char kBytes[] = "bytes";
constexpr char kBytesWritten[] = "bytes_written";
constexpr char kCancellationBlockBytes[] = "cancellation_block_bytes";
constexpr char kCancellationBlockFacets[] = "cancellation_block_facets";
constexpr char kCancellationBlockScalars[] = "cancellation_block_scalars";
constexpr char kCancellationBlockTokens[] = "cancellation_block_tokens";
constexpr char kCaptured[] = "captured";
constexpr char kCategory[] = "category";
constexpr char kCleanup[] = "cleanup";
constexpr char kCleanupError[] = "cleanup_error";
constexpr char kCode[] = "code";
constexpr char kComponents[] = "components";
constexpr char kCornerVertices[] = "corner_vertices";
constexpr char kDeclarations[] = "declarations";
constexpr char kDelivery[] = "delivery";
constexpr char kDestination[] = "destination";
constexpr char kDiagnostic[] = "diagnostic";
constexpr char kDomain[] = "domain";
constexpr char kEntries[] = "entries";
constexpr char kExecution[] = "execution";
constexpr char kExitCode[] = "exit_code";
constexpr char kFaceOffsets[] = "face_offsets";
constexpr char kFacts[] = "facts";
constexpr char kField[] = "field";
constexpr char kFileDiagnostic[] = "file_diagnostic";
constexpr char kFiles[] = "files";
constexpr char kFreshDocument[] = "fresh_document";
constexpr char kFreshRead[] = "fresh_read";
constexpr char kGeometry[] = "geometry";
constexpr char kInput[] = "input";
constexpr char kInspection[] = "inspection";
constexpr char kIoBlockBytes[] = "io_block_bytes";
constexpr char kJobId[] = "job_id";
constexpr char kJobIndex[] = "job_index";
constexpr char kJobs[] = "jobs";
constexpr char kKind[] = "kind";
constexpr char kLabel[] = "label";
constexpr char kLineIndex[] = "line_index";
constexpr char kMaterialization[] = "materialization";
constexpr char kMaxApplicationNewRequestedBytes[] =
    "max_application_new_requested_bytes";
constexpr char kMaxAttributeOffsets[] = "max_attribute_offsets";
constexpr char kMaxAttributeScalars[] = "max_attribute_scalars";
constexpr char kMaxAttributes[] = "max_attributes";
constexpr char kMaxCallerReportBytes[] = "max_caller_report_bytes";
constexpr char kMaxCodecNewRequestedBytes[] = "max_codec_new_requested_bytes";
constexpr char kMaxCorners[] = "max_corners";
constexpr char kMaxDeliveryBytes[] = "max_delivery_bytes";
constexpr char kMaxDescriptorBytes[] = "max_descriptor_bytes";
constexpr char kMaxEntryBytes[] = "max_entry_bytes";
constexpr char kMaxEntryNameBytes[] = "max_entry_name_bytes";
constexpr char kMaxFaceOffsets[] = "max_face_offsets";
constexpr char kMaxFaces[] = "max_faces";
constexpr char kMaxFacets[] = "max_facets";
constexpr char kMaxFileNewRequestedBytes[] = "max_file_new_requested_bytes";
constexpr char kMaxIssues[] = "max_issues";
constexpr char kMaxJobIdBytes[] = "max_job_id_bytes";
constexpr char kMaxJobNewRequestedBytes[] = "max_job_new_requested_bytes";
constexpr char kMaxJobs[] = "max_jobs";
constexpr char kMaxLineBytes[] = "max_line_bytes";
constexpr char kMaxManifestBytes[] = "max_manifest_bytes";
constexpr char kMaxManifestLineBytes[] = "max_manifest_line_bytes";
constexpr char kMaxMetadataEntries[] = "max_metadata_entries";
constexpr char kMaxNameBytes[] = "max_name_bytes";
constexpr char kMaxNativePathCodeUnits[] = "max_native_path_code_units";
constexpr char kMaxNewRequestedBytes[] = "max_new_requested_bytes";
constexpr char kMaxOutputBytes[] = "max_output_bytes";
constexpr char kMaxPresenceBytes[] = "max_presence_bytes";
constexpr char kMaxReceiptBytes[] = "max_receipt_bytes";
constexpr char kMaxResultBytes[] = "max_result_bytes";
constexpr char kMaxSourceBytes[] = "max_source_bytes";
constexpr char kMaxTokenBytes[] = "max_token_bytes";
constexpr char kMaxTotalFileBytes[] = "max_total_file_bytes";
constexpr char kMaxTotalNewRequestedBytes[] = "max_total_new_requested_bytes";
constexpr char kMaxUnitLabelBytes[] = "max_unit_label_bytes";
constexpr char kMaxVertices[] = "max_vertices";
constexpr char kMetadata[] = "metadata";
constexpr char kMetadataMode[] = "metadata_mode";
constexpr char kMinimumParallelVertices[] = "minimum_parallel_vertices";
constexpr char kMode[] = "mode";
constexpr char kName[] = "name";
constexpr char kNotAttemptedReason[] = "not_attempted_reason";
constexpr char kObservations[] = "observations";
constexpr char kOffsets[] = "offsets";
constexpr char kOutcome[] = "outcome";
constexpr char kOutput[] = "output";
constexpr char kOutputEncoding[] = "output_encoding";
constexpr char kOwners[] = "owners";
constexpr char kParallelMinBytes[] = "parallel_min_bytes";
constexpr char kPartitionBytes[] = "partition_bytes";
constexpr char kPhase[] = "phase";
constexpr char kPositions[] = "positions";
constexpr char kPresent[] = "present";
constexpr char kRead[] = "read";
constexpr char kReadLimits[] = "read_limits";
constexpr char kReadWork[] = "read_work";
constexpr char kReport[] = "report";
constexpr char kRequestedBytes[] = "requested_bytes";
constexpr char kRescaled[] = "rescaled";
constexpr char kRewriteLimits[] = "rewrite_limits";
constexpr char kRewriteWork[] = "rewrite_work";
constexpr char kScalarType[] = "scalar_type";
constexpr char kSchema[] = "schema";
constexpr char kSemantic[] = "semantic";
constexpr char kSetIndex[] = "set_index";
constexpr char kSha256[] = "sha256";
constexpr char kSingleExecution[] = "single_execution";
constexpr char kSourceEncoding[] = "source_encoding";
constexpr char kState[] = "state";
constexpr char kTrackedPayloadBudgetBytes[] = "tracked_payload_budget_bytes";
constexpr char kUnits[] = "units";
constexpr char kValue[] = "value";
constexpr char kValues[] = "values";
constexpr char kVerified[] = "verified";
constexpr char kWorkerBudget[] = "worker_budget";
constexpr char kWrite[] = "write";
}  // namespace wire_key
namespace app_value {
constexpr char kNotAttempted[] = "not_attempted";
constexpr char kNotApplicable[] = "not_applicable";
constexpr char kSucceeded[] = "succeeded";
constexpr char kFailed[] = "failed";
constexpr char kCanceled[] = "canceled";
constexpr char kPublished[] = "published";
constexpr char kAdmissionFailed[] = "admission_failed";
constexpr char kCompleted[] = "completed";
constexpr char kFailFast[] = "fail_fast";
constexpr char kFraming[] = "framing";
constexpr char kFlush[] = "flush";
constexpr char kInterrupted[] = "interrupted";
constexpr char kContinue[] = "continue";
constexpr char kFailFastPolicy[] = "fail-fast";
}  // namespace app_value
namespace admission_code {
constexpr char kRepresentation[] = "representation";
constexpr char kManifestUtf8[] = "manifest_utf8";
constexpr char kManifestJson[] = "manifest_json";
constexpr char kManifestExtent[] = "manifest_extent";
constexpr char kJobExtent[] = "job_extent";
constexpr char kDuplicateJobId[] = "duplicate_job_id";
constexpr char kPathAdmission[] = "path_admission";
constexpr char kDomainOverlap[] = "domain_overlap";
constexpr char kRootBudget[] = "root_budget";
constexpr char kPreviouslyRun[] = "previously_run";
}  // namespace admission_code
constexpr std::string_view kRead = wire_key::kRead, kRewrite = "rewrite",
                           kBatch = "batch";
constexpr std::string_view kAscii = "ascii", kBinary = "binary",
                           kRetain = "retain", kReplace = "replace";
constexpr std::string_view kUnknown = "unknown", kLabel = wire_key::kLabel,
                           kResultSchema = "meshvale.stl-result/1";
constexpr std::string_view kReceipt = "stl-publication.json";
constexpr std::size_t kPositionComponents = 3;
constexpr int kExitAdmission = 2, kExitCanceled = 130, kExitDelivery = 3,
              kExitJobFailure = 1, kExitSuccess = 0;
StlCancellationReader g_cancellation_reader = nullptr;
std::size_t Host(std::uint64_t value) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    throw nb::value_error("native extent exceeds the host range");
  return static_cast<std::size_t>(value);
}
std::uint64_t UInt(nb::handle value) {
  if (!PyLong_CheckExact(value.ptr()))
    throw nb::type_error("expected a positive built-in int");
  const auto n = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  if (!n) throw nb::value_error("a required bound is zero");
  (void)Host(n);
  return n;
}
std::uint64_t Sum(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    throw nb::value_error("declared sum overflows");
  (void)Host(a + b);
  return a + b;
}
std::string_view Text(nb::handle value) {
  if (!PyUnicode_Check(value.ptr())) throw nb::type_error("expected str");
  Py_ssize_t size = 0;
  const char* p = PyUnicode_AsUTF8AndSize(value.ptr(), &size);
  if (!p) throw nb::python_error();
  return {p, static_cast<std::size_t>(size)};
}
nb::object PythonText(std::string_view value) {
  return nb::str(value.data(), value.size());
}
nb::object Binary(std::span<const std::byte> value) {
  return nb::bytes(value.data(), value.size());
}
nb::object Hex(std::span<const std::byte> bytes) {
  constexpr std::string_view kHex = "0123456789abcdef";
  constexpr unsigned kNibbleBits = 4, kNibbleMask = 0xf;
  constexpr std::size_t kHexCharsPerByte = 2;
  // Hash/header wire extents are fixed and independently bounded before use.
  std::array<char, io::kStlFileBinaryHeaderBytes * kHexCharsPerByte> out{};
  if (bytes.size() > io::kStlFileBinaryHeaderBytes)
    throw std::logic_error("invalid fixed wire extent");
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const auto b = std::to_integer<unsigned char>(bytes[i]);
    out[i * kHexCharsPerByte] = kHex[b >> kNibbleBits];
    out[i * kHexCharsPerByte + 1] = kHex[b & kNibbleMask];
  }
  return nb::str(out.data(), bytes.size() * kHexCharsPerByte);
}
std::string_view EnumName(io::StlFileStatus value) {
  switch (value) {
    case io::StlFileStatus::kSuccess:
      return "success";
    case io::StlFileStatus::kInvalidConfiguration:
      return "invalid_configuration";
    case io::StlFileStatus::kInvalidRequest:
      return "invalid_request";
    case io::StlFileStatus::kUnsupportedProfile:
      return "unsupported_profile";
    case io::StlFileStatus::kLimitExceeded:
      return "limit_exceeded";
    case io::StlFileStatus::kBudgetExceeded:
      return "budget_exceeded";
    case io::StlFileStatus::kIoFailure:
      return "io_failure";
    case io::StlFileStatus::kFileChanged:
      return "file_changed";
    case io::StlFileStatus::kStlFailure:
      return "stl_failure";
    case io::StlFileStatus::kPreservationMismatch:
      return "preservation_mismatch";
    case io::StlFileStatus::kProviderFailure:
      return "provider_failure";
    case io::StlFileStatus::kDestinationExists:
      return "destination_exists";
    case io::StlFileStatus::kPublicationFailed:
      return "publication_failed";
    case io::StlFileStatus::kCanceled:
      return app_value::kCanceled;
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlFilePhase value) {
  switch (value) {
    case io::StlFilePhase::kConfiguration:
      return "configuration";
    case io::StlFilePhase::kPathPlanning:
      return "path_planning";
    case io::StlFilePhase::kCapture:
      return "capture";
    case io::StlFilePhase::kRead:
      return wire_key::kRead;
    case io::StlFilePhase::kWrite:
      return wire_key::kWrite;
    case io::StlFilePhase::kStaging:
      return "staging";
    case io::StlFilePhase::kStagedReread:
      return "staged_reread";
    case io::StlFilePhase::kFreshRead:
      return wire_key::kFreshRead;
    case io::StlFilePhase::kVerification:
      return "verification";
    case io::StlFilePhase::kProvider:
      return "provider";
    case io::StlFilePhase::kReceiptVerification:
      return "receipt_verification";
    case io::StlFilePhase::kInventory:
      return "inventory";
    case io::StlFilePhase::kPublication:
      return "publication";
    case io::StlFilePhase::kComplete:
      return "complete";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlFileCleanupOutcome value) {
  switch (value) {
    case io::StlFileCleanupOutcome::kNotRequired:
      return "not_required";
    case io::StlFileCleanupOutcome::kRemoved:
      return "removed";
    case io::StlFileCleanupOutcome::kFailed:
      return app_value::kFailed;
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlFileObservationState value) {
  switch (value) {
    case io::StlFileObservationState::kNotAttempted:
      return app_value::kNotAttempted;
    case io::StlFileObservationState::kNotApplicable:
      return app_value::kNotApplicable;
    case io::StlFileObservationState::kFailed:
      return app_value::kFailed;
    case io::StlFileObservationState::kCanceled:
      return app_value::kCanceled;
    case io::StlFileObservationState::kSucceeded:
      return app_value::kSucceeded;
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlFileDiagnosticCode value) {
  switch (value) {
    case io::StlFileDiagnosticCode::kNone:
      return "none";
    case io::StlFileDiagnosticCode::kRequiredFieldZero:
      return "required_field_zero";
    case io::StlFileDiagnosticCode::kConfigurationExtent:
      return "configuration_extent";
    case io::StlFileDiagnosticCode::kEncoding:
      return "encoding";
    case io::StlFileDiagnosticCode::kMetadata:
      return wire_key::kMetadata;
    case io::StlFileDiagnosticCode::kUnitDeclaration:
      return "unit_declaration";
    case io::StlFileDiagnosticCode::kCountLimit:
      return "count_limit";
    case io::StlFileDiagnosticCode::kPathUnsupported:
      return "path_unsupported";
    case io::StlFileDiagnosticCode::kNameUnsupported:
      return "name_unsupported";
    case io::StlFileDiagnosticCode::kNameCollision:
      return "name_collision";
    case io::StlFileDiagnosticCode::kSourceDestinationOverlap:
      return "source_destination_overlap";
    case io::StlFileDiagnosticCode::kNotRegular:
      return "not_regular";
    case io::StlFileDiagnosticCode::kOpen:
      return "open";
    case io::StlFileDiagnosticCode::kRead:
      return wire_key::kRead;
    case io::StlFileDiagnosticCode::kWrite:
      return wire_key::kWrite;
    case io::StlFileDiagnosticCode::kClose:
      return "close";
    case io::StlFileDiagnosticCode::kChanged:
      return "changed";
    case io::StlFileDiagnosticCode::kRequestBudget:
      return "request_budget";
    case io::StlFileDiagnosticCode::kSharedBudget:
      return "shared_budget";
    case io::StlFileDiagnosticCode::kStopRequested:
      return "stop_requested";
    case io::StlFileDiagnosticCode::kDelegatedStl:
      return "delegated_stl";
    case io::StlFileDiagnosticCode::kByteMismatch:
      return "byte_mismatch";
    case io::StlFileDiagnosticCode::kValueMismatch:
      return "value_mismatch";
    case io::StlFileDiagnosticCode::kNormalMismatch:
      return "normal_mismatch";
    case io::StlFileDiagnosticCode::kOrderMismatch:
      return "order_mismatch";
    case io::StlFileDiagnosticCode::kMetadataMismatch:
      return "metadata_mismatch";
    case io::StlFileDiagnosticCode::kOutputFacts:
      return "output_facts";
    case io::StlFileDiagnosticCode::kProvider:
      return "provider";
    case io::StlFileDiagnosticCode::kReportUtf8:
      return "report_utf8";
    case io::StlFileDiagnosticCode::kStagedInventory:
      return "staged_inventory";
    case io::StlFileDiagnosticCode::kDestinationExists:
      return "destination_exists";
    case io::StlFileDiagnosticCode::kCommit:
      return "commit";
    case io::StlFileDiagnosticCode::kCommitUnsupported:
      return "commit_unsupported";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlFileMetadataMode value) {
  switch (value) {
    case io::StlFileMetadataMode::kRetain:
      return "retain";
    case io::StlFileMetadataMode::kReplace:
      return "replace";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlFileUnitKind value) {
  switch (value) {
    case io::StlFileUnitKind::kUnknown:
      return "unknown";
    case io::StlFileUnitKind::kLabel:
      return wire_key::kLabel;
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlEncoding value) {
  switch (value) {
    case io::StlEncoding::kInvalid:
      return "invalid";
    case io::StlEncoding::kAscii:
      return "ascii";
    case io::StlEncoding::kBinary:
      return "binary";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlStatus value) {
  switch (value) {
    case io::StlStatus::kSuccess:
      return "success";
    case io::StlStatus::kInvalidConfiguration:
      return "invalid_configuration";
    case io::StlStatus::kMalformed:
      return "malformed";
    case io::StlStatus::kUnsupportedVariant:
      return "unsupported_variant";
    case io::StlStatus::kUnsupportedNumericConversion:
      return "unsupported_numeric_conversion";
    case io::StlStatus::kLimitExceeded:
      return "limit_exceeded";
    case io::StlStatus::kBudgetExceeded:
      return "budget_exceeded";
    case io::StlStatus::kCanceled:
      return app_value::kCanceled;
    case io::StlStatus::kInvalidStorage:
      return "invalid_storage";
    case io::StlStatus::kGeometryFailure:
      return "geometry_failure";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlStage value) {
  switch (value) {
    case io::StlStage::kConfiguration:
      return "configuration";
    case io::StlStage::kScan:
      return "scan";
    case io::StlStage::kDecode:
      return "decode";
    case io::StlStage::kMaterialization:
      return wire_key::kMaterialization;
    case io::StlStage::kInspection:
      return wire_key::kInspection;
    case io::StlStage::kValidation:
      return "validation";
    case io::StlStage::kEncode:
      return "encode";
    case io::StlStage::kPublication:
      return "publication";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlDiagnosticCode value) {
  switch (value) {
    case io::StlDiagnosticCode::kNone:
      return "none";
    case io::StlDiagnosticCode::kRequiredFieldZero:
      return "required_field_zero";
    case io::StlDiagnosticCode::kConfigurationExtent:
      return "configuration_extent";
    case io::StlDiagnosticCode::kEncoding:
      return "encoding";
    case io::StlDiagnosticCode::kSourceLimit:
      return "source_limit";
    case io::StlDiagnosticCode::kFacetLimit:
      return "facet_limit";
    case io::StlDiagnosticCode::kNameLimit:
      return "name_limit";
    case io::StlDiagnosticCode::kLineLimit:
      return "line_limit";
    case io::StlDiagnosticCode::kTokenLimit:
      return "token_limit";
    case io::StlDiagnosticCode::kOutputLimit:
      return "output_limit";
    case io::StlDiagnosticCode::kSyntax:
      return "syntax";
    case io::StlDiagnosticCode::kNumericSyntax:
      return "numeric_syntax";
    case io::StlDiagnosticCode::kNumericRange:
      return "numeric_range";
    case io::StlDiagnosticCode::kNonfinite:
      return "nonfinite";
    case io::StlDiagnosticCode::kBinaryExtent:
      return "binary_extent";
    case io::StlDiagnosticCode::kTrailingBytes:
      return "trailing_bytes";
    case io::StlDiagnosticCode::kAttributeWord:
      return "attribute_word";
    case io::StlDiagnosticCode::kRequestBudget:
      return "request_budget";
    case io::StlDiagnosticCode::kStopRequested:
      return "stop_requested";
    case io::StlDiagnosticCode::kGeometry:
      return wire_key::kGeometry;
    case io::StlDiagnosticCode::kStorageIssues:
      return "storage_issues";
    case io::StlDiagnosticCode::kNotTriangles:
      return "not_triangles";
    case io::StlDiagnosticCode::kNormalSelection:
      return "normal_selection";
    case io::StlDiagnosticCode::kUnsupportedAttribute:
      return "unsupported_attribute";
    case io::StlDiagnosticCode::kMetadata:
      return wire_key::kMetadata;
    case io::StlDiagnosticCode::kExpansionAcknowledgment:
      return "expansion_acknowledgment";
    case io::StlDiagnosticCode::kNumericConversion:
      return "numeric_conversion";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(io::StlMetadataPolicy value) {
  switch (value) {
    case io::StlMetadataPolicy::kExplicitWithoutSource:
      return "explicit_without_source";
    case io::StlMetadataPolicy::kRetainedSourceMetadata:
      return "retained_source_metadata";
    case io::StlMetadataPolicy::kReplacedSourceMetadata:
      return "replaced_source_metadata";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(geo::StorageIssueCode value) {
  switch (value) {
    case geo::StorageIssueCode::kPositionNonfinite:
      return "position_nonfinite";
    case geo::StorageIssueCode::kFaceOffsetsEmpty:
      return "face_offsets_empty";
    case geo::StorageIssueCode::kFaceOffsetsStart:
      return "face_offsets_start";
    case geo::StorageIssueCode::kFaceOffsetsEnd:
      return "face_offsets_end";
    case geo::StorageIssueCode::kFaceOffsetRange:
      return "face_offset_range";
    case geo::StorageIssueCode::kFaceOffsetOrder:
      return "face_offset_order";
    case geo::StorageIssueCode::kFaceTooShort:
      return "face_too_short";
    case geo::StorageIssueCode::kCornerVertexRange:
      return "corner_vertex_range";
    case geo::StorageIssueCode::kAttributeDuplicateName:
      return "attribute_duplicate_name";
    case geo::StorageIssueCode::kAttributeDomain:
      return "attribute_domain";
    case geo::StorageIssueCode::kAttributeNameEmpty:
      return "attribute_name_empty";
    case geo::StorageIssueCode::kAttributeComponentsZero:
      return "attribute_components_zero";
    case geo::StorageIssueCode::kAttributeDenseShape:
      return "attribute_dense_shape";
    case geo::StorageIssueCode::kAttributeOffsetsEmpty:
      return "attribute_offsets_empty";
    case geo::StorageIssueCode::kAttributeOffsetsCount:
      return "attribute_offsets_count";
    case geo::StorageIssueCode::kAttributeOffsetsStart:
      return "attribute_offsets_start";
    case geo::StorageIssueCode::kAttributeOffsetsEnd:
      return "attribute_offsets_end";
    case geo::StorageIssueCode::kAttributeOffsetRange:
      return "attribute_offset_range";
    case geo::StorageIssueCode::kAttributeOffsetOrder:
      return "attribute_offset_order";
    case geo::StorageIssueCode::kAttributeOffsetAlignment:
      return "attribute_offset_alignment";
    case geo::StorageIssueCode::kAttributePresenceCount:
      return "attribute_presence_count";
    case geo::StorageIssueCode::kAttributePresenceValue:
      return "attribute_presence_value";
    case geo::StorageIssueCode::kMetadataDuplicateKey:
      return "metadata_duplicate_key";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(geo::MaterializationStatus value) {
  switch (value) {
    case geo::MaterializationStatus::kSuccess:
      return "success";
    case geo::MaterializationStatus::kInvalidConfiguration:
      return "invalid_configuration";
    case geo::MaterializationStatus::kInvalidTransport:
      return "invalid_transport";
    case geo::MaterializationStatus::kLimitExceeded:
      return "limit_exceeded";
    case geo::MaterializationStatus::kUnsupportedAllocationProfile:
      return "unsupported_allocation_profile";
    case geo::MaterializationStatus::kBudgetExceeded:
      return "budget_exceeded";
    case geo::MaterializationStatus::kCanceled:
      return app_value::kCanceled;
    case geo::MaterializationStatus::kInvalidSourceStorage:
      return "invalid_source_storage";
    case geo::MaterializationStatus::kInvalidSelection:
      return "invalid_selection";
  }
  throw std::logic_error("invalid native enum");
}
std::string_view EnumName(geo::MaterializationDiagnosticCode value) {
  switch (value) {
    case geo::MaterializationDiagnosticCode::kNone:
      return "none";
    case geo::MaterializationDiagnosticCode::kRequiredFieldZero:
      return "required_field_zero";
    case geo::MaterializationDiagnosticCode::kConfigurationExtent:
      return "configuration_extent";
    case geo::MaterializationDiagnosticCode::kTransportSelector:
      return "transport_selector";
    case geo::MaterializationDiagnosticCode::kPositionExtent:
      return "position_extent";
    case geo::MaterializationDiagnosticCode::kTriangleExtent:
      return "triangle_extent";
    case geo::MaterializationDiagnosticCode::kStorageExtent:
      return "storage_extent";
    case geo::MaterializationDiagnosticCode::kCountLimit:
      return "count_limit";
    case geo::MaterializationDiagnosticCode::kIssueLimit:
      return "issue_limit";
    case geo::MaterializationDiagnosticCode::kAllocationProfile:
      return "allocation_profile";
    case geo::MaterializationDiagnosticCode::kRequestBudget:
      return "request_budget";
    case geo::MaterializationDiagnosticCode::kStopRequested:
      return "stop_requested";
    case geo::MaterializationDiagnosticCode::kSourceStorage:
      return "source_storage";
    case geo::MaterializationDiagnosticCode::kSelectionOrder:
      return "selection_order";
    case geo::MaterializationDiagnosticCode::kSelectionRange:
      return "selection_range";
  }
  throw std::logic_error("invalid native enum");
}
template <class T>
nb::object Wire(const T& value);
template <class T>
nb::object Wire(const std::optional<T>& value);
nb::object Wire(const geo::StorageIssue& value);
nb::object Wire(const geo::MaterializationDiagnostic& value);
nb::object Wire(const geo::MaterializationExecutionStats& value);
nb::object Wire(const io::StlDiagnostic& value);
nb::object Wire(const io::StlExecution& value);
nb::object Wire(const io::StlRequestedBytes& value);
nb::object Wire(const io::StlWriteFacts& value);
nb::object Wire(const io::StlFileDiagnostic& value);
nb::object Wire(const io::StlFileCaptureFacts& value);
nb::object Wire(const io::StlFileReadFacts& value);
nb::object Wire(const io::StlFileWriteFacts& value);
nb::object Wire(const io::StlFilePayloadFacts& value);
nb::object Wire(const io::StlFileVerificationFacts& value);
nb::object Wire(const io::StlFileInventoryFacts& value);
nb::object Wire(const io::StlFilePublicationFacts& value);
nb::object Wire(const io::StlFileObservation& value);
nb::object Wire(const io::StlFileObservations& value);
nb::object Wire(const io::StlFileExecution& value);
nb::object Wire(const io::StlFileRequestedBytes& value);
nb::object Wire(const std::error_code& value);
template <class T>
nb::object Wire(const T& value) {
  if constexpr (std::is_enum_v<T>)
    return PythonText(EnumName(value));
  else if constexpr (std::is_arithmetic_v<T>)
    return nb::cast(value);
  else if constexpr (std::is_same_v<T, std::string_view>)
    return PythonText(value);
  else if constexpr (std::is_same_v<T, std::monostate>)
    return nb::none();
  else
    static_assert(!sizeof(T), "missing STL observation serializer");
}
template <class T>
nb::object Wire(const std::optional<T>& value) {
  return value ? Wire(*value) : nb::none();
}
#define STL_PY_WIRE_FIELD(target, value, field) \
  target[#field] = Wire(value.field)
nb::object Wire(const geo::StorageIssue& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, code);
  STL_PY_WIRE_FIELD(out, value, attribute);
  STL_PY_WIRE_FIELD(out, value, element);
  return out;
}
nb::object Wire(const geo::MaterializationDiagnostic& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, code);
  STL_PY_WIRE_FIELD(out, value, attribute);
  STL_PY_WIRE_FIELD(out, value, element);
  STL_PY_WIRE_FIELD(out, value, storage_issue);
  return out;
}
nb::object Wire(const geo::MaterializationExecutionStats& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, planned_jobs);
  STL_PY_WIRE_FIELD(out, value, launched_workers);
  STL_PY_WIRE_FIELD(out, value, joined_workers);
  STL_PY_WIRE_FIELD(out, value, caller_jobs);
  return out;
}
nb::object Wire(const io::StlDiagnostic& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, stage);
  STL_PY_WIRE_FIELD(out, value, code);
  STL_PY_WIRE_FIELD(out, value, byte_offset);
  STL_PY_WIRE_FIELD(out, value, facet);
  STL_PY_WIRE_FIELD(out, value, attribute);
  STL_PY_WIRE_FIELD(out, value, geometry_status);
  STL_PY_WIRE_FIELD(out, value, geometry_diagnostic);
  STL_PY_WIRE_FIELD(out, value, storage_issue);
  return out;
}
nb::object Wire(const io::StlExecution& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, codec);
  STL_PY_WIRE_FIELD(out, value, materialization);
  STL_PY_WIRE_FIELD(out, value, inspection);
  return out;
}
nb::object Wire(const io::StlRequestedBytes& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, codec);
  STL_PY_WIRE_FIELD(out, value, materialization);
  STL_PY_WIRE_FIELD(out, value, inspection);
  STL_PY_WIRE_FIELD(out, value, total);
  return out;
}
nb::object Wire(const io::StlWriteFacts& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, encoding);
  STL_PY_WIRE_FIELD(out, value, face_normal_attribute);
  STL_PY_WIRE_FIELD(out, value, source_vertex_count);
  STL_PY_WIRE_FIELD(out, value, source_face_count);
  STL_PY_WIRE_FIELD(out, value, corner_expansion_acknowledged);
  STL_PY_WIRE_FIELD(out, value, source_encoding);
  STL_PY_WIRE_FIELD(out, value, metadata_policy);
  return out;
}
nb::object Wire(const io::StlFileDiagnostic& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, status);
  STL_PY_WIRE_FIELD(out, value, phase);
  STL_PY_WIRE_FIELD(out, value, code);
  STL_PY_WIRE_FIELD(out, value, file_index);
  STL_PY_WIRE_FIELD(out, value, byte_offset);
  STL_PY_WIRE_FIELD(out, value, facet);
  STL_PY_WIRE_FIELD(out, value, system_error);
  STL_PY_WIRE_FIELD(out, value, stl_status);
  STL_PY_WIRE_FIELD(out, value, stl_diagnostic);
  return out;
}
nb::object Wire(const io::StlFileVerificationFacts& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, position_bits);
  STL_PY_WIRE_FIELD(out, value, raw_normal_bits);
  STL_PY_WIRE_FIELD(out, value, facet_order);
  STL_PY_WIRE_FIELD(out, value, corner_order);
  STL_PY_WIRE_FIELD(out, value, soup_identity);
  STL_PY_WIRE_FIELD(out, value, metadata);
  STL_PY_WIRE_FIELD(out, value, exact_bytes);
  return out;
}
nb::object Wire(const io::StlFileObservations& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, capture);
  STL_PY_WIRE_FIELD(out, value, read);
  STL_PY_WIRE_FIELD(out, value, write);
  STL_PY_WIRE_FIELD(out, value, staged_reread);
  STL_PY_WIRE_FIELD(out, value, fresh_read);
  STL_PY_WIRE_FIELD(out, value, verification);
  STL_PY_WIRE_FIELD(out, value, provider);
  STL_PY_WIRE_FIELD(out, value, receipt_verification);
  STL_PY_WIRE_FIELD(out, value, inventory);
  STL_PY_WIRE_FIELD(out, value, publication);
  return out;
}
nb::object Wire(const io::StlFileExecution& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, capture);
  STL_PY_WIRE_FIELD(out, value, writing_files);
  STL_PY_WIRE_FIELD(out, value, staged_reread);
  STL_PY_WIRE_FIELD(out, value, byte_verification);
  STL_PY_WIRE_FIELD(out, value, value_verification);
  STL_PY_WIRE_FIELD(out, value, receipt_verification);
  STL_PY_WIRE_FIELD(out, value, inventory);
  STL_PY_WIRE_FIELD(out, value, hashing);
  STL_PY_WIRE_FIELD(out, value, provider_import);
  STL_PY_WIRE_FIELD(out, value, read);
  STL_PY_WIRE_FIELD(out, value, write);
  STL_PY_WIRE_FIELD(out, value, fresh_read);
  return out;
}
nb::object Wire(const io::StlFileRequestedBytes& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, file_capture);
  STL_PY_WIRE_FIELD(out, value, file_publication);
  STL_PY_WIRE_FIELD(out, value, application);
  STL_PY_WIRE_FIELD(out, value, read);
  STL_PY_WIRE_FIELD(out, value, write);
  STL_PY_WIRE_FIELD(out, value, fresh_read);
  return out;
}
nb::object Wire(const std::error_code& value) {
  nb::dict out;
  out[wire_key::kCategory] = value.category().name();
  out[wire_key::kValue] = value.value();
  return out;
}
nb::object Wire(const io::StlFileCaptureFacts& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, entry);
  STL_PY_WIRE_FIELD(out, value, bytes);
  out[wire_key::kSha256] = Hex(value.sha256);
  return out;
}
nb::object Wire(const io::StlFileReadFacts& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, encoding);
  STL_PY_WIRE_FIELD(out, value, facets);
  STL_PY_WIRE_FIELD(out, value, vertices);
  STL_PY_WIRE_FIELD(out, value, corners);
  STL_PY_WIRE_FIELD(out, value, ascii_name);
  out[wire_key::kBinaryHeaderHex] =
      value.binary_header ? Hex(*value.binary_header) : nb::none();
  return out;
}
nb::object Wire(const io::StlFileWriteFacts& value) {
  auto out = nb::cast<nb::dict>(Wire(value.codec));
  out[wire_key::kBytes] = value.bytes;
  out[wire_key::kSha256] = Hex(value.sha256);
  return out;
}
nb::object Wire(const io::StlFilePayloadFacts& value) {
  nb::dict out;
  out[wire_key::kBytes] = value.bytes;
  out[wire_key::kSha256] = Hex(value.sha256);
  return out;
}
nb::object Wire(const io::StlFileInventoryFacts& value) {
  nb::dict out;
  nb::list entries;
  for (const auto& e : value.entries) entries.append(Wire(e));
  out[wire_key::kEntries] = entries;
  return out;
}
nb::object Wire(const io::StlFilePublicationFacts& value) {
  if (!value.published) throw std::logic_error("invalid publication fact");
  nb::dict out;
  out[wire_key::kOutcome] = app_value::kPublished;
  return out;
}
nb::object Wire(const io::StlFileObservation& value) {
  nb::dict out;
  STL_PY_WIRE_FIELD(out, value, state);
  STL_PY_WIRE_FIELD(out, value, diagnostic);
  out[wire_key::kFacts] =
      std::visit([](const auto& f) { return Wire(f); }, value.facts);
  return out;
}
#undef STL_PY_WIRE_FIELD
struct StlPythonApplicationLimits {
  std::uint64_t max_jobs, max_manifest_bytes, max_manifest_line_bytes,
      max_job_id_bytes, max_result_bytes, max_delivery_bytes,
      max_application_new_requested_bytes, max_job_new_requested_bytes;
};
struct StlPythonPreset {
  StlPythonApplicationLimits application;
  io::StlFileReadLimits read_limits;
  io::StlFileReadWorkPolicy read_work;
  io::StlFileRewriteLimits rewrite_limits;
  io::StlFileRewriteWorkPolicy rewrite_work;
};
#define STL_PY_READ_BOUND(field) out.field = UInt(record[#field])
geo::MeshMaterializationLimits ReadMaterialization(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value,
      {wire_key::kMaxVertices, wire_key::kMaxFaces, wire_key::kMaxCorners,
       wire_key::kMaxFaceOffsets, wire_key::kMaxAttributes,
       wire_key::kMaxAttributeScalars, wire_key::kMaxAttributeOffsets,
       wire_key::kMaxPresenceBytes, wire_key::kMaxMetadataEntries,
       wire_key::kMaxDescriptorBytes, wire_key::kMaxNewRequestedBytes});
  geo::MeshMaterializationLimits out;
  STL_PY_READ_BOUND(max_vertices);
  STL_PY_READ_BOUND(max_faces);
  STL_PY_READ_BOUND(max_corners);
  STL_PY_READ_BOUND(max_face_offsets);
  STL_PY_READ_BOUND(max_attributes);
  STL_PY_READ_BOUND(max_attribute_scalars);
  STL_PY_READ_BOUND(max_attribute_offsets);
  STL_PY_READ_BOUND(max_presence_bytes);
  STL_PY_READ_BOUND(max_metadata_entries);
  STL_PY_READ_BOUND(max_descriptor_bytes);
  STL_PY_READ_BOUND(max_new_requested_bytes);
  return out;
}
geo::MeshInspectionLimits ReadInspection(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kMaxIssues, wire_key::kMaxNewRequestedBytes});
  geo::MeshInspectionLimits out;
  STL_PY_READ_BOUND(max_issues);
  STL_PY_READ_BOUND(max_new_requested_bytes);
  return out;
}
geo::MeshMaterializationWorkPolicy ReadGeometryWork(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kParallelMinBytes, wire_key::kPartitionBytes,
              wire_key::kCancellationBlockScalars, wire_key::kMaxJobs});
  geo::MeshMaterializationWorkPolicy out;
  STL_PY_READ_BOUND(parallel_min_bytes);
  STL_PY_READ_BOUND(partition_bytes);
  STL_PY_READ_BOUND(cancellation_block_scalars);
  STL_PY_READ_BOUND(max_jobs);
  return out;
}
io::StlReadLimits ReadCodecLimits(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kMaxSourceBytes, wire_key::kMaxFacets,
              wire_key::kMaxNameBytes, wire_key::kMaxLineBytes,
              wire_key::kMaxTokenBytes, wire_key::kMaxCodecNewRequestedBytes,
              wire_key::kMaxTotalNewRequestedBytes, wire_key::kMaterialization,
              wire_key::kInspection});
  io::StlReadLimits out;
  STL_PY_READ_BOUND(max_source_bytes);
  STL_PY_READ_BOUND(max_facets);
  STL_PY_READ_BOUND(max_name_bytes);
  STL_PY_READ_BOUND(max_line_bytes);
  STL_PY_READ_BOUND(max_token_bytes);
  STL_PY_READ_BOUND(max_codec_new_requested_bytes);
  STL_PY_READ_BOUND(max_total_new_requested_bytes);
  out.materialization = ReadMaterialization(record[wire_key::kMaterialization]);
  out.inspection = ReadInspection(record[wire_key::kInspection]);
  return out;
}
io::StlWriteLimits ReadWriteLimits(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kMaxOutputBytes, wire_key::kMaxFacets,
              wire_key::kMaxNameBytes, wire_key::kMaxLineBytes,
              wire_key::kMaxTokenBytes, wire_key::kMaxCodecNewRequestedBytes,
              wire_key::kMaxTotalNewRequestedBytes, wire_key::kInspection});
  io::StlWriteLimits out;
  STL_PY_READ_BOUND(max_output_bytes);
  STL_PY_READ_BOUND(max_facets);
  STL_PY_READ_BOUND(max_name_bytes);
  STL_PY_READ_BOUND(max_line_bytes);
  STL_PY_READ_BOUND(max_token_bytes);
  STL_PY_READ_BOUND(max_codec_new_requested_bytes);
  STL_PY_READ_BOUND(max_total_new_requested_bytes);
  out.inspection = ReadInspection(record[wire_key::kInspection]);
  return out;
}
io::StlWorkPolicy ReadCodecWork(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value,
      {wire_key::kCancellationBlockBytes, wire_key::kCancellationBlockTokens,
       wire_key::kCancellationBlockFacets, wire_key::kGeometry});
  io::StlWorkPolicy out;
  STL_PY_READ_BOUND(cancellation_block_bytes);
  STL_PY_READ_BOUND(cancellation_block_tokens);
  STL_PY_READ_BOUND(cancellation_block_facets);
  out.geometry = ReadGeometryWork(record[wire_key::kGeometry]);
  return out;
}
io::StlFileLimits ReadFileLimits(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value,
      {wire_key::kMaxEntryBytes, wire_key::kMaxReceiptBytes,
       wire_key::kMaxTotalFileBytes, wire_key::kMaxEntryNameBytes,
       wire_key::kMaxNativePathCodeUnits, wire_key::kMaxUnitLabelBytes,
       wire_key::kMaxCallerReportBytes, wire_key::kMaxFileNewRequestedBytes});
  io::StlFileLimits out;
  STL_PY_READ_BOUND(max_entry_bytes);
  STL_PY_READ_BOUND(max_receipt_bytes);
  STL_PY_READ_BOUND(max_total_file_bytes);
  STL_PY_READ_BOUND(max_entry_name_bytes);
  STL_PY_READ_BOUND(max_native_path_code_units);
  STL_PY_READ_BOUND(max_unit_label_bytes);
  STL_PY_READ_BOUND(max_caller_report_bytes);
  STL_PY_READ_BOUND(max_file_new_requested_bytes);
  return out;
}
io::StlFileWorkPolicy ReadFileWork(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kIoBlockBytes, wire_key::kParallelMinBytes,
              wire_key::kPartitionBytes, wire_key::kCancellationBlockBytes,
              wire_key::kMaxJobs});
  io::StlFileWorkPolicy out;
  STL_PY_READ_BOUND(io_block_bytes);
  STL_PY_READ_BOUND(parallel_min_bytes);
  STL_PY_READ_BOUND(partition_bytes);
  STL_PY_READ_BOUND(cancellation_block_bytes);
  STL_PY_READ_BOUND(max_jobs);
  return out;
}
io::StlFileReadLimits ReadLimits(nb::handle value) {
  const auto record =
      geo::python::exact_dict(value, {wire_key::kFiles, wire_key::kRead});
  io::StlFileReadLimits out;
  out.files = ReadFileLimits(record[wire_key::kFiles]);
  out.read = ReadCodecLimits(record[wire_key::kRead]);
  return out;
}
io::StlFileReadWorkPolicy ReadWork(nb::handle value) {
  const auto record =
      geo::python::exact_dict(value, {wire_key::kFiles, wire_key::kRead});
  io::StlFileReadWorkPolicy out;
  out.files = ReadFileWork(record[wire_key::kFiles]);
  out.read = ReadCodecWork(record[wire_key::kRead]);
  return out;
}
io::StlFileRewriteLimits ReadRewriteLimits(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kMaxTotalNewRequestedBytes, wire_key::kFiles,
              wire_key::kWrite, wire_key::kFreshRead});
  io::StlFileRewriteLimits out;
  STL_PY_READ_BOUND(max_total_new_requested_bytes);
  out.files = ReadFileLimits(record[wire_key::kFiles]);
  out.write = ReadWriteLimits(record[wire_key::kWrite]);
  out.fresh_read = ReadCodecLimits(record[wire_key::kFreshRead]);
  return out;
}
io::StlFileRewriteWorkPolicy ReadRewriteWork(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kFiles, wire_key::kWrite, wire_key::kFreshRead});
  io::StlFileRewriteWorkPolicy out;
  out.files = ReadFileWork(record[wire_key::kFiles]);
  out.write = ReadCodecWork(record[wire_key::kWrite]);
  out.fresh_read = ReadCodecWork(record[wire_key::kFreshRead]);
  return out;
}
StlPythonApplicationLimits ReadApplication(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kMaxJobs, wire_key::kMaxManifestBytes,
              wire_key::kMaxManifestLineBytes, wire_key::kMaxJobIdBytes,
              wire_key::kMaxResultBytes, wire_key::kMaxDeliveryBytes,
              wire_key::kMaxApplicationNewRequestedBytes,
              wire_key::kMaxJobNewRequestedBytes});
  StlPythonApplicationLimits out;
  STL_PY_READ_BOUND(max_jobs);
  STL_PY_READ_BOUND(max_manifest_bytes);
  STL_PY_READ_BOUND(max_manifest_line_bytes);
  STL_PY_READ_BOUND(max_job_id_bytes);
  STL_PY_READ_BOUND(max_result_bytes);
  STL_PY_READ_BOUND(max_delivery_bytes);
  STL_PY_READ_BOUND(max_application_new_requested_bytes);
  STL_PY_READ_BOUND(max_job_new_requested_bytes);
  return out;
}
#undef STL_PY_READ_BOUND
geo::ExecutionOptions ExecutionOptions(nb::handle workers, nb::handle minimum,
                                       nb::handle payload) {
  return {Host(UInt(workers)), Host(UInt(minimum)), Host(UInt(payload))};
}
struct StlPythonExecution {
  StlPythonExecution(nb::handle workers, nb::handle minimum, nb::handle payload)
      : context(ExecutionOptions(workers, minimum, payload)) {}
  geo::ExecutionContext context;
};
void FacetExtents(std::uint64_t facets) {
  constexpr std::uint64_t kFacetCorners = 3, kBinaryPrefix = 84,
                          kBinaryFacet = 50;
  if (facets > std::numeric_limits<std::uint64_t>::max() / kBinaryFacet)
    throw nb::value_error("facet extent overflows");
  (void)Host(facets * kFacetCorners);
  (void)Sum(facets, 1);
  (void)Sum(kBinaryPrefix, facets * kBinaryFacet);
}
void FileExtents(const io::StlFileLimits& limits) {
  (void)Sum(limits.max_entry_bytes, limits.max_receipt_bytes);
  if (limits.max_native_path_code_units >
      std::numeric_limits<std::uint64_t>::max() / sizeof(sd::NativeChar))
    throw nb::value_error("path extent overflows");
  (void)Host(limits.max_native_path_code_units * sizeof(sd::NativeChar));
}
void ExecutionFields(nb::handle value) {
  const auto record = geo::python::exact_dict(
      value, {wire_key::kWorkerBudget, wire_key::kMinimumParallelVertices,
              wire_key::kTrackedPayloadBudgetBytes});
  (void)UInt(record[wire_key::kWorkerBudget]);
  (void)UInt(record[wire_key::kMinimumParallelVertices]);
  (void)UInt(record[wire_key::kTrackedPayloadBudgetBytes]);
}
StlPythonPreset Preset(nb::handle value) {
  const auto r = geo::python::exact_dict(
      value,
      {wire_key::kApplicationLimits, wire_key::kSingleExecution,
       wire_key::kBatchExecution, wire_key::kReadLimits, wire_key::kReadWork,
       wire_key::kRewriteLimits, wire_key::kRewriteWork});
  StlPythonPreset p{ReadApplication(r[wire_key::kApplicationLimits]),
                    ReadLimits(r[wire_key::kReadLimits]),
                    ReadWork(r[wire_key::kReadWork]),
                    ReadRewriteLimits(r[wire_key::kRewriteLimits]),
                    ReadRewriteWork(r[wire_key::kRewriteWork])};
  ExecutionFields(r[wire_key::kSingleExecution]);
  ExecutionFields(r[wire_key::kBatchExecution]);
  FacetExtents(p.read_limits.read.max_facets);
  FacetExtents(p.rewrite_limits.write.max_facets);
  FacetExtents(p.rewrite_limits.fresh_read.max_facets);
  FileExtents(p.read_limits.files);
  FileExtents(p.rewrite_limits.files);
  const auto read_sum =
      Sum(Sum(p.read_limits.read.max_codec_new_requested_bytes,
              p.read_limits.read.materialization.max_new_requested_bytes),
          p.read_limits.read.inspection.max_new_requested_bytes);
  const auto fresh_sum = Sum(
      Sum(p.rewrite_limits.fresh_read.max_codec_new_requested_bytes,
          p.rewrite_limits.fresh_read.materialization.max_new_requested_bytes),
      p.rewrite_limits.fresh_read.inspection.max_new_requested_bytes);
  const auto write_sum =
      Sum(p.rewrite_limits.write.max_codec_new_requested_bytes,
          p.rewrite_limits.write.inspection.max_new_requested_bytes);
  if (read_sum > p.read_limits.read.max_total_new_requested_bytes ||
      fresh_sum > p.rewrite_limits.fresh_read.max_total_new_requested_bytes ||
      write_sum > p.rewrite_limits.write.max_total_new_requested_bytes ||
      Sum(Sum(p.rewrite_limits.files.max_file_new_requested_bytes,
              p.rewrite_limits.write.max_total_new_requested_bytes),
          p.rewrite_limits.fresh_read.max_total_new_requested_bytes) >
          p.rewrite_limits.max_total_new_requested_bytes ||
      Sum(Sum(p.read_limits.files.max_file_new_requested_bytes,
              p.read_limits.read.max_total_new_requested_bytes),
          p.rewrite_limits.max_total_new_requested_bytes) >
          p.application.max_job_new_requested_bytes)
    throw nb::value_error("incomplete declared request allowance");
  return p;
}
template <class Native>
struct StlPythonOwner {
  StlPythonOwner() = default;
  StlPythonOwner(const StlPythonOwner&) = default;
  std::optional<Native> native;
  nb::object source = nb::none(), output = nb::none(), document = nb::none(),
             verified = nb::none();
  ~StlPythonOwner() { Clear(); }
  void Clear() noexcept {
    native.reset();
    auto* old_source = source.release().ptr();
    auto* old_output = output.release().ptr();
    auto* old_document = document.release().ptr();
    auto* old_verified = verified.release().ptr();
    Py_XDECREF(old_source);
    Py_XDECREF(old_output);
    Py_XDECREF(old_document);
    Py_XDECREF(old_verified);
  }
};
using StlPythonCaptured = StlPythonOwner<io::StlCapturedFile>;
using StlPythonDocument = StlPythonOwner<io::StlDocument>;
using StlPythonAsset = StlPythonOwner<io::StlFileAsset>;
using StlPythonOutput = StlPythonOwner<io::StlOutput>;
using StlPythonVerified = StlPythonOwner<io::StlVerifiedRewrite>;
using StlPythonBundle = StlPythonOwner<io::StlPublishedBundle>;
template <class T>
nb::object Shell() {
  nb::object object = nb::inst_alloc(nb::type<T>());
  new (nb::inst_ptr<T>(object)) T();
  nb::inst_mark_ready(object);
  return object;
}
struct StlPythonOwners {
  nb::object captured, source_document, asset, output, fresh_document, verified,
      bundle;
  StlPythonCaptured* captured_ptr = nullptr;
  StlPythonDocument* source_document_ptr = nullptr;
  StlPythonAsset* asset_ptr = nullptr;
  StlPythonOutput* output_ptr = nullptr;
  StlPythonDocument* fresh_document_ptr = nullptr;
  StlPythonVerified* verified_ptr = nullptr;
  StlPythonBundle* bundle_ptr = nullptr;
  StlPythonOwners()
      : captured(Shell<StlPythonCaptured>()),
        source_document(Shell<StlPythonDocument>()),
        asset(Shell<StlPythonAsset>()),
        output(Shell<StlPythonOutput>()),
        fresh_document(Shell<StlPythonDocument>()),
        verified(Shell<StlPythonVerified>()),
        bundle(Shell<StlPythonBundle>()) {
    captured_ptr = nb::inst_ptr<StlPythonCaptured>(captured);
    source_document_ptr = nb::inst_ptr<StlPythonDocument>(source_document);
    asset_ptr = nb::inst_ptr<StlPythonAsset>(asset);
    output_ptr = nb::inst_ptr<StlPythonOutput>(output);
    fresh_document_ptr = nb::inst_ptr<StlPythonDocument>(fresh_document);
    verified_ptr = nb::inst_ptr<StlPythonVerified>(verified);
    bundle_ptr = nb::inst_ptr<StlPythonBundle>(bundle);
    asset_ptr->document = source_document;
    auto* v = nb::inst_ptr<StlPythonVerified>(verified);
    v->source = asset;
    v->output = output;
    v->document = fresh_document;
    nb::inst_ptr<StlPythonBundle>(bundle)->verified = verified;
  }
};
struct StlPythonApp;
struct StlPythonJob {
  explicit StlPythonJob(const alloc::AllocationContext& a)
      : job_id(alloc::Allocator<char>(a)),
        input(alloc::Allocator<sd::NativeChar>(a)),
        destination(alloc::Allocator<sd::NativeChar>(a)),
        logical(alloc::Allocator<sd::NativeChar>(a)),
        destination_logical(alloc::Allocator<sd::NativeChar>(a)),
        ascii_name(alloc::Allocator<char>(a)),
        unit_label(alloc::Allocator<char>(a)) {}
  sd::String job_id;
  sd::NativeString input, destination, logical, destination_logical;
  sd::String ascii_name, unit_label;
  std::array<std::byte, io::kStlFileBinaryHeaderBytes> binary_header{};
  io::StlEncoding source_encoding = io::StlEncoding::kInvalid,
                  output_encoding = io::StlEncoding::kInvalid;
  io::StlFileMetadataMode metadata_mode = io::StlFileMetadataMode::kRetain;
  io::StlFileUnitKind unit_kind = io::StlFileUnitKind::kUnknown;
  bool rewrite = false, attempted = false, acknowledge = false,
       boundary_canceled = false;
  std::string_view skipped;
  sd::NativeMetadata identity;
  nb::object provider = nb::none(), report_backing = nb::none();
  StlPythonOwners owners;
  io::StlFileResult initial;
  std::optional<io::StlFileReadResult> read;
  std::optional<io::StlFileRewriteResult> written;
  StlPythonApp* application = nullptr;
};
struct StlPythonAdmission {
  std::string_view code, field;
  std::optional<std::uint64_t> line_index, job_index;
  std::optional<io::StlFileDiagnostic> file_diagnostic;
};
struct StlPythonDelivery {
  std::string_view state = app_value::kNotAttempted, code;
  std::uint64_t bytes_written = 0;
};
// A Python exception instance is a GC-visible edge, never an opaque native
// exception_ptr payload. The value is already normalized by pinned nanobind.
struct StlPythonUnexpected {
  StlPythonUnexpected() = default;
  StlPythonUnexpected(const StlPythonUnexpected&) = delete;
  StlPythonUnexpected& operator=(const StlPythonUnexpected&) = delete;
  nb::object python_value;
  // Only Run's non-python_error catch populates this native-only slot.
  std::exception_ptr native;
  ~StlPythonUnexpected() {
    auto* value = python_value.release().ptr();
    Py_XDECREF(value);
  }
};
struct StlPythonApp {
  StlPythonApp(const alloc::AllocationContext& a,
               const geo::ExecutionContext& c, const StlPythonPreset& p,
               std::string_view m, bool fail)
      : allocation(a),
        context(c),
        preset(p),
        mode(m),
        fail_fast(fail),
        jobs(alloc::Allocator<StlPythonJob>(a)),
        frame(a, Host(p.application.max_result_bytes)),
        manifest_path(alloc::Allocator<sd::NativeChar>(a)),
        manifest_logical(alloc::Allocator<sd::NativeChar>(a)),
        manifest_parent(alloc::Allocator<sd::NativeChar>(a)) {
    jobs.reserve(Host(p.application.max_jobs));
    for (std::uint64_t i = 0; i < p.application.max_jobs; ++i) {
      jobs.emplace_back(a);
      jobs.back().application = this;
    }
  }
  alloc::AllocationContext allocation;
  geo::ExecutionContext context;
  StlPythonPreset preset;
  std::string_view mode;
  bool fail_fast = false, running = false, used = false, finished = false,
       interrupted = false, canceled = false;
  std::stop_source stop;
  std::vector<StlPythonJob, alloc::Allocator<StlPythonJob>> jobs;
  std::size_t count = 0, frame_size = 0;
  sd::Buffer frame;
  std::optional<sd::Buffer> manifest;
  sd::NativeString manifest_path, manifest_logical, manifest_parent;
  std::optional<sd::NativeMetadata> manifest_identity;
  StlPythonAdmission admission;
  StlPythonDelivery delivery;
  StlPythonUnexpected unexpected;
};
// Exactly one GC node owns the native App. Every prepared/result alias owns
// a counted Python edge to this node rather than an invisible C++ App alias.
struct StlPythonApplicationOwner {
  StlPythonApplicationOwner() = default;
  StlPythonApplicationOwner(const StlPythonApplicationOwner&) = delete;
  StlPythonApplicationOwner& operator=(const StlPythonApplicationOwner&) =
      delete;
  std::shared_ptr<StlPythonApp> app;
  ~StlPythonApplicationOwner() { Clear(); }
  void Clear() noexcept {
    std::shared_ptr<StlPythonApp> detached;
    detached.swap(app);
    // The published pointer is empty before App destruction can invoke Python.
  }
};
constexpr char kApplicationOwnerName[] = "_StlApplicationOwner";
class StlPythonApplicationRef {
 public:
  explicit StlPythonApplicationRef(nb::object owner)
      : owner_(std::move(owner)) {}
  StlPythonApplicationRef(const StlPythonApplicationRef&) = default;
  StlPythonApplicationRef(StlPythonApplicationRef&&) noexcept = default;
  StlPythonApplicationRef& operator=(const StlPythonApplicationRef&) = delete;
  StlPythonApplicationRef& operator=(StlPythonApplicationRef&&) = delete;
  ~StlPythonApplicationRef() { Clear(); }
  StlPythonApp& operator*() const { return Get(); }
  StlPythonApp* operator->() const { return &Get(); }
  PyObject* Owner() const noexcept { return owner_.ptr(); }
  PyObject* Release() noexcept { return owner_.release().ptr(); }
  void Clear() noexcept {
    auto* old_owner = Release();
    Py_XDECREF(old_owner);
  }

 private:
  StlPythonApp& Get() const {
    if (!owner_.is_valid() || !nb::inst_ready(owner_))
      throw nb::value_error("STL application reference is cleared or unready");
    const auto& app = nb::inst_ptr<StlPythonApplicationOwner>(owner_)->app;
    if (!app) throw nb::value_error("STL application has been cleared");
    return *app;
  }
  nb::object owner_;
};
struct StlPythonResult {
  StlPythonResult(const StlPythonApplicationRef& application,
                  std::size_t result_index)
      : app(application), index(result_index) {}
  StlPythonResult(const StlPythonResult&) = default;
  StlPythonApplicationRef app;
  std::size_t index;
  ~StlPythonResult() { Clear(); }
  void Clear() noexcept { app.Clear(); }
};
struct StlPythonBatchResult {
  StlPythonBatchResult(const StlPythonApplicationRef& application,
                       nb::tuple result_jobs)
      : app(application), jobs(std::move(result_jobs)) {}
  StlPythonBatchResult(const StlPythonBatchResult&) = default;
  StlPythonApplicationRef app;
  nb::tuple jobs;
  ~StlPythonBatchResult() { Clear(); }
  void Clear() noexcept {
    auto* old_app = app.Release();
    auto* old_jobs = jobs.release().ptr();
    Py_XDECREF(old_jobs);
    Py_XDECREF(old_app);
  }
};
struct StlPythonPreparedStorage {
  StlPythonPreparedStorage(const StlPythonApplicationRef& application,
                           nb::tuple result_slots, nb::object holder)
      : app(application),
        slots(std::move(result_slots)),
        result(std::move(holder)) {}
  StlPythonPreparedStorage(const StlPythonPreparedStorage&) = default;
  StlPythonApplicationRef app;
  nb::tuple slots;
  nb::object result;
  ~StlPythonPreparedStorage() { Clear(); }
  void Clear() noexcept {
    auto* old_app = app.Release();
    auto* old_slots = slots.release().ptr();
    auto* old_result = result.release().ptr();
    Py_XDECREF(old_result);
    Py_XDECREF(old_slots);
    Py_XDECREF(old_app);
  }
};
struct StlPythonPreparedSingle : StlPythonPreparedStorage {};
struct StlPythonPreparedBatch : StlPythonPreparedStorage {};
template <class T>
T& ReadyPayload(nb::handle value) {
  if (!nb::inst_ready(value)) throw nb::value_error("STL holder is unready");
  return *nb::inst_ptr<T>(value);
}
StlPythonPreparedStorage& Prepared(nb::handle value) {
  if (nb::isinstance<StlPythonPreparedSingle>(value))
    return ReadyPayload<StlPythonPreparedSingle>(value);
  if (nb::isinstance<StlPythonPreparedBatch>(value))
    return ReadyPayload<StlPythonPreparedBatch>(value);
  throw nb::type_error("expected a prepared STL operation");
}
StlPythonApp& Application(nb::handle value) {
  if (nb::isinstance<StlPythonResult>(value))
    return *ReadyPayload<StlPythonResult>(value).app;
  if (nb::isinstance<StlPythonBatchResult>(value))
    return *ReadyPayload<StlPythonBatchResult>(value).app;
  throw nb::type_error("expected a stable STL result");
}

// These visits describe physical owning references, including duplicate
// targets. The shared native App is traversed only at its one application-owner
// node.
int Traverse(const StlPythonApplicationOwner& owner, visitproc visit,
             void* arg) noexcept {
  const auto* app = owner.app.get();
  if (!app) return 0;
  for (const auto& job : app->jobs) {
    Py_VISIT(job.provider.ptr());
    Py_VISIT(job.report_backing.ptr());
    Py_VISIT(job.owners.captured.ptr());
    Py_VISIT(job.owners.source_document.ptr());
    Py_VISIT(job.owners.asset.ptr());
    Py_VISIT(job.owners.output.ptr());
    Py_VISIT(job.owners.fresh_document.ptr());
    Py_VISIT(job.owners.verified.ptr());
    Py_VISIT(job.owners.bundle.ptr());
  }
  Py_VISIT(app->unexpected.python_value.ptr());
  return 0;
}
int Traverse(const StlPythonResult& self, visitproc visit, void* arg) noexcept {
  Py_VISIT(self.app.Owner());
  return 0;
}
int Traverse(const StlPythonBatchResult& self, visitproc visit,
             void* arg) noexcept {
  Py_VISIT(self.app.Owner());
  Py_VISIT(self.jobs.ptr());
  return 0;
}
int Traverse(const StlPythonPreparedStorage& self, visitproc visit,
             void* arg) noexcept {
  Py_VISIT(self.app.Owner());
  Py_VISIT(self.slots.ptr());
  Py_VISIT(self.result.ptr());
  return 0;
}
template <class Native>
int Traverse(const StlPythonOwner<Native>& self, visitproc visit,
             void* arg) noexcept {
  Py_VISIT(self.source.ptr());
  Py_VISIT(self.output.ptr());
  Py_VISIT(self.document.ptr());
  Py_VISIT(self.verified.ptr());
  return 0;
}
template <class T>
int TraverseShell(PyObject* self, visitproc visit, void* arg) noexcept {
  // NB may track the shell before placement construction. The type reference
  // already exists, but no C++ payload field may be read before mark-ready.
  // CPython's subtype traversal handles subclass fields and delegates this
  // heap-type edge here; do not visit it again through a base traversal.
  Py_VISIT(Py_TYPE(self));
  const nb::handle object(self);
  if (!nb::inst_ready(object)) return 0;
  return Traverse(*nb::inst_ptr<T>(object), visit, arg);
}
template <class T>
int ClearShell(PyObject* self) noexcept {
  const nb::handle object(self);
  if (nb::inst_ready(object)) nb::inst_ptr<T>(object)->Clear();
  return 0;
}
template <class T>
nb::type_slots GcSlots() {
  static const PyType_Slot kSlots[] = {
      {Py_tp_traverse, reinterpret_cast<void*>(TraverseShell<T>)},
      {Py_tp_clear, reinterpret_cast<void*>(ClearShell<T>)},
      {0, nullptr}};
  return nb::type_slots(kSlots);
}

void NativeExtent(std::size_t size, std::uint64_t cap) {
  if (size >= cap)
    throw nb::value_error(
        "native path extent exceeds its inclusive terminated bound");
}
sd::NativeString NativePath(nb::handle value, const alloc::AllocationContext& a,
                            std::uint64_t cap) {
  auto path = nb::steal<nb::object>(PyOS_FSPath(value.ptr()));
  if (!path.is_valid()) throw nb::python_error();
  // Check representation, NUL and the preconversion extent before entering
  // the Python filesystem codec. Its temporary buffers are interpreter-owned;
  // every retained native plan below uses the actual charged allocator.
  if (PyUnicode_Check(path.ptr())) {
    const auto length = PyUnicode_GetLength(path.ptr());
    NativeExtent(static_cast<std::size_t>(length), cap);
    const auto found = PyUnicode_FindChar(path.ptr(), 0, 0, length, 1);
    if (found == -2) throw nb::python_error();
    if (found >= 0) throw nb::value_error("path contains NUL");
  } else if (PyBytes_Check(path.ptr())) {
    const auto length = static_cast<std::size_t>(PyBytes_GET_SIZE(path.ptr()));
    NativeExtent(length, cap);
    if (std::memchr(PyBytes_AS_STRING(path.ptr()), 0, length))
      throw nb::value_error("path contains NUL");
  } else
    throw nb::type_error("path must resolve to str or bytes");
  sd::NativeString result{alloc::Allocator<sd::NativeChar>(a)};
#ifdef _WIN32
  if (PyBytes_Check(path.ptr())) {
    NativeExtent(static_cast<std::size_t>(PyBytes_GET_SIZE(path.ptr())), cap);
    path = nb::steal<nb::object>(PyUnicode_DecodeFSDefaultAndSize(
        PyBytes_AS_STRING(path.ptr()), PyBytes_GET_SIZE(path.ptr())));
    if (!path.is_valid()) throw nb::python_error();
  }
  if (!PyUnicode_Check(path.ptr()))
    throw nb::type_error("path must resolve to str or bytes");
  NativeExtent(static_cast<std::size_t>(PyUnicode_GetLength(path.ptr())), cap);
  Py_ssize_t units = 0;
  struct WideOwner {
    wchar_t* data = nullptr;
    ~WideOwner() noexcept { PyMem_Free(data); }
  } wide;
  wide.data = PyUnicode_AsWideCharString(path.ptr(), &units);
  if (!wide.data) throw nb::python_error();
  NativeExtent(static_cast<std::size_t>(units), cap);
  result.reserve(static_cast<std::size_t>(units));
  for (Py_ssize_t i = 0; i < units; ++i) {
    if (!wide.data[i]) throw nb::value_error("path contains NUL");
    result.push_back(wide.data[i]);
  }
#else
  if (PyUnicode_Check(path.ptr())) {
    NativeExtent(static_cast<std::size_t>(PyUnicode_GetLength(path.ptr())),
                 cap);
    path = nb::steal<nb::object>(PyUnicode_EncodeFSDefault(path.ptr()));
    if (!path.is_valid()) throw nb::python_error();
  }
  if (!PyBytes_Check(path.ptr()))
    throw nb::type_error("path must resolve to str or bytes");
  const auto size = static_cast<std::size_t>(PyBytes_GET_SIZE(path.ptr()));
  NativeExtent(size, cap);
  const auto* bytes = PyBytes_AS_STRING(path.ptr());
  if (std::memchr(bytes, 0, size)) throw nb::value_error("path contains NUL");
  sd::Assign(result, std::string_view(bytes, size), a);
#endif
  if (result.empty()) throw nb::value_error("path is empty");
  return result;
}
constexpr Py_UCS4 kAsciiPrintableBegin = 0x20;
constexpr Py_UCS4 kAsciiPrintableEnd = 0x7f;
constexpr Py_UCS4 kUnicodeDelAndC1ControlEnd = 0xa0;
void ScalarText(nb::handle value, sd::String& out, std::uint64_t cap,
                const alloc::AllocationContext& a, bool nonempty,
                bool controls) {
  const auto text = Text(value);
  if (text.size() > cap || (nonempty && text.empty()))
    throw nb::value_error("text extent exceeds its bound");
  if (!controls)
    for (Py_ssize_t i = 0; i < PyUnicode_GetLength(value.ptr()); ++i) {
      const auto c = PyUnicode_ReadChar(value.ptr(), i);
      if (c < kAsciiPrintableBegin ||
          (c >= kAsciiPrintableEnd && c < kUnicodeDelAndC1ControlEnd))
        throw nb::value_error("text contains a control character");
    }
  sd::Assign(out, text, a);
}
io::StlEncoding Encoding(nb::handle value) {
  const auto text = Text(value);
  if (text == kAscii) return io::StlEncoding::kAscii;
  if (text == kBinary) return io::StlEncoding::kBinary;
  throw nb::value_error("explicit ascii or binary encoding is required");
}
void ReadOnly(io::StlFileObservations& o) noexcept {
  for (auto* stage :
       {&o.write, &o.staged_reread, &o.fresh_read, &o.verification, &o.provider,
        &o.receipt_verification, &o.inventory, &o.publication})
    stage->state = io::StlFileObservationState::kNotApplicable;
}
nb::tuple ResultSlots(const StlPythonApplicationRef& app, std::size_t count) {
  auto slots =
      nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(count)));
  if (!slots.is_valid()) throw nb::python_error();
  for (std::size_t i = 0; i < count; ++i) {
    auto shell = nb::inst_alloc(nb::type<StlPythonResult>());
    new (nb::inst_ptr<StlPythonResult>(shell)) StlPythonResult{app, i};
    nb::inst_mark_ready(shell);
    PyTuple_SET_ITEM(slots.ptr(), static_cast<Py_ssize_t>(i),
                     shell.release().ptr());
  }
  return slots;
}
nb::object Create(nb::handle config, nb::handle execution, nb::handle mode,
                  nb::handle policy) {
  if (!nb::isinstance<StlPythonExecution>(execution))
    throw nb::type_error("execution must be StlExecutionContext");
  const auto m = Text(mode);
  const auto fail = Text(policy);
  const bool batch = m == kBatch;
  if (m != kRead && m != kRewrite && !batch)
    throw nb::value_error("invalid STL operation mode");
  if (fail != app_value::kContinue && fail != app_value::kFailFastPolicy)
    throw nb::value_error("invalid failure policy");
  const auto preset = Preset(config);
  const auto& context = ReadyPayload<StlPythonExecution>(execution).context;
  auto a = alloc::AllocationContext(context, {})
               .WithRequestLimit(Host(
                   preset.application.max_application_new_requested_bytes));
  auto application_owner = Shell<StlPythonApplicationOwner>();
  nb::inst_ptr<StlPythonApplicationOwner>(application_owner)->app =
      std::allocate_shared<StlPythonApp>(
          alloc::Allocator<StlPythonApp>(a), a, context, preset,
          batch ? kBatch : (m == kRead ? kRead : kRewrite),
          fail == app_value::kFailFastPolicy);
  const StlPythonApplicationRef app(std::move(application_owner));
  const auto slots = ResultSlots(app, Host(preset.application.max_jobs));
  nb::object result;
  if (batch) {
    result = nb::inst_alloc(nb::type<StlPythonBatchResult>());
    new (nb::inst_ptr<StlPythonBatchResult>(result))
        StlPythonBatchResult{app, nb::tuple()};
    nb::inst_mark_ready(result);
  } else
    result = nb::borrow<nb::object>(slots[0]);
  auto prepared = nb::inst_alloc(batch ? nb::type<StlPythonPreparedBatch>()
                                       : nb::type<StlPythonPreparedSingle>());
  if (batch)
    new (nb::inst_ptr<StlPythonPreparedBatch>(prepared))
        StlPythonPreparedBatch{{app, slots, result}};
  else
    new (nb::inst_ptr<StlPythonPreparedSingle>(prepared))
        StlPythonPreparedSingle{{app, slots, result}};
  nb::inst_mark_ready(prepared);
  return prepared;
}
void Add(nb::handle prepared, nb::handle value) {
  auto& app = *Prepared(prepared).app;
  if (app.finished || app.used) throw nb::value_error("preparation is sealed");
  if (app.count >= app.jobs.size())
    throw nb::value_error("job extent exceeds max_jobs");
  auto& job = app.jobs[app.count];
  const auto mode = Text(value[wire_key::kMode]);
  job.rewrite = mode == kRewrite;
  if (mode != kRead && !job.rewrite) throw nb::value_error("invalid job mode");
  const auto record =
      job.rewrite
          ? geo::python::exact_dict(
                value,
                {wire_key::kJobId, wire_key::kMode, wire_key::kInput,
                 wire_key::kSourceEncoding, wire_key::kUnits,
                 wire_key::kDestination, wire_key::kOutputEncoding,
                 wire_key::kMetadata, wire_key::kAcknowledgeCornerExpansion,
                 wire_key::kReport})
          : geo::python::exact_dict(
                value, {wire_key::kJobId, wire_key::kMode, wire_key::kInput,
                        wire_key::kSourceEncoding, wire_key::kUnits});
  const auto& a = app.allocation;
  ScalarText(record[wire_key::kJobId], job.job_id,
             app.preset.application.max_job_id_bytes, a, true, false);
  job.input =
      NativePath(record[wire_key::kInput], a,
                 app.preset.read_limits.files.max_native_path_code_units);
  job.source_encoding = Encoding(record[wire_key::kSourceEncoding]);
  const auto units = geo::python::exact_dict(
      record[wire_key::kUnits], {wire_key::kKind, wire_key::kLabel});
  const auto kind = Text(units[wire_key::kKind]);
  if (kind == kUnknown) {
    job.unit_kind = io::StlFileUnitKind::kUnknown;
    if (!units[wire_key::kLabel].is_none())
      throw nb::value_error("unknown requires label=None");
  } else if (kind == kLabel) {
    job.unit_kind = io::StlFileUnitKind::kLabel;
    ScalarText(units[wire_key::kLabel], job.unit_label,
               app.preset.rewrite_limits.files.max_unit_label_bytes, a, true,
               false);
  } else
    throw nb::value_error("invalid unit kind");
  if (job.rewrite) {
    job.destination =
        NativePath(record[wire_key::kDestination], a,
                   app.preset.rewrite_limits.files.max_native_path_code_units);
    job.output_encoding = Encoding(record[wire_key::kOutputEncoding]);
    if (record[wire_key::kAcknowledgeCornerExpansion].ptr() != Py_True)
      throw nb::value_error("built-in True acknowledgment is required");
    job.acknowledge = true;
    const auto metadata = geo::python::exact_dict(
        record[wire_key::kMetadata],
        {wire_key::kMode, wire_key::kAsciiName, wire_key::kBinaryHeader});
    const auto requested = Text(metadata[wire_key::kMode]);
    if (requested == kRetain) {
      job.metadata_mode = io::StlFileMetadataMode::kRetain;
      if (job.source_encoding != job.output_encoding ||
          !metadata[wire_key::kAsciiName].is_none() ||
          !metadata[wire_key::kBinaryHeader].is_none())
        throw nb::value_error(
            "retain requires matching encodings and inactive replacement "
            "fields");
    } else if (requested == kReplace) {
      job.metadata_mode = io::StlFileMetadataMode::kReplace;
      if (job.output_encoding == io::StlEncoding::kAscii) {
        if (!metadata[wire_key::kBinaryHeader].is_none())
          throw nb::value_error("ASCII replacement has no binary header");
        ScalarText(
            metadata[wire_key::kAsciiName], job.ascii_name,
            std::min(app.preset.rewrite_limits.write.max_name_bytes,
                     app.preset.rewrite_limits.fresh_read.max_name_bytes),
            a, false, true);
        for (unsigned char c : job.ascii_name)
          if (c < kAsciiPrintableBegin || c >= kAsciiPrintableEnd)
            throw nb::value_error("ASCII metadata is not printable ASCII");
      } else {
        if (!metadata[wire_key::kAsciiName].is_none() ||
            !PyBytes_Check(metadata[wire_key::kBinaryHeader].ptr()) ||
            static_cast<std::size_t>(
                PyBytes_GET_SIZE(metadata[wire_key::kBinaryHeader].ptr())) !=
                io::kStlFileBinaryHeaderBytes)
          throw nb::value_error(
              "binary replacement requires an opaque 80-byte header");
        std::memcpy(job.binary_header.data(),
                    PyBytes_AS_STRING(metadata[wire_key::kBinaryHeader].ptr()),
                    job.binary_header.size());
      }
    } else
      throw nb::value_error("invalid metadata mode");
    job.provider = nb::borrow<nb::object>(record[wire_key::kReport]);
    if (!job.provider.is_none() && !PyCallable_Check(job.provider.ptr()))
      throw nb::type_error("report must be callable or None");
    if (job.provider.is_none())
      job.initial.observations.provider.state =
          io::StlFileObservationState::kNotApplicable;
  } else
    ReadOnly(job.initial.observations);
  ++app.count;
}
void Reject(StlPythonApp& app, std::string_view code,
            std::optional<std::uint64_t> line = {},
            std::optional<std::uint64_t> index = {},
            std::string_view field = {},
            std::optional<io::StlFileDiagnostic> file = {}) noexcept {
  if (!app.admission.code.empty()) return;
  app.admission = {code, field, line, index, file};
  for (std::size_t i = 0; i < app.count; ++i)
    if (!app.jobs[i].attempted)
      app.jobs[i].skipped = app_value::kAdmissionFailed;
}
std::string_view AdmissionCode(nb::handle value) {
  const auto code = Text(value);
  for (auto known :
       {admission_code::kRepresentation, admission_code::kManifestUtf8,
        admission_code::kManifestJson, admission_code::kManifestExtent,
        admission_code::kJobExtent, admission_code::kDuplicateJobId,
        admission_code::kPathAdmission, admission_code::kDomainOverlap,
        admission_code::kRootBudget, admission_code::kPreviouslyRun})
    if (code == known) return known;
  throw nb::value_error("invalid admission code");
}
std::optional<std::uint64_t> OptionalIndex(nb::handle value) {
  if (value.is_none()) return {};
  if (!PyLong_CheckExact(value.ptr()))
    throw nb::type_error("index must be int or None");
  const auto n = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  return n;
}
void RejectPython(nb::handle prepared, nb::handle code, nb::handle line,
                  nb::handle index, nb::handle field) {
  std::string_view named;
  if (!field.is_none()) {
    const auto text = Text(field);
    for (auto key :
         {wire_key::kJobId, wire_key::kMode, wire_key::kInput,
          wire_key::kSourceEncoding, wire_key::kUnits, wire_key::kDestination,
          wire_key::kOutputEncoding, wire_key::kMetadata,
          wire_key::kAcknowledgeCornerExpansion, "report_text"})
      if (text == key) named = key;
    if (named.empty()) throw nb::value_error("invalid admitted field");
  }
  Reject(*Prepared(prepared).app, AdmissionCode(code), OptionalIndex(line),
         OptionalIndex(index), named);
}
sd::NativeChar Fold(sd::NativeChar c) noexcept {
#ifdef _WIN32
  if (c >= L'A' && c <= L'Z')
    return static_cast<sd::NativeChar>(c + (L'a' - L'A'));
#endif
  return c;
}
bool EqualPath(sd::NativeView a, sd::NativeView b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (Fold(a[i]) != Fold(b[i])) return false;
  return true;
}
bool Overlap(sd::NativeView a, sd::NativeView b) noexcept {
  const auto ancestor = [](sd::NativeView first, sd::NativeView second) {
    return first.size() <= second.size() &&
           EqualPath(first, second.substr(0, first.size())) &&
           (first.size() == second.size() || first.back() == sd::kSeparator ||
            second[first.size()] == sd::kSeparator);
  };
  return ancestor(a, b) || ancestor(b, a);
}
sd::NativeString Normalize(sd::NativeView path,
                           const alloc::AllocationContext& a) {
  sd::NativeString out{alloc::Allocator<sd::NativeChar>(a)};
  out.reserve(path.size());
#ifdef _WIN32
  constexpr auto root = sd::kDriveRootUnits;
#else
  constexpr std::size_t root = 1;
#endif
  sd::Extend(out, path.substr(0, root), a);
  for (std::size_t begin = root; begin < path.size();) {
    while (begin < path.size() && path[begin] == sd::kSeparator) ++begin;
    auto end = begin;
    while (end < path.size() && path[end] != sd::kSeparator) ++end;
    const auto part = path.substr(begin, end - begin);
    if (part.size() == 1 && part[0] == static_cast<sd::NativeChar>('.')) {
    } else if (part.size() == 2 &&
               part[0] == static_cast<sd::NativeChar>('.') &&
               part[1] == static_cast<sd::NativeChar>('.')) {
      if (out.size() > root) {
        auto last = out.size();
        while (last > root && out[last - 1] != sd::kSeparator) --last;
        out.resize(last > root ? last - 1 : root);
      }
    } else if (!part.empty()) {
      if (out.back() != sd::kSeparator) out.push_back(sd::kSeparator);
      sd::Extend(out, part, a);
    }
    begin = end;
  }
  return out;
}
sd::NativeString Absolute(sd::NativeView path, sd::NativeView cwd,
                          const alloc::AllocationContext& a,
                          const io::StlFileLimits& limits) {
#ifdef _WIN32
  const bool qualified = path.front() == L'\\' || path.front() == L'/' ||
                         (path.size() > 1 && path[1] == L':');
#else
  const bool qualified = path.front() == sd::kSeparator;
#endif
  if (qualified) return sd::AbsolutePath(path, a, limits);
  sd::NativeString joined{alloc::Allocator<sd::NativeChar>(a)};
  NativeExtent(cwd.size() + path.size() + 1, limits.max_native_path_code_units);
  joined.reserve(cwd.size() + path.size() + 1);
  sd::Extend(joined, cwd, a);
  if (joined.back() != sd::kSeparator) joined.push_back(sd::kSeparator);
  sd::Extend(joined, path, a);
  return sd::AbsolutePath(joined, a, limits);
}
void AdmitSource(sd::NativeView path, sd::NativeString& logical,
                 sd::NativeMetadata& identity, StlPythonApp& app,
                 bool stl_entry = true) {
  const auto& a = app.allocation;
  const auto& limits = app.preset.read_limits.files;
  if (stl_entry) {
    auto name = sd::EntryName(path, a, limits);
    if (sd::EqualFold(name, kReceipt, &a))
      sd::Fail(io::StlFileStatus::kInvalidRequest,
               io::StlFilePhase::kPathPlanning,
               io::StlFileDiagnosticCode::kNameCollision);
  }
  sd::RejectAliases(path, false, a, limits);
  logical = Normalize(path, a);
  auto file = sd::OpenFile(path, 0, {}, false, io::StlFilePhase::kPathPlanning);
  identity = sd::Metadata(file);
  auto canonical = sd::CanonicalPath(file, a, limits);
  if (!EqualPath(logical, canonical))
    sd::Fail(io::StlFileStatus::kFileChanged, io::StlFilePhase::kPathPlanning,
             io::StlFileDiagnosticCode::kChanged);
  sd::CloseChecked(file, {}, io::StlFilePhase::kPathPlanning);
}
void Finish(nb::handle prepared, nb::handle current_directory) {
  auto& storage = Prepared(prepared);
  auto& app = *storage.app;
  if (app.finished) throw nb::value_error("preparation is sealed");
  if (app.mode == kBatch) {
    auto tuple =
        nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(app.count)));
    if (!tuple.is_valid()) throw nb::python_error();
    for (std::size_t i = 0; i < app.count; ++i) {
      auto item = nb::borrow<nb::object>(storage.slots[i]);
      PyTuple_SET_ITEM(tuple.ptr(), static_cast<Py_ssize_t>(i),
                       item.release().ptr());
    }
    auto& result = ReadyPayload<StlPythonBatchResult>(storage.result);
    (void)*result.app;
    result.jobs = std::move(tuple);
  }
  app.finished = true;
  if (!app.admission.code.empty()) return;
  const auto& a = app.allocation;
  const sd::TestOptions testing;
  const auto cwd =
      NativePath(current_directory, a,
                 app.preset.read_limits.files.max_native_path_code_units);
  const sd::NativeView base = app.manifest_parent.empty()
                                  ? sd::NativeView(cwd)
                                  : sd::NativeView(app.manifest_parent);
  for (std::size_t i = 0; i < app.count; ++i) {
    auto& job = app.jobs[i];
    for (std::size_t earlier = 0; earlier < i; ++earlier)
      if (job.job_id == app.jobs[earlier].job_id) {
        Reject(app, admission_code::kDuplicateJobId, {}, i, wire_key::kJobId);
        return;
      }
    std::string_view planning_field = wire_key::kInput;
    try {
      job.input = Absolute(job.input, base, a, app.preset.read_limits.files);
      AdmitSource(job.input, job.logical, job.identity, app);
      if (job.rewrite) {
        planning_field = wire_key::kDestination;
        job.destination =
            Absolute(job.destination, base, a, app.preset.rewrite_limits.files);
        const auto entry =
            sd::EntryName(job.input, a, app.preset.read_limits.files);
        sd::NativePublication plan(a, app.preset.rewrite_limits.files, testing);
        plan.Plan(job.destination, entry, kReceipt);
        auto normalized = Normalize(job.destination, a);
        const auto separator = sd::LastSeparator(normalized, a);
        const auto tail = sd::NativeView(normalized).substr(separator + 1);
        sd::NativeString canonical{alloc::Allocator<sd::NativeChar>(a)};
        canonical.reserve(plan.Parent().size() + tail.size() + 1);
        sd::Extend(canonical, plan.Parent(), a);
        if (canonical.back() != sd::kSeparator)
          canonical.push_back(sd::kSeparator);
        sd::Extend(canonical, tail, a);
        job.destination_logical = Normalize(canonical, a);
      }
    } catch (const sd::Failure& error) {
      Reject(app, admission_code::kPathAdmission, {}, i, planning_field,
             error.diagnostic);
      return;
    }
  }
  for (std::size_t i = 0; i < app.count; ++i)
    for (std::size_t j = 0; j < app.count; ++j) {
      const auto& first = app.jobs[i];
      const auto& second = app.jobs[j];
      const bool same_identity =
          first.identity.volume == second.identity.volume &&
          first.identity.identity == second.identity.identity;
      if ((i != j &&
           (EqualPath(first.logical, second.logical) || same_identity)) ||
          (first.rewrite &&
           (Overlap(first.destination_logical, second.logical) ||
            (i != j && second.rewrite &&
             Overlap(first.destination_logical, second.destination_logical)) ||
            (!app.manifest_logical.empty() &&
             Overlap(first.destination_logical, app.manifest_logical))))) {
        io::StlFileDiagnostic d;
        d.status = io::StlFileStatus::kInvalidRequest;
        d.phase = io::StlFilePhase::kPathPlanning;
        d.code = io::StlFileDiagnosticCode::kSourceDestinationOverlap;
        Reject(app, admission_code::kDomainOverlap, {}, i, {}, d);
        return;
      }
    }
  auto declared =
      Sum(app.context.ActiveTrackedPayload(),
          app.preset.application.max_application_new_requested_bytes);
  for (std::size_t i = 0; i < app.count; ++i)
    declared =
        Sum(declared, app.preset.application.max_job_new_requested_bytes);
  if (declared > app.context.TrackedPayloadBudget())
    Reject(app, admission_code::kRootBudget);
}
nb::object Manifest(nb::handle prepared, nb::handle path,
                    nb::handle current_directory) {
  auto& app = *Prepared(prepared).app;
  const auto& a = app.allocation;
  const auto& limits = app.preset.read_limits.files;
  auto cwd =
      NativePath(current_directory, a, limits.max_native_path_code_units);
  auto input = NativePath(path, a, limits.max_native_path_code_units);
  app.manifest_path = Absolute(input, cwd, a, limits);
  try {
    sd::NativeMetadata identity;
    AdmitSource(app.manifest_path, app.manifest_logical, identity, app, false);
    app.manifest_identity = identity;
    if (identity.size > app.preset.application.max_manifest_bytes) {
      Reject(app, admission_code::kManifestExtent);
      return nb::none();
    }
    auto file = sd::OpenFile(app.manifest_path, 0, {}, false,
                             io::StlFilePhase::kPathPlanning);
    if (sd::Metadata(file) != identity)
      sd::Fail(io::StlFileStatus::kFileChanged, io::StlFilePhase::kPathPlanning,
               io::StlFileDiagnosticCode::kChanged);
    app.manifest.emplace(a, Host(identity.size));
    const auto block = Host(std::min(app.preset.read_work.files.io_block_bytes,
                                     sd::MaximumReadBlockBytes()));
    for (std::size_t offset = 0; offset < app.manifest->size;) {
      const auto count = std::min(block, app.manifest->size - offset);
      sd::ReadBlock(file, offset, app.manifest->Bytes().subspan(offset, count));
      offset += count;
    }
    sd::RequireEnd(file, identity.size);
    if (sd::Metadata(file) != identity)
      sd::Fail(io::StlFileStatus::kFileChanged, io::StlFilePhase::kPathPlanning,
               io::StlFileDiagnosticCode::kChanged);
    sd::CloseChecked(file, {}, io::StlFilePhase::kPathPlanning);
    const auto separator = sd::LastSeparator(app.manifest_logical, a);
    sd::Assign(app.manifest_parent,
               sd::NativeView(app.manifest_logical)
                   .substr(0, separator ? separator : 1),
               a);
    return Binary(app.manifest->Bytes());
  } catch (const sd::Failure& error) {
    Reject(app, admission_code::kPathAdmission, {}, {}, {}, error.diagnostic);
    return nb::none();
  }
}

const io::StlFileResult& NativeResult(const StlPythonJob& job) noexcept {
  if (job.written) return *job.written;
  if (job.read) return *job.read;
  return job.initial;
}
std::string_view JobOutcome(const StlPythonJob& job) noexcept {
  if (!job.attempted || (!job.read && !job.written))
    return app_value::kNotAttempted;
  if (job.owners.bundle_ptr->native) return app_value::kPublished;
  const auto status = NativeResult(job).status;
  if (status == io::StlFileStatus::kCanceled) return app_value::kCanceled;
  if (status != io::StlFileStatus::kSuccess) return app_value::kFailed;
  if (job.boundary_canceled) return app_value::kCanceled;
  return wire_key::kRead;
}
// Called with the GIL held. Preserve the same normalized instance and its
// current traceback/payload, but give each consuming NB throwable fresh refs.
// CPython introduced the raised-instance interface in 3.12. A preprocessor
// version constant is required because the older interpreter lacks its symbol.
#define MESHVALE_STL_RAISED_EXCEPTION_API_MIN_VERSION 0x030C0000
void RethrowUnexpected(const StlPythonUnexpected& unexpected) {
  if (unexpected.python_value.is_valid()) {
    auto* value = unexpected.python_value.ptr();
#if PY_VERSION_HEX >= MESHVALE_STL_RAISED_EXCEPTION_API_MIN_VERSION
    PyErr_SetRaisedException(Py_NewRef(value));
#else
    PyErr_Restore(Py_NewRef(Py_TYPE(value)), Py_NewRef(value),
                  PyException_GetTraceback(value));
#endif
    throw nb::python_error();
  }
  if (unexpected.native) std::rethrow_exception(unexpected.native);
}
#undef MESHVALE_STL_RAISED_EXCEPTION_API_MIN_VERSION
std::string_view BatchOutcome(const StlPythonApp& app) {
  RethrowUnexpected(app.unexpected);
  if (!app.admission.code.empty()) return app_value::kAdmissionFailed;
  if (app.canceled || app.interrupted) return app_value::kCanceled;
  for (std::size_t i = 0; i < app.count; ++i)
    if (JobOutcome(app.jobs[i]) == app_value::kFailed)
      return app_value::kFailed;
  return app_value::kCompleted;
}
int ExitCode(const StlPythonApp& app) {
  RethrowUnexpected(app.unexpected);
  if (!app.admission.code.empty()) return kExitAdmission;
  if (app.canceled || app.interrupted) return kExitCanceled;
  if (app.delivery.state == app_value::kFailed) return kExitDelivery;
  for (std::size_t i = 0; i < app.count; ++i)
    if (JobOutcome(app.jobs[i]) == app_value::kFailed) return kExitJobFailure;
  return kExitSuccess;
}
void SyncRead(StlPythonJob& job) noexcept {
  if (!job.read) return;
  const auto& r = *job.read;
  auto& o = job.owners;
  if (r.captured)
    o.captured_ptr->native.emplace(*r.captured);
  else
    o.captured_ptr->native.reset();
  if (r.asset) {
    o.asset_ptr->native.emplace(*r.asset);
    o.source_document_ptr->native.emplace(r.asset->Document());
  }
}
void SyncRewrite(StlPythonJob& job) noexcept {
  if (!job.written) return;
  const auto& r = *job.written;
  auto& o = job.owners;
  if (r.output) o.output_ptr->native.emplace(*r.output);
  if (r.fresh_document) o.fresh_document_ptr->native.emplace(*r.fresh_document);
  if (r.verified) o.verified_ptr->native.emplace(*r.verified);
  if (r.bundle) o.bundle_ptr->native.emplace(*r.bundle);
}
void Interrupt(StlPythonApp& app) noexcept {
  app.interrupted = true;
  app.canceled = true;
  (void)app.stop.request_stop();
  app.delivery.state = app_value::kCanceled;
  app.delivery.code = app_value::kInterrupted;
}
bool Signals(StlPythonApp& app) {
  if (!PyErr_Occurred() && PyErr_CheckSignals() == 0) return false;
  if (PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
    PyErr_Clear();
    Interrupt(app);
    return true;
  }
  throw nb::python_error();
}
struct StlPythonProviderFailure {};
std::span<const std::byte> Provider(const io::StlFileVerifiedContent& content,
                                    void* opaque) {
  auto& job = *static_cast<StlPythonJob*>(opaque);
  auto& app = *job.application;
  nb::gil_scoped_acquire acquire;
  // Every Python owner shell and child link already exists. These are only
  // nonallocating copies of genuine owners reached by the native publisher.
  job.owners.output_ptr->native.emplace(content.verified.Output());
  job.owners.fresh_document_ptr->native.emplace(content.verified.Document());
  job.owners.verified_ptr->native.emplace(content.verified);
  const auto raised = [&]() -> bool {
    if (PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
      PyErr_Clear();
      Interrupt(app);
      return true;
    }
    if (PyErr_ExceptionMatches(PyExc_MemoryError)) {
      PyErr_Clear();
      throw std::bad_alloc();
    }
    bool interrupted = app.interrupted;
    bool allocation_failed = false;
    {
      // PyErr_CheckSignals needs a clear indicator. Preserve the original
      // ordinary provider error until a pending handler has been classified.
      nb::error_scope preserve;
      if (PyErr_CheckSignals() < 0) {
        interrupted =
            interrupted || PyErr_ExceptionMatches(PyExc_KeyboardInterrupt);
        allocation_failed = PyErr_ExceptionMatches(PyExc_MemoryError);
        PyErr_Clear();
      }
    }
    // The original error has been restored. Both ordinary provider/handler
    // errors become provider_failure unless an interruption was observed;
    // allocation failure keeps its distinct unexpected propagation.
    PyErr_Clear();
    if (allocation_failed) throw std::bad_alloc();
    if (interrupted) {
      Interrupt(app);
      return true;
    }
    throw StlPythonProviderFailure{};
  };
  if (app.interrupted && !PyErr_Occurred()) return {};
  if (PyErr_Occurred() && raised()) return {};
  if (PyErr_CheckSignals() < 0 && raised()) return {};
  auto returned = nb::steal<nb::object>(
      PyObject_CallOneArg(job.provider.ptr(), job.owners.verified.ptr()));
  if (!returned.is_valid() && raised()) return {};
  if (PyErr_Occurred() && raised()) return {};
  if (PyErr_CheckSignals() < 0 && raised()) return {};
  if (PyUnicode_Check(returned.ptr())) {
    returned = nb::steal<nb::object>(PyUnicode_AsUTF8String(returned.ptr()));
    if (!returned.is_valid() && raised()) return {};
  }
  if (!PyBytes_Check(returned.ptr())) throw StlPythonProviderFailure{};
  job.report_backing = std::move(returned);
  return {reinterpret_cast<const std::byte*>(
              PyBytes_AS_STRING(job.report_backing.ptr())),
          static_cast<std::size_t>(PyBytes_GET_SIZE(job.report_backing.ptr()))};
}
struct StlPythonForwardStop {
  std::stop_source* source;
  void operator()() const noexcept { (void)source->request_stop(); }
};
struct StlPythonRunGuard {
  StlPythonApp& app;
  ~StlPythonRunGuard() noexcept { app.running = false; }
};
nb::object Run(nb::handle prepared, nb::handle cancellation) {
  auto& storage = Prepared(prepared);
  auto& app = *storage.app;
  if (app.running) throw nb::value_error("prepared operation is non-reentrant");
  if (!app.finished) throw nb::value_error("preparation is incomplete");
  if (app.used) {
    Reject(app, admission_code::kPreviouslyRun);
    return storage.result;
  }
  const auto token = g_cancellation_reader(cancellation);
  std::stop_callback<StlPythonForwardStop> forward(
      token, StlPythonForwardStop{&app.stop});
  app.used = true;
  app.running = true;
  StlPythonRunGuard guard{app};
  try {
    if (!app.admission.code.empty()) return storage.result;
    if (Signals(app)) {
      for (std::size_t i = 0; i < app.count; ++i)
        app.jobs[i].skipped = app_value::kCanceled;
      return storage.result;
    }
    const sd::TestOptions testing;
    for (std::size_t index = 0; index < app.count; ++index) {
      auto& job = app.jobs[index];
      if (app.stop.stop_requested() && app.mode == kBatch) {
        app.canceled = true;
        for (std::size_t remaining = index; remaining < app.count; ++remaining)
          app.jobs[remaining].skipped = app_value::kCanceled;
        break;
      }
      job.attempted = true;
      {
        nb::gil_scoped_release release;
        job.read.emplace(sd::ReadNative(
            job.input, job.source_encoding, app.preset.read_limits,
            app.preset.read_work, app.context, app.stop.get_token(), testing));
        SyncRead(job);
      }
      if (Signals(app) && job.rewrite) job.boundary_canceled = true;
      if (job.read->asset && job.rewrite && !app.interrupted) {
        io::StlFileRewriteOptions options;
        options.encoding = job.output_encoding;
        options.metadata_mode = job.metadata_mode;
        options.ascii_name = job.ascii_name;
        if (job.metadata_mode == io::StlFileMetadataMode::kReplace &&
            job.output_encoding == io::StlEncoding::kBinary)
          options.binary_header = job.binary_header;
        options.acknowledge_corner_expansion = job.acknowledge;
        options.unit_kind = job.unit_kind;
        options.unit_label = job.unit_label;
        if (!job.provider.is_none())
          options.report = io::StlFileReportProvider{Provider, &job};
        {
          nb::gil_scoped_release release;
          job.written.emplace(sd::RewriteNative(
              *job.read->asset, job.destination, options,
              app.preset.rewrite_limits, app.preset.rewrite_work,
              app.stop.get_token(), testing));
          job.written->execution.capture = job.read->execution.capture;
          job.written->execution.read = job.read->execution.read;
          SyncRewrite(job);
        }
        // No Python owner creation, receipt conversion or physical action is
        // mandatory after native commit. An actual interpreter boundary may
        // record a late interruption without changing a committed publication.
        if (Signals(app)) job.boundary_canceled = true;
      }
      const auto outcome = JobOutcome(job);
      if (outcome == app_value::kCanceled || app.interrupted) {
        app.canceled = true;
        for (std::size_t remaining = index + 1; remaining < app.count;
             ++remaining)
          app.jobs[remaining].skipped = app_value::kCanceled;
        break;
      }
      if (outcome == app_value::kFailed && app.fail_fast) {
        for (std::size_t remaining = index + 1; remaining < app.count;
             ++remaining)
          app.jobs[remaining].skipped = app_value::kFailFast;
        break;
      }
    }
    return storage.result;
  } catch (const nb::python_error& error) {
    // Pinned NB3.1's pre3.12 fetch normalizes the C-API triple and attaches its
    // fetched traceback to value before this catch. Retain it before release;
    // do not replace it with a traceback snapshot or reconstruct the exception.
    app.unexpected.python_value = nb::borrow<nb::object>(error.value());
    RethrowUnexpected(app.unexpected);
    throw;
  } catch (...) {
    app.unexpected.native = std::current_exception();
    throw;
  }
}
nb::object Interrupted(nb::handle prepared) {
  auto& storage = Prepared(prepared);
  Interrupt(*storage.app);
  for (std::size_t i = 0; i < storage.app->count; ++i)
    if (!storage.app->jobs[i].attempted)
      storage.app->jobs[i].skipped = app_value::kCanceled;
  return storage.result;
}
nb::object Declarations(const StlPythonJob& job) {
  nb::dict out;
  out[wire_key::kSourceEncoding] = Wire(job.source_encoding);
  out[wire_key::kOutputEncoding] =
      job.rewrite ? Wire(job.output_encoding) : nb::none();
  out[wire_key::kMetadataMode] =
      job.rewrite ? Wire(job.metadata_mode) : nb::none();
  nb::dict units;
  units[wire_key::kKind] = Wire(job.unit_kind);
  units[wire_key::kLabel] = job.unit_kind == io::StlFileUnitKind::kLabel
                                ? PythonText(job.unit_label)
                                : nb::none();
  units[wire_key::kRescaled] = false;
  out[wire_key::kUnits] = units;
  out[wire_key::kAcknowledgeCornerExpansion] =
      job.rewrite ? nb::object(nb::bool_(job.acknowledge)) : nb::none();
  return out;
}
nb::object Admission(const StlPythonApp& app) {
  if (app.admission.code.empty()) return nb::none();
  const auto& a = app.admission;
  nb::dict out;
  out[wire_key::kCode] = PythonText(a.code);
  out[wire_key::kLineIndex] = Wire(a.line_index);
  out[wire_key::kJobIndex] = Wire(a.job_index);
  out[wire_key::kField] = a.field.empty() ? nb::none() : PythonText(a.field);
  out[wire_key::kFileDiagnostic] = Wire(a.file_diagnostic);
  return out;
}
nb::object Delivery(const StlPythonDelivery& value) {
  nb::dict out;
  out[wire_key::kState] = PythonText(value.state);
  out[wire_key::kCode] =
      value.code.empty() ? nb::none() : PythonText(value.code);
  out[wire_key::kBytesWritten] = value.bytes_written;
  return out;
}
io::StlFileRequestedBytes Requested(const StlPythonApp& app,
                                    const StlPythonJob& job) noexcept {
  auto out = NativeResult(job).requested_bytes;
  out.application = app.allocation.OperationActiveBytes();
  if (job.owners.captured_ptr->native)
    out.file_capture = job.owners.captured_ptr->native->NewRequestedBytes();
  if (job.owners.asset_ptr->native) {
    out.file_capture = job.owners.asset_ptr->native->NewRequestedBytes();
    out.read = job.owners.asset_ptr->native->Document().NewRequestedBytes();
  }
  if (job.owners.output_ptr->native)
    out.write = job.owners.output_ptr->native->NewRequestedBytes();
  if (job.owners.fresh_document_ptr->native)
    out.fresh_read = job.owners.fresh_document_ptr->native->NewRequestedBytes();
  if (job.owners.verified_ptr->native)
    out.file_publication = job.owners.verified_ptr->native->NewRequestedBytes();
  if (job.owners.bundle_ptr->native)
    out.file_publication = job.owners.bundle_ptr->native->NewRequestedBytes();
  return out;
}
nb::object Owners(const StlPythonJob& job) {
  nb::dict out;
  const auto& o = job.owners;
  out[wire_key::kCaptured] = o.captured_ptr->native.has_value();
  out[wire_key::kAsset] = o.asset_ptr->native.has_value();
  out[wire_key::kOutput] = o.output_ptr->native.has_value();
  out[wire_key::kFreshDocument] = o.fresh_document_ptr->native.has_value();
  out[wire_key::kVerified] = o.verified_ptr->native.has_value();
  out[wire_key::kBundle] = o.bundle_ptr->native.has_value();
  return out;
}
nb::object JobWire(const StlPythonApp& app, const StlPythonJob& job) {
  const auto& r = NativeResult(job);
  nb::dict out;
  out[wire_key::kJobId] = PythonText(job.job_id);
  out[wire_key::kMode] = PythonText(job.rewrite ? kRewrite : kRead);
  out[wire_key::kDeclarations] = Declarations(job);
  out[wire_key::kOutcome] = PythonText(JobOutcome(job));
  out[wire_key::kPhase] = Wire(r.phase);
  out[wire_key::kDiagnostic] =
      (job.read || job.written) && r.status != io::StlFileStatus::kSuccess
          ? Wire(r.diagnostic)
          : nb::none();
  out[wire_key::kObservations] = Wire(r.observations);
  out[wire_key::kExecution] = Wire(r.execution);
  out[wire_key::kRequestedBytes] = Wire(Requested(app, job));
  out[wire_key::kCleanup] = Wire(r.cleanup);
  out[wire_key::kCleanupError] = Wire(r.cleanup_error);
  out[wire_key::kNotAttemptedReason] =
      job.skipped.empty() ? nb::none() : PythonText(job.skipped);
  out[wire_key::kOwners] = Owners(job);
  return out;
}
nb::object Frame(const StlPythonApp& app) {
  // An unexpected exception has no fabricated terminal frame. Genuine owners
  // remain inspectable; explicitly requested framing preserves propagation.
  RethrowUnexpected(app.unexpected);
  nb::dict out;
  out[wire_key::kSchema] = PythonText(kResultSchema);
  out[wire_key::kMode] = PythonText(app.mode);
  out[wire_key::kOutcome] = PythonText(
      app.mode == kBatch
          ? BatchOutcome(app)
          : (app.count ? JobOutcome(app.jobs[0])
                       : std::string_view(app_value::kNotAttempted)));
  out[wire_key::kAdmissionDiagnostic] = Admission(app);
  nb::list jobs;
  for (std::size_t i = 0; i < app.count; ++i)
    jobs.append(JobWire(app, app.jobs[i]));
  out[wire_key::kJobs] = jobs;
  out[wire_key::kDelivery] = Delivery(StlPythonDelivery{});
  out[wire_key::kExitCode] = ExitCode(app);
  return out;
}
void SetDelivery(nb::handle result, nb::handle state, nb::handle code,
                 std::uint64_t written) {
  auto& app = Application(result);
  const auto selected = Text(state);
  std::string_view admitted;
  for (auto item : {app_value::kNotAttempted, app_value::kSucceeded,
                    app_value::kFailed, app_value::kCanceled})
    if (selected == item) admitted = item;
  if (admitted.empty()) throw nb::value_error("invalid delivery state");
  std::string_view reason;
  if (!code.is_none()) {
    const auto text = Text(code);
    for (auto item : {app_value::kFraming, wire_key::kWrite, app_value::kFlush,
                      app_value::kInterrupted})
      if (text == item) reason = item;
    if (reason.empty()) throw nb::value_error("invalid delivery code");
  }
  app.delivery = {admitted, reason, written};
  if (reason == app_value::kInterrupted) Interrupt(app);
}
void FrameBegin(nb::handle result) {
  auto& app = Application(result);
  app.frame_size = 0;
  app.delivery = {};
}
bool FrameChunk(nb::handle result, nb::bytes bytes) {
  auto& app = Application(result);
  const auto size = static_cast<std::size_t>(PyBytes_GET_SIZE(bytes.ptr()));
  const auto maximum = std::min(app.preset.application.max_result_bytes,
                                app.preset.application.max_delivery_bytes);
  if (size > maximum - app.frame_size) {
    app.delivery = {app_value::kFailed, app_value::kFraming, 0};
    return false;
  }
  std::memcpy(app.frame.data + app.frame_size, PyBytes_AS_STRING(bytes.ptr()),
              size);
  app.frame_size += size;
  return true;
}
nb::object FrameFinish(nb::handle result) {
  auto& app = Application(result);
  auto bytes = nb::bytes(app.frame.data, app.frame_size);
  app.delivery = {
      app_value::kSucceeded, {}, static_cast<std::uint64_t>(app.frame_size)};
  return bytes;
}

template <class Native>
const Native& RequireOwner(const StlPythonOwner<Native>& owner) {
  if (!owner.native) throw nb::value_error("native owner is not yet available");
  return *owner.native;
}
nb::object OutputHash(const io::StlOutput& output) {
  const auto bytes = output.Bytes();
  static constexpr std::byte kEmptyBuffer{};
  const auto* data = bytes.empty() ? &kEmptyBuffer : bytes.data();
  auto view = nb::steal<nb::object>(PyMemoryView_FromMemory(
      reinterpret_cast<char*>(const_cast<std::byte*>(data)),
      static_cast<Py_ssize_t>(bytes.size()), PyBUF_READ));
  if (!view.is_valid()) throw nb::python_error();
  return nb::module_::import_("hashlib").attr(wire_key::kSha256)(view).attr(
      "hexdigest")();
}
nb::object ExportMesh(const StlPythonDocument& owner) {
  const auto mesh = RequireOwner(owner).Mesh().View();
  nb::dict record;
  record[wire_key::kSchema] = geo::python::record_schema;
  record[wire_key::kPositions] = geo::python::WriteBufferData(
      mesh.positions.data(), mesh.positions.size(), sizeof(double), "d");
  record[wire_key::kFaceOffsets] = geo::python::WriteBufferData(
      mesh.face_offsets.data(), mesh.face_offsets.size(), sizeof(geo::index_t),
      "Q");
  record[wire_key::kCornerVertices] = geo::python::WriteBufferData(
      mesh.corner_vertices.data(), mesh.corner_vertices.size(),
      sizeof(geo::index_t), "Q");
  nb::list attributes;
  constexpr std::array<const char*, 7> kScalarNames{
      "float32", "float64", "int32", "uint8", "uint16", "uint32", "uint64"};
  constexpr std::array<const char*, 7> kScalarFormats{"f", "d", "i", "B",
                                                      "H", "I", "Q"};
  for (const auto& attribute : mesh.attributes) {
    nb::dict out;
    out[wire_key::kDomain] =
        attribute.domain == geo::AttributeDomain::face
            ? "face"
            : (attribute.domain == geo::AttributeDomain::vertex ? "vertex"
                                                                : "corner");
    out[wire_key::kName] = PythonText(attribute.name);
    out[wire_key::kSemantic] = PythonText(attribute.semantic);
    out[wire_key::kSetIndex] = Wire(attribute.set_index);
    out[wire_key::kComponents] = attribute.components;
    out[wire_key::kScalarType] = kScalarNames[attribute.values.index()];
    out[wire_key::kValues] = std::visit(
        [&](const auto& values) {
          using Scalar = typename std::decay_t<decltype(values)>::value_type;
          return geo::python::WriteBufferData(
              values.data(), values.size(), sizeof(Scalar),
              kScalarFormats[attribute.values.index()]);
        },
        attribute.values);
    out[wire_key::kOffsets] = attribute.offsets ? geo::python::WriteBufferData(
                                                      attribute.offsets->data(),
                                                      attribute.offsets->size(),
                                                      sizeof(geo::index_t), "Q")
                                                : nb::none();
    out[wire_key::kPresent] = attribute.present ? geo::python::WriteBufferData(
                                                      attribute.present->data(),
                                                      attribute.present->size(),
                                                      sizeof(std::uint8_t), "B")
                                                : nb::none();
    nb::dict metadata;
    for (const auto& item : attribute.metadata)
      metadata[PythonText(item.key)] = PythonText(item.value);
    out[wire_key::kMetadata] = metadata;
    attributes.append(out);
  }
  record[wire_key::kAttributes] = attributes;
  return nb::module_::import_("meshvale_geometry")
      .attr("Mesh")
      .attr("from_record")(record);
}
nb::object ExportJson(nb::handle value) {
  return nb::module_::import_("meshvale_interchange.stl_files")
      .attr("_export_json")(value);
}
StlPythonJob& Job(const StlPythonResult& result) {
  return result.app->jobs[result.index];
}
int JobExitCode(const StlPythonResult& result) {
  RethrowUnexpected(result.app->unexpected);
  if (result.app->mode != kBatch) return ExitCode(*result.app);
  if (!result.app->admission.code.empty()) return kExitAdmission;
  if (result.app->interrupted ||
      JobOutcome(Job(result)) == app_value::kCanceled)
    return kExitCanceled;
  return JobOutcome(Job(result)) == app_value::kFailed ? kExitJobFailure
                                                       : kExitSuccess;
}
}  // namespace
void RegisterStlFiles(nb::module_& module, StlCancellationReader reader) {
  g_cancellation_reader = reader;
  nb::class_<StlPythonApplicationOwner>(module, kApplicationOwnerName,
                                        GcSlots<StlPythonApplicationOwner>());
  nb::class_<StlPythonExecution>(module, "StlExecutionContext")
      .def(nb::init<nb::handle, nb::handle, nb::handle>(),
           nb::arg(wire_key::kWorkerBudget),
           nb::arg(wire_key::kMinimumParallelVertices),
           nb::arg(wire_key::kTrackedPayloadBudgetBytes))
      .def_prop_ro(wire_key::kWorkerBudget,
                   [](const StlPythonExecution& self) {
                     return self.context.WorkerBudget();
                   })
      .def_prop_ro("active_workers",
                   [](const StlPythonExecution& self) {
                     return self.context.ActiveWorkers();
                   })
      .def_prop_ro("peak_workers",
                   [](const StlPythonExecution& self) {
                     return self.context.PeakWorkers();
                   })
      .def_prop_ro(wire_key::kTrackedPayloadBudgetBytes,
                   [](const StlPythonExecution& self) {
                     return self.context.TrackedPayloadBudget();
                   })
      .def_prop_ro("active_tracked_payload_bytes",
                   [](const StlPythonExecution& self) {
                     return self.context.ActiveTrackedPayload();
                   })
      .def_prop_ro("peak_tracked_payload_bytes",
                   [](const StlPythonExecution& self) {
                     return self.context.PeakTrackedPayload();
                   });
  nb::class_<StlPythonCaptured>(module, "StlCapturedFile",
                                GcSlots<StlPythonCaptured>())
      .def_prop_ro("entry",
                   [](const StlPythonCaptured& self) {
                     return PythonText(RequireOwner(self).EntryPath());
                   })
      .def_prop_ro("byte_count",
                   [](const StlPythonCaptured& self) {
                     return RequireOwner(self).EntryBytes().size();
                   })
      .def_prop_ro(wire_key::kSha256,
                   [](const StlPythonCaptured& self) {
                     return Hex(RequireOwner(self).Sha256());
                   })
      .def_prop_ro("new_requested_bytes",
                   [](const StlPythonCaptured& self) {
                     return RequireOwner(self).NewRequestedBytes();
                   })
      .def("export_bytes", [](const StlPythonCaptured& self) {
        return Binary(RequireOwner(self).EntryBytes());
      });
  nb::class_<StlPythonDocument>(module, "StlDocument",
                                GcSlots<StlPythonDocument>())
      .def_prop_ro("encoding",
                   [](const StlPythonDocument& self) {
                     return Wire(RequireOwner(self).Encoding());
                   })
      .def_prop_ro(wire_key::kAsciiName,
                   [](const StlPythonDocument& self) {
                     const auto& d = RequireOwner(self);
                     return d.Encoding() == io::StlEncoding::kAscii
                                ? PythonText(d.AsciiName())
                                : nb::none();
                   })
      .def_prop_ro(wire_key::kBinaryHeader,
                   [](const StlPythonDocument& self) {
                     const auto& d = RequireOwner(self);
                     return d.Encoding() == io::StlEncoding::kBinary
                                ? Binary(d.BinaryHeader())
                                : nb::none();
                   })
      .def_prop_ro(
          "facet_count",
          [](const StlPythonDocument& self) {
            return RequireOwner(self).Mesh().View().face_offsets.size() - 1;
          })
      .def_prop_ro("vertex_count",
                   [](const StlPythonDocument& self) {
                     return RequireOwner(self).Mesh().View().positions.size() /
                            kPositionComponents;
                   })
      .def_prop_ro(
          "corner_count",
          [](const StlPythonDocument& self) {
            return RequireOwner(self).Mesh().View().corner_vertices.size();
          })
      .def_prop_ro("new_requested_bytes",
                   [](const StlPythonDocument& self) {
                     return Wire(RequireOwner(self).NewRequestedBytes());
                   })
      .def("export_bytes",
           [](const StlPythonDocument& self) {
             return Binary(RequireOwner(self).OriginalBytes());
           })
      .def("export_mesh", ExportMesh);
  nb::class_<StlPythonAsset>(module, "StlFileAsset", GcSlots<StlPythonAsset>())
      .def_prop_ro("document",
                   [](const StlPythonAsset& self) {
                     (void)RequireOwner(self);
                     return self.document;
                   })
      .def_prop_ro("entry",
                   [](const StlPythonAsset& self) {
                     return PythonText(RequireOwner(self).EntryPath());
                   })
      .def_prop_ro("byte_count",
                   [](const StlPythonAsset& self) {
                     return RequireOwner(self).EntryBytes().size();
                   })
      .def_prop_ro(wire_key::kSha256,
                   [](const StlPythonAsset& self) {
                     return Hex(RequireOwner(self).Sha256());
                   })
      .def_prop_ro("new_requested_bytes",
                   [](const StlPythonAsset& self) {
                     return RequireOwner(self).NewRequestedBytes();
                   })
      .def("export_bytes", [](const StlPythonAsset& self) {
        return Binary(RequireOwner(self).EntryBytes());
      });
  nb::class_<StlPythonOutput>(module, "StlOutput", GcSlots<StlPythonOutput>())
      .def_prop_ro("encoding",
                   [](const StlPythonOutput& self) {
                     return Wire(RequireOwner(self).Facts().encoding);
                   })
      .def_prop_ro("byte_count",
                   [](const StlPythonOutput& self) {
                     return RequireOwner(self).Bytes().size();
                   })
      .def_prop_ro(wire_key::kSha256,
                   [](const StlPythonOutput& self) {
                     return OutputHash(RequireOwner(self));
                   })
      .def_prop_ro(wire_key::kFacts,
                   [](const StlPythonOutput& self) {
                     const auto& n = RequireOwner(self);
                     auto out = nb::cast<nb::dict>(Wire(n.Facts()));
                     out[wire_key::kBytes] = n.Bytes().size();
                     out[wire_key::kSha256] = OutputHash(n);
                     return out;
                   })
      .def_prop_ro("new_requested_bytes",
                   [](const StlPythonOutput& self) {
                     return Wire(RequireOwner(self).NewRequestedBytes());
                   })
      .def("export_bytes",
           [](const StlPythonOutput& self) {
             return Binary(RequireOwner(self).Bytes());
           })
      .def("export_corner_source_vertices", [](const StlPythonOutput& self) {
        const auto values = RequireOwner(self).CornerSourceVertices();
        auto out = nb::steal<nb::tuple>(
            PyTuple_New(static_cast<Py_ssize_t>(values.size())));
        if (!out.is_valid()) throw nb::python_error();
        for (std::size_t i = 0; i < values.size(); ++i) {
          auto* n = PyLong_FromUnsignedLongLong(values[i]);
          if (!n) throw nb::python_error();
          PyTuple_SET_ITEM(out.ptr(), static_cast<Py_ssize_t>(i), n);
        }
        return out;
      });
  nb::class_<StlPythonVerified>(module, "StlVerifiedRewrite",
                                GcSlots<StlPythonVerified>())
      .def_prop_ro("source",
                   [](const StlPythonVerified& self) {
                     (void)RequireOwner(self);
                     return self.source;
                   })
      .def_prop_ro(wire_key::kOutput,
                   [](const StlPythonVerified& self) {
                     (void)RequireOwner(self);
                     return self.output;
                   })
      .def_prop_ro("document",
                   [](const StlPythonVerified& self) {
                     (void)RequireOwner(self);
                     return self.document;
                   })
      .def_prop_ro("entry",
                   [](const StlPythonVerified& self) {
                     return PythonText(RequireOwner(self).EntryPath());
                   })
      .def_prop_ro("byte_count",
                   [](const StlPythonVerified& self) {
                     return RequireOwner(self).EntryBytes().size();
                   })
      .def_prop_ro(wire_key::kSha256,
                   [](const StlPythonVerified& self) {
                     return Hex(RequireOwner(self).Sha256());
                   })
      .def_prop_ro("new_requested_bytes",
                   [](const StlPythonVerified& self) {
                     return RequireOwner(self).NewRequestedBytes();
                   })
      .def("export_bytes", [](const StlPythonVerified& self) {
        return Binary(RequireOwner(self).EntryBytes());
      });
  nb::class_<StlPythonBundle>(module, "StlPublishedBundle",
                              GcSlots<StlPythonBundle>())
      .def_prop_ro(wire_key::kVerified,
                   [](const StlPythonBundle& self) {
                     (void)RequireOwner(self);
                     return self.verified;
                   })
      .def_prop_ro("receipt_entry",
                   [](const StlPythonBundle& self) {
                     return PythonText(RequireOwner(self).ReceiptPath());
                   })
      .def_prop_ro("receipt_byte_count",
                   [](const StlPythonBundle& self) {
                     return RequireOwner(self).ReceiptBytes().size();
                   })
      .def_prop_ro("receipt_sha256",
                   [](const StlPythonBundle& self) {
                     return Hex(RequireOwner(self).ReceiptSha256());
                   })
      .def_prop_ro("new_requested_bytes",
                   [](const StlPythonBundle& self) {
                     return RequireOwner(self).NewRequestedBytes();
                   })
      .def("export_receipt_bytes", [](const StlPythonBundle& self) {
        return Binary(RequireOwner(self).ReceiptBytes());
      });
  auto result = nb::class_<StlPythonResult>(module, "StlResult",
                                            GcSlots<StlPythonResult>());
  result.def_prop_ro(wire_key::kJobId, [](const StlPythonResult& self) {
    return PythonText(Job(self).job_id);
  });
  result.def_prop_ro(wire_key::kMode, [](const StlPythonResult& self) {
    return PythonText(Job(self).rewrite ? kRewrite : kRead);
  });
  result.def_prop_ro(wire_key::kDeclarations, [](const StlPythonResult& self) {
    return Declarations(Job(self));
  });
  result.def_prop_ro(wire_key::kOutcome, [](const StlPythonResult& self) {
    return PythonText(JobOutcome(Job(self)));
  });
  result.def_prop_ro(wire_key::kPhase, [](const StlPythonResult& self) {
    return Wire(NativeResult(Job(self)).phase);
  });
  result.def_prop_ro(wire_key::kDiagnostic, [](const StlPythonResult& self) {
    const auto& j = Job(self);
    const auto& r = NativeResult(j);
    return (j.read || j.written) && r.status != io::StlFileStatus::kSuccess
               ? Wire(r.diagnostic)
               : nb::none();
  });
  result.def_prop_ro(wire_key::kObservations, [](const StlPythonResult& self) {
    return Wire(NativeResult(Job(self)).observations);
  });
  result.def_prop_ro(wire_key::kExecution, [](const StlPythonResult& self) {
    return Wire(NativeResult(Job(self)).execution);
  });
  result.def_prop_ro(wire_key::kRequestedBytes,
                     [](const StlPythonResult& self) {
                       return Wire(Requested(*self.app, Job(self)));
                     });
  result.def_prop_ro(wire_key::kCleanup, [](const StlPythonResult& self) {
    return Wire(NativeResult(Job(self)).cleanup);
  });
  result.def_prop_ro(wire_key::kCleanupError, [](const StlPythonResult& self) {
    return Wire(NativeResult(Job(self)).cleanup_error);
  });
  result.def_prop_ro(wire_key::kNotAttemptedReason,
                     [](const StlPythonResult& self) {
                       const auto reason = Job(self).skipped;
                       return reason.empty() ? nb::none() : PythonText(reason);
                     });
  result.def_prop_ro(wire_key::kCaptured, [](const StlPythonResult& self) {
    const auto& o = Job(self).owners;
    return o.captured_ptr->native ? o.captured : nb::none();
  });
  result.def_prop_ro(wire_key::kAsset, [](const StlPythonResult& self) {
    const auto& o = Job(self).owners;
    return o.asset_ptr->native ? o.asset : nb::none();
  });
  result.def_prop_ro(wire_key::kOutput, [](const StlPythonResult& self) {
    const auto& o = Job(self).owners;
    return o.output_ptr->native ? o.output : nb::none();
  });
  result.def_prop_ro(wire_key::kFreshDocument, [](const StlPythonResult& self) {
    const auto& o = Job(self).owners;
    return o.fresh_document_ptr->native ? o.fresh_document : nb::none();
  });
  result.def_prop_ro(wire_key::kVerified, [](const StlPythonResult& self) {
    const auto& o = Job(self).owners;
    return o.verified_ptr->native ? o.verified : nb::none();
  });
  result.def_prop_ro(wire_key::kBundle, [](const StlPythonResult& self) {
    const auto& o = Job(self).owners;
    return o.bundle_ptr->native ? o.bundle : nb::none();
  });
  result.def_prop_ro(wire_key::kDelivery, [](const StlPythonResult& self) {
    return Delivery(self.app->mode == kBatch ? StlPythonDelivery{}
                                             : self.app->delivery);
  });
  result.def_prop_ro(wire_key::kExitCode, JobExitCode);
  result.def("export_json", [](nb::handle self) { return ExportJson(self); });
  result.def("_wire",
             [](const StlPythonResult& self) { return Frame(*self.app); });
  nb::class_<StlPythonBatchResult>(module, "StlBatchResult",
                                   GcSlots<StlPythonBatchResult>())
      .def_prop_ro(wire_key::kJobs,
                   [](const StlPythonBatchResult& self) {
                     (void)*self.app;
                     return self.jobs;
                   })
      .def_prop_ro(wire_key::kOutcome,
                   [](const StlPythonBatchResult& self) {
                     return PythonText(BatchOutcome(*self.app));
                   })
      .def_prop_ro(
          wire_key::kAdmissionDiagnostic,
          [](const StlPythonBatchResult& self) { return Admission(*self.app); })
      .def_prop_ro(wire_key::kDelivery,
                   [](const StlPythonBatchResult& self) {
                     return Delivery(self.app->delivery);
                   })
      .def_prop_ro(
          wire_key::kExitCode,
          [](const StlPythonBatchResult& self) { return ExitCode(*self.app); })
      .def("export_json", [](nb::handle self) { return ExportJson(self); })
      .def("_wire",
           [](const StlPythonBatchResult& self) { return Frame(*self.app); });
  nb::class_<StlPythonPreparedSingle>(module, "PreparedStl",
                                      GcSlots<StlPythonPreparedSingle>())
      .def_prop_ro("result", [](const StlPythonPreparedSingle& self) {
        (void)*self.app;
        return self.result;
      });
  nb::class_<StlPythonPreparedBatch>(module, "PreparedStlBatch",
                                     GcSlots<StlPythonPreparedBatch>())
      .def_prop_ro("result", [](const StlPythonPreparedBatch& self) {
        (void)*self.app;
        return self.result;
      });
  module.def("_stl_create", [](nb::handle config, nb::handle execution,
                               nb::handle mode, nb::handle policy) {
    try {
      return Create(config, execution, mode, policy);
    } catch (const alloc::AdmissionFailure&) {
      throw std::bad_alloc();
    }
  });
  module.def("_stl_add", [](nb::handle prepared, nb::handle value) {
    try {
      Add(prepared, value);
    } catch (const alloc::AdmissionFailure&) {
      throw std::bad_alloc();
    }
  });
  module.def("_stl_finish", [](nb::handle prepared, nb::handle cwd) {
    try {
      Finish(prepared, cwd);
    } catch (const alloc::AdmissionFailure&) {
      Reject(*Prepared(prepared).app, admission_code::kRootBudget);
    }
  });
  module.def(
      "_stl_manifest",
      [](nb::handle prepared, nb::handle path, nb::handle cwd) -> nb::object {
        try {
          return Manifest(prepared, path, cwd);
        } catch (const alloc::AdmissionFailure&) {
          Reject(*Prepared(prepared).app, admission_code::kRootBudget);
          return nb::none();
        }
      });
  module.def("_stl_reject", RejectPython, nb::arg(), nb::arg(),
             nb::arg().none(), nb::arg().none(), nb::arg().none());
  module.def("_stl_run", Run, nb::arg(), nb::arg().none());
  module.def("_stl_interrupt", Interrupted);
  module.def("_stl_frame_begin", FrameBegin);
  module.def("_stl_frame_chunk", FrameChunk);
  module.def("_stl_frame_finish", FrameFinish);
  module.def("_stl_delivery", SetDelivery, nb::arg(), nb::arg(),
             nb::arg().none(), nb::arg());
  module.def("_stl_delivery_limit", [](nb::handle value) {
    return Application(value).preset.application.max_delivery_bytes;
  });
}
}  // namespace meshvale::interchange::stl_python_detail
