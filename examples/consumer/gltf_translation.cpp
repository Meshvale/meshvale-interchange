// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace mv = meshvale::interchange;
constexpr std::size_t kMiB = 1024 * 1024;
constexpr std::size_t kBytesPerFloat = 4;
constexpr std::size_t kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 0xff;
constexpr std::uint32_t kOne = 0x3f800000;
constexpr std::uint32_t kTwo = 0x40000000;
constexpr std::array<std::uint32_t, 17> kWords{
    0, 0,    0, kOne, 0, 0, 0, kOne, 0,  // Three local vertices.
    0, kOne,                             // Two time keys.
    0, 0,    0, kTwo, 0, 0};             // Direct translation samples.
constexpr std::string_view kSource =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"moving.bin","byteLength":68}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},{"buffer":0,"byteOffset":36,"byteLength":8},{"buffer":0,"byteOffset":44,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":2,"componentType":5126,"count":2,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"children":[1]},{"mesh":0}],"scenes":[{"nodes":[0]}],"animations":[{"samplers":[{"input":1,"output":2,"interpolation":"STEP"}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}]})";
void Require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
}  // namespace
int main() {
  try {
    meshvale::geometry::ExecutionContext context({4, 1, 512 * kMiB});
    std::optional<mv::GltfPrimitiveExtraction> retained;
    {
      std::array<std::byte, kWords.size() * kBytesPerFloat> bytes{};
      for (std::size_t word = 0; word < kWords.size(); ++word)
        for (std::size_t byte = 0; byte < kBytesPerFloat; ++byte)
          bytes[word * kBytesPerFloat + byte] =
              std::byte((kWords[word] >> (byte * kBitsPerByte)) & kByteMask);
      const std::array<mv::SuppliedGltfResource, 1> resources{
          {{mv::GltfResourceKind::kBuffer, 0, bytes}}};
      const mv::GltfReadLimits source_limits{
          kMiB, 8 * kMiB, 16 * kMiB, 65536, 64,    kMiB,   256,
          256,  32,       65536,     65536, 65536, 262144, 8 * kMiB};
      const mv::GltfWorkPolicy work{
          kMiB, kMiB, kMiB, 16384, {16384, 16384, 256, 64}};
      auto source = mv::ReadGltfAssetCapped(
          std::as_bytes(std::span(kSource.data(), kSource.size())), resources,
          {source_limits, 16 * kMiB}, work, context);
      Require(source.asset.has_value(),
              "installed reader admits original moving fixture");
      const mv::GltfExtractionLimits limits{
          kMiB,
          kMiB,
          40 * kMiB,
          {65536, 65536, 4096, 4096, 65536, 262144, 1048576, 2 * kMiB, 4 * kMiB,
           8 * kMiB}};
      const auto result = mv::ExtractGltfPrimitiveCapped(
          *source.asset, *source.asset->Primitive(0),
          {mv::GltfTriangleRequest::kRequireTriangles,
           mv::GltfValueRequest::kRaw,
           mv::GltfProcessingProfile::kDirectTranslationTrianglesV1},
          limits);
      Require(result.extraction && result.direct_translation &&
                  result.direct_translation->complete,
              "installed extraction retains actual complete proof");
      Require(result.direct_translation->keys_checked == 2 &&
                  result.direct_translation->decoded_bytes_checked == 32,
              "two keys decoded once");
      const auto proof = result.extraction->DirectTranslation();
      Require(proof && proof->mesh_users.size() == 1 &&
                  proof->mesh_users[0] == 1 &&
                  std::ranges::equal(proof->node_membership,
                                     std::array<std::uint8_t, 2>{2, 1}) &&
                  proof->tracks.size() == 1 &&
                  proof->tracks[0].interpolation ==
                      mv::GltfDirectTranslationInterpolation::kStep,
              "public borrowed proof agrees with literal fixture");
      retained = result.extraction;
    }
    Require(retained->DirectTranslationFacts()->keys_checked == 2,
            "installed strong retention");
    retained.reset();
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "installed owners release real ledger/workers");
    std::cout << "installed direct translation extraction foundation passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
