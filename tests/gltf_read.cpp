// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

#include "meshvale/interchange/gltf.h"

namespace {
using namespace meshvale::interchange;
void Require(bool value) {
  if (!value) throw std::runtime_error("reader expectation failed");
}
GltfReadLimits Limits() {
  return {65536, 65536, 131072, 8192, 64,   65536, 256,
          256,   32,    8192,   8192, 8192, 8192,  131072};
}
GltfWorkPolicy Work() { return {8, 8, 8, 8}; }
GltfReadResult Read(std::string_view source,
                    std::span<const SuppliedGltfResource> resources,
                    const meshvale::geometry::ExecutionContext& context) {
  return ReadGltfAsset(std::as_bytes(std::span(source.data(), source.size())),
                       resources, Limits(), Work(), context, {});
}
}  // namespace
int main() {
  try {
    meshvale::geometry::ExecutionContext context({4, 1, 10485760});
    const auto minimal = Read(
        R"({"asset":{"version":"2.0"},"extras":{"number":1e9999999999999999999}})",
        {}, context);
    Require(minimal.asset.has_value() &&
            minimal.asset->RawNumbers().size() == 1);
    Require(minimal.asset->RawNumbers()[0].lexeme == "1e9999999999999999999");
    auto data = Read(
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4,"uri":"data:application/gltf-buffer;base64,AAECAw=="}]})",
        {}, context);
    Require(data.asset.has_value());
    Require(data.asset->Resources()[0].bytes.size() == 4 &&
            data.asset->Resources()[0].bytes[3] == std::byte{3});
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"data:application/octet-stream;base64,AB=="}]})",
            {}, context)
            .diagnostic.status == GltfStatus::kMalformed);
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"DATA:application/octet-stream;base64,AA=="}]})",
            {}, context)
            .diagnostic.status == GltfStatus::kUnsupportedProfile);
    std::array<std::byte, 40> buffer{};
    buffer.back() = std::byte{42};
    const SuppliedGltfResource supplied{GltfResourceKind::kBuffer, 0, buffer};
    auto asset = Read(
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36,"uri":"b.bin"}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0,"translation":[-0,0,0]}],"scenes":[{"nodes":[0]},{"nodes":[0]}]})",
        std::span(&supplied, 1), context);
    Require(asset.asset.has_value());
    Require(asset.asset->Resources()[0].bytes.size() == 40 &&
            asset.asset->Resources()[0].logical_bytes == 36);
    Require(asset.asset->InstanceCount() == 2 &&
            asset.asset->PrimitiveCount() == 1);
    Require(asset.asset->Nodes()[0].local_transform_bits[0] == 0x80000000u);
    Require(asset.asset->Capability(*asset.asset->Primitive(0)).reason ==
            GltfCapabilityReason::kBinaryAdmissionPending);
    auto other = Read(R"({"asset":{"version":"2.0"}})", {}, context);
    Require(other.asset->Capability(*asset.asset->Primitive(0)).reason ==
            GltfCapabilityReason::kForeignOwner);
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]},{"children":[0]}]})",
            {}, context)
            .diagnostic.status == GltfStatus::kMalformed);
    Require(context.ActiveWorkers() == 0);
    std::cout << "owned reader/resource/graph boundaries passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
