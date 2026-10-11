// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_GLTF_TOPOLOGY_WRITE_SEMANTICS_FIXTURE_SUPPORT_H_
#define MESHVALE_INTERCHANGE_TESTS_GLTF_TOPOLOGY_WRITE_SEMANTICS_FIXTURE_SUPPORT_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gltf_topology_write_semantics {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;

enum class FixtureKind { kTriangles, kStrip, kFan, kImplicit, kLargeIndex };

struct Fixture {
  std::string json;
  std::vector<std::byte> external_buffer;
  FixtureKind kind;
  std::uint64_t primitive_ordinal;
  std::uint64_t source_mode;
  std::uint64_t vertex_count;
  std::vector<std::uint32_t> source_triangles;
  std::vector<std::uint64_t> stream_corners;
  std::vector<std::string_view> preserved_lexemes;
};

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

Fixture MakeFixture(FixtureKind kind);
geo::ExecutionContext Context(bool large = false);
geo::MeshMaterializationWorkPolicy GeometryWork(bool serial = false);
mv::GltfReadLimits ReadLimits(bool large = false);
mv::GltfWorkPolicy ReaderWork();
mv::GltfExtractionLimits ExtractionLimits(bool large = false);
mv::GltfMaterializationLimits TransferLimits(bool large = false);
mv::GltfAsset ReadFixture(const Fixture& fixture,
                          const geo::ExecutionContext& context);
mv::GltfPrimitiveExtraction Extract(const mv::GltfAsset& asset,
                                    const Fixture& fixture,
                                    std::uint64_t ordinal);
mv::GltfMaterializedPrimitive Transfer(const Fixture& fixture,
                                       const geo::ExecutionContext& context);
geo::MeshInspection Inspect(const geo::MaterializedMesh& mesh,
                            bool large = false);
void CheckSourceExtraction(const Fixture& fixture,
                           const mv::GltfPrimitiveExtraction& extraction);
void CheckRawChannels(const mv::GltfPrimitiveExtraction& extraction);
void CheckLexicalPreservation(const Fixture& fixture,
                              std::span<const std::byte> output);
mv::GltfTopologyWriteLimits WriteLimits(bool large = false);
mv::GltfTopologyWriteWorkPolicy WriteWork(bool serial = false);
void CheckWrittenBundle(const Fixture& fixture,
                        const mv::GltfTopologyWriteBundle& bundle, bool changed,
                        geo::index_t selected_face = 1);
void CheckSelectionAndReload();
void CheckIdentityAndLifetimes();
void CheckRejectedCandidates();
void CheckLargeIndex(std::uint64_t resource_budget_mib);

}  // namespace gltf_topology_write_semantics
#endif  // MESHVALE_INTERCHANGE_TESTS_GLTF_TOPOLOGY_WRITE_SEMANTICS_FIXTURE_SUPPORT_H_
