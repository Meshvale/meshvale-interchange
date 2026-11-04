// SPDX-License-Identifier: Apache-2.0
#include "surface_python_options.h"

#include <Python.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

#include "../src/gltf_json_snapshot.h"

namespace meshvale::interchange::surface_python_detail {
namespace nb = nanobind;
namespace geo = geometry;
namespace alloc = gltf_detail;
namespace {
constexpr char kAcknowledgeCornerExpansion[] = "acknowledge_corner_expansion";
constexpr char kAcknowledgeOrdinaryViewLoss[] =
    "acknowledge_ordinary_view_loss";
constexpr char kAcknowledgeTriangleConversion[] =
    "acknowledge_triangle_conversion";
constexpr char kAcknowledgeUnusedVertices[] = "acknowledge_unused_vertices";
constexpr char kAsciiName[] = "ascii_name";
constexpr char kBinaryHeader[] = "binary_header";
constexpr char kCancellationBlockBytes[] = "cancellation_block_bytes";
constexpr char kCancellationBlockFacets[] = "cancellation_block_facets";
constexpr char kCancellationBlockItems[] = "cancellation_block_items";
constexpr char kCancellationBlockKeys[] = "cancellation_block_keys";
constexpr char kCancellationBlockScalars[] = "cancellation_block_scalars";
constexpr char kCancellationBlockTokens[] = "cancellation_block_tokens";
constexpr char kCatalogMaterial[] = "catalog_material";
constexpr char kCatalogReadLimits[] = "catalog_read_limits";
constexpr char kCatalogReadWork[] = "catalog_read_work";
constexpr char kCatalogSet[] = "catalog_set";
constexpr char kCatalogUv[] = "catalog_uv";
constexpr char kChannelPartitionElements[] = "channel_partition_elements";
constexpr char kChannels[] = "channels";
constexpr char kCodec[] = "codec";
constexpr char kContainer[] = "container";
constexpr char kCoordinates[] = "coordinates";
constexpr char kCornerNormalAttribute[] = "corner_normal_attribute";
constexpr char kCounts[] = "counts";
constexpr char kDeclaredUnit[] = "declared_unit";
constexpr char kDelegates[] = "delegates";
constexpr char kDerivedMesh[] = "derived_mesh";
constexpr char kDirectTranslation[] = "direct_translation";
constexpr char kEncodeLimits[] = "encode_limits";
constexpr char kEncodeWork[] = "encode_work";
constexpr char kEncoding[] = "encoding";
constexpr char kExecution[] = "execution";
constexpr char kExtraction[] = "extraction";
constexpr char kFiles[] = "files";
constexpr char kFormats[] = "formats";
constexpr char kGeometry[] = "geometry";
constexpr char kGlb[] = "glb";
constexpr char kGltf[] = "gltf";
constexpr char kGltfJson[] = "gltf-json";
constexpr char kIngressLimits[] = "ingress_limits";
constexpr char kIngressWork[] = "ingress_work";
constexpr char kInspectLimits[] = "inspect_limits";
constexpr char kInspectWork[] = "inspect_work";
constexpr char kInspection[] = "inspection";
constexpr char kIoBlockBytes[] = "io_block_bytes";
constexpr char kKind[] = "kind";
constexpr char kMaterialization[] = "materialization";
constexpr char kMaterials[] = "materials";
constexpr char kMaxAccessorElements[] = "max_accessor_elements";
constexpr char kMaxAttributeBytes[] = "max_attribute_bytes";
constexpr char kMaxAttributeOffsets[] = "max_attribute_offsets";
constexpr char kMaxAttributeScalars[] = "max_attribute_scalars";
constexpr char kMaxAttributes[] = "max_attributes";
constexpr char kMaxBinaryBytes[] = "max_binary_bytes";
constexpr char kMaxBridgeNewRequestedBytes[] = "max_bridge_new_requested_bytes";
constexpr char kMaxBufferBytes[] = "max_buffer_bytes";
constexpr char kMaxBufferViews[] = "max_buffer_views";
constexpr char kMaxCallerReportBytes[] = "max_caller_report_bytes";
constexpr char kMaxCapsuleBinaryBytes[] = "max_capsule_binary_bytes";
constexpr char kMaxCapsuleJsonBytes[] = "max_capsule_json_bytes";
constexpr char kMaxCatalogResourceBindings[] = "max_catalog_resource_bindings";
constexpr char kMaxChannelReferences[] = "max_channel_references";
constexpr char kMaxChunks[] = "max_chunks";
constexpr char kMaxCodecNewRequestedBytes[] = "max_codec_new_requested_bytes";
constexpr char kMaxCoincidenceComparisons[] = "max_coincidence_comparisons";
constexpr char kMaxComponents[] = "max_components";
constexpr char kMaxContainerBytes[] = "max_container_bytes";
constexpr char kMaxCoreNewRequestedBytes[] = "max_core_new_requested_bytes";
constexpr char kMaxCorners[] = "max_corners";
constexpr char kMaxCornersPerFace[] = "max_corners_per_face";
constexpr char kMaxDecimalCoefficientDigits[] =
    "max_decimal_coefficient_digits";
constexpr char kMaxDecimalExponentDigits[] = "max_decimal_exponent_digits";
constexpr char kMaxDecodedBytes[] = "max_decoded_bytes";
constexpr char kMaxDecodedStringBytes[] = "max_decoded_string_bytes";
constexpr char kMaxDescriptorBytes[] = "max_descriptor_bytes";
constexpr char kMaxDestinationNativeCodeUnits[] =
    "max_destination_native_code_units";
constexpr char kMaxDistinctSamplers[] = "max_distinct_samplers";
constexpr char kMaxDocumentDepth[] = "max_document_depth";
constexpr char kMaxDocumentNodes[] = "max_document_nodes";
constexpr char kMaxFaceOffsets[] = "max_face_offsets";
constexpr char kMaxFaces[] = "max_faces";
constexpr char kMaxFacets[] = "max_facets";
constexpr char kMaxGraphEdges[] = "max_graph_edges";
constexpr char kMaxInstances[] = "max_instances";
constexpr char kMaxInventoryEntries[] = "max_inventory_entries";
constexpr char kMaxIssues[] = "max_issues";
constexpr char kMaxJobs[] = "max_jobs";
constexpr char kMaxJsonBytes[] = "max_json_bytes";
constexpr char kMaxJsonDepth[] = "max_json_depth";
constexpr char kMaxJsonNodes[] = "max_json_nodes";
constexpr char kMaxKeysPerSampler[] = "max_keys_per_sampler";
constexpr char kMaxLineBytes[] = "max_line_bytes";
constexpr char kMaxMapEntries[] = "max_map_entries";
constexpr char kMaxMaterialBindings[] = "max_material_bindings";
constexpr char kMaxMemberBytes[] = "max_member_bytes";
constexpr char kMaxMembers[] = "max_members";
constexpr char kMaxMeshUsers[] = "max_mesh_users";
constexpr char kMaxMetadataBytes[] = "max_metadata_bytes";
constexpr char kMaxMetadataEntries[] = "max_metadata_entries";
constexpr char kMaxNameBytes[] = "max_name_bytes";
constexpr char kMaxNativePathCodeUnits[] = "max_native_path_code_units";
constexpr char kMaxNewRequestedBytes[] = "max_new_requested_bytes";
constexpr char kMaxNodeRows[] = "max_node_rows";
constexpr char kMaxNumericIntegerBits[] = "max_numeric_integer_bits";
constexpr char kMaxNumericTokenBytes[] = "max_numeric_token_bytes";
constexpr char kMaxOperationRequestedAllowance[] =
    "max_operation_requested_allowance";
constexpr char kMaxOptionBytes[] = "max_option_bytes";
constexpr char kMaxOptionRows[] = "max_option_rows";
constexpr char kMaxOutputBytes[] = "max_output_bytes";
constexpr char kMaxOutputRequestedBytes[] = "max_output_requested_bytes";
constexpr char kMaxOwnedSourceResourceBytes[] =
    "max_owned_source_resource_bytes";
constexpr char kMaxPathBytes[] = "max_path_bytes";
constexpr char kMaxPayloadMemberBytes[] = "max_payload_member_bytes";
constexpr char kMaxPayloadMembers[] = "max_payload_members";
constexpr char kMaxPositionLinfError[] = "max_position_linf_error";
constexpr char kMaxPresenceBytes[] = "max_presence_bytes";
constexpr char kMaxProjectionBytes[] = "max_projection_bytes";
constexpr char kMaxReceiptBytes[] = "max_receipt_bytes";
constexpr char kMaxRelationshipSteps[] = "max_relationship_steps";
constexpr char kMaxRequestedBytes[] = "max_requested_bytes";
constexpr char kMaxResourceBytes[] = "max_resource_bytes";
constexpr char kMaxResources[] = "max_resources";
constexpr char kMaxSourceBytes[] = "max_source_bytes";
constexpr char kMaxStageNameAttempts[] = "max_stage_name_attempts";
constexpr char kMaxTableRows[] = "max_table_rows";
constexpr char kMaxTokenBytes[] = "max_token_bytes";
constexpr char kMaxTotalBufferBytes[] = "max_total_buffer_bytes";
constexpr char kMaxTotalKeys[] = "max_total_keys";
constexpr char kMaxTotalMemberBytes[] = "max_total_member_bytes";
constexpr char kMaxTotalNewRequestedBytes[] = "max_total_new_requested_bytes";
constexpr char kMaxTotalPathBytes[] = "max_total_path_bytes";
constexpr char kMaxTotalPayloadBytes[] = "max_total_payload_bytes";
constexpr char kMaxTotalRequestedAllowance[] = "max_total_requested_allowance";
constexpr char kMaxTriangleCorners[] = "max_triangle_corners";
constexpr char kMaxUvComponentErrors[] = "max_uv_component_errors";
constexpr char kMaxVertices[] = "max_vertices";
constexpr char kMaxWireCorners[] = "max_wire_corners";
constexpr char kMaxWirePrimitives[] = "max_wire_primitives";
constexpr char kMaxWireVertices[] = "max_wire_vertices";
constexpr char kMeshWork[] = "mesh_work";
constexpr char kMinimumParallelFaces[] = "minimum_parallel_faces";
constexpr char kMinimumParallelVertices[] = "minimum_parallel_vertices";
constexpr char kOutput[] = "output";
constexpr char kOutputInspection[] = "output_inspection";
constexpr char kParallelMinBytes[] = "parallel_min_bytes";
constexpr char kParallelMinChannelComponents[] =
    "parallel_min_channel_components";
constexpr char kParallelMinKeys[] = "parallel_min_keys";
constexpr char kParallelMinResourceBytes[] = "parallel_min_resource_bytes";
constexpr char kPartitionBytes[] = "partition_bytes";
constexpr char kPartitionKeys[] = "partition_keys";
constexpr char kPositions[] = "positions";
constexpr char kPublicationLimits[] = "publication_limits";
constexpr char kPublicationWork[] = "publication_work";
constexpr char kRead[] = "read";
constexpr char kRepresentation[] = "representation";
constexpr char kResourcePartitionBytes[] = "resource_partition_bytes";
constexpr char kRole[] = "role";
constexpr char kSource[] = "source";
constexpr char kSourceAttribute[] = "source_attribute";
constexpr char kSourceId[] = "source_id";
constexpr char kSourceInspection[] = "source_inspection";
constexpr char kSourceMaterialization[] = "source_materialization";
constexpr char kSourceSet[] = "source_set";
constexpr char kStl[] = "stl";
constexpr char kStlAscii[] = "stl-ascii";
constexpr char kStlBinary[] = "stl-binary";
constexpr char kSurface[] = "surface";
constexpr char kTrackedPayloadBudgetBytes[] = "tracked_payload_budget_bytes";
constexpr char kTriangulationLimits[] = "triangulation_limits";
constexpr char kTriangulationOptions[] = "triangulation_options";
constexpr char kUv[] = "uv";
constexpr char kWireSet[] = "wire_set";
constexpr char kWorkerBudget[] = "worker_budget";

constexpr char kStrictBareName[] = "strict-bare";
constexpr char kAnnotatedBundleName[] = "annotated-bundle-v1";
constexpr char kBundleOnlyName[] = "bundle-only";
constexpr char kUvName[] = "uv";
constexpr char kStlNormalName[] = "stl-corner-normal";
constexpr char kMaterialIdName[] = "material-id";
constexpr char kExact32Name[] = "exact32";
constexpr char kBounded32Name[] = "bounded-rne32";
constexpr char kExact64Name[] = "exact64";
constexpr char kMetersName[] = "declared-meters-right-handed-y-up";
constexpr char kStlUnitsName[] = "stl-units-unspecified";
constexpr char kGltfDelegateName[] = "gltf";
constexpr char kStlDelegateName[] = "stl";
// STL's fixed binary header and printable ASCII codec domain.
constexpr std::size_t kBinaryHeaderBytes = 80;
constexpr Py_UCS4 kPrintableAsciiFirst = 32, kPrintableAsciiLast = 126;
constexpr Py_UCS4 kUnicodeError = static_cast<Py_UCS4>(-1);
constexpr Py_UCS4 kSurrogateFirst = 0xd800, kSurrogateLast = 0xdfff;
constexpr Py_UCS4 kUtf8OneByteMax = 0x7f, kUtf8TwoByteMax = 0x7ff,
                  kUtf8ThreeByteMax = 0xffff, kUnicodeMax = 0x10ffff;
constexpr unsigned kUtf8ContinuationBits = 6, kUtf8ContinuationMask = 0x3f,
                   kUtf8ContinuationPrefix = 0x80, kUtf8TwoPrefix = 0xc0,
                   kUtf8ThreePrefix = 0xe0, kUtf8FourPrefix = 0xf0;
constexpr std::uint64_t kTriangulationMinimumCorners = 3;
// Matches the current native single-loop triangulation profile.
constexpr std::uint64_t kTriangulationMaximumCorners = 4096;
// Worst-case JSON encoding of one byte as a six-byte Unicode escape.
constexpr std::uint64_t kMaximumJsonEscapeBytes = 6;
constexpr std::array<SurfaceEncoding, kSurfacePythonFormatCount> kEncodings{
    SurfaceEncoding::kGltfJson, SurfaceEncoding::kGlb,
    SurfaceEncoding::kStlAscii, SurfaceEncoding::kStlBinary};

void Valid(bool value, const char* message) {
  if (!value) throw nb::value_error(message);
}
std::size_t Host(std::uint64_t value) {
  if (value > std::numeric_limits<std::size_t>::max() ||
      value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::ptrdiff_t>::max()) ||
      value > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
    throw std::overflow_error("surface configuration exceeds host extent");
  return static_cast<std::size_t>(value);
}
std::uint64_t Sum(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    throw std::overflow_error("surface configuration sum overflow");
  const auto result = a + b;
  (void)Host(result);
  return result;
}
std::uint64_t Product(std::uint64_t count, std::size_t width) {
  if (count > std::numeric_limits<std::uint64_t>::max() / width)
    throw std::overflow_error("surface configuration product overflow");
  const auto result = count * width;
  (void)Host(result);
  return result;
}
bool Equal(nb::handle value, const char* text) {
  if (!PyUnicode_CheckExact(value.ptr()))
    throw nb::type_error("surface selectors require built-in strings");
  const auto result = PyUnicode_CompareWithASCIIString(value.ptr(), text);
  if (result == -1 && PyErr_Occurred()) throw nb::python_error();
  return result == 0;
}
std::uint64_t Unsigned(nb::handle value) {
  if (!PyLong_CheckExact(value.ptr()))
    throw nb::type_error("surface integers require built-in int, not bool");
  const auto result = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  return result;
}
std::uint64_t Extent(nb::handle value) {
  const auto result = Unsigned(value);
  (void)Host(result);
  return result;
}
std::uint32_t Uint32(nb::handle value) {
  const auto result = Unsigned(value);
  if (result > std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("surface integer exceeds uint32");
  return static_cast<std::uint32_t>(result);
}
std::int32_t Int32(nb::handle value) {
  if (!PyLong_CheckExact(value.ptr()))
    throw nb::type_error("surface integers require built-in int, not bool");
  const auto result = PyLong_AsLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  if (result < std::numeric_limits<std::int32_t>::min() ||
      result > std::numeric_limits<std::int32_t>::max())
    throw std::overflow_error("surface integer exceeds int32");
  return static_cast<std::int32_t>(result);
}
bool Boolean(nb::handle value) {
  if (!PyBool_Check(value.ptr()))
    throw nb::type_error("surface flag requires bool");
  return value.ptr() == Py_True;
}
double Bound(nb::handle value) {
  if (!PyFloat_CheckExact(value.ptr()))
    throw nb::type_error("surface bounds require built-in float");
  const double result = PyFloat_AS_DOUBLE(value.ptr());
  Valid(std::isfinite(result) && result >= 0,
        "surface bound must be finite and nonnegative");
  return result;
}
// No branch reconstruction: inspect a bounded number of existing key objects,
// then acquire only each required existing value. Python iterator/key objects
// are interpreter overhead; no native vector/string or mapping copy is made.
class Mapping {
 public:
  Mapping(nb::handle value, std::initializer_list<const char*> keys)
      : value_(nb::borrow<nb::object>(value)) {
    if (!PyDict_CheckExact(value.ptr()) &&
        !Py_IS_TYPE(value.ptr(), &PyDictProxy_Type))
      throw nb::type_error("preset branches require dict or MappingProxyType");
    const auto size = PyObject_Size(value.ptr());
    if (size < 0) throw nb::python_error();
    Valid(static_cast<std::size_t>(size) == keys.size(),
          "preset branch has missing or extra fields");
    auto iterator = nb::steal<nb::object>(PyObject_GetIter(value.ptr()));
    if (!iterator.is_valid()) throw nb::python_error();
    for (std::size_t i = 0; i < keys.size(); ++i) {
      auto key = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
      if (!key.is_valid()) {
        if (PyErr_Occurred()) throw nb::python_error();
        throw nb::value_error("preset key inventory changed");
      }
      bool found = false;
      for (const auto* expected : keys) found = found || Equal(key, expected);
      Valid(found, "preset branch has unknown fields");
    }
    auto extra = nb::steal<nb::object>(PyIter_Next(iterator.ptr()));
    if (PyErr_Occurred()) throw nb::python_error();
    Valid(!extra.is_valid(), "preset key inventory changed");
  }
  nb::object Get(const char* key) const {
    auto result = nb::steal<nb::object>(
        PyObject_GetItem(value_.ptr(), nb::str(key).ptr()));
    if (!result.is_valid()) throw nb::python_error();
    return result;
  }
  std::uint64_t Number(const char* key, bool active = true) const {
    const auto result = Extent(Get(key));
    Valid(active ? result != 0 : result == 0,
          "preset field requires positive active value or canonical inactive "
          "zero");
    return result;
  }

 private:
  nb::object value_;
};

geo::MeshMaterializationLimits ReadMaterialization(nb::handle value,
                                                   bool active = true) {
  const Mapping map(
      value,
      {kMaxVertices, kMaxFaces, kMaxCorners, kMaxFaceOffsets, kMaxAttributes,
       kMaxAttributeScalars, kMaxAttributeOffsets, kMaxPresenceBytes,
       kMaxMetadataEntries, kMaxDescriptorBytes, kMaxNewRequestedBytes});
  geo::MeshMaterializationLimits out;
  out.max_vertices = map.Number(kMaxVertices, active);
  out.max_faces = map.Number(kMaxFaces, active);
  out.max_corners = map.Number(kMaxCorners, active);
  out.max_face_offsets = map.Number(kMaxFaceOffsets, active);
  out.max_attributes = map.Number(kMaxAttributes, active);
  out.max_attribute_scalars = map.Number(kMaxAttributeScalars, active);
  out.max_attribute_offsets = map.Number(kMaxAttributeOffsets, active);
  out.max_presence_bytes = map.Number(kMaxPresenceBytes, active);
  out.max_metadata_entries = map.Number(kMaxMetadataEntries, active);
  out.max_descriptor_bytes = map.Number(kMaxDescriptorBytes, active);
  out.max_new_requested_bytes = map.Number(kMaxNewRequestedBytes, active);
  return out;
}

geo::MeshInspectionLimits ReadInspection(nb::handle value, bool active = true) {
  const Mapping map(value, {kMaxIssues, kMaxNewRequestedBytes});
  geo::MeshInspectionLimits out;
  out.max_issues = map.Number(kMaxIssues, active);
  out.max_new_requested_bytes = map.Number(kMaxNewRequestedBytes, active);
  return out;
}

geo::MeshMaterializationWorkPolicy ReadMeshWork(nb::handle value,
                                                bool active = true) {
  const Mapping map(value, {kParallelMinBytes, kPartitionBytes,
                            kCancellationBlockScalars, kMaxJobs});
  geo::MeshMaterializationWorkPolicy out;
  out.parallel_min_bytes = map.Number(kParallelMinBytes, active);
  out.partition_bytes = map.Number(kPartitionBytes, active);
  out.cancellation_block_scalars =
      map.Number(kCancellationBlockScalars, active);
  out.max_jobs = map.Number(kMaxJobs, active);
  return out;
}

GlbLimits ReadGlbLimits(nb::handle value, bool active = true) {
  const Mapping map(value,
                    {kMaxContainerBytes, kMaxChunks, kMaxRequestedBytes});
  GlbLimits out;
  out.max_container_bytes = map.Number(kMaxContainerBytes, active);
  out.max_chunks = map.Number(kMaxChunks, active);
  out.max_requested_bytes = map.Number(kMaxRequestedBytes, active);
  return out;
}

GlbWorkPolicy ReadGlbWork(nb::handle value, bool active = true) {
  const Mapping map(value, {kParallelMinBytes, kPartitionBytes});
  GlbWorkPolicy out;
  out.parallel_min_bytes = map.Number(kParallelMinBytes, active);
  out.partition_bytes = map.Number(kPartitionBytes, active);
  return out;
}

GltfDirectTranslationLimits ReadDirectLimits(nb::handle value,
                                             bool active = true) {
  const Mapping map(
      value,
      {kMaxNodeRows, kMaxMeshUsers, kMaxChannelReferences, kMaxDistinctSamplers,
       kMaxKeysPerSampler, kMaxTotalKeys, kMaxRelationshipSteps,
       kMaxMetadataBytes, kMaxDecodedBytes, kMaxNewRequestedBytes});
  GltfDirectTranslationLimits out;
  out.max_node_rows = map.Number(kMaxNodeRows, active);
  out.max_mesh_users = map.Number(kMaxMeshUsers, active);
  out.max_channel_references = map.Number(kMaxChannelReferences, active);
  out.max_distinct_samplers = map.Number(kMaxDistinctSamplers, active);
  out.max_keys_per_sampler = map.Number(kMaxKeysPerSampler, active);
  out.max_total_keys = map.Number(kMaxTotalKeys, active);
  out.max_relationship_steps = map.Number(kMaxRelationshipSteps, active);
  out.max_metadata_bytes = map.Number(kMaxMetadataBytes, active);
  out.max_decoded_bytes = map.Number(kMaxDecodedBytes, active);
  out.max_new_requested_bytes = map.Number(kMaxNewRequestedBytes, active);
  return out;
}

GltfDirectTranslationWorkPolicy ReadDirectWork(nb::handle value,
                                               bool active = true) {
  const Mapping map(value, {kParallelMinKeys, kPartitionKeys,
                            kCancellationBlockKeys, kMaxJobs});
  GltfDirectTranslationWorkPolicy out;
  out.parallel_min_keys = map.Number(kParallelMinKeys, active);
  out.partition_keys = map.Number(kPartitionKeys, active);
  out.cancellation_block_keys = map.Number(kCancellationBlockKeys, active);
  out.max_jobs = map.Number(kMaxJobs, active);
  return out;
}

SurfaceCountLimits ReadCounts(nb::handle value, bool active = true) {
  const Mapping map(value, {kMaxVertices,
                            kMaxFaces,
                            kMaxCorners,
                            kMaxFaceOffsets,
                            kMaxAttributes,
                            kMaxAttributeScalars,
                            kMaxAttributeBytes,
                            kMaxAttributeOffsets,
                            kMaxPresenceBytes,
                            kMaxMetadataEntries,
                            kMaxDescriptorBytes,
                            kMaxWirePrimitives,
                            kMaxWireVertices,
                            kMaxWireCorners,
                            kMaxMapEntries,
                            kMaxMembers,
                            kMaxMemberBytes,
                            kMaxTotalMemberBytes,
                            kMaxResources,
                            kMaxResourceBytes,
                            kMaxJsonBytes,
                            kMaxBinaryBytes,
                            kMaxCapsuleJsonBytes,
                            kMaxCapsuleBinaryBytes,
                            kMaxMaterialBindings,
                            kMaxNumericTokenBytes,
                            kMaxJsonNodes,
                            kMaxJsonDepth,
                            kMaxNumericIntegerBits,
                            kMaxCoincidenceComparisons,
                            kMaxRelationshipSteps});
  SurfaceCountLimits out;
  out.max_vertices = map.Number(kMaxVertices, active);
  out.max_faces = map.Number(kMaxFaces, active);
  out.max_corners = map.Number(kMaxCorners, active);
  out.max_face_offsets = map.Number(kMaxFaceOffsets, active);
  out.max_attributes = map.Number(kMaxAttributes, active);
  out.max_attribute_scalars = map.Number(kMaxAttributeScalars, active);
  out.max_attribute_bytes = map.Number(kMaxAttributeBytes, active);
  out.max_attribute_offsets = map.Number(kMaxAttributeOffsets, active);
  out.max_presence_bytes = map.Number(kMaxPresenceBytes, active);
  out.max_metadata_entries = map.Number(kMaxMetadataEntries, active);
  out.max_descriptor_bytes = map.Number(kMaxDescriptorBytes, active);
  out.max_wire_primitives = map.Number(kMaxWirePrimitives, active);
  out.max_wire_vertices = map.Number(kMaxWireVertices, active);
  out.max_wire_corners = map.Number(kMaxWireCorners, active);
  out.max_map_entries = map.Number(kMaxMapEntries, active);
  out.max_members = map.Number(kMaxMembers, active);
  out.max_member_bytes = map.Number(kMaxMemberBytes, active);
  out.max_total_member_bytes = map.Number(kMaxTotalMemberBytes, active);
  out.max_resources = map.Number(kMaxResources, active);
  out.max_resource_bytes = map.Number(kMaxResourceBytes, active);
  out.max_json_bytes = map.Number(kMaxJsonBytes, active);
  out.max_binary_bytes = map.Number(kMaxBinaryBytes, active);
  out.max_capsule_json_bytes = map.Number(kMaxCapsuleJsonBytes, active);
  out.max_capsule_binary_bytes = map.Number(kMaxCapsuleBinaryBytes, active);
  out.max_material_bindings = map.Number(kMaxMaterialBindings, active);
  out.max_numeric_token_bytes = map.Number(kMaxNumericTokenBytes, active);
  out.max_json_nodes = map.Number(kMaxJsonNodes, active);
  out.max_json_depth = map.Number(kMaxJsonDepth, active);
  out.max_numeric_integer_bits = map.Number(kMaxNumericIntegerBits, active);
  out.max_coincidence_comparisons =
      map.Number(kMaxCoincidenceComparisons, active);
  out.max_relationship_steps = map.Number(kMaxRelationshipSteps, active);
  Valid(out.max_json_depth <=
                static_cast<std::uint64_t>(std::numeric_limits<int>::max()) &&
            out.max_json_nodes <= alloc::MaximumJsonCaptureNodes(),
        "preset exceeds native JSON capture extent");
  return out;
}

GltfReadLimits ReadGltfSourceLimits(nb::handle value, bool active = true) {
  const Mapping map(
      value, {kMaxJsonBytes, kMaxResourceBytes, kMaxOwnedSourceResourceBytes,
              kMaxDocumentNodes, kMaxDocumentDepth, kMaxDecodedStringBytes,
              kMaxNumericTokenBytes, kMaxDecimalCoefficientDigits,
              kMaxDecimalExponentDigits, kMaxTableRows, kMaxGraphEdges,
              kMaxInstances, kMaxAccessorElements, kMaxProjectionBytes});
  GltfReadLimits out;
  out.max_json_bytes = map.Number(kMaxJsonBytes, active);
  out.max_resource_bytes = map.Number(kMaxResourceBytes, active);
  out.max_owned_source_resource_bytes =
      map.Number(kMaxOwnedSourceResourceBytes, active);
  out.max_document_nodes = map.Number(kMaxDocumentNodes, active);
  out.max_document_depth = map.Number(kMaxDocumentDepth, active);
  out.max_decoded_string_bytes = map.Number(kMaxDecodedStringBytes, active);
  out.max_numeric_token_bytes = map.Number(kMaxNumericTokenBytes, active);
  out.max_decimal_coefficient_digits =
      map.Number(kMaxDecimalCoefficientDigits, active);
  out.max_decimal_exponent_digits =
      map.Number(kMaxDecimalExponentDigits, active);
  out.max_table_rows = map.Number(kMaxTableRows, active);
  out.max_graph_edges = map.Number(kMaxGraphEdges, active);
  out.max_instances = map.Number(kMaxInstances, active);
  out.max_accessor_elements = map.Number(kMaxAccessorElements, active);
  out.max_projection_bytes = map.Number(kMaxProjectionBytes, active);
  Valid(out.max_document_depth <=
                static_cast<std::uint64_t>(std::numeric_limits<int>::max()) &&
            out.max_document_nodes <= alloc::MaximumJsonCaptureNodes(),
        "preset exceeds native JSON capture extent");
  return out;
}

SurfaceFileLimits ReadFileLimits(nb::handle value, bool active = true) {
  const Mapping map(
      value, {kMaxPayloadMembers, kMaxPayloadMemberBytes, kMaxTotalPayloadBytes,
              kMaxReceiptBytes, kMaxCallerReportBytes, kMaxPathBytes,
              kMaxTotalPathBytes, kMaxNativePathCodeUnits, kMaxInventoryEntries,
              kMaxStageNameAttempts, kMaxNewRequestedBytes});
  SurfaceFileLimits out;
  out.max_payload_members = map.Number(kMaxPayloadMembers, active);
  out.max_payload_member_bytes = map.Number(kMaxPayloadMemberBytes, active);
  out.max_total_payload_bytes = map.Number(kMaxTotalPayloadBytes, active);
  out.max_receipt_bytes = map.Number(kMaxReceiptBytes, active);
  out.max_caller_report_bytes = map.Number(kMaxCallerReportBytes, active);
  out.max_path_bytes = map.Number(kMaxPathBytes, active);
  out.max_total_path_bytes = map.Number(kMaxTotalPathBytes, active);
  out.max_native_path_code_units = map.Number(kMaxNativePathCodeUnits, active);
  out.max_inventory_entries = map.Number(kMaxInventoryEntries, active);
  out.max_stage_name_attempts = map.Number(kMaxStageNameAttempts, active);
  out.max_new_requested_bytes = map.Number(kMaxNewRequestedBytes, active);
  Valid(out.max_caller_report_bytes <=
            static_cast<std::uint64_t>(
                std::numeric_limits<std::ptrdiff_t>::max()) /
                kMaximumJsonEscapeBytes,
        "preset exceeds native escaped report extent");
  return out;
}

SurfaceFileWorkPolicy ReadFileWork(nb::handle value, bool active = true) {
  const Mapping map(value, {kIoBlockBytes, kParallelMinBytes, kPartitionBytes,
                            kCancellationBlockBytes, kMaxJobs});
  SurfaceFileWorkPolicy out;
  out.io_block_bytes = map.Number(kIoBlockBytes, active);
  out.parallel_min_bytes = map.Number(kParallelMinBytes, active);
  out.partition_bytes = map.Number(kPartitionBytes, active);
  out.cancellation_block_bytes = map.Number(kCancellationBlockBytes, active);
  out.max_jobs = map.Number(kMaxJobs, active);
  return out;
}

GltfReadOperationLimits ReadGltfOperationLimits(nb::handle value) {
  const Mapping map(value, {kSource, kMaxNewRequestedBytes});
  return {ReadGltfSourceLimits(map.Get(kSource)),
          map.Number(kMaxNewRequestedBytes)};
}
GltfWorkPolicy ReadGltfWork(nb::handle value, bool active) {
  const Mapping map(value, {kParallelMinResourceBytes, kResourcePartitionBytes,
                            kParallelMinChannelComponents,
                            kChannelPartitionElements, kDirectTranslation});
  GltfWorkPolicy out;
  out.parallel_min_resource_bytes =
      map.Number(kParallelMinResourceBytes, active);
  out.resource_partition_bytes = map.Number(kResourcePartitionBytes, active);
  out.parallel_min_channel_components =
      map.Number(kParallelMinChannelComponents, active);
  out.channel_partition_elements =
      map.Number(kChannelPartitionElements, active);
  out.direct_translation = ReadDirectWork(map.Get(kDirectTranslation), false);
  return out;
}
GltfExtractionLimits ReadExtraction(nb::handle value) {
  const Mapping map(value, {kMaxComponents, kMaxTriangleCorners,
                            kMaxOutputRequestedBytes, kDirectTranslation});
  return {map.Number(kMaxComponents), map.Number(kMaxTriangleCorners),
          map.Number(kMaxOutputRequestedBytes),
          ReadDirectLimits(map.Get(kDirectTranslation), false)};
}
StlReadLimits ReadStlRead(nb::handle value) {
  const Mapping map(
      value, {kMaxSourceBytes, kMaxFacets, kMaxNameBytes, kMaxLineBytes,
              kMaxTokenBytes, kMaxCodecNewRequestedBytes,
              kMaxTotalNewRequestedBytes, kInspection, kMaterialization});
  StlReadLimits out;
  out.max_source_bytes = map.Number(kMaxSourceBytes);
  out.max_facets = map.Number(kMaxFacets);
  out.max_name_bytes = map.Number(kMaxNameBytes);
  out.max_line_bytes = map.Number(kMaxLineBytes);
  out.max_token_bytes = map.Number(kMaxTokenBytes);
  out.max_codec_new_requested_bytes = map.Number(kMaxCodecNewRequestedBytes);
  out.max_total_new_requested_bytes = map.Number(kMaxTotalNewRequestedBytes);
  out.materialization = ReadMaterialization(map.Get(kMaterialization));
  out.inspection = ReadInspection(map.Get(kInspection));
  Valid(Sum(Sum(out.max_codec_new_requested_bytes,
                out.materialization.max_new_requested_bytes),
            out.inspection.max_new_requested_bytes) <=
            out.max_total_new_requested_bytes,
        "STL read allowance does not contain its delegates");
  return out;
}
StlWriteLimits ReadStlWrite(nb::handle value) {
  const Mapping map(value,
                    {kMaxOutputBytes, kMaxFacets, kMaxNameBytes, kMaxLineBytes,
                     kMaxTokenBytes, kMaxCodecNewRequestedBytes,
                     kMaxTotalNewRequestedBytes, kInspection});
  StlWriteLimits out;
  out.max_output_bytes = map.Number(kMaxOutputBytes);
  out.max_facets = map.Number(kMaxFacets);
  out.max_name_bytes = map.Number(kMaxNameBytes);
  out.max_line_bytes = map.Number(kMaxLineBytes);
  out.max_token_bytes = map.Number(kMaxTokenBytes);
  out.max_codec_new_requested_bytes = map.Number(kMaxCodecNewRequestedBytes);
  out.max_total_new_requested_bytes = map.Number(kMaxTotalNewRequestedBytes);
  out.inspection = ReadInspection(map.Get(kInspection));
  Valid(Sum(out.max_codec_new_requested_bytes,
            out.inspection.max_new_requested_bytes) <=
            out.max_total_new_requested_bytes,
        "STL write allowance does not contain its delegates");
  return out;
}
StlWorkPolicy ReadStlWork(nb::handle value, bool active) {
  const Mapping map(value, {kCancellationBlockBytes, kCancellationBlockTokens,
                            kCancellationBlockFacets, kGeometry});
  return {map.Number(kCancellationBlockBytes, active),
          map.Number(kCancellationBlockTokens, active),
          map.Number(kCancellationBlockFacets, active),
          ReadMeshWork(map.Get(kGeometry), active)};
}
bool IsGltf(SurfaceEncoding encoding) {
  return encoding == SurfaceEncoding::kGltfJson ||
         encoding == SurfaceEncoding::kGlb;
}
SurfaceWorkPolicy ReadSurfaceWork(nb::handle value, SurfaceEncoding encoding,
                                  bool inspection) {
  const Mapping map(value, {kSurface, kContainer, kGltf, kStl});
  return {ReadMeshWork(map.Get(kSurface)),
          ReadGlbWork(map.Get(kContainer), encoding == SurfaceEncoding::kGlb),
          ReadGltfWork(map.Get(kGltf), IsGltf(encoding) && inspection),
          ReadStlWork(map.Get(kStl), !IsGltf(encoding))};
}
SurfaceEncodeLimits ReadEncode(nb::handle value, SurfaceEncoding encoding) {
  const Mapping map(value, {kCounts, kMaxNewRequestedBytes,
                            kMaxTotalRequestedAllowance, kDelegates});
  SurfaceEncodeLimits out;
  out.counts = ReadCounts(map.Get(kCounts));
  out.max_new_requested_bytes = map.Number(kMaxNewRequestedBytes);
  out.max_total_requested_allowance = map.Number(kMaxTotalRequestedAllowance);
  auto allowance = out.max_new_requested_bytes;
  if (IsGltf(encoding)) {
    const Mapping delegates(map.Get(kDelegates), {kKind, kContainer});
    Valid(Equal(delegates.Get(kKind), kGltfDelegateName),
          "encoding requires glTF delegates");
    GltfSurfaceEncodeLimits limits{ReadGlbLimits(
        delegates.Get(kContainer), encoding == SurfaceEncoding::kGlb)};
    allowance = Sum(allowance, limits.container.max_requested_bytes);
    out.delegates = limits;
  } else {
    const Mapping delegates(map.Get(kDelegates), {kKind, kDerivedMesh, kCodec});
    Valid(Equal(delegates.Get(kKind), kStlDelegateName),
          "encoding requires STL delegates");
    StlSurfaceEncodeLimits limits{
        ReadMaterialization(delegates.Get(kDerivedMesh)),
        ReadStlWrite(delegates.Get(kCodec))};
    allowance = Sum(Sum(allowance, limits.derived_mesh.max_new_requested_bytes),
                    limits.codec.max_total_new_requested_bytes);
    out.delegates = limits;
  }
  Valid(allowance <= out.max_total_requested_allowance,
        "surface encode allowance is too small");
  return out;
}
SurfaceInspectLimits ReadInspect(nb::handle value, SurfaceEncoding encoding) {
  const Mapping map(value, {kCounts, kMaxNewRequestedBytes,
                            kMaxTotalRequestedAllowance, kDelegates});
  SurfaceInspectLimits out;
  out.counts = ReadCounts(map.Get(kCounts));
  out.max_new_requested_bytes = map.Number(kMaxNewRequestedBytes);
  out.max_total_requested_allowance = map.Number(kMaxTotalRequestedAllowance);
  auto allowance = out.max_new_requested_bytes;
  if (IsGltf(encoding)) {
    const Mapping delegates(map.Get(kDelegates),
                            {kKind, kContainer, kRead, kExtraction});
    Valid(Equal(delegates.Get(kKind), kGltfDelegateName),
          "encoding requires glTF delegates");
    GltfSurfaceInspectLimits limits{
        ReadGlbLimits(delegates.Get(kContainer),
                      encoding == SurfaceEncoding::kGlb),
        ReadGltfOperationLimits(delegates.Get(kRead)),
        ReadExtraction(delegates.Get(kExtraction))};
    allowance = Sum(Sum(Sum(allowance, limits.container.max_requested_bytes),
                        limits.read.max_new_requested_bytes),
                    limits.extraction.max_output_requested_bytes);
    out.delegates = limits;
  } else {
    const Mapping delegates(map.Get(kDelegates), {kKind, kCodec});
    Valid(Equal(delegates.Get(kKind), kStlDelegateName),
          "encoding requires STL delegates");
    StlSurfaceInspectLimits limits{ReadStlRead(delegates.Get(kCodec))};
    allowance = Sum(allowance, limits.codec.max_total_new_requested_bytes);
    out.delegates = limits;
  }
  Valid(allowance <= out.max_total_requested_allowance,
        "surface inspection allowance is too small");
  return out;
}
SurfaceFormatPreset ReadFormat(nb::handle value, SurfaceEncoding encoding) {
  const Mapping map(value,
                    {kEncodeLimits, kEncodeWork, kInspectLimits, kInspectWork,
                     kPublicationLimits, kPublicationWork});
  SurfaceFormatPreset out;
  out.encode_limits = ReadEncode(map.Get(kEncodeLimits), encoding);
  out.encode_work = ReadSurfaceWork(map.Get(kEncodeWork), encoding, false);
  out.inspect_limits = ReadInspect(map.Get(kInspectLimits), encoding);
  out.inspect_work = ReadSurfaceWork(map.Get(kInspectWork), encoding, true);
  const Mapping publication(map.Get(kPublicationLimits),
                            {kFiles, kInspection, kMaxTotalRequestedAllowance});
  out.publication_limits.files = ReadFileLimits(publication.Get(kFiles));
  out.publication_limits.inspection =
      ReadInspect(publication.Get(kInspection), encoding);
  out.publication_limits.max_total_requested_allowance =
      publication.Number(kMaxTotalRequestedAllowance);
  Valid(Sum(out.publication_limits.files.max_new_requested_bytes,
            out.publication_limits.inspection.max_total_requested_allowance) <=
            out.publication_limits.max_total_requested_allowance,
        "publication allowance does not contain fresh inspection");
  const Mapping work(map.Get(kPublicationWork), {kFiles, kInspection});
  out.publication_work.files = ReadFileWork(work.Get(kFiles));
  out.publication_work.inspection =
      ReadSurfaceWork(work.Get(kInspection), encoding, true);
  return out;
}
nb::object Attribute(nb::handle options, const char* field) {
  return nb::getattr(options, field);
}
std::size_t TupleSize(nb::handle value) {
  if (!PyTuple_CheckExact(value.ptr()))
    throw nb::type_error("surface rows require a tuple");
  return static_cast<std::size_t>(PyTuple_GET_SIZE(value.ptr()));
}
nb::handle Item(nb::handle value, std::size_t ordinal) {
  return nb::handle(
      PyTuple_GET_ITEM(value.ptr(), static_cast<Py_ssize_t>(ordinal)));
}
SurfaceEncoding Encoding(nb::handle value) {
  if (Equal(value, kGltfJson)) return SurfaceEncoding::kGltfJson;
  if (Equal(value, kGlb)) return SurfaceEncoding::kGlb;
  if (Equal(value, kStlAscii)) return SurfaceEncoding::kStlAscii;
  if (Equal(value, kStlBinary)) return SurfaceEncoding::kStlBinary;
  throw nb::value_error("unknown surface encoding");
}
SurfaceChannelRole Role(nb::handle value) {
  if (Equal(value, kBundleOnlyName)) return SurfaceChannelRole::kBundleOnly;
  if (Equal(value, kUvName)) return SurfaceChannelRole::kUv;
  if (Equal(value, kStlNormalName)) return SurfaceChannelRole::kStlCornerNormal;
  if (Equal(value, kMaterialIdName)) return SurfaceChannelRole::kMaterialId;
  throw nb::value_error("unknown surface channel role");
}
SurfaceChannelRule Channel(nb::handle value) {
  return {Extent(Attribute(value, kSourceAttribute)),
          Role(Attribute(value, kRole)),
          Boolean(Attribute(value, kAcknowledgeOrdinaryViewLoss))};
}
SurfaceUvBinding Uv(nb::handle value) {
  return {Extent(Attribute(value, kSourceAttribute)),
          Uint32(Attribute(value, kSourceSet)),
          Uint32(Attribute(value, kWireSet))};
}
SurfaceMaterialBinding Material(nb::handle value) {
  return {Int32(Attribute(value, kSourceId)),
          Extent(Attribute(value, kCatalogMaterial))};
}
SurfaceCatalogUvBinding CatalogUv(nb::handle value) {
  return {Uint32(Attribute(value, kCatalogSet)),
          Uint32(Attribute(value, kSourceSet))};
}
std::size_t Utf8Width(Py_UCS4 codepoint) {
  if (codepoint <= kUtf8OneByteMax) return 1;
  if (codepoint <= kUtf8TwoByteMax) return 2;
  if (codepoint <= kUtf8ThreeByteMax) return 3;
  return 4;
}
std::size_t TextBytes(nb::handle value, std::uint64_t limit,
                      bool ascii = false) {
  if (!PyUnicode_CheckExact(value.ptr()))
    throw nb::type_error("surface text requires built-in str");
  const auto length = PyUnicode_GetLength(value.ptr());
  if (length < 0) throw nb::python_error();
  Valid(static_cast<std::uint64_t>(length) <= limit,
        "surface option text exceeds byte cap");
  std::uint64_t bytes = 0;
  for (Py_ssize_t i = 0; i < length; ++i) {
    const auto codepoint = PyUnicode_ReadChar(value.ptr(), i);
    if (codepoint == kUnicodeError && PyErr_Occurred())
      throw nb::python_error();
    Valid(codepoint <= kUnicodeMax &&
              !(codepoint >= kSurrogateFirst && codepoint <= kSurrogateLast),
          "surface text must encode as strict UTF-8");
    Valid(!ascii || (codepoint >= kPrintableAsciiFirst &&
                     codepoint <= kPrintableAsciiLast),
          "ASCII STL name must contain printable ASCII");
    bytes = Sum(bytes, Utf8Width(codepoint));
    Valid(bytes <= limit, "surface option text exceeds byte cap");
  }
  return Host(bytes);
}
void CopyText(nb::handle value, std::span<char> destination) {
  std::size_t offset = 0;
  auto emit = [&](unsigned byte) {
    if (offset == destination.size())
      throw std::logic_error("surface option text changed");
    destination[offset++] = static_cast<char>(byte);
  };
  const auto length = PyUnicode_GetLength(value.ptr());
  if (length < 0) throw nb::python_error();
  for (Py_ssize_t i = 0; i < length; ++i) {
    const auto codepoint = PyUnicode_ReadChar(value.ptr(), i);
    if (codepoint == kUnicodeError && PyErr_Occurred())
      throw nb::python_error();
    const auto width = Utf8Width(codepoint);
    if (width == 1)
      emit(codepoint);
    else {
      const auto prefix = width == 2   ? kUtf8TwoPrefix
                          : width == 3 ? kUtf8ThreePrefix
                                       : kUtf8FourPrefix;
      emit(prefix | (codepoint >> (kUtf8ContinuationBits * (width - 1))));
      for (auto remaining = width - 1; remaining; --remaining)
        emit(kUtf8ContinuationPrefix |
             ((codepoint >> (kUtf8ContinuationBits * (remaining - 1))) &
              kUtf8ContinuationMask));
    }
  }
  if (offset != destination.size())
    throw std::logic_error("surface option text changed");
}
}  // namespace

const SurfaceFormatPreset& SurfacePresetProjection::For(
    SurfaceEncoding encoding) const {
  for (std::size_t i = 0; i < kEncodings.size(); ++i)
    if (kEncodings[i] == encoding) return formats[i];
  throw nb::value_error("unknown surface encoding");
}
std::uint64_t SurfacePresetProjection::OperationAllowance(
    SurfaceEncoding encoding, bool catalog, bool publication) const {
  Valid(!catalog || IsGltf(encoding), "STL does not accept a catalog");
  const auto& format = For(encoding);
  auto amount = ingress_limits.max_bridge_new_requested_bytes;
  for (const auto value :
       {source_materialization.max_new_requested_bytes,
        source_inspection.max_new_requested_bytes,
        triangulation_limits.source_inspection.max_new_requested_bytes,
        triangulation_limits.output.max_new_requested_bytes,
        triangulation_limits.output_inspection.max_new_requested_bytes,
        triangulation_limits.max_core_new_requested_bytes,
        triangulation_limits.max_bridge_new_requested_bytes,
        format.encode_limits.max_total_requested_allowance,
        format.inspect_limits.max_total_requested_allowance})
    amount = Sum(amount, value);
  if (catalog)
    amount = Sum(amount, catalog_read_limits.max_new_requested_bytes);
  if (publication)
    amount =
        Sum(amount, format.publication_limits.max_total_requested_allowance);
  Valid(amount <= ingress_limits.max_operation_requested_allowance,
        "operation allowance is too small");
  return amount;
}
SurfacePresetProjection ReadSurfacePreset(nb::handle values) {
  const Mapping map(
      values,
      {kExecution, kIngressLimits, kIngressWork, kSourceMaterialization,
       kSourceInspection, kMeshWork, kTriangulationOptions,
       kTriangulationLimits, kCatalogReadLimits, kCatalogReadWork, kFormats});
  SurfacePresetProjection out;
  const Mapping execution(
      map.Get(kExecution),
      {kWorkerBudget, kMinimumParallelVertices, kTrackedPayloadBudgetBytes});
  out.execution = {Host(execution.Number(kWorkerBudget)),
                   Host(execution.Number(kMinimumParallelVertices)),
                   Host(execution.Number(kTrackedPayloadBudgetBytes))};
  const Mapping ingress(
      map.Get(kIngressLimits),
      {kMaxBufferViews, kMaxBufferBytes, kMaxTotalBufferBytes, kMaxAttributes,
       kMaxMetadataEntries, kMaxDescriptorBytes, kMaxOptionRows,
       kMaxOptionBytes, kMaxCatalogResourceBindings,
       kMaxDestinationNativeCodeUnits, kMaxBridgeNewRequestedBytes,
       kMaxOperationRequestedAllowance});
  out.ingress_limits.record = {ingress.Number(kMaxBufferViews),
                               ingress.Number(kMaxBufferBytes),
                               ingress.Number(kMaxTotalBufferBytes),
                               ingress.Number(kMaxAttributes),
                               ingress.Number(kMaxMetadataEntries),
                               ingress.Number(kMaxDescriptorBytes)};
  out.ingress_limits.max_option_rows = ingress.Number(kMaxOptionRows);
  out.ingress_limits.max_option_bytes = ingress.Number(kMaxOptionBytes);
  out.ingress_limits.max_catalog_resource_bindings =
      ingress.Number(kMaxCatalogResourceBindings);
  out.ingress_limits.max_destination_native_code_units =
      ingress.Number(kMaxDestinationNativeCodeUnits);
  out.ingress_limits.max_bridge_new_requested_bytes =
      ingress.Number(kMaxBridgeNewRequestedBytes);
  out.ingress_limits.max_operation_requested_allowance =
      ingress.Number(kMaxOperationRequestedAllowance);
  const Mapping work(map.Get(kIngressWork),
                     {kCancellationBlockBytes, kCancellationBlockItems});
  out.ingress_work = {work.Number(kCancellationBlockBytes),
                      work.Number(kCancellationBlockItems)};
  out.source_materialization =
      ReadMaterialization(map.Get(kSourceMaterialization));
  out.source_inspection = ReadInspection(map.Get(kSourceInspection));
  out.mesh_work = ReadMeshWork(map.Get(kMeshWork));
  const Mapping triangulation(map.Get(kTriangulationOptions),
                              {kMaxCornersPerFace, kMinimumParallelFaces});
  out.triangulation_options = {
      triangulation.Number(kMaxCornersPerFace),
      Host(triangulation.Number(kMinimumParallelFaces))};
  Valid(out.triangulation_options.max_corners_per_face >=
                kTriangulationMinimumCorners &&
            out.triangulation_options.max_corners_per_face <=
                kTriangulationMaximumCorners,
        "unsupported native triangulation corner limit");
  const Mapping limits(
      map.Get(kTriangulationLimits),
      {kSource, kSourceInspection, kOutput, kOutputInspection, kMaxMapEntries,
       kMaxCoreNewRequestedBytes, kMaxBridgeNewRequestedBytes});
  out.triangulation_limits = {ReadMaterialization(limits.Get(kSource)),
                              ReadInspection(limits.Get(kSourceInspection)),
                              ReadMaterialization(limits.Get(kOutput)),
                              ReadInspection(limits.Get(kOutputInspection)),
                              limits.Number(kMaxMapEntries),
                              limits.Number(kMaxCoreNewRequestedBytes),
                              limits.Number(kMaxBridgeNewRequestedBytes)};
  out.catalog_read_limits =
      ReadGltfOperationLimits(map.Get(kCatalogReadLimits));
  out.catalog_read_work = ReadGltfWork(map.Get(kCatalogReadWork), true);
  const Mapping formats(map.Get(kFormats),
                        {kGltfJson, kGlb, kStlAscii, kStlBinary});
  const std::array<const char*, kSurfacePythonFormatCount> names{
      kGltfJson, kGlb, kStlAscii, kStlBinary};
  for (std::size_t i = 0; i < names.size(); ++i) {
    out.formats[i] = ReadFormat(formats.Get(names[i]), kEncodings[i]);
    for (bool catalog : {false, true}) {
      if (catalog && !IsGltf(kEncodings[i])) continue;
      for (bool publication : {false, true})
        (void)out.OperationAllowance(kEncodings[i], catalog, publication);
    }
  }
  return out;
}

SurfaceOptionsPlan ValidateSurfaceOptions(
    nb::handle options, const SurfacePresetProjection& preset) {
  SurfaceOptionsPlan out;
  out.encoding = Encoding(Attribute(options, kEncoding));
  const auto representation = Attribute(options, kRepresentation);
  if (Equal(representation, kStrictBareName))
    out.representation = SurfaceRepresentation::kStrictBare;
  else if (Equal(representation, kAnnotatedBundleName))
    out.representation = SurfaceRepresentation::kAnnotatedBundleV1;
  else
    throw nb::value_error("unknown surface representation");
  const auto positions = Attribute(options, kPositions);
  if (Equal(positions, kExact32Name))
    out.positions = SurfaceNarrowing::kExact32;
  else if (Equal(positions, kBounded32Name))
    out.positions = SurfaceNarrowing::kBoundedRne32;
  else if (Equal(positions, kExact64Name))
    out.positions = SurfaceNarrowing::kExact64;
  else
    throw nb::value_error("unknown surface position policy");
  out.acknowledge_triangle_conversion =
      Boolean(Attribute(options, kAcknowledgeTriangleConversion));
  out.acknowledge_corner_expansion =
      Boolean(Attribute(options, kAcknowledgeCornerExpansion));
  out.acknowledge_unused_vertices =
      Boolean(Attribute(options, kAcknowledgeUnusedVertices));
  Valid(out.acknowledge_triangle_conversion && out.acknowledge_corner_expansion,
        "surface triangle conversion and corner expansion require "
        "acknowledgment");
  out.max_position_linf_error =
      Bound(Attribute(options, kMaxPositionLinfError));
  const auto channels = Attribute(options, kChannels);
  const auto uv = Attribute(options, kUv);
  const auto materials = Attribute(options, kMaterials);
  const auto catalog_uv = Attribute(options, kCatalogUv);
  const auto errors = Attribute(options, kMaxUvComponentErrors);
  out.channels = TupleSize(channels);
  out.uv = TupleSize(uv);
  out.materials = TupleSize(materials);
  out.catalog_uv = TupleSize(catalog_uv);
  out.uv_error_bounds = TupleSize(errors);
  for (auto count : {out.channels, out.uv, out.materials, out.catalog_uv,
                     out.uv_error_bounds})
    out.option_rows = Sum(out.option_rows, count);
  Valid(out.option_rows <= preset.ingress_limits.max_option_rows,
        "surface option row cap exceeded");
  for (std::size_t i = 0; i < out.channels; ++i)
    Valid(Channel(Item(channels, i)).source_attribute == i,
          "surface channel dispositions require complete source ordinal order");
  for (std::size_t i = 0; i < out.uv; ++i) (void)Uv(Item(uv, i));
  for (std::size_t i = 0; i < out.materials; ++i)
    (void)Material(Item(materials, i));
  for (std::size_t i = 0; i < out.catalog_uv; ++i)
    (void)CatalogUv(Item(catalog_uv, i));
  for (std::size_t i = 0; i < out.uv_error_bounds; ++i)
    (void)Bound(Item(errors, i));
  const auto normal = Attribute(options, kCornerNormalAttribute);
  if (!normal.is_none()) out.corner_normal_attribute = Extent(normal);
  const auto coordinates = Attribute(options, kCoordinates);
  const bool gltf = IsGltf(out.encoding);
  Valid(Equal(coordinates, gltf ? kMetersName : kStlUnitsName),
        "surface coordinates differ from encoding");
  out.coordinates = gltf ? SurfaceCoordinates::kDeclaredMetersRightHandedYUp
                         : SurfaceCoordinates::kStlUnitsUnspecified;
  const auto unit = Attribute(options, kDeclaredUnit);
  const auto ascii = Attribute(options, kAsciiName);
  const auto header = Attribute(options, kBinaryHeader);
  out.declared_unit_bytes =
      TextBytes(unit, preset.ingress_limits.max_option_bytes);
  out.ascii_name_bytes =
      TextBytes(ascii, preset.ingress_limits.max_option_bytes,
                out.encoding == SurfaceEncoding::kStlAscii);
  if (!PyBytes_CheckExact(header.ptr()))
    throw nb::type_error("surface binary header requires built-in bytes");
  out.binary_header_bytes =
      static_cast<std::size_t>(PyBytes_GET_SIZE(header.ptr()));
  out.text_bytes = Host(Sum(Sum(out.declared_unit_bytes, out.ascii_name_bytes),
                            out.binary_header_bytes));
  out.option_bytes = out.text_bytes;
  for (auto amount : {Product(out.channels, sizeof(SurfaceChannelRule)),
                      Product(out.uv, sizeof(SurfaceUvBinding)),
                      Product(out.materials, sizeof(SurfaceMaterialBinding)),
                      Product(out.catalog_uv, sizeof(SurfaceCatalogUvBinding)),
                      Product(out.uv_error_bounds, sizeof(double))})
    out.option_bytes = Sum(out.option_bytes, amount);
  Valid(out.option_bytes <= preset.ingress_limits.max_option_bytes,
        "surface option backing byte cap exceeded");
  if (gltf) {
    Valid(out.positions != SurfaceNarrowing::kExact64,
          "glTF requires binary32 position policy");
    Valid(!out.corner_normal_attribute && !out.declared_unit_bytes &&
              !out.ascii_name_bytes && !out.binary_header_bytes,
          "glTF requires canonical inactive STL options");
    Valid(out.uv_error_bounds == out.uv,
          "surface UV binding requires one explicit error bound");
  } else {
    Valid(out.corner_normal_attribute.has_value(),
          "STL requires an explicit corner normal attribute");
    Valid(!out.uv && !out.materials && !out.catalog_uv && !out.uv_error_bounds,
          "STL requires inactive UV and material options");
    if (out.encoding == SurfaceEncoding::kStlAscii)
      Valid(!out.binary_header_bytes,
            "ASCII STL requires an empty binary header");
    else
      Valid(out.positions != SurfaceNarrowing::kExact64 &&
                !out.ascii_name_bytes &&
                out.binary_header_bytes == kBinaryHeaderBytes,
            "binary STL requires binary32 positions, empty ASCII name and "
            "fixed header");
  }
  (void)preset.For(out.encoding);
  return out;
}

OwnedSurfaceOptions::OwnedSurfaceOptions(const SurfaceOptionsPlan& plan,
                                         nb::handle options,
                                         const alloc::AllocationContext& bridge)
    : plan_(plan),
      channels_(alloc::Allocator<SurfaceChannelRule>(bridge)),
      uv_(alloc::Allocator<SurfaceUvBinding>(bridge)),
      materials_(alloc::Allocator<SurfaceMaterialBinding>(bridge)),
      catalog_uv_(alloc::Allocator<SurfaceCatalogUvBinding>(bridge)),
      uv_errors_(alloc::Allocator<double>(bridge)),
      text_(alloc::Allocator<char>(bridge)) {
  if (!bridge.HasRequestLimit())
    throw std::invalid_argument("surface options require a capped bridge");
  channels_.resize(plan.channels);
  uv_.resize(plan.uv);
  materials_.resize(plan.materials);
  catalog_uv_.resize(plan.catalog_uv);
  uv_errors_.resize(plan.uv_error_bounds);
  text_.resize(plan.text_bytes);
  const auto channels = Attribute(options, kChannels);
  const auto uv = Attribute(options, kUv);
  const auto materials = Attribute(options, kMaterials);
  const auto catalog_uv = Attribute(options, kCatalogUv);
  const auto errors = Attribute(options, kMaxUvComponentErrors);
  if (TupleSize(channels) != plan.channels || TupleSize(uv) != plan.uv ||
      TupleSize(materials) != plan.materials ||
      TupleSize(catalog_uv) != plan.catalog_uv ||
      TupleSize(errors) != plan.uv_error_bounds)
    throw std::logic_error("surface option rows changed after validation");
  for (std::size_t i = 0; i < plan.channels; ++i)
    channels_[i] = Channel(Item(channels, i));
  for (std::size_t i = 0; i < plan.uv; ++i) uv_[i] = Uv(Item(uv, i));
  for (std::size_t i = 0; i < plan.materials; ++i)
    materials_[i] = Material(Item(materials, i));
  for (std::size_t i = 0; i < plan.catalog_uv; ++i)
    catalog_uv_[i] = CatalogUv(Item(catalog_uv, i));
  for (std::size_t i = 0; i < plan.uv_error_bounds; ++i)
    uv_errors_[i] = Bound(Item(errors, i));
  auto bytes = std::span<char>(text_);
  CopyText(Attribute(options, kDeclaredUnit),
           bytes.first(plan.declared_unit_bytes));
  CopyText(Attribute(options, kAsciiName),
           bytes.subspan(plan.declared_unit_bytes, plan.ascii_name_bytes));
  const auto header = Attribute(options, kBinaryHeader);
  if (!PyBytes_CheckExact(header.ptr()) ||
      static_cast<std::size_t>(PyBytes_GET_SIZE(header.ptr())) !=
          plan.binary_header_bytes)
    throw std::logic_error("surface binary header changed after validation");
  if (plan.binary_header_bytes)
    std::memcpy(text_.data() + plan.declared_unit_bytes + plan.ascii_name_bytes,
                PyBytes_AS_STRING(header.ptr()), plan.binary_header_bytes);
}
SurfaceEncoding OwnedSurfaceOptions::Encoding() const noexcept {
  return plan_.encoding;
}
SurfaceEncodeRequest OwnedSurfaceOptions::Request(
    const geo::MeshInspection& original,
    const geo::ChargedTriangulation& triangles,
    const GltfAsset* catalog) const {
  SurfaceEncodeRequest request{
      original,
      triangles,
      plan_.representation,
      plan_.acknowledge_triangle_conversion,
      plan_.acknowledge_corner_expansion,
      plan_.acknowledge_unused_vertices,
      channels_,
      SurfaceNumericPolicy{plan_.positions, plan_.max_position_linf_error,
                           uv_errors_},
      {}};
  if (IsGltf(plan_.encoding))
    request.destination =
        GltfSurfaceOptions{plan_.encoding, plan_.coordinates, uv_,
                           catalog,        materials_,        catalog_uv_};
  else {
    if (catalog)
      throw std::logic_error("STL surface request cannot borrow a catalog");
    const auto bytes = std::span<const char>(text_);
    const auto unit =
        plan_.declared_unit_bytes
            ? std::string_view(bytes.data(), plan_.declared_unit_bytes)
            : std::string_view{};
    const auto name =
        plan_.ascii_name_bytes
            ? std::string_view(bytes.data() + plan_.declared_unit_bytes,
                               plan_.ascii_name_bytes)
            : std::string_view{};
    const auto header = plan_.binary_header_bytes
                            ? std::span<const std::byte>(
                                  reinterpret_cast<const std::byte*>(
                                      bytes.data() + plan_.declared_unit_bytes +
                                      plan_.ascii_name_bytes),
                                  plan_.binary_header_bytes)
                            : std::span<const std::byte>{};
    request.destination =
        StlSurfaceOptions{plan_.encoding, plan_.coordinates,
                          unit,           plan_.corner_normal_attribute,
                          header,         name};
  }
  return request;
}
}  // namespace meshvale::interchange::surface_python_detail
