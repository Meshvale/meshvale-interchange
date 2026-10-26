// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/gltf_topology_write.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_write.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_direct_translation.h"
#include "gltf_json_snapshot.h"
#include "gltf_topology_write_internal.h"
#include "gltf_topology_write_workers.h"
#if defined(MESHVALE_INTERCHANGE_TESTING)
#include "gltf_topology_bundle_test_internal.h"
#endif

namespace meshvale::interchange::gltf_topology_write_detail {
using geometry::index_t;
using gltf_detail::AdmissionCode;
using gltf_detail::AdmissionFailure;
using gltf_detail::AllocationContext;
using gltf_detail::Allocator;
using Stage = GltfTopologyWriteStage;
using Status = GltfTopologyWriteStatus;
using Code = GltfTopologyWriteDiagnosticCode;
namespace {
constexpr char kFailureText[] = "glTF topology output rejected";
constexpr std::size_t kPositionComponents = 3;
constexpr std::size_t kCornersPerTriangle = 3;
constexpr std::size_t kIndexBytes = sizeof(std::uint32_t);
constexpr std::uint32_t kRestartIndex = UINT32_MAX;
constexpr std::uint64_t kTrianglesMode = 4;
constexpr std::size_t kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = UINT32_C(0xff);
constexpr std::size_t kBase64InputGroup = 3;
constexpr std::size_t kBase64OutputGroup = 4;
constexpr unsigned kBase64SextetMask = 63;
constexpr char kBase64Padding = '=';
constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view kDataUriPrefix =
    "data:application/octet-stream;base64,";
constexpr std::string_view kBuffersKey = "buffers";
constexpr std::string_view kViewsKey = "bufferViews";
constexpr std::string_view kAccessorsKey = "accessors";
constexpr std::string_view kIndicesKey = "indices";
constexpr std::string_view kModeKey = "mode";
constexpr std::string_view kComma = ",";
constexpr std::string_view kBuffersMember = "\"buffers\":[";
constexpr std::string_view kViewsMember = "\"bufferViews\":[";
constexpr std::string_view kAccessorsMember = "\"accessors\":[";
constexpr std::string_view kArrayEnd = "]";
constexpr std::string_view kBufferStart = "{\"byteLength\":";
constexpr std::string_view kBufferUri = ",\"uri\":\"";
constexpr std::string_view kBufferEnd = "\"}";
constexpr std::string_view kViewStart = "{\"buffer\":";
constexpr std::string_view kViewLength = ",\"byteLength\":";
constexpr std::string_view kObjectEnd = "}";
constexpr std::string_view kAccessorStart = "{\"bufferView\":";
// Core glTF UNSIGNED_INT component and SCALAR shape. Encoded text is private
// emission protocol, independent of the retained raw component enum's ordinal.
constexpr std::string_view kAccessorCount =
    " ,\"componentType\":5125,\"count\":";
constexpr std::string_view kAccessorEnd = ",\"type\":\"SCALAR\"}";
constexpr std::string_view kIndicesMember = "\"indices\":";
constexpr std::string_view kModeMember = "\"mode\":4";
constexpr std::string_view kTrianglesToken = "4";
constexpr std::size_t kUnsignedDecimalCapacity =
    std::numeric_limits<std::uint64_t>::digits10 + 1;
constexpr std::size_t kNoDifference = std::numeric_limits<std::size_t>::max();

void Configuration(const GltfTopologyWriteLimits& limits,
                   const GltfTopologyWriteWorkPolicy& work) {
  for (const auto field :
       {limits.max_source_json_bytes, limits.max_source_json_nodes,
        limits.max_source_json_depth, limits.max_vertices, limits.max_channels,
        limits.max_attribute_scalars, limits.max_descriptor_bytes,
        limits.max_candidate_faces, limits.max_candidate_corners,
        limits.max_index_bytes, limits.output.max_json_bytes,
        limits.output.max_resources, limits.output.max_resource_bytes,
        limits.output.max_total_resource_bytes,
        limits.output.max_output_requested_bytes, work.parallel_min_bytes,
        work.partition_bytes, work.cancellation_block_bytes, work.max_jobs}) {
    if (field == 0)
      Reject(Status::kInvalidConfiguration, Stage::kAdmission,
             Code::kRequiredFieldZero);
    if (field > std::numeric_limits<std::size_t>::max())
      Reject(Status::kInvalidConfiguration, Stage::kAdmission,
             Code::kConfigurationExtent);
  }
}
void TranslationFailure(const gltf_detail::DirectTranslationResult& result,
                        Stage stage) {
  const auto& diagnostic = result.diagnostic;
  switch (diagnostic.status) {
    case GltfStatus::kUnsupportedProfile:
      if (diagnostic.code == GltfDiagnosticCode::kLimitExceeded ||
          diagnostic.code == GltfDiagnosticCode::kSizeOverflow)
        Reject(Status::kLimitExceeded, stage,
               result.request_limit ? Code::kWriterRequestLimit
                                    : Code::kCountLimit);
      Reject(Status::kUnsupportedProfile, stage, Code::kSourceCapability);
    case GltfStatus::kBudgetExceeded:
      Reject(Status::kBudgetExceeded, stage, Code::kRequestBudget);
    case GltfStatus::kCanceled:
      Reject(Status::kCanceled, stage, Code::kStopRequested);
    case GltfStatus::kResourceBindingMismatch:
      Reject(Status::kSourceContractFailure, stage, Code::kSourceProvenance, {},
             {}, diagnostic.json_span);
    default:
      Reject(Status::kSourceContractFailure, stage, Code::kSourceDescriptor, {},
             {}, diagnostic.json_span);
  }
}
void Bound(std::size_t count, std::uint64_t limit, Stage stage) {
  if (count > limit) Reject(Status::kLimitExceeded, stage, Code::kCountLimit);
}
std::size_t Scalars(const geometry::ScalarReadView& values) {
  return std::visit([](auto span) { return span.size(); }, values);
}
std::span<const std::byte> Bytes(const geometry::ScalarReadView& values) {
  return std::visit([](auto span) { return std::as_bytes(span); }, values);
}
std::span<const std::byte> TextBytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}
void DescriptorCounts(geometry::MeshReadView view,
                      const GltfTopologyWriteLimits& limits,
                      const AllocationContext& allocation) {
  std::size_t scalars = 0;
  std::size_t descriptor_bytes = 0;
  for (const auto& attribute : view.attributes) {
    allocation.Checkpoint();
    scalars = Add(scalars, Scalars(attribute.values), Stage::kAdmission);
    descriptor_bytes =
        Add(descriptor_bytes, attribute.name.size(), Stage::kAdmission);
    descriptor_bytes =
        Add(descriptor_bytes, attribute.semantic.size(), Stage::kAdmission);
    for (const auto& metadata : attribute.metadata) {
      allocation.Checkpoint();
      descriptor_bytes =
          Add(descriptor_bytes, metadata.key.size(), Stage::kAdmission);
      descriptor_bytes =
          Add(descriptor_bytes, metadata.value.size(), Stage::kAdmission);
    }
  }
  Bound(scalars, limits.max_attribute_scalars, Stage::kAdmission);
  Bound(descriptor_bytes, limits.max_descriptor_bytes, Stage::kAdmission);
}
void ResourceCounts(std::span<const GltfResourceView> resources,
                    const GltfWriteLimits& limits, std::size_t added_bytes,
                    bool changed, const AllocationContext& allocation,
                    Stage stage);
void PhysicalCounts(const GltfTopologyWriteRequest& request,
                    const GltfTopologyWriteLimits& limits,
                    const gltf_detail::AssetState& asset,
                    const AllocationContext& allocation) {
  const auto source = request.source.Mesh().View();
  const auto candidate = request.candidate.Source().View();
  for (const auto view : {source, candidate}) {
    Bound(view.positions.size() / kPositionComponents, limits.max_vertices,
          Stage::kAdmission);
    Bound(Add(view.attributes.size(), 1, Stage::kAdmission),
          limits.max_channels, Stage::kAdmission);
    DescriptorCounts(view, limits, allocation);
  }
  Bound(asset.document.Original().size(), limits.max_source_json_bytes,
        Stage::kAdmission);
  Bound(asset.document.NodeCount(), limits.max_source_json_nodes,
        Stage::kAdmission);
  Bound(candidate.face_offsets.empty() ? 0 : candidate.face_offsets.size() - 1,
        limits.max_candidate_faces, Stage::kAdmission);
  Bound(candidate.corner_vertices.size(), limits.max_candidate_corners,
        Stage::kAdmission);
  Bound(request.source_faces.size(), limits.max_candidate_faces,
        Stage::kAdmission);
  Bound(request.source_corners.size(), limits.max_candidate_corners,
        Stage::kAdmission);
  const bool changed =
      candidate.face_offsets.size() != source.face_offsets.size();
  const auto index_bytes = changed ? Multiply(candidate.corner_vertices.size(),
                                              kIndexBytes, Stage::kAdmission)
                                   : 0;
  Bound(index_bytes, limits.max_index_bytes, Stage::kAdmission);
  ResourceCounts(asset.resources, limits.output, index_bytes, changed,
                 allocation, Stage::kAdmission);
  if (!changed)
    Bound(asset.document.Original().size(), limits.output.max_json_bytes,
          Stage::kAdmission);
  // An allocation-free preorder walk checks container depth before any writer
  // seed/index allocation. Each node/ancestor transition is visited at most
  // once.
  std::size_t node = 0;
  std::size_t depth = 0;
  while (true) {
    allocation.Checkpoint();
    const auto kind = asset.document.Kind(node);
    const bool container = kind == gltf_detail::JsonKind::kObject ||
                           kind == gltf_detail::JsonKind::kArray;
    if (container)
      Bound(depth + 1, limits.max_source_json_depth, Stage::kAdmission);
    if (const auto child = asset.document.FirstChild(node)) {
      ++depth;
      node = *child;
      continue;
    }
    while (!asset.document.NextSibling(node)) {
      allocation.Checkpoint();
      const auto parent = asset.document.Parent(node);
      if (!parent) return;
      node = *parent;
      --depth;
    }
    node = *asset.document.NextSibling(node);
  }
}
void ResourceCounts(std::span<const GltfResourceView> resources,
                    const GltfWriteLimits& limits, std::size_t added_bytes,
                    bool changed, const AllocationContext& allocation,
                    Stage stage) {
  Bound(Add(resources.size(), changed ? 1 : 0, stage), limits.max_resources,
        stage);
  std::size_t total = 0;
  for (const auto& resource : resources) {
    allocation.Checkpoint();
    Bound(resource.bytes.size(), limits.max_resource_bytes, stage);
    total = Add(total, resource.bytes.size(), stage);
  }
  if (changed) {
    Bound(added_bytes, limits.max_resource_bytes, stage);
    total = Add(total, added_bytes, stage);
  }
  Bound(total, limits.max_total_resource_bytes, stage);
}
struct Session {
  const geometry::ExecutionContext& context;
  const AllocationContext& allocation;
  const GltfTopologyWriteWorkPolicy& work;
  std::stop_token stop;
  const NativeWorkerBackend& backend;
  GltfTopologyWriteExecution& execution;
  template <class Function>
  void Run(std::size_t count, std::size_t width, Function& operation,
           GltfTopologyWriteExecutionStats& stats) {
    RunRange(
        context, allocation, work, count, width,
        [](void* state, std::size_t begin, std::size_t end) {
          (*static_cast<Function*>(state))(begin, end);
        },
        &operation, stop, stats, backend);
  }
  void Check(std::size_t processed, std::size_t width = 1) const {
    const auto block =
        std::max<std::size_t>(1, work.cancellation_block_bytes / width);
    if (processed % block == 0) allocation.Checkpoint();
  }
  // A detected mismatch survives later cancellation. Joined cancellation may
  // leave an earlier prefix unexamined; finalize only that prefix, without
  // allocation/stop, to obtain the same first diagnostic as complete traversal.
  template <class Different>
  std::optional<std::size_t> Find(std::size_t count, std::size_t width,
                                  Different different) {
    std::atomic<std::size_t> first{kNoDifference};
    auto scan = [&](std::size_t begin, std::size_t end) {
      for (auto i = begin; i < end; ++i) {
        Check(i - begin, width);
        if (different(i)) {
          auto known = first.load(std::memory_order_relaxed);
          while (i < known && !first.compare_exchange_weak(
                                  known, i, std::memory_order_relaxed)) {
          }
          break;
        }
      }
    };
    try {
      Run(count, width, scan, execution.verification);
    } catch (const AdmissionFailure& failure) {
      const auto known = first.load(std::memory_order_relaxed);
      if (failure.Code() != AdmissionCode::kCanceled || known == kNoDifference)
        throw;
      for (std::size_t i = 0; i <= known; ++i)
        if (different(i)) return i;
      throw;
    }
    const auto mismatch = first.load(std::memory_order_relaxed);
    return mismatch == kNoDifference ? std::nullopt : std::optional(mismatch);
  }
  template <class Different>
  void Verify(std::size_t count, std::size_t width, Different different,
              Status status, Code code, std::optional<index_t> attribute = {},
              std::size_t diagnostic_divisor = 1) {
    const auto mismatch = Find(count, width, different);
    if (mismatch)
      Reject(status, Stage::kVerification, code, attribute,
             static_cast<index_t>(*mismatch / diagnostic_divisor));
  }
  void SameBytes(std::span<const std::byte> source,
                 std::span<const std::byte> candidate, Code code,
                 std::optional<index_t> attribute = {},
                 std::size_t scalar_bytes = 1) {
    if (source.size() != candidate.size())
      Reject(Status::kPreservationMismatch, Stage::kVerification, code,
             attribute);
    Verify(
        source.size(), 1,
        [&](std::size_t i) { return source[i] != candidate[i]; },
        Status::kPreservationMismatch, code, attribute, scalar_bytes);
  }
  void SameText(std::string_view source, std::string_view candidate,
                std::optional<index_t> attribute) {
    SameBytes(TextBytes(source), TextBytes(candidate),
              Code::kAttributeDescriptor, attribute);
  }
};
void VerifyAttributes(geometry::MeshReadView source,
                      geometry::MeshReadView candidate, Session& session) {
  if (source.positions.size() != candidate.positions.size())
    Reject(Status::kPreservationMismatch, Stage::kVerification,
           Code::kPositionBits);
  session.SameBytes(std::as_bytes(source.positions),
                    std::as_bytes(candidate.positions), Code::kPositionBits, {},
                    sizeof(double) * kPositionComponents);
  if (source.attributes.size() != candidate.attributes.size())
    Reject(Status::kPreservationMismatch, Stage::kVerification,
           Code::kChannelCount);
  for (std::size_t ordinal = 0; ordinal < source.attributes.size(); ++ordinal) {
    session.allocation.Checkpoint();
    const auto& a = source.attributes[ordinal];
    const auto& b = candidate.attributes[ordinal];
    const auto index = static_cast<index_t>(ordinal);
    if (a.domain != b.domain || a.set_index != b.set_index ||
        a.components != b.components || a.values.index() != b.values.index() ||
        a.offsets.has_value() != b.offsets.has_value() ||
        a.present.has_value() != b.present.has_value() ||
        a.metadata.size() != b.metadata.size())
      Reject(Status::kPreservationMismatch, Stage::kVerification,
             Code::kAttributeDescriptor, index);
    session.SameText(a.name, b.name, index);
    session.SameText(a.semantic, b.semantic, index);
    if (a.offsets)
      session.SameBytes(std::as_bytes(*a.offsets), std::as_bytes(*b.offsets),
                        Code::kAttributeDescriptor, index, sizeof(index_t));
    if (a.present)
      session.SameBytes(std::as_bytes(*a.present), std::as_bytes(*b.present),
                        Code::kAttributeDescriptor, index);
    for (std::size_t metadata = 0; metadata < a.metadata.size(); ++metadata) {
      session.allocation.Checkpoint();
      session.SameText(a.metadata[metadata].key, b.metadata[metadata].key,
                       index);
      session.SameText(a.metadata[metadata].value, b.metadata[metadata].value,
                       index);
    }
    const auto width = std::visit(
        [](auto span) { return sizeof(typename decltype(span)::value_type); },
        a.values);
    session.SameBytes(Bytes(a.values), Bytes(b.values), Code::kAttributeBits,
                      index, width);
  }
}
void VerifyTopology(const GltfTopologyWriteRequest& request,
                    geometry::MeshReadView source,
                    geometry::MeshReadView candidate, Session& session) {
  const auto indices = request.source.SourceExtraction().TriangleIndices();
  if (source.corner_vertices.size() != indices.size() ||
      source.face_offsets.size() != indices.size() / kCornersPerTriangle + 1 ||
      request.source.SourceExtraction().SourceCorners().size() !=
          indices.size())
    Reject(Status::kSourceContractFailure, Stage::kVerification,
           Code::kSourceDescriptor);
  session.Verify(
      source.face_offsets.size(), sizeof(index_t),
      [&](std::size_t i) {
        return source.face_offsets[i] != i * kCornersPerTriangle;
      },
      Status::kSourceContractFailure, Code::kSourceDescriptor);
  session.Verify(
      indices.size(), sizeof(index_t),
      [&](std::size_t i) {
        return source.corner_vertices[i] != indices[i] ||
               indices[i] == kRestartIndex;
      },
      Status::kSourceContractFailure, Code::kSourceProvenance);
  const auto faces = request.source_faces;
  session.Verify(
      candidate.face_offsets.size(), sizeof(index_t),
      [&](std::size_t i) {
        return candidate.face_offsets[i] != i * kCornersPerTriangle;
      },
      Status::kPreservationMismatch, Code::kTriangleShape);
  session.Verify(
      request.source_corners.size(), sizeof(index_t),
      [&](std::size_t i) {
        return request.source_corners[i] !=
               faces[i / kCornersPerTriangle] * kCornersPerTriangle +
                   i % kCornersPerTriangle;
      },
      Status::kInvalidCorrespondence, Code::kCornerMapValue);
  session.Verify(
      candidate.corner_vertices.size(), sizeof(index_t),
      [&](std::size_t i) {
        return candidate.corner_vertices[i] !=
               indices[static_cast<std::size_t>(request.source_corners[i])];
      },
      Status::kPreservationMismatch, Code::kTriangleLoop);
}
struct BlockDelete {
  void operator()(std::byte* pointer) const noexcept {
    AllocationContext::Deallocate(pointer);
  }
};
struct ByteBlock {
  std::unique_ptr<std::byte, BlockDelete> data;
  std::size_t size = 0;
  void Allocate(std::size_t bytes, const AllocationContext& allocation) {
    data.reset(static_cast<std::byte*>(
        allocation.Allocate(bytes, alignof(std::byte))));
    size = bytes;
  }
  std::span<std::byte> View() noexcept { return {data.get(), size}; }
  std::span<const std::byte> View() const noexcept {
    return {data.get(), size};
  }
};
}  // namespace

