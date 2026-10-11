// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_GLTF_MATERIALIZATION_SEMANTICS_FIXTURE_SUPPORT_H_
#define MESHVALE_INTERCHANGE_TESTS_GLTF_MATERIALIZATION_SEMANTICS_FIXTURE_SUPPORT_H_

#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gltf_materialization_semantics {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;

struct Fixture {
  std::string json;
  std::vector<std::byte> bytes;
  std::uint64_t primitive_ordinal = 0;
  bool convert_strip_or_fan = false;
  bool large = false;
};
enum class TopologyFixture { kTriangles, kStrip, kFan };

void Require(bool value, std::string_view message,
             std::source_location source = std::source_location::current());
template <class T>
void RequireBits(std::span<const T> actual, std::span<const T> expected,
                 std::string_view message) {
  Require(actual.size() == expected.size(), message);
  Require(actual.empty() || std::memcmp(actual.data(), expected.data(),
                                        actual.size_bytes()) == 0,
          message);
}

Fixture RawChannelsFixture();
Fixture MakeTopologyFixture(TopologyFixture kind);
Fixture LargeSourceIndexFixture();
geo::ExecutionContext Context(bool large = false);
geo::MeshMaterializationWorkPolicy Work();
mv::GltfMaterializationLimits TransferLimits(bool large = false);
mv::GltfPrimitiveExtraction ReadAndExtract(
    const Fixture& fixture, mv::GltfValueRequest values,
    const geo::ExecutionContext& context);
mv::GltfMaterializedPrimitive Transfer(
    const Fixture& fixture,
    mv::GltfValueRequest values = mv::GltfValueRequest::kRawAndNormalized);
const geo::AttributeReadView& Attribute(const geo::MeshReadView& mesh,
                                        std::string_view name);
const mv::GltfChannelView& Channel(const mv::GltfPrimitiveExtraction& source,
                                   std::string_view semantic);

void CheckRawChannels();
void CheckTopologyAndSelection();
void CheckSourceAndMeshLifetimes();
void CheckLargeSourceIndex();

}  // namespace gltf_materialization_semantics
#endif  // MESHVALE_INTERCHANGE_TESTS_GLTF_MATERIALIZATION_SEMANTICS_FIXTURE_SUPPORT_H_
