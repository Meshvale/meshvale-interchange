// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "gltf_asset_state.h"
#include "gltf_metadata.h"

namespace {
namespace mv = meshvale::interchange;
namespace detail = mv::gltf_detail;
void Require(bool value) {
  if (!value) throw std::runtime_error("extraction assertion failed");
}
mv::GltfAsset CaptureFixture(
    std::string_view json, std::span<const std::byte> resource,
    const meshvale::geometry::ExecutionContext& context) {
  detail::AllocationContext allocation(context, {});
  auto captured = detail::CaptureJsonSnapshot(
      json, {100000, 10000, 100000, 100}, allocation);
  Require(captured.snapshot.has_value());
  mv::GltfReadLimits limits{100000, 100000, 100000, 10000, 100,
                            100000, 100,    100,    100,   1000,
                            1000,   1000,   100000, 100000};
  auto state = std::allocate_shared<detail::AssetState>(
      detail::Allocator<detail::AssetState>(allocation), context,
      std::stop_token{}, limits, mv::GltfWorkPolicy{1, 1, 1, 1},
      std::move(*captured.snapshot));
  if (!resource.empty()) {
    state->resource_storage.emplace_back(
        detail::Allocator<std::byte>(allocation));
    state->resource_storage.back().assign(resource.begin(), resource.end());
    state->resources.push_back({mv::GltfResourceKind::kBuffer,
                                0,
                                mv::GltfResourceOrigin::kCallerBinding,
                                {},
                                state->resource_storage.back(),
                                resource.size(),
                                {},
                                {}});
  }
  const auto mesh = detail::ChildAt(
      state->document, *detail::FindMember(state->document, 0, "meshes"), 0);
  const auto primitive = detail::ChildAt(
      state->document,
      *detail::FindMember(state->document, *mesh, "primitives"), 0);
  state->primitives.push_back(
      {0, 0, *primitive, mv::GltfCapabilityReason::kBinaryAdmissionPending});
  return detail::Access::MakeAsset(std::move(state));
}
mv::GltfExtractionLimits Limits() { return {100000, 100000, 10000000}; }
void TopologyAndOwnership() {
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  std::optional<mv::GltfPrimitiveExtraction> retained;
  const std::string_view strip =
      R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4,"uri":"memory.bin"}],"bufferViews":[{"buffer":0,"byteLength":4}],"accessors":[{"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[0,0,0]},{"componentType":5121,"type":"SCALAR","count":4,"bufferView":0}],"meshes":[{"primitives":[{"mode":5,"indices":1,"attributes":{"POSITION":0}}]}]})";
  const std::vector<std::byte> bytes{std::byte{0}, std::byte{1}, std::byte{1},
                                     std::byte{2}};
  {
    auto asset = CaptureFixture(strip, bytes, context);
    auto other = CaptureFixture(strip, bytes, context);
    auto result =
        mv::ExtractGltfPrimitive(asset, *asset.Primitive(0),
                                 {mv::GltfTriangleRequest::kConvertStripOrFan,
                                  mv::GltfValueRequest::kRaw},
                                 Limits(), {});
    Require(result.extraction.has_value());
    const std::vector<std::uint32_t> expected{0, 1, 1, 1, 1, 2};
    Require(std::ranges::equal(result.extraction->TriangleIndices(), expected));
    const std::vector<std::uint64_t> stream{0, 1, 2, 2, 1, 3};
    for (std::size_t i = 0; i < stream.size(); ++i)
      Require(result.extraction->SourceCorners()[i].primitive_stream_corner ==
              stream[i]);
    Require(std::get<std::span<const std::uint8_t>>(
                result.extraction->SourceIndices()->raw)[2] == 1);
    auto foreign =
        mv::ExtractGltfPrimitive(asset, *other.Primitive(0),
                                 {mv::GltfTriangleRequest::kConvertStripOrFan,
                                  mv::GltfValueRequest::kRaw},
                                 Limits(), {});
    Require(foreign.diagnostic.status == mv::GltfStatus::kForeignOwner);
    auto wrong =
        mv::ExtractGltfPrimitive(asset, *asset.Primitive(0),
                                 {mv::GltfTriangleRequest::kRequireTriangles,
                                  mv::GltfValueRequest::kRaw},
                                 Limits(), {});
    Require(wrong.diagnostic.status == mv::GltfStatus::kUnsupportedProfile);
    auto tiny = Limits();
    tiny.max_output_requested_bytes = 1;
    const auto charge = context.ActiveTrackedPayload();
    auto limited =
        mv::ExtractGltfPrimitive(asset, *asset.Primitive(0),
                                 {mv::GltfTriangleRequest::kConvertStripOrFan,
                                  mv::GltfValueRequest::kRaw},
                                 tiny, {});
    Require(limited.diagnostic.status == mv::GltfStatus::kUnsupportedProfile);
    Require(context.ActiveTrackedPayload() == charge &&
            context.ActiveWorkers() == 0);
    retained = std::move(result.extraction);
  }
  Require(retained->Channels()[0].semantic == "POSITION");
  Require(retained->TriangleIndices()[5] == 2 &&
          context.ActiveTrackedPayload() > 0);
  retained.reset();
  Require(context.ActiveTrackedPayload() == 0);
  const std::string_view fan =
      R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"mode":6,"attributes":{"POSITION":0}}]}]})";
  auto asset = CaptureFixture(fan, {}, context);
  auto result = mv::ExtractGltfPrimitive(
      asset, *asset.Primitive(0),
      {mv::GltfTriangleRequest::kConvertStripOrFan, mv::GltfValueRequest::kRaw},
      Limits(), {});
  Require(result.extraction.has_value());
  const std::vector<std::uint32_t> expected{0, 1, 2, 0, 2, 3};
  Require(std::ranges::equal(result.extraction->TriangleIndices(), expected));
}
void TypedBoundsAndFailures() {
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  const std::string_view document =
      R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":16,"uri":"memory.bin"}],"bufferViews":[{"buffer":0,"byteLength":16,"byteStride":4}],"accessors":[{"componentType":5126,"type":"VEC3","count":3,"min":[0,0,0],"max":[0,0,0]},{"componentType":5120,"type":"SCALAR","count":3,"bufferView":0,"normalized":true,"min":[-128],"max":[127]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"_SIGNED":1}}]}]})";
  std::vector<std::byte> bytes(16);
  bytes[0] = std::byte{128};
  bytes[4] = std::byte{127};
  bytes[8] = std::byte{255};
  auto asset = CaptureFixture(document, bytes, context);
  auto result =
      mv::ExtractGltfPrimitive(asset, *asset.Primitive(0),
                               {mv::GltfTriangleRequest::kRequireTriangles,
                                mv::GltfValueRequest::kRawAndNormalized},
                               Limits(), {});
  Require(result.extraction.has_value());
  const auto& channel = result.extraction->Channels()[1];
  Require(std::get<std::span<const std::int8_t>>(channel.raw)[0] == -128);
  Require(std::get<std::span<const std::int8_t>>(*channel.exact_min)[0] ==
          -128);
  Require(std::get<std::span<const std::int8_t>>(*channel.exact_max)[0] == 127);
  Require(channel.normalized_values[0] == -1 &&
          channel.normalized_values[1] == 1);
  auto bad_document = std::string(document);
  bad_document.replace(bad_document.find("[-128]"), 6, "[-127]");
  auto bad_asset = CaptureFixture(bad_document, bytes, context);
  auto bad = mv::ExtractGltfPrimitive(
      bad_asset, *bad_asset.Primitive(0),
      {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw},
      Limits(), {});
  Require(bad.diagnostic.status == mv::GltfStatus::kMalformed);
  auto invalid =
      mv::ExtractGltfPrimitive(asset, *asset.Primitive(0), {}, Limits(), {});
  Require(invalid.diagnostic.status == mv::GltfStatus::kInvalidRequest);
  std::stop_source stop;
  stop.request_stop();
  auto canceled = mv::ExtractGltfPrimitive(
      asset, *asset.Primitive(0),
      {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw},
      Limits(), stop.get_token());
  Require(canceled.diagnostic.status == mv::GltfStatus::kCanceled);
}
std::string ReadFile(const char* name) {
  std::ifstream input(name, std::ios::binary);
  if (!input) throw std::runtime_error("oracle fixture unavailable");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
void PrintRaw(std::string_view tag, const mv::GltfRawValues& values) {
  std::cout << tag;
  std::visit(
      [](const auto raw) {
        using Value = typename std::remove_cvref_t<decltype(raw)>::value_type;
        for (const auto value : raw) {
          std::uint64_t bits = 0;
          if constexpr (std::is_same_v<Value, float>)
            bits = std::bit_cast<std::uint32_t>(value);
          else
            bits = static_cast<std::make_unsigned_t<Value>>(value);
          for (std::size_t byte = 0; byte < sizeof(Value); ++byte)
            std::cout << ' ' << ((bits >> (byte * 8)) & 255U);
        }
      },
      values);
  std::cout << '\n';
}
void RunOracleCase(char** argv) {
  const auto json = ReadFile(argv[1]);
  const auto resource = ReadFile(argv[2]);
  const std::span bytes(reinterpret_cast<const std::byte*>(resource.data()),
                        resource.size());
  meshvale::geometry::ExecutionContext context({4, 1, 10000000});
  auto asset = CaptureFixture(json, bytes, context);
  auto result = mv::ExtractGltfPrimitive(
      asset, *asset.Primitive(0),
      {std::string_view(argv[3]) == "triangles"
           ? mv::GltfTriangleRequest::kRequireTriangles
           : mv::GltfTriangleRequest::kConvertStripOrFan,
       mv::GltfValueRequest::kRawAndNormalized},
      Limits(), {});
  std::cout << "D " << static_cast<int>(result.diagnostic.status) << ' '
            << static_cast<int>(result.diagnostic.code) << '\n';
  if (!result.extraction) return;
  for (const auto& channel : result.extraction->Channels()) {
    std::cout << "C " << channel.semantic << '\n';
    std::cout << "M " << channel.accessor_index << ' '
              << static_cast<int>(channel.component_type) << ' '
              << static_cast<int>(channel.shape) << ' ' << channel.normalized
              << ' ' << channel.element_count << ' ';
    if (channel.set_index)
      std::cout << *channel.set_index;
    else
      std::cout << -1;
    std::cout << ' ';
    if (channel.base_buffer_view)
      std::cout << *channel.base_buffer_view;
    else
      std::cout << -1;
    std::cout << ' ' << channel.base_byte_offset << ' '
              << channel.base_byte_stride << ' ' << channel.packed_sparse_step
              << '\n';
    if (channel.sparse_source) {
      const auto& sparse = *channel.sparse_source;
      std::cout << "Q " << sparse.indices_buffer_view << ' '
                << sparse.indices_byte_offset << ' '
                << static_cast<int>(sparse.indices_component_type) << ' '
                << sparse.values_buffer_view << ' ' << sparse.values_byte_offset
                << '\n';
    }
    PrintRaw("R", channel.raw);
    if (channel.exact_min) PrintRaw("L", *channel.exact_min);
    if (channel.exact_max) PrintRaw("H", *channel.exact_max);
    std::cout << "N";
    for (const auto value : channel.normalized_values)
      std::cout << ' ' << std::bit_cast<std::uint64_t>(value);
    std::cout << "\nP";
    for (const auto row : channel.sparse_destinations) std::cout << ' ' << row;
    std::cout << '\n';
  }
  if (const auto indices = result.extraction->SourceIndices())
    PrintRaw("I", indices->raw);
  std::cout << "T";
  for (const auto index : result.extraction->TriangleIndices())
    std::cout << ' ' << index;
  std::cout << "\nS";
  for (const auto& corner : result.extraction->SourceCorners())
    std::cout << ' ' << corner.primitive_stream_corner << ' '
              << corner.accessor_row;
  std::cout << '\n';
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 4) {
      RunOracleCase(argv);
      return 0;
    }
    TopologyAndOwnership();
    TypedBoundsAndFailures();
    std::cout << "public primitive extraction: maps, raw bounds, limits and "
                 "ownership passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