Failure::Failure(GltfTopologyWriteStatus status,
                 GltfTopologyWriteDiagnostic diagnostic) noexcept
    : status_(status), diagnostic_(diagnostic) {}
Status Failure::Status() const noexcept { return status_; }
const GltfTopologyWriteDiagnostic& Failure::Diagnostic() const noexcept {
  return diagnostic_;
}
const char* Failure::what() const noexcept { return kFailureText; }
[[noreturn]] void Reject(Status status, Stage stage, Code code,
                         std::optional<index_t> attribute,
                         std::optional<index_t> element,
                         std::optional<GltfSourceSpan> source) {
  throw Failure(status, {stage, code, attribute, element, source, {}});
}
std::size_t Add(std::size_t left, std::size_t right, Stage stage) {
  if (right > std::numeric_limits<std::size_t>::max() - left)
    Reject(Status::kLimitExceeded, stage, Code::kOutputExtent);
  return left + right;
}
std::size_t Multiply(std::size_t left, std::size_t right, Stage stage) {
  if (left && right > std::numeric_limits<std::size_t>::max() / left)
    Reject(Status::kLimitExceeded, stage, Code::kOutputExtent);
  return left * right;
}
struct BundleState {
  BundleState(const GltfMaterializedPrimitive& input,
              const AllocationContext& operation)
      : source(input),
        allocation(operation),
        faces(Allocator<index_t>(operation)),
        corners(Allocator<index_t>(operation)),
        original_corners(Allocator<GltfSourceCorner>(operation)),
        resources(Allocator<GltfResourceView>(operation))
#if defined(MESHVALE_INTERCHANGE_TESTING)
        ,
        test_resource_storage(Allocator<ByteBlock>(operation))
#endif
  {
  }
  GltfMaterializedPrimitive source;
  AllocationContext allocation;
  Vector<index_t> faces;
  Vector<index_t> corners;
  Vector<GltfSourceCorner> original_corners;
  Vector<GltfResourceView> resources;
  ByteBlock indices;
  ByteBlock uri;
  ByteBlock json;
  std::optional<GltfTopologyBindingChange> change;
  std::optional<GltfDirectTranslationObservation> direct_translation;
#if defined(MESHVALE_INTERCHANGE_TESTING)
  std::optional<GltfTopologyWriteBundle> test_parent;
  Vector<ByteBlock> test_resource_storage;
#endif
};
struct Access {
  static GltfTopologyWriteBundle Make(
      std::shared_ptr<const BundleState> state) noexcept {
    return GltfTopologyWriteBundle(std::move(state));
  }
};

