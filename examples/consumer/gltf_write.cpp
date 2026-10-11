// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace mv = meshvale::interchange;
constexpr std::string_view kSource =
    "\n"
    R"({"asset":{"version":"2.0"},"extras":{"exact":9007199254740993,"zero":-0,"decimal":1.00e+01,"text":"\u0394"},"extensionsUsed":["VENDOR_retained"],"extensions":{"VENDOR_retained":{"value":1e400}},"buffers":[{"uri":"payload.bin","byteLength":9}],"bufferViews":[{"buffer":0,"byteOffset":1,"byteLength":8}],"images":[{"bufferView":0,"mimeType":"image/png"},{"uri":"payload.bin","mimeType":"image/jpeg"}],"accessors":[{"componentType":5126,"type":"VEC3","count":3,"min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0,"name":"\u0394"}],"scenes":[{"nodes":[0]}]})"
    "\r\n";
constexpr std::array<std::byte, 12> kBuffer{
    std::byte{0},  std::byte{137}, std::byte{80}, std::byte{78},
    std::byte{71}, std::byte{13},  std::byte{10}, std::byte{26},
    std::byte{10}, std::byte{42},  std::byte{43}, std::byte{44}};
constexpr std::array<std::byte, 4> kImage{std::byte{255}, std::byte{216},
                                          std::byte{255}, std::byte{217}};
void Require(bool value) {
  if (!value) throw std::runtime_error("installed writer assertion failed");
}
mv::GltfReadLimits ReadLimits() {
  return {10000, 10000, 10000, 1000, 32,  10000, 100,
          100,   100,   100,   100,  100, 10000, 10000};
}
void Inspect(const mv::GltfWriteBundle& bundle) {
  const auto json = bundle.Json();
  Require(std::string_view(reinterpret_cast<const char*>(json.data()),
                           json.size()) == kSource);
  const auto resources = bundle.Resources();
  Require(resources.size() == 3);
  Require(std::ranges::equal(resources[0].bytes, kBuffer));
  Require(resources[0].logical_bytes == 9 &&
          resources[0].authored_uri == "payload.bin");
  Require(resources[1].bytes.size() == 8 &&
          resources[1].bytes.data() == resources[0].bytes.data() + 1 &&
          resources[1].buffer_index == 0 &&
          resources[1].buffer_view_index == 0);
  Require(resources[2].kind == mv::GltfResourceKind::kImage &&
          resources[2].table_index == 1 &&
          resources[2].authored_uri == "payload.bin" &&
          std::ranges::equal(resources[2].bytes, kImage));
}
mv::GltfWriteBundle Emit(const meshvale::geometry::ExecutionContext& context) {
  std::string json(kSource);
  auto buffer = kBuffer;
  auto image = kImage;
  const std::array<mv::SuppliedGltfResource, 2> bindings{
      mv::SuppliedGltfResource{mv::GltfResourceKind::kBuffer, 0, buffer},
      mv::SuppliedGltfResource{mv::GltfResourceKind::kImage, 1, image}};
  std::stop_source capture_stop;
  auto read = mv::ReadGltfAsset(
      {reinterpret_cast<const std::byte*>(json.data()), json.size()}, bindings,
      ReadLimits(), {1, 16, 1, 1}, context, capture_stop.get_token());
  Require(read.asset.has_value());
  capture_stop.request_stop();
  auto output = mv::WriteRetainedGltfAsset(
      *read.asset, {10000, 100, 10000, 10000, 100000}, {1, 32}, {});
  Require(output.bundle && context.ActiveWorkers() == 0);
  Require(output.bundle->Json().data() != read.asset->OriginalJson().data());
  Require(output.bundle->NewRequestedBytes() < context.ActiveTrackedPayload());
  std::fill(json.begin(), json.end(), ' ');
  std::fill(buffer.begin(), buffer.end(), std::byte{99});
  std::fill(image.begin(), image.end(), std::byte{99});
  return *output.bundle;
}
}  // namespace
int main() {
  try {
    std::optional<mv::GltfWriteBundle> retained;
    {
      meshvale::geometry::ExecutionContext context({4, 1, 1000000});
      retained = Emit(context);
      Inspect(*retained);
    }
    Inspect(*retained);  // Caller input, asset and visible context are gone.
    meshvale::geometry::ExecutionContext reload_context({4, 1, 1000000});
    std::vector<mv::SuppliedGltfResource> bindings;
    for (const auto& resource : retained->Resources())
      if (resource.origin == mv::GltfResourceOrigin::kCallerBinding)
        bindings.push_back(
            {resource.kind, resource.table_index, resource.bytes});
    auto reloaded = mv::ReadGltfAsset(retained->Json(), bindings, ReadLimits(),
                                      {1, 16, 1, 1}, reload_context, {});
    Require(reloaded.asset && reloaded.asset->InstanceCount() == 1 &&
            reloaded.asset->Nodes()[0].name == "\xce\x94");
    Require(
        std::ranges::equal(reloaded.asset->OriginalJson(), retained->Json()));
    std::cout << "installed retained writer and actual reload passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
