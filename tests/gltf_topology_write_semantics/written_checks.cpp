// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "fixture_support.h"

namespace gltf_topology_write_semantics {
namespace {
constexpr std::uint64_t kMebibyte = 1024 * 1024;
constexpr std::uint64_t kCountLimit = 4096;
constexpr std::uint64_t kLargeVertexLimit = 16777218;
constexpr std::uint64_t kLargeScalarLimit = 64 * 1024 * 1024;
constexpr std::uint64_t kChannelLimit = 32;
constexpr std::uint64_t kSourceDepthLimit = 64;
constexpr std::uint64_t kOutputResourceLimit = 32;
constexpr std::uint64_t kWriterRequestBytes = 16 * kMebibyte;
constexpr std::uint64_t kMinimumParallelBytes = 1;
constexpr std::uint64_t kSerialThresholdBytes =
    std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t kPartitionBytes = 1024;
constexpr std::uint64_t kCancellationBlockBytes = 64;
constexpr std::uint64_t kJobLimit = 32;
constexpr std::uint64_t kTriangleMode = 4;
constexpr std::size_t kTriangleWidth = 3;
constexpr std::size_t kSharedOldResourceCount = 4;
constexpr std::uint64_t kSharedNewBuffer = 2;
constexpr std::uint64_t kSharedNewView = 8;
constexpr std::uint64_t kSharedImageBuffer = 1;
constexpr std::uint64_t kSharedImageView = 7;
constexpr std::size_t kPngSignatureBytes = 8;
constexpr std::uint64_t kSharedNewAccessor = 7;
constexpr std::uint64_t kSharedOldIndexAccessor = 3;
constexpr std::size_t kSelectedIndexBytes = 12;
constexpr unsigned kUInt32Bits = 32;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xffU;
constexpr std::string_view kBufferUri = "memory.bin";
constexpr std::string_view kTriangleUri =
    "data:application/octet-stream;base64,AAAAAAIAAAACAAAA";
constexpr std::string_view kStripUri =
    "data:application/octet-stream;base64,AgAAAAEAAAAAAAAA";
constexpr std::string_view kFanUri =
    "data:application/octet-stream;base64,AwAAAAIAAAAAAAAA";
constexpr std::string_view kImplicitUri =
    "data:application/octet-stream;base64,AwAAAAQAAAAFAAAA";
constexpr std::string_view kLargeUri =
    "data:application/octet-stream;base64,AQAAAQAAAAABAAAA";

bool HasRawChannels(const Fixture& fixture) {
  return fixture.kind == FixtureKind::kTriangles ||
         fixture.kind == FixtureKind::kStrip ||
         fixture.kind == FixtureKind::kFan;
}
std::string_view ExpectedUri(FixtureKind kind) {
  switch (kind) {
    case FixtureKind::kTriangles:
      return kTriangleUri;
    case FixtureKind::kStrip:
      return kStripUri;
    case FixtureKind::kFan:
      return kFanUri;
    case FixtureKind::kImplicit:
      return kImplicitUri;
    case FixtureKind::kLargeIndex:
      return kLargeUri;
  }
  Require(false, "Unexpected wire fixture kind");
  return {};
}
std::vector<std::byte> ExpectedIndexBytes(const Fixture& fixture,
                                          geo::index_t face) {
  std::vector<std::byte> bytes;
  for (std::size_t i = 0; i < kTriangleWidth; ++i) {
    const auto value = fixture.source_triangles.at(face * kTriangleWidth + i);
    // Independent UINT32 little-endian expectation; never convert via float.
    for (unsigned shift = 0; shift < kUInt32Bits; shift += kByteBits)
      bytes.push_back(static_cast<std::byte>((value >> shift) & kByteMask));
  }
  return bytes;
}
void CheckResourceRetention(const Fixture& fixture,
                            const mv::GltfTopologyWriteBundle& bundle,
                            bool changed, geo::index_t face) {
  const auto original = bundle.OriginalAsset().Resources();
  const auto output = bundle.Resources();
  Require(output.size() == original.size() + (changed ? 1 : 0),
          "Writer changed more than one resource record");
  for (std::size_t i = 0; i < original.size(); ++i) {
    const auto& before = original[i];
    const auto& after = output[i];
    Require(after.kind == before.kind &&
                after.table_index == before.table_index &&
                after.origin == before.origin &&
                after.authored_uri == before.authored_uri &&
                after.logical_bytes == before.logical_bytes &&
                after.buffer_index == before.buffer_index &&
                after.buffer_view_index == before.buffer_view_index &&
                after.bytes.data() == before.bytes.data(),
            "Original resource identity, alias or encoding changed");
    RequireBits<std::byte>(after.bytes, before.bytes,
                           "Original resource bytes changed");
  }
  if (!fixture.external_buffer.empty()) {
    Require(
        original.front().kind == mv::GltfResourceKind::kBuffer &&
            original.front().table_index == 0 &&
            original.front().authored_uri == kBufferUri &&
            original.front().origin == mv::GltfResourceOrigin::kCallerBinding,
        "External original buffer binding changed");
    RequireBits<std::byte>(
        original.front().bytes, fixture.external_buffer,
        "Complete original bytes and trailing bytes changed");
  }
  if (HasRawChannels(fixture)) {
    Require(original.size() == kSharedOldResourceCount &&
                original[0].logical_bytes == 132 &&
                original[1].logical_bytes == kPngSignatureBytes &&
                original[2].bytes.size() == kPngSignatureBytes &&
                original[3].origin == mv::GltfResourceOrigin::kBufferView &&
                original[3].buffer_index == kSharedImageBuffer &&
                original[3].buffer_view_index == kSharedImageView &&
                original[3].bytes.data() == original[1].bytes.data() &&
                original[3].bytes.size() == kPngSignatureBytes,
            "Golden original resource inventory and image alias differ");
    constexpr std::array<std::byte, kPngSignatureBytes> kPng{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
    RequireBits<std::byte>(original[1].bytes, kPng,
                           "Original embedded buffer changed");
    RequireBits<std::byte>(original[2].bytes, kPng,
                           "Original embedded image changed");
    RequireBits<std::byte>(original[3].bytes, kPng,
                           "Original aliased image changed");
  }
  if (changed) {
    const auto expected = ExpectedIndexBytes(fixture, face);
    const auto& appended = output.back();
    Require(appended.kind == mv::GltfResourceKind::kBuffer &&
                appended.origin == mv::GltfResourceOrigin::kDecodedDataUri &&
                appended.logical_bytes == kSelectedIndexBytes &&
                appended.authored_uri == ExpectedUri(fixture.kind),
            "New buffer must use the golden canonical data URI");
    RequireBits<std::byte>(
        appended.bytes, expected,
        "Appended UINT32 bytes differ from literal topology");
  }
}
mv::GltfAsset Reload(const Fixture& fixture,
                     const mv::GltfTopologyWriteBundle& bundle) {
  std::vector<mv::SuppliedGltfResource> bindings;
  for (const auto& resource : bundle.Resources())
    if (resource.origin == mv::GltfResourceOrigin::kCallerBinding)
      bindings.push_back({resource.kind, resource.table_index, resource.bytes});
  const auto context = Context(fixture.kind == FixtureKind::kLargeIndex);
  const auto read =
      mv::ReadGltfAsset(bundle.Json(), bindings,
                        ReadLimits(fixture.kind == FixtureKind::kLargeIndex),
                        ReaderWork(), context, {});
  Require(
      read.asset && read.diagnostic.status == mv::GltfStatus::kAccepted,
      "Actual emitted JSON/resources must pass an independent public reload");
  return *read.asset;
}
}  // namespace

mv::GltfTopologyWriteLimits WriteLimits(bool large) {
  mv::GltfTopologyWriteLimits limits;
  limits.max_source_json_bytes = kMebibyte;
  limits.max_source_json_nodes = kCountLimit;
  limits.max_source_json_depth = kSourceDepthLimit;
  limits.max_vertices = large ? kLargeVertexLimit : kCountLimit;
  limits.max_channels = kChannelLimit;
  limits.max_attribute_scalars = large ? kLargeScalarLimit : kCountLimit;
  limits.max_descriptor_bytes = kMebibyte;
  limits.max_candidate_faces = kCountLimit;
  limits.max_candidate_corners = kCountLimit;
  limits.max_index_bytes = kMebibyte;
  limits.output = {kMebibyte, kOutputResourceLimit, kMebibyte, kMebibyte,
                   kWriterRequestBytes};
  return limits;
}
mv::GltfTopologyWriteWorkPolicy WriteWork(bool serial) {
  return {serial ? kSerialThresholdBytes : kMinimumParallelBytes,
          kPartitionBytes, kCancellationBlockBytes, kJobLimit};
}
void CheckWrittenBundle(const Fixture& fixture,
                        const mv::GltfTopologyWriteBundle& bundle, bool changed,
                        geo::index_t selected_face) {
  const auto text =
      std::string_view(reinterpret_cast<const char*>(bundle.Json().data()),
                       bundle.Json().size());
  for (const auto original : fixture.preserved_lexemes)
    Require(text.find(original) != std::string_view::npos,
            "Original lexical table/graph/opaque record was rewritten");
  Require(bundle.OriginalPrimitive().MeshIndex() == 0 &&
              bundle.OriginalPrimitive().PrimitiveIndex() ==
                  fixture.primitive_ordinal &&
              bundle.Source().SourcePrimitive().PrimitiveIndex() ==
                  fixture.primitive_ordinal,
          "Output retained a different original primitive");
  Require(std::string_view(reinterpret_cast<const char*>(
                               bundle.OriginalAsset().OriginalJson().data()),
                           bundle.OriginalAsset().OriginalJson().size()) ==
              fixture.json,
          "Retained original JSON identity changed");
  CheckSourceExtraction(fixture, bundle.Source().SourceExtraction());
  const auto change = bundle.ChangedBinding();
  Require(change.has_value() == changed,
          "No-op/conversion annotation disagrees with actual edit");
  if (!changed) {
    Require(text == fixture.json, "Identity selection must be byte-exact JSON");
    Require(
        bundle.Json().data() != bundle.OriginalAsset().OriginalJson().data(),
        "No-op must own fresh output JSON");
    constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
    constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
    RequireBits<geo::index_t>(bundle.SourceFaces(), kIdentityFaces,
                              "Identity face correspondence changed");
    RequireBits<geo::index_t>(bundle.SourceCorners(), kIdentityCorners,
                              "Identity corner correspondence changed");
  } else {
    CheckLexicalPreservation(fixture, bundle.Json());
    Require(change->source_mode == fixture.source_mode &&
                change->output_mode == kTriangleMode,
            "Conversion annotation flattened original mode");
    const auto expected_buffer = HasRawChannels(fixture) ? kSharedNewBuffer
                                 : fixture.kind == FixtureKind::kLargeIndex
                                     ? 1U
                                     : 0U;
    const auto expected_view = HasRawChannels(fixture) ? kSharedNewView
                               : fixture.kind == FixtureKind::kLargeIndex ? 1U
                                                                          : 0U;
    const auto expected_accessor = HasRawChannels(fixture) ? kSharedNewAccessor
                                   : fixture.kind == FixtureKind::kLargeIndex
                                       ? 2U
                                       : 1U;
    Require(change->appended_buffer == expected_buffer &&
                change->appended_buffer_view == expected_view &&
                change->output_indices_accessor == expected_accessor,
            "Writer renumbered old table identities");
    if (HasRawChannels(fixture))
      Require(change->source_indices_accessor == kSharedOldIndexAccessor,
              "Shared old index accessor was lost");
    else if (fixture.kind == FixtureKind::kImplicit)
      Require(!change->source_indices_accessor,
              "Implicit source indices were reported as authored accessor");
    else
      Require(change->source_indices_accessor == 1,
              "Original UINT32 accessor identity was lost");
    Require(bundle.SourceFaces().size() == 1 &&
                bundle.SourceFaces()[0] == selected_face &&
                bundle.SourceCorners().size() == kTriangleWidth,
            "Owned edited correspondence extent changed");
    for (std::size_t i = 0; i < kTriangleWidth; ++i)
      Require(bundle.SourceCorners()[i] == selected_face * kTriangleWidth + i,
              "Owned edited corner correspondence differs");
  }
  Require(bundle.OriginalCorners().size() == bundle.SourceCorners().size(),
          "Composed original provenance is incomplete");
  for (std::size_t i = 0; i < bundle.SourceCorners().size(); ++i) {
    const auto source_corner = bundle.SourceCorners()[i];
    Require(bundle.OriginalCorners()[i].primitive_stream_corner ==
                    fixture.stream_corners.at(source_corner) &&
                bundle.OriginalCorners()[i].accessor_row ==
                    fixture.source_triangles.at(source_corner),
            "Composed strip/fan/original accessor provenance changed");
  }
  CheckResourceRetention(fixture, bundle, changed, selected_face);
  const auto reloaded = Reload(fixture, bundle);
  RequireBits<std::byte>(reloaded.OriginalJson(), bundle.Json(),
                         "Reload must capture exact emitted JSON");
  const auto primitive = reloaded.Primitive(fixture.primitive_ordinal);
  Require(primitive.has_value(), "Reload lost selected primitive ordinal");
  const mv::GltfExtractionRequest request{
      changed ? mv::GltfTriangleRequest::kRequireTriangles
      : fixture.kind == FixtureKind::kStrip || fixture.kind == FixtureKind::kFan
          ? mv::GltfTriangleRequest::kConvertStripOrFan
          : mv::GltfTriangleRequest::kRequireTriangles,
      mv::GltfValueRequest::kRawAndNormalized};
  const auto extracted = mv::ExtractGltfPrimitive(
      reloaded, *primitive, request,
      ExtractionLimits(fixture.kind == FixtureKind::kLargeIndex), {});
  Require(extracted.extraction.has_value(),
          "Reloaded selected primitive must remain processable");
  if (!changed) {
    CheckSourceExtraction(fixture, *extracted.extraction);
  } else {
    const auto expected =
        std::span<const std::uint32_t>(fixture.source_triangles)
            .subspan(selected_face * kTriangleWidth, kTriangleWidth);
    RequireBits<std::uint32_t>(extracted.extraction->TriangleIndices(),
                               expected,
                               "Reloaded directed selected loop differs");
    const auto indices = extracted.extraction->SourceIndices();
    Require(
        indices && indices->accessor_index == change->output_indices_accessor &&
            indices->component_type == mv::GltfComponentType::kUint32 &&
            indices->shape == mv::GltfAccessorShape::kScalar &&
            !indices->normalized && indices->element_count == kTriangleWidth,
        "Reloaded appended index accessor is not exact SCALAR/UINT32");
    RequireBits<std::uint32_t>(
        std::get<std::span<const std::uint32_t>>(indices->raw), expected,
        "Reloaded original UINT32 index bytes differ");
    if (HasRawChannels(fixture)) CheckRawChannels(*extracted.extraction);
  }
  if (HasRawChannels(fixture)) {
    Require(
        reloaded.PrimitiveCount() == 3 && reloaded.InstanceCount() == 7 &&
            reloaded.Nodes().size() == 4 && reloaded.Scenes().size() == 2 &&
            reloaded.Nodes()[0].mesh == 0 && reloaded.Nodes()[1].mesh == 0 &&
            reloaded.Nodes()[2].mesh == 1 && reloaded.Nodes()[2].skin == 0 &&
            reloaded.Nodes()[0].name == "first\xCE\x94",
        "Unrelated graph or shared mesh instances changed");
    const auto untouched = Extract(reloaded, fixture, 0);
    CheckSourceExtraction(fixture, untouched);
    Require(
        untouched.SourceIndices()->accessor_index == kSharedOldIndexAccessor,
        "Unrelated primitive was rebound to edited indices");
    const auto unrelated = reloaded.Primitive(2);
    Require(unrelated && reloaded.Capability(*unrelated).reason !=
                             mv::GltfCapabilityReason::kReady,
            "Unrelated unsupported skin/morph/animation became processable");
  }
}
}  // namespace gltf_topology_write_semantics