#if defined(MESHVALE_INTERCHANGE_TESTING)
GltfTopologyWriteBundle CloneBundleForTest(
    const GltfTopologyWriteBundle& original,
    const BundleTestOverrides& overrides) {
  const auto context = original.Source().Mesh().ShareExecutionContext();
  const AllocationContext allocation(context, {});
  auto copy = std::allocate_shared<BundleState>(
      Allocator<BundleState>(allocation), original.Source(), allocation);
  copy->test_parent = original;
  const auto json = overrides.json.value_or(original.Json());
  copy->json.Allocate(json.size(), allocation);
  if (!json.empty())
    std::memcpy(copy->json.data.get(), json.data(), json.size());
  const auto faces = overrides.faces.value_or(original.SourceFaces());
  copy->faces.assign(faces.begin(), faces.end());
  const auto corners = overrides.corners.value_or(original.SourceCorners());
  copy->corners.assign(corners.begin(), corners.end());
  const auto provenance =
      overrides.original_corners.value_or(original.OriginalCorners());
  copy->original_corners.assign(provenance.begin(), provenance.end());
  copy->change = overrides.changed_binding.value_or(original.ChangedBinding());
  const auto resources = overrides.resources.value_or(original.Resources());
  copy->resources.assign(resources.begin(), resources.end());
  if (overrides.resources) {
    for (auto& resource : copy->resources) {
      ByteBlock uri;
      uri.Allocate(resource.authored_uri.size(), allocation);
      if (!resource.authored_uri.empty())
        std::memcpy(uri.data.get(), resource.authored_uri.data(),
                    resource.authored_uri.size());
      resource.authored_uri = {reinterpret_cast<const char*>(uri.data.get()),
                               uri.size};
      copy->test_resource_storage.push_back(std::move(uri));
      ByteBlock bytes;
      bytes.Allocate(resource.bytes.size(), allocation);
      if (!resource.bytes.empty())
        std::memcpy(bytes.data.get(), resource.bytes.data(),
                    resource.bytes.size());
      resource.bytes = bytes.View();
      copy->test_resource_storage.push_back(std::move(bytes));
    }
  }
  return Access::Make(std::move(copy));
}
#endif

