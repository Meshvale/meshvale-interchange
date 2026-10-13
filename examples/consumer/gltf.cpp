// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace {
namespace mv = meshvale::interchange;
void Require(bool value,
             std::source_location source = std::source_location::current()) {
  if (!value)
    throw std::runtime_error(
        "installed glTF consumer assertion failed at line " +
        std::to_string(source.line()));
}
mv::GltfReadLimits ReadLimits() {
  // Explicit caller policy for this finite fixture, not library defaults.
  return {65536, 4096, 131072, 4096, 32,   65536, 128,
          128,   128,  1024,   1024, 1024, 1024,  131072};
}
mv::GltfExtractionLimits ExtractionLimits() { return {4096, 4096, 1048576}; }
mv::GltfPrimitiveExtraction CaptureAndExtract(
    const meshvale::geometry::ExecutionContext& context) {
  std::string json =
      R"({"asset":{"version":"2.0"},"extras":{"integer":9007199254740993,"signedZero":-0.0},"buffers":[{"uri":"memory.bin","byteLength":28}],"bufferViews":[{"buffer":0,"byteLength":16,"byteStride":4},{"buffer":0,"byteOffset":16,"byteLength":2},{"buffer":0,"byteOffset":20,"byteLength":4},{"buffer":0,"byteOffset":24,"byteLength":4}],"accessors":[{"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[0,0,0]},{"componentType":5120,"type":"SCALAR","count":4,"bufferView":0,"normalized":true,"min":[-128],"max":[127]},{"componentType":5121,"type":"VEC2","count":4,"normalized":true,"sparse":{"count":2,"indices":{"bufferView":1,"componentType":5121},"values":{"bufferView":2}},"min":[0,0],"max":[255,254]},{"componentType":5121,"type":"SCALAR","count":4,"bufferView":3},{"componentType":5121,"type":"SCALAR","count":3,"bufferView":3}],"meshes":[{"primitives":[{"mode":5,"indices":3,"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2}}]},{"primitives":[{"indices":4,"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0,"translation":[1,2,3]},{"mesh":0,"name":"\u0394"},{"mesh":1,"scale":[2,2,2]}],"scenes":[{"nodes":[0,1]},{"nodes":[0]}]})";
  std::array<std::byte, 31> bytes{};
  bytes[0] = std::byte{128};
  bytes[4] = std::byte{127};
  bytes[8] = std::byte{255};
  bytes[16] = std::byte{1};
  bytes[17] = std::byte{3};
  bytes[20] = std::byte{255};
  bytes[21] = std::byte{0};
  bytes[22] = std::byte{1};
  bytes[23] = std::byte{254};
  bytes[24] = std::byte{0};
  bytes[25] = std::byte{1};
  bytes[26] = std::byte{1};
  bytes[27] = std::byte{3};
  bytes[28] = std::byte{42};
  bytes[29] = std::byte{43};
  bytes[30] = std::byte{44};
  const std::array<mv::SuppliedGltfResource, 1> bindings{
      mv::SuppliedGltfResource{mv::GltfResourceKind::kBuffer, 0, bytes}};
  std::stop_source capture_stop;
  auto read = mv::ReadGltfAsset(
      {reinterpret_cast<const std::byte*>(json.data()), json.size()}, bindings,
      ReadLimits(), {1, 4, 1, 1}, context, capture_stop.get_token());
  Require(read.asset && read.diagnostic.status == mv::GltfStatus::kAccepted);
  const auto source_charge = context.ActiveTrackedPayload();
  const auto source_requests = read.asset->NewRequestedBytes();
  auto context_copy = context;
  auto moved_context = std::move(context_copy);
  meshvale::geometry::ExecutionContext foreign_context;
  const auto asset_copy = *read.asset;
  Require(asset_copy.SharesExecutionBudget(context) &&
          asset_copy.SharesExecutionBudget(moved_context) &&
          !asset_copy.SharesExecutionBudget(context_copy) &&
          !asset_copy.SharesExecutionBudget(foreign_context));
  Require(context.ActiveTrackedPayload() == source_charge &&
          read.asset->NewRequestedBytes() == source_requests);
  Require(read.asset->PrimitiveCount() == 2 &&
          read.asset->InstanceCount() == 3);
  Require(read.asset->Nodes().size() == 3 && read.asset->Scenes().size() == 2);
  Require(read.asset->Nodes()[1].name == "\xce\x94");
  Require(read.asset->Resources()[0].bytes.size() == bytes.size());
  Require(read.asset->Resources()[0].logical_bytes == 28);
  Require(read.asset->Resources()[0].bytes[30] == std::byte{44});
  Require(read.asset->Capability(*read.asset->Primitive(0)).reason ==
          mv::GltfCapabilityReason::kBinaryAdmissionPending);
  bool exact_integer = false, signed_zero = false;
  for (const auto& number : read.asset->RawNumbers()) {
    exact_integer |= number.lexeme == "9007199254740993";
    signed_zero |= number.lexeme == "-0.0";
  }
  Require(exact_integer && signed_zero);
  // Extraction has an independent operation token on the retained shared
  // ledger.
  capture_stop.request_stop();
  const mv::GltfExtractionRequest request{
      mv::GltfTriangleRequest::kConvertStripOrFan,
      mv::GltfValueRequest::kRawAndNormalized};
  const auto captured_charge = context.ActiveTrackedPayload();
  auto tiny_limits = ExtractionLimits();
  tiny_limits.max_output_requested_bytes = 1;
  auto limited = mv::ExtractGltfPrimitive(
      *read.asset, *read.asset->Primitive(0), request, tiny_limits, {});
  Require(!limited.extraction &&
          limited.diagnostic.status == mv::GltfStatus::kUnsupportedProfile);
  Require(context.ActiveTrackedPayload() == captured_charge &&
          context.ActiveWorkers() == 0);
  std::stop_source extraction_stop;
  extraction_stop.request_stop();
  auto canceled =
      mv::ExtractGltfPrimitive(*read.asset, *read.asset->Primitive(0), request,
                               ExtractionLimits(), extraction_stop.get_token());
  Require(!canceled.extraction &&
          canceled.diagnostic.status == mv::GltfStatus::kCanceled);
  Require(context.ActiveTrackedPayload() == captured_charge &&
          context.ActiveWorkers() == 0);
  auto extracted = mv::ExtractGltfPrimitive(
      *read.asset, *read.asset->Primitive(0), request, ExtractionLimits(), {});
  Require(extracted.extraction &&
          extracted.diagnostic.status == mv::GltfStatus::kAccepted);
  std::fill(bytes.begin(), bytes.end(), std::byte{99});
  std::fill(json.begin(), json.end(), ' ');
  return *extracted.extraction;
}
void Inspect(const mv::GltfPrimitiveExtraction& result) {
  const std::array<std::uint32_t, 6> triangles{0, 1, 1, 1, 1, 3};
  const std::array<std::uint64_t, 6> stream{0, 1, 2, 2, 1, 3};
  Require(std::ranges::equal(result.TriangleIndices(), triangles));
  for (std::size_t i = 0; i < stream.size(); ++i) {
    Require(result.SourceCorners()[i].primitive_stream_corner == stream[i]);
    Require(result.SourceCorners()[i].accessor_row == triangles[i]);
  }
  Require(result.Channels().size() == 3 && result.SourceMode() == 5);
  const auto& signed_channel = result.Channels()[1];
  const auto raw = std::get<std::span<const std::int8_t>>(signed_channel.raw);
  Require(raw[0] == -128 && raw[1] == 127 && raw[2] == -1 && raw[3] == 0);
  Require(std::get<std::span<const std::int8_t>>(
              *signed_channel.exact_min)[0] == -128);
  Require(signed_channel.normalized_values[0] == -1 &&
          signed_channel.normalized_values[1] == 1);
  const auto& uv = result.Channels()[2];
  Require(uv.set_index == 0 && uv.sparse_source.has_value());
  Require(uv.packed_sparse_step == 2 && uv.sparse_destinations.size() == 2);
  Require(uv.sparse_destinations[0] == 1 && uv.sparse_destinations[1] == 3);
  const auto uv_raw = std::get<std::span<const std::uint8_t>>(uv.raw);
  const std::array<std::uint8_t, 8> expected_uv{0, 0, 255, 0, 0, 0, 1, 254};
  Require(std::ranges::equal(uv_raw, expected_uv));
  Require(uv.normalized_values[2] == 1 &&
          uv.normalized_values[7] == 254.0 / 255.0);
}
}  // namespace
int main() {
  try {
    std::optional<mv::GltfPrimitiveExtraction> retained;
    {
      meshvale::geometry::ExecutionContext context({4, 1, 10485760});
      retained = CaptureAndExtract(context);
      Require(context.ActiveTrackedPayload() > 0 &&
              context.ActiveWorkers() == 0);
      Inspect(*retained);
    }
    Inspect(*retained);  // Visible input, asset and caller context are gone.
    retained.reset();
    std::cout << "installed immutable glTF read and typed extraction passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
