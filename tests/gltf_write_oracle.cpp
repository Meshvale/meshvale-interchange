// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf_write.h>

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace mv = meshvale::interchange;
void Require(bool value) {
  if (!value) throw std::runtime_error("writer oracle assertion failed");
}
std::string ReadFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  Require(input.is_open());
  return {std::istreambuf_iterator<char>(input), {}};
}
void WriteFile(const std::string& path, std::span<const std::byte> bytes) {
  Require(bytes.size() <= static_cast<std::size_t>(
                              std::numeric_limits<std::streamsize>::max()));
  std::ofstream output(path, std::ios::binary);
  Require(output.is_open());
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  Require(output.good());
}
void Hex(std::span<const std::byte> bytes) {
  if (bytes.empty()) {
    std::cout << '-';
    return;
  }
  std::cout << std::hex << std::setfill('0');
  for (const auto byte : bytes)
    std::cout << std::setw(2) << std::to_integer<unsigned>(byte);
  std::cout << std::dec;
}
void Hex(std::string_view text) {
  Hex({reinterpret_cast<const std::byte*>(text.data()), text.size()});
}
void Optional(std::optional<std::uint64_t> value) {
  if (value)
    std::cout << *value;
  else
    std::cout << -1;
}
mv::GltfReadLimits ReadLimits() {
  return {1000000, 1000000, 4000000, 100000, 100,    1000000, 1000,
          1000,    1000,    100000,  100000, 100000, 1000000, 1000000};
}
void Observe(const mv::GltfAsset& reloaded) {
  for (const auto& number : reloaded.RawNumbers())
    std::cout << "L " << number.source.offset << ' ' << number.source.length
              << ' ' << number.lexeme << '\n';
  for (const auto& node : reloaded.Nodes()) {
    std::cout << "V " << node.table_index << ' ';
    Optional(node.parent);
    std::cout << ' ';
    Optional(node.mesh);
    std::cout << ' ';
    Optional(node.skin);
    std::cout << ' ' << static_cast<int>(node.transform_kind) << ' '
              << static_cast<unsigned>(node.authored_transform_mask) << ' ';
    Hex(node.name);
    std::cout << ' ' << node.children.size();
    for (const auto child : node.children) std::cout << ' ' << child;
    for (const auto bits : node.local_transform_bits) std::cout << ' ' << bits;
    std::cout << '\n';
  }
  for (const auto& scene : reloaded.Scenes()) {
    std::cout << "S " << scene.table_index << ' ';
    Hex(scene.name);
    std::cout << ' ' << scene.roots.size();
    for (const auto root : scene.roots) std::cout << ' ' << root;
    std::cout << '\n';
  }
  for (std::uint64_t ordinal = 0; ordinal < reloaded.InstanceCount();
       ++ordinal) {
    const auto instance = *reloaded.Instance(ordinal);
    const auto primitive = instance.Primitive();
    std::cout << "I " << ordinal << ' ' << instance.SceneIndex() << ' '
              << instance.NodeIndex() << ' ' << primitive.MeshIndex() << ' '
              << primitive.PrimitiveIndex() << '\n';
  }
  for (std::uint64_t ordinal = 0; ordinal < reloaded.PrimitiveCount();
       ++ordinal) {
    const auto primitive = *reloaded.Primitive(ordinal);
    std::cout << "P " << ordinal << ' ' << primitive.MeshIndex() << ' '
              << primitive.PrimitiveIndex() << '\n';
    auto result =
        mv::ExtractGltfPrimitive(reloaded, primitive,
                                 {mv::GltfTriangleRequest::kRequireTriangles,
                                  mv::GltfValueRequest::kRawAndNormalized},
                                 {1000000, 1000000, 10000000}, {});
    std::cout << "X " << ordinal << ' '
              << static_cast<int>(result.diagnostic.status) << '\n';
    if (!result.extraction) continue;
    for (const auto& channel : result.extraction->Channels()) {
      std::cout << "A " << ordinal << ' ';
      Hex(channel.semantic);
      std::cout << ' ' << static_cast<int>(channel.component_type) << ' '
                << static_cast<int>(channel.shape) << ' ' << channel.normalized
                << ' ' << channel.element_count << ' ';
      std::visit([](const auto values) { Hex(std::as_bytes(values)); },
                 channel.raw);
      std::cout << ' ' << channel.normalized_values.size();
      for (const auto value : channel.normalized_values)
        std::cout << ' ' << std::bit_cast<std::uint64_t>(value);
      std::cout << '\n';
    }
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    Require(argc >= 3 && (argc - 3) % 3 == 0);
    std::optional<mv::GltfWriteBundle> bundle;
    {
      const auto json = ReadFile(argv[1]);
      std::vector<std::string> storage;
      storage.reserve(static_cast<std::size_t>((argc - 3) / 3));
      std::vector<mv::SuppliedGltfResource> bindings;
      for (int argument = 3; argument < argc; argument += 3) {
        const std::string_view kind(argv[argument]);
        Require(kind == "buffer" || kind == "image");
        const std::string_view index(argv[argument + 1]);
        std::uint64_t ordinal = 0;
        const auto parsed =
            std::from_chars(index.data(), index.data() + index.size(), ordinal);
        Require(parsed.ec == std::errc{} &&
                parsed.ptr == index.data() + index.size());
        storage.push_back(ReadFile(argv[argument + 2]));
        const auto& bytes = storage.back();
        bindings.push_back(
            {kind == "buffer" ? mv::GltfResourceKind::kBuffer
                              : mv::GltfResourceKind::kImage,
             ordinal,
             {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()}});
      }
      meshvale::geometry::ExecutionContext context({4, 1, 100000000});
      auto read = mv::ReadGltfAsset(
          {reinterpret_cast<const std::byte*>(json.data()), json.size()},
          bindings, ReadLimits(), {1, 128, 1, 128}, context, {});
      Require(read.asset.has_value());
      auto emitted = mv::WriteRetainedGltfAsset(
          *read.asset, {1000000, 100000, 1000000, 4000000, 10000000}, {1, 128},
          {});
      Require(emitted.bundle.has_value());
      bundle = *emitted.bundle;
      Require(context.ActiveWorkers() == 0);
    }
    const std::string prefix(argv[2]);
    WriteFile(prefix + ".json", bundle->Json());
    std::vector<mv::SuppliedGltfResource> bindings;
    std::size_t ordinal = 0;
    for (const auto& resource : bundle->Resources()) {
      WriteFile(prefix + ".resource-" + std::to_string(ordinal),
                resource.bytes);
      std::cout << "R " << static_cast<int>(resource.kind) << ' '
                << resource.table_index << ' '
                << static_cast<int>(resource.origin) << ' '
                << resource.logical_bytes << ' ' << resource.bytes.size()
                << ' ';
      Optional(resource.buffer_index);
      std::cout << ' ';
      Optional(resource.buffer_view_index);
      std::cout << ' ';
      Hex(resource.authored_uri);
      std::cout << '\n';
      if (resource.origin == mv::GltfResourceOrigin::kCallerBinding)
        bindings.push_back(
            {resource.kind, resource.table_index, resource.bytes});
      ++ordinal;
    }
    meshvale::geometry::ExecutionContext reload_context({4, 1, 100000000});
    auto reloaded = mv::ReadGltfAsset(bundle->Json(), bindings, ReadLimits(),
                                      {1, 128, 1, 128}, reload_context, {});
    Require(reloaded.asset.has_value());
    Observe(*reloaded.asset);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