namespace {
struct UIntText {
  char text[kUnsignedDecimalCapacity];
  std::size_t size = 0;
  void Set(std::uint64_t value) {
    const auto result =
        std::to_chars(text, text + kUnsignedDecimalCapacity, value);
    if (result.ec != std::errc{})
      Reject(Status::kLimitExceeded, Stage::kLexicalPlanning,
             Code::kOutputExtent);
    size = static_cast<std::size_t>(result.ptr - text);
  }
  std::string_view View() const noexcept { return {text, size}; }
};
struct Splice {
  Splice(std::size_t begin, std::size_t end,
         const AllocationContext& allocation)
      : begin(begin),
        end(end),
        fragments(Allocator<std::string_view>(allocation)) {}
  std::size_t begin;
  std::size_t end;
  Vector<std::string_view> fragments;
};
struct CopySegment {
  std::size_t output_begin;
  std::span<const std::byte> bytes;
};
struct Plan {
  explicit Plan(const AllocationContext& allocation)
      : splices(Allocator<Splice>(allocation)),
        segments(Allocator<CopySegment>(allocation)) {}
  // Stable inline decimal owners: fragment views never point into a moving
  // container or a temporary conversion buffer.
  UIntText buffer_ordinal;
  UIntText view_ordinal;
  UIntText accessor_ordinal;
  UIntText byte_length;
  UIntText corner_count;
  Vector<Splice> splices;
  Vector<CopySegment> segments;
  std::size_t output_size = 0;
};
Splice& At(Plan& plan, std::size_t begin, std::size_t end,
           const AllocationContext& allocation) {
  for (auto& splice : plan.splices)
    if (splice.begin == begin && splice.end == end) return splice;
  plan.splices.emplace_back(begin, end, allocation);
  return plan.splices.back();
}
void Append(Splice& splice, std::initializer_list<std::string_view> fragments) {
  splice.fragments.insert(splice.fragments.end(), fragments.begin(),
                          fragments.end());
}
std::size_t TableCount(const gltf_detail::JsonSnapshot& document,
                       std::optional<std::size_t> table,
                       const AllocationContext& allocation) {
  if (!table) return 0;
  if (document.Kind(*table) != gltf_detail::JsonKind::kArray)
    Reject(Status::kSourceContractFailure, Stage::kLexicalPlanning,
           Code::kLexicalSource);
  std::size_t count = 0;
  for (auto child = document.FirstChild(*table); child;
       child = document.NextSibling(*child)) {
    allocation.Checkpoint();
    count = Add(count, 1, Stage::kLexicalPlanning);
  }
  return count;
}
void TableRecord(Plan& plan, const gltf_detail::JsonSnapshot& document,
                 std::span<const LexicalSpan> spans,
                 std::optional<std::size_t> table, std::size_t old_count,
                 std::string_view member,
                 std::initializer_list<std::string_view> record,
                 const AllocationContext& allocation) {
  const auto insertion = table ? spans[*table].closing : spans.front().closing;
  auto& splice = At(plan, insertion, insertion, allocation);
  if (table) {
    if (old_count) Append(splice, {kComma});
  } else {
    if (document.FirstChild(0) || !splice.fragments.empty())
      Append(splice, {kComma});
    Append(splice, {member});
  }
  Append(splice, record);
  if (!table) Append(splice, {kArrayEnd});
}
void Segments(Plan& plan, std::string_view original,
              const AllocationContext& allocation) {
  std::sort(plan.splices.begin(), plan.splices.end(),
            [](const Splice& a, const Splice& b) { return a.begin < b.begin; });
  auto add = [&](std::span<const std::byte> bytes) {
    if (bytes.empty()) return;
    plan.segments.push_back({plan.output_size, bytes});
    plan.output_size =
        Add(plan.output_size, bytes.size(), Stage::kLexicalPlanning);
  };
  std::size_t previous = 0;
  for (const auto& splice : plan.splices) {
    allocation.Checkpoint();
    if (splice.begin < previous || splice.end < splice.begin ||
        splice.end > original.size())
      Reject(Status::kSourceContractFailure, Stage::kLexicalPlanning,
             Code::kLexicalSource);
    add(TextBytes(original.substr(previous, splice.begin - previous)));
    for (const auto fragment : splice.fragments) add(TextBytes(fragment));
    previous = splice.end;
  }
  add(TextBytes(original.substr(previous)));
}
void MakePlan(Plan& plan, const GltfTopologyWriteRequest& request,
              const gltf_detail::AssetState& asset,
              std::span<const LexicalSpan> spans, BundleState& output,
              const AllocationContext& allocation) {
  const auto& document = asset.document;
  if (document.Kind(0) != gltf_detail::JsonKind::kObject)
    Reject(Status::kSourceContractFailure, Stage::kLexicalPlanning,
           Code::kLexicalSource);
  const auto buffers = gltf_detail::FindMember(document, 0, kBuffersKey);
  const auto views = gltf_detail::FindMember(document, 0, kViewsKey);
  const auto accessors = gltf_detail::FindMember(document, 0, kAccessorsKey);
  const auto buffer_count = TableCount(document, buffers, allocation);
  const auto view_count = TableCount(document, views, allocation);
  const auto accessor_count = TableCount(document, accessors, allocation);
  const auto* primitive = gltf_detail::Access::PrimitiveRecordFor(
      request.source.SourceAsset(), request.source.SourcePrimitive());
  if (!primitive || primitive->json_node >= spans.size() ||
      document.Kind(primitive->json_node) != gltf_detail::JsonKind::kObject)
    Reject(Status::kSourceContractFailure, Stage::kLexicalPlanning,
           Code::kSourceProvenance);
  const auto indices =
      gltf_detail::FindMember(document, primitive->json_node, kIndicesKey);
  const auto mode =
      gltf_detail::FindMember(document, primitive->json_node, kModeKey);
  const auto source_indices = request.source.SourceExtraction().SourceIndices();
  if (indices.has_value() != source_indices.has_value() ||
      (indices && gltf_detail::ReadExactUnsigned(document, *indices) !=
                      source_indices->accessor_index) ||
      (mode ? gltf_detail::ReadExactUnsigned(document, *mode)
            : kTrianglesMode) != request.source.SourceExtraction().SourceMode())
    Reject(Status::kSourceContractFailure, Stage::kLexicalPlanning,
           Code::kSourceProvenance);
  plan.buffer_ordinal.Set(buffer_count);
  plan.view_ordinal.Set(view_count);
  plan.accessor_ordinal.Set(accessor_count);
  plan.byte_length.Set(output.indices.size);
  plan.corner_count.Set(request.source_corners.size());
  const auto uri = std::string_view(
      reinterpret_cast<const char*>(output.uri.data.get()), output.uri.size);
  TableRecord(
      plan, document, spans, buffers, buffer_count, kBuffersMember,
      {kBufferStart, plan.byte_length.View(), kBufferUri, uri, kBufferEnd},
      allocation);
  TableRecord(plan, document, spans, views, view_count, kViewsMember,
              {kViewStart, plan.buffer_ordinal.View(), kViewLength,
               plan.byte_length.View(), kObjectEnd},
              allocation);
  TableRecord(plan, document, spans, accessors, accessor_count,
              kAccessorsMember,
              {kAccessorStart, plan.view_ordinal.View(), kAccessorCount,
               plan.corner_count.View(), kAccessorEnd},
              allocation);
  if (indices) {
    auto& splice =
        At(plan, spans[*indices].begin, spans[*indices].end, allocation);
    Append(splice, {plan.accessor_ordinal.View()});
  } else {
    auto& splice = At(plan, spans[primitive->json_node].closing,
                      spans[primitive->json_node].closing, allocation);
    if (document.FirstChild(primitive->json_node)) Append(splice, {kComma});
    Append(splice, {kIndicesMember, plan.accessor_ordinal.View()});
  }
  if (request.source.SourceExtraction().SourceMode() != kTrianglesMode) {
    if (mode) {
      auto& splice = At(plan, spans[*mode].begin, spans[*mode].end, allocation);
      Append(splice, {kTrianglesToken});
    } else {
      auto& splice = At(plan, spans[primitive->json_node].closing,
                        spans[primitive->json_node].closing, allocation);
      if (document.FirstChild(primitive->json_node) ||
          !splice.fragments.empty())
        Append(splice, {kComma});
      Append(splice, {kModeMember});
    }
  }
  output.change = GltfTopologyBindingChange{
      request.source.SourceExtraction().SourceMode(),
      source_indices ? std::optional(source_indices->accessor_index)
                     : std::nullopt,
      kTrianglesMode,
      accessor_count,
      buffer_count,
      view_count};
  output.resources.push_back({GltfResourceKind::kBuffer,
                              buffer_count,
                              GltfResourceOrigin::kDecodedDataUri,
                              uri,
                              output.indices.View(),
                              output.indices.size,
                              buffer_count,
                              {}});
  Segments(plan, document.Original(), allocation);
}
void EncodeIndices(const GltfTopologyWriteRequest& request, BundleState& output,
                   Session& session) {
  const auto source = request.source.SourceExtraction().TriangleIndices();
  auto encode = [&](std::size_t begin, std::size_t end) {
    for (auto i = begin; i < end; ++i) {
      session.Check(i - begin, kIndexBytes);
      const auto value =
          source[static_cast<std::size_t>(request.source_corners[i])];
      for (std::size_t byte = 0; byte < kIndexBytes; ++byte)
        output.indices.data.get()[i * kIndexBytes + byte] =
            static_cast<std::byte>((value >> (byte * kBitsPerByte)) &
                                   kByteMask);
    }
  };
  session.Run(request.source_corners.size(), kIndexBytes, encode,
              session.execution.index_encoding);
}
void EncodeBase64(BundleState& output, Session& session) {
  const auto input = output.indices.View();
  std::memcpy(output.uri.data.get(), kDataUriPrefix.data(),
              kDataUriPrefix.size());
  const auto groups = input.size() / kBase64InputGroup +
                      (input.size() % kBase64InputGroup != 0 ? 1 : 0);
  auto encode = [&](std::size_t begin, std::size_t end) {
    auto* destination =
        reinterpret_cast<char*>(output.uri.data.get()) + kDataUriPrefix.size();
    for (auto group = begin; group < end; ++group) {
      session.Check(group - begin, kBase64InputGroup);
      const auto offset = group * kBase64InputGroup;
      const auto count = std::min(kBase64InputGroup, input.size() - offset);
      const auto first = std::to_integer<unsigned>(input[offset]);
      const auto second =
          count > 1 ? std::to_integer<unsigned>(input[offset + 1]) : 0;
      const auto third =
          count > 2 ? std::to_integer<unsigned>(input[offset + 2]) : 0;
      constexpr std::size_t kFirstSextetShift = 2;
      constexpr std::size_t kSecondTailBits = 4;
      constexpr std::size_t kThirdHeadBits = 4;
      constexpr std::size_t kThirdTailBits = 2;
      constexpr std::size_t kFourthHeadBits = 6;
      constexpr unsigned kFirstTailMask = 3;
      constexpr unsigned kSecondTailMask = 15;
      const auto result = group * kBase64OutputGroup;
      destination[result] = kBase64Alphabet[first >> kFirstSextetShift];
      destination[result + 1] =
          kBase64Alphabet[((first & kFirstTailMask) << kSecondTailBits) |
                          (second >> kThirdHeadBits)];
      destination[result + 2] =
          count > 1
              ? kBase64Alphabet[((second & kSecondTailMask) << kThirdTailBits) |
                                (third >> kFourthHeadBits)]
              : kBase64Padding;
      destination[result + 3] = count > 2
                                    ? kBase64Alphabet[third & kBase64SextetMask]
                                    : kBase64Padding;
    }
  };
  session.Run(groups, kBase64InputGroup, encode,
              session.execution.base64_encoding);
}
void CopyOutput(const Plan& plan, BundleState& output, Session& session) {
  auto copy = [&](std::size_t begin, std::size_t end) {
    auto segment =
        std::upper_bound(plan.segments.begin(), plan.segments.end(), begin,
                         [](std::size_t value, const CopySegment& part) {
                           return value < part.output_begin;
                         });
    if (segment != plan.segments.begin()) --segment;
    while (begin < end) {
      session.allocation.Checkpoint();
      if (segment == plan.segments.end())
        Reject(Status::kSourceContractFailure, Stage::kOutputCopy,
               Code::kOutputExtent);
      const auto offset = begin - segment->output_begin;
      const auto bytes = std::min(
          {end - begin, segment->bytes.size() - offset,
           static_cast<std::size_t>(session.work.cancellation_block_bytes)});
      std::memcpy(output.json.data.get() + begin,
                  segment->bytes.data() + offset, bytes);
      begin += bytes;
      if (offset + bytes == segment->bytes.size()) ++segment;
    }
  };
  session.Run(output.json.size, 1, copy, session.execution.output_copy);
}
void CopyMaps(const GltfTopologyWriteRequest& request, BundleState& output,
              Session& session) {
  output.faces.resize(request.source_faces.size());
  output.corners.resize(request.source_corners.size());
  output.original_corners.resize(request.source_corners.size());
  auto faces = [&](std::size_t begin, std::size_t end) {
    for (auto i = begin; i < end; ++i) {
      session.Check(i - begin, sizeof(index_t));
      output.faces[i] = request.source_faces[i];
    }
  };
  session.Run(output.faces.size(), sizeof(index_t), faces,
              session.execution.output_copy);
  const auto provenance = request.source.SourceExtraction().SourceCorners();
  auto corners = [&](std::size_t begin, std::size_t end) {
    for (auto i = begin; i < end; ++i) {
      session.Check(i - begin, sizeof(index_t) + sizeof(GltfSourceCorner));
      output.corners[i] = request.source_corners[i];
      output.original_corners[i] =
          provenance[static_cast<std::size_t>(request.source_corners[i])];
    }
  };
  session.Run(output.corners.size(), sizeof(index_t) + sizeof(GltfSourceCorner),
              corners, session.execution.output_copy);
}
}  // namespace

