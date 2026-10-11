// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cfenv>
#include <cstddef>
#include <iostream>
#include <limits>
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
    const std::array<std::string_view, 9> alternative_payloads = {
        "AB==", "AAB=",  "AA",     "AAA",   "AA%3D%3D",
        "AA-_", "AA== ", "AA==\t", "AA==\n"};
    for (const auto payload : alternative_payloads) {
      // JSON string escapes preserve the whitespace payloads in source.
      std::string encoded;
      for (const auto character : payload) {
        if (character == '\t')
          encoded += "\\t";
        else if (character == '\n')
          encoded += "\\n";
        else
          encoded += character;
      }
      const auto source =
          R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"data:application/octet-stream;base64,)" +
          encoded + R"("}]})";
      Require(Read(source, {}, context).diagnostic.code ==
              GltfDiagnosticCode::kUnsupportedResourceRepresentation);
    }
    const std::array<std::string_view, 7> malformed_payloads = {
        "A", "A=", "AA=", "AA===", "====", "AA*A", "A=AA"};
    for (const auto payload : malformed_payloads) {
      const auto source =
          std::string(
              R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"data:application/octet-stream;base64,)") +
          std::string(payload) + R"("}]})";
      Require(Read(source, {}, context).diagnostic.code ==
              GltfDiagnosticCode::kEmbeddedUriSyntax);
    }
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"data:application/octet-stream;base64AA=="}]})",
            {}, context)
            .diagnostic.code == GltfDiagnosticCode::kEmbeddedUriSyntax);
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"data:application/octet-stream;base64,AB=="}]})",
            {}, context)
            .diagnostic.status == GltfStatus::kUnsupportedProfile);
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"DATA:application/octet-stream;base64,AA=="}]})",
            {}, context)
            .diagnostic.status == GltfStatus::kUnsupportedProfile);
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"images":[{"uri":"data:image/png;base64,iVBORw0KGgo="}]})",
            {}, context)
            .asset.has_value());
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"images":[{"uri":"data:image/png;base64,iVBORw0KGgo=","mimeType":"image/jpeg"}]})",
            {}, context)
            .diagnostic.code == GltfDiagnosticCode::kEmbeddedMediaMismatch);
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"images":[{"uri":"data:image/png;base64,iVBORw0KGgo=","mimeType":"image/future"}]})",
            {}, context)
            .diagnostic.code == GltfDiagnosticCode::kEmbeddedMediaMismatch);
    Require(Read(R"({"asset":{"version":"2.0"},"extras":{"invalid":"\uD800"}})",
                 {}, context)
                .diagnostic.code == GltfDiagnosticCode::kUnicodeScalar);
    auto oversized_nodes = Limits();
    oversized_nodes.max_document_nodes =
        std::numeric_limits<std::ptrdiff_t>::max();
    Require(ReadGltfAsset({}, {}, oversized_nodes, Work(), context, {})
                .diagnostic.status == GltfStatus::kInvalidLimits);
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
    const auto original_rounding = std::fegetround();
    constexpr std::array kRoundingModes = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD,
                                           FE_TOWARDZERO};
    constexpr std::array<std::uint32_t, 3> kExpectedTranslationBits = {
        0x3f800001u, 0x80000000u, 1u};
    for (const auto mode : kRoundingModes) {
      Require(std::fesetround(mode) == 0);
      const auto rounded = Read(
          R"({"asset":{"version":"2.0"},"nodes":[{"translation":[1.0000000596046447753906251,-0,1.401298464324817e-45]}]})",
          {}, context);
      const auto preserved = std::fegetround() == mode;
      Require(std::fesetround(original_rounding) == 0);
      Require(rounded.asset.has_value() && preserved);
      for (std::size_t axis = 0; axis < kExpectedTranslationBits.size(); ++axis)
        Require(rounded.asset->Nodes()[0].local_transform_bits[axis] ==
                kExpectedTranslationBits[axis]);
    }
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
    auto ancestor_skin = Read(
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]},{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":1,"skin":0,"children":[1]},{"mesh":0},{}],"skins":[{"joints":[2]}]})",
        {}, context);
    Require(ancestor_skin.asset.has_value());
    Require(ancestor_skin.asset->Capability(*ancestor_skin.asset->Primitive(0))
                .reason == GltfCapabilityReason::kBinaryAdmissionPending);
    auto unrelated_extension = Read(
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":3,"type":"VEC3","extensions":{"CUSTOM":{}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})",
        {}, context);
    Require(unrelated_extension.asset.has_value());
    Require(unrelated_extension.asset
                ->Capability(*unrelated_extension.asset->Primitive(0))
                .reason == GltfCapabilityReason::kOptionalExtensionEffect);
    const std::array<std::string_view, 6> malformed_declarations = {
        R"({"asset":{"version":"2.0"},"nodes":[{},{}],"skins":[{"joints":[0,1]}]})",
        R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]},{},{}],"skins":[{"joints":[1],"skeleton":2}]})",
        R"({"asset":{"version":"2.0"},"nodes":[{}],"accessors":[{"componentType":5126,"count":1,"type":"VEC3"}],"skins":[{"joints":[0],"inverseBindMatrices":0}]})",
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"targets":[{"NORMAL":0}]}]}]})",
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"targets":[{"POSITION":0}]}],"weights":[0,0]}]})",
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"SCALAR"},{"componentType":5126,"count":1,"type":"VEC3"}],"nodes":[{}],"animations":[{"samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}},{"sampler":0,"target":{"node":0,"path":"translation"}}]}]})"};
    for (const auto declaration : malformed_declarations)
      Require(Read(declaration, {}, context).diagnostic.code ==
              GltfDiagnosticCode::kGraphRelationship);
    Require(
        Read(
            R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"targets":[{"POSITION":0}]}],"weights":[0]}],"nodes":[{"mesh":0,"weights":[1]}]})",
            {}, context)
            .asset.has_value());
    Require(context.ActiveWorkers() == 0);
    std::cout << "owned reader/resource/graph boundaries passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
