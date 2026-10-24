// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_OFF_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_OFF_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace meshvale::interchange::off_fixture {
// Literal geometry and syntax are authored independently of codec constants.
inline constexpr std::string_view kWire =
    "# original indexed polygons\r\n\tOFF # base profile\r\n"
    "8 3 17\n-0 0 0\n1 0 0\n1 1 0\n0 1 0\n"
    "2 0 0\n3 0 0\n2 1 0\n99 -0 5 # unused vertex\n"
    "4 0 1 2 3 # first quad\n4 2 3 0 1\n3 4 5 6";
inline constexpr std::array<double, 24> kPositions{-0., 0, 0, 1, 0, 0,  1,   1,
                                                   0,   0, 1, 0, 2, 0,  0,   3,
                                                   0,   0, 2, 1, 0, 99, -0., 5};
inline constexpr std::array<geometry::index_t, 4> kOffsets{0, 4, 8, 11};
inline constexpr std::array<geometry::index_t, 11> kCorners{0, 1, 2, 3, 2, 3,
                                                            0, 1, 4, 5, 6};
inline constexpr std::uint64_t kOriginalEdges = 17;
inline constexpr std::uint64_t kPhysicalLimit = 1024 * 1024;
inline constexpr std::uint64_t kRequestLimit = 8 * kPhysicalLimit;
inline constexpr std::uint64_t kCountLimit = 65536;
inline constexpr std::uint64_t kDescriptorLimit = 4096;
inline constexpr std::uint64_t kAttributeLimit = 8;
inline constexpr std::uint64_t kLineLimit = 512;
inline constexpr std::uint64_t kTokenLimit = 128;
inline constexpr std::uint64_t kCommentLimit = 256;

inline void Require(
    bool condition, std::string_view message,
    std::source_location where = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(where.file_name()) + ":" +
                             std::to_string(where.line()) + ": " +
                             std::string(message));
}
inline std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text));
}
inline std::string_view Text(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
inline std::string Replace(std::string source, std::string_view from,
                           std::string_view to) {
  const auto offset = source.find(from);
  Require(offset != std::string::npos,
          "owned literal mutation finds its marker");
  source.replace(offset, from.size(), to);
  return source;
}
inline geometry::MeshMaterializationLimits MeshLimits() {
  return {kCountLimit,      kCountLimit,      kCountLimit,  kCountLimit,
          kAttributeLimit,  kCountLimit,      kCountLimit,  kCountLimit,
          kDescriptorLimit, kDescriptorLimit, kRequestLimit};
}
inline OffReadLimits ReadLimits() {
  return {kPhysicalLimit,    kCountLimit,   kCountLimit,
          kCountLimit,       kCountLimit,   kLineLimit,
          kTokenLimit,       kCommentLimit, kRequestLimit,
          3 * kRequestLimit, MeshLimits(),  {kCountLimit, kRequestLimit}};
}
inline OffWriteLimits WriteLimits() {
  return {kPhysicalLimit,    kCountLimit,
          kCountLimit,       kCountLimit,
          kCountLimit,       kLineLimit,
          kTokenLimit,       kRequestLimit,
          2 * kRequestLimit, {kCountLimit, kRequestLimit}};
}
inline OffWorkPolicy Work() { return {16, 4, 1, {1, 64, 8, 64}}; }
inline OffWriteOptions Options(const OffDocument* source = nullptr) {
  return {OffTextPolicy::kCanonicalAscii, OffEdgeCountPolicy::kUnknownZero,
          source};
}
inline void SameBits(double actual, double expected) {
  Require(std::bit_cast<std::uint64_t>(actual) ==
              std::bit_cast<std::uint64_t>(expected),
          "canonical position retains exact binary64 bits including zero sign");
}
inline void Check(const OffDocument& document,
                  std::span<const double> positions = kPositions,
                  std::span<const geometry::index_t> offsets = kOffsets,
                  std::span<const geometry::index_t> corners = kCorners) {
  const auto view = document.Mesh().View();
  Require(document.Profile() == OffProfile::kAsciiRecords &&
              document.Inspection().IsStorageValid() && view.attributes.empty(),
          "actual complete base-profile inspection has no fabricated channels");
  Require(view.positions.size() == positions.size() &&
              std::ranges::equal(view.face_offsets, offsets) &&
              std::ranges::equal(view.corner_vertices, corners),
          "literal vertex/face/corner cardinality, identities, start order and "
          "winding");
  for (std::size_t i = 0; i < positions.size(); ++i)
    SameBits(view.positions[i], positions[i]);
}
inline void CheckFacts(const OffWriteFacts& facts, geometry::index_t vertices,
                       geometry::index_t faces, geometry::index_t corners) {
  Require(facts.profile == OffProfile::kAsciiRecords &&
              facts.vertex_count == vertices && facts.face_count == faces &&
              facts.corner_count == corners &&
              facts.text_policy == OffTextPolicy::kCanonicalAscii &&
              facts.edge_count_policy == OffEdgeCountPolicy::kUnknownZero &&
              facts.emitted_edge_count == 0 &&
              facts.correspondence == OffCorrespondence::kIdentity,
          "actual codec facts declare canonical text/unknown edges and "
          "supplied-mesh identity");
}
}  // namespace meshvale::interchange::off_fixture
#endif  // MESHVALE_INTERCHANGE_TESTS_OFF_FIXTURE_H_