// Per-call allocation/backend seam stays in the private owner, with no change
// to the public reader or unchanged writer. Production always uses defaults.
GltfTopologyWriteResult WriteWithBackend(
    const GltfTopologyWriteRequest& request,
    const GltfTopologyWriteLimits& limits,
    const GltfTopologyWriteWorkPolicy& work, std::stop_token stop,
    const gltf_detail::AllocationBackend& allocation_backend,
    const NativeWorkerBackend& worker_backend) {
  GltfTopologyWriteResult result;
  auto stage = Stage::kAdmission;
  try {
    Configuration(limits, work);
    const bool translated =
        request.source.SourceExtraction().ProcessingProfile() ==
        GltfProcessingProfile::kDirectTranslationTrianglesV1;
    if (translated) {
      try {
        gltf_detail::ValidateDirectTranslationConfiguration(
            limits.direct_translation, work.direct_translation);
      } catch (const gltf_detail::GltfFailure& failure) {
        Reject(Status::kInvalidConfiguration, Stage::kAdmission,
               failure.Diagnostic().code == GltfDiagnosticCode::kZeroLimit
                   ? Code::kRequiredFieldZero
                   : Code::kConfigurationExtent);
      }
    }
    const auto context = request.source.Mesh().ShareExecutionContext();
    AllocationContext root(context, stop, allocation_backend);
    const auto& asset =
        gltf_detail::Access::State(request.source.SourceAsset());
    PhysicalCounts(request, limits, asset, root);
    if (!geometry::SharesExecutionBudget(
            context, request.candidate.Source().ShareExecutionContext()) ||
        !geometry::SharesExecutionBudget(context, asset.context))
      Reject(Status::kForeignBudget, stage, Code::kForeignBudget);
    if (!request.candidate.IsStorageValid()) {
      GltfTopologyWriteDiagnostic diagnostic;
      diagnostic.code = Code::kCandidateStorage;
      if (!request.candidate.Issues().empty())
        diagnostic.storage_issue = request.candidate.Issues().front();
      throw Failure(Status::kInvalidCandidateStorage, diagnostic);
    }
    // Reader metadata leaves binary admission pending. The retained successful
    // extraction has already completed that proof without mutating its asset.
    const auto capability = request.source.SourceAsset()
                                .Capability(request.source.SourcePrimitive())
                                .reason;
    if (!translated && capability != GltfCapabilityReason::kReady &&
        capability != GltfCapabilityReason::kBinaryAdmissionPending)
      Reject(Status::kUnsupportedProfile, stage, Code::kSourceCapability);
    const auto* proof = gltf_detail::Access::ExtractionTranslation(
        request.source.SourceExtraction());
    const auto* primitive = gltf_detail::Access::PrimitiveRecordFor(
        request.source.SourceAsset(), request.source.SourcePrimitive());
    if (translated &&
        (!proof || !proof->facts.complete ||
         proof->facts.status != GltfStatus::kAccepted ||
         proof->asset !=
             gltf_detail::Access::Owner(request.source.SourceAsset()) ||
         !primitive || proof->primitive.mesh_index != primitive->mesh_index ||
         proof->primitive.primitive_index != primitive->primitive_index))
      Reject(Status::kSourceContractFailure, stage, Code::kSourceProvenance);
    const auto source = request.source.Mesh().View();
    const auto candidate = request.candidate.Source().View();
    const auto candidate_faces = candidate.face_offsets.size() - 1;
    const auto source_faces = source.face_offsets.size() - 1;
    if (request.source_faces.size() != candidate_faces)
      Reject(Status::kInvalidCorrespondence, stage, Code::kFaceMapShape);
    if (request.source_corners.size() != candidate.corner_vertices.size())
      Reject(Status::kInvalidCorrespondence, stage, Code::kCornerMapShape);
    root.Checkpoint();
    auto allocation = root.WithRequestLimit(
        static_cast<std::size_t>(limits.output.max_output_requested_bytes));
    Session session{context, allocation,     work,
                    stop,    worker_backend, result.execution};
    stage = Stage::kVerification;
    const auto bad_face = session.Find(
        request.source_faces.size(), sizeof(index_t), [&](std::size_t i) {
          return request.source_faces[i] >= source_faces ||
                 (i && request.source_faces[i] <= request.source_faces[i - 1]);
        });
    if (bad_face)
      Reject(Status::kInvalidCorrespondence, stage,
             request.source_faces[*bad_face] >= source_faces
                 ? Code::kFaceMapRange
                 : Code::kFaceMapOrder,
             {}, static_cast<index_t>(*bad_face));
    VerifyAttributes(source, candidate, session);
    VerifyTopology(request, source, candidate, session);
    const bool changed = candidate_faces != source_faces;
    if (changed && candidate_faces == 0)
      Reject(Status::kUnsupportedSelection, stage,
             Code::kEmptyChangedSelection);
    if (translated) {
      const auto cap =
          static_cast<std::size_t>(limits.output.max_output_requested_bytes);
      const auto live = allocation.OperationActiveBytes();
      if (live >= cap)
        Reject(Status::kLimitExceeded, stage, Code::kWriterRequestLimit);
      const gltf_detail::NativeExecutionOptions validation_execution{
          worker_backend, nullptr};
      // No ordinary growth occurs until this independent transient scope dies.
      auto validation = gltf_detail::ValidateDirectTranslation(
          gltf_detail::Access::Owner(request.source.SourceAsset()), *primitive,
          limits.direct_translation, work.direct_translation, root, cap - live,
          &validation_execution, proof);
      result.direct_translation = validation.observation;
      if (!validation.proof) TranslationFailure(validation, stage);
    }
    stage = Stage::kLexicalPlanning;
    const auto index_bytes =
        changed ? Multiply(candidate.corner_vertices.size(), kIndexBytes, stage)
                : 0;
    const auto base64_groups = index_bytes / kBase64InputGroup +
                               (index_bytes % kBase64InputGroup != 0 ? 1 : 0);
    const auto uri_bytes =
        changed ? Add(kDataUriPrefix.size(),
                      Multiply(base64_groups, kBase64OutputGroup, stage), stage)
                : 0;
    // Lexical ownership is independent even for exact no-op output.
    const auto spans =
        BuildLexicalIndex(asset.document, allocation, limits, work);
    auto output = std::allocate_shared<BundleState>(
        Allocator<BundleState>(allocation), request.source, allocation);
    output->direct_translation = result.direct_translation;
    output->resources.assign(asset.resources.begin(), asset.resources.end());
    Plan plan(allocation);
    if (changed) {
      output->indices.Allocate(index_bytes, allocation);
      output->uri.Allocate(uri_bytes, allocation);
      stage = Stage::kIndexEncoding;
      EncodeIndices(request, *output, session);
      stage = Stage::kBase64Encoding;
      EncodeBase64(*output, session);
      stage = Stage::kLexicalPlanning;
      MakePlan(plan, request, asset, spans, *output, allocation);
    } else {
      Segments(plan, asset.document.Original(), allocation);
    }
    Bound(plan.output_size, limits.output.max_json_bytes, stage);
    output->json.Allocate(plan.output_size, allocation);
    stage = Stage::kOutputCopy;
    CopyMaps(request, *output, session);
    CopyOutput(plan, *output, session);
    stage = Stage::kPublication;
    allocation.Checkpoint();
    result.status = Status::kSuccess;
    result.diagnostic.stage = stage;
    result.bundle = Access::Make(std::move(output));
  } catch (const Failure& failure) {
    result.status = failure.Status();
    result.diagnostic = failure.Diagnostic();
  } catch (const AdmissionFailure& failure) {
    result.diagnostic.stage = stage;
    switch (failure.Code()) {
      case AdmissionCode::kSizeLimit:
        result.status = Status::kLimitExceeded;
        result.diagnostic.code = Code::kWriterRequestLimit;
        break;
      case AdmissionCode::kBudgetExceeded:
        result.status = Status::kBudgetExceeded;
        result.diagnostic.code = Code::kRequestBudget;
        break;
      case AdmissionCode::kCanceled:
        result.status = Status::kCanceled;
        result.diagnostic.code = Code::kStopRequested;
        break;
      case AdmissionCode::kInvalidObject:
        throw;
    }
  }
  return result;
}

}  // namespace meshvale::interchange::gltf_topology_write_detail

namespace meshvale::interchange {
GltfTopologyWriteBundle::GltfTopologyWriteBundle(
    std::shared_ptr<const gltf_topology_write_detail::BundleState>
        owner) noexcept
    : owner_(std::move(owner)) {}
GltfTopologyWriteBundle::GltfTopologyWriteBundle(
    const GltfTopologyWriteBundle&) noexcept = default;
GltfTopologyWriteBundle& GltfTopologyWriteBundle::operator=(
    const GltfTopologyWriteBundle&) noexcept = default;
GltfTopologyWriteBundle::~GltfTopologyWriteBundle() noexcept = default;
std::span<const std::byte> GltfTopologyWriteBundle::Json() const noexcept {
  return owner_->json.View();
}
std::span<const GltfResourceView> GltfTopologyWriteBundle::Resources()
    const noexcept {
  return owner_->resources;
}
const GltfMaterializedPrimitive& GltfTopologyWriteBundle::Source()
    const noexcept {
  return owner_->source;
}
const GltfAsset& GltfTopologyWriteBundle::OriginalAsset() const noexcept {
  return owner_->source.SourceAsset();
}
const GltfPrimitiveHandle& GltfTopologyWriteBundle::OriginalPrimitive()
    const noexcept {
  return owner_->source.SourcePrimitive();
}
std::span<const geometry::index_t> GltfTopologyWriteBundle::SourceFaces()
    const noexcept {
  return owner_->faces;
}
std::span<const geometry::index_t> GltfTopologyWriteBundle::SourceCorners()
    const noexcept {
  return owner_->corners;
}
std::span<const GltfSourceCorner> GltfTopologyWriteBundle::OriginalCorners()
    const noexcept {
  return owner_->original_corners;
}
std::optional<GltfTopologyBindingChange>
GltfTopologyWriteBundle::ChangedBinding() const noexcept {
  return owner_->change;
}
const GltfDirectTranslationObservation*
GltfTopologyWriteBundle::DirectTranslationFacts() const noexcept {
  return owner_->direct_translation ? &*owner_->direct_translation : nullptr;
}
std::uint64_t GltfTopologyWriteBundle::NewRequestedBytes() const noexcept {
  return owner_->allocation.OperationActiveBytes();
}
GltfTopologyWriteResult WriteGltfTopologySelection(
    const GltfTopologyWriteRequest& request,
    const GltfTopologyWriteLimits& limits,
    const GltfTopologyWriteWorkPolicy& work, std::stop_token stop) {
  return gltf_topology_write_detail::WriteWithBackend(
      request, limits, work, stop, gltf_detail::DefaultAllocationBackend(),
      gltf_topology_write_detail::DefaultNativeWorkerBackend());
}
}  // namespace meshvale::interchange
