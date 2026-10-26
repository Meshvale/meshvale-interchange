// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PUBLISH_TRANSLATION_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PUBLISH_TRANSLATION_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gltf_file_provider_fixture.h"
#include "gltf_translation_output_fixture.h"

namespace meshvale::interchange::test_translation_publication {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace fs = std::filesystem;
namespace moving = gltf_translation_output_fixture;
namespace files = test_provider;
using files::Bytes;
using files::ReadBytes;
using files::Require;
using files::Write;
inline constexpr std::string_view kEntry = "scene.gltf";
inline constexpr std::uint32_t kOpaqueType = 0x31415926;
inline constexpr std::array<std::byte, 4> kOpaque{
    std::byte{0x42}, std::byte{0}, std::byte{0xfe}, std::byte{0x11}};
inline constexpr std::size_t kWordBytes = 4;
inline constexpr unsigned kBitsPerByte = 8;
inline constexpr std::uint32_t kByteMask = 255;
inline constexpr std::uint64_t kRequestBytes = 64 * moving::kMiB;

inline ic::GltfFilePublicationLimits Limits() {
  auto limits = files::Limits();
  limits.reload = {moving::ReadLimits(), 16 * moving::kMiB};
  limits.extraction = {moving::kCount, moving::kCount, 40 * moving::kMiB,
                       moving::TranslationLimits()};
  limits.inspection = moving::InspectionLimits();
  return limits;
}
inline ic::GltfFilePublicationWorkPolicy Work() {
  auto work = files::Work();
  work.reload = moving::ReadWork();
  work.reload.direct_translation = moving::TranslationWork(true);
  work.inspection = moving::InspectionWork();
  return work;
}
inline bool SameWork(const ic::GltfDirectTranslationWorkPolicy& a,
                     const ic::GltfDirectTranslationWorkPolicy& b) {
  return a.parallel_min_keys == b.parallel_min_keys &&
         a.partition_keys == b.partition_keys &&
         a.cancellation_block_keys == b.cancellation_block_keys &&
         a.max_jobs == b.max_jobs;
}
inline void Word(std::vector<std::byte>& bytes, std::uint32_t value) {
  for (unsigned byte = 0; byte < kWordBytes; ++byte)
    bytes.push_back(
        static_cast<std::byte>((value >> (byte * kBitsPerByte)) & kByteMask));
}
// Original GLB2 encoding, independent of the production container writer.
// All156 BIN bytes are logical bytes; the unused final word is not padding.
inline std::vector<std::byte> Wire(std::string json,
                                   std::span<const std::byte> bin) {
  constexpr std::uint32_t kMagic = 0x46546c67;
  constexpr std::uint32_t kVersion = 2;
  constexpr std::uint32_t kJsonType = 0x4e4f534a;
  constexpr std::uint32_t kBinType = 0x004e4942;
  constexpr std::size_t kHeaderBytes = 12;
  constexpr std::size_t kChunkHeaderBytes = 8;
  while (json.size() % kWordBytes) json.push_back(' ');
  Require(bin.size() == 156 && bin.size() % kWordBytes == 0,
          "original logical BIN extent");
  const auto size = kHeaderBytes + 3 * kChunkHeaderBytes + json.size() +
                    bin.size() + kOpaque.size();
  Require(size < moving::kMiB, "original wire fits literal finite extent");
  std::vector<std::byte> bytes;
  Word(bytes, kMagic);
  Word(bytes, kVersion);
  Word(bytes, static_cast<std::uint32_t>(size));
  const auto chunk = [&](std::uint32_t type, std::span<const std::byte> data) {
    Word(bytes, static_cast<std::uint32_t>(data.size()));
    Word(bytes, type);
    bytes.insert(bytes.end(), data.begin(), data.end());
  };
  chunk(kJsonType, Bytes(json));
  chunk(kBinType, bin);
  chunk(kOpaqueType, kOpaque);
  Require(bytes.size() == size, "independently encoded complete GLB2 length");
  return bytes;
}
struct Fixture {
  fs::path root;
  fs::path input;
  files::RootRemoval cleanup{root};
  geo::ExecutionContext context;
  bool glb;
  bool identity;
  std::vector<std::byte> original_wire;
  std::optional<ic::GltfFileAsset> file;
  std::optional<ic::GltfMaterializedPrimitive> materialized;
  std::optional<geo::MeshInspection> candidate;
  std::optional<ic::GltfTopologyOutputInspection> parent;

  Fixture(const geo::ExecutionContext& ledger, bool container, bool unchanged,
          bool step)
      : context(ledger), glb(container), identity(unchanged) {
    static std::atomic<std::uint64_t> sequence{0};
    root = fs::canonical(fs::temp_directory_path()) /
           ("meshvale-translation-publication-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(sequence.fetch_add(1)));
    Require(fs::create_directory(root), "isolated physical translation parent");
    input = root / "source";
    Require(fs::create_directory(input), "original physical source directory");
    auto json = moving::Json(step);
    const auto binary = moving::Bytes();
    if (glb) {
      constexpr std::string_view kExternal =
          R"("uri":"moving.bin","byteLength":152)";
      const auto offset = json.find(kExternal);
      Require(offset != std::string::npos, "original external buffer spelling");
      json.replace(offset, kExternal.size(), R"("byteLength":156)");
      original_wire = Wire(json, binary);
    } else {
      original_wire.assign(Bytes(json).begin(), Bytes(json).end());
      Write(input / "moving.bin", binary);
    }
    Write(input / kEntry, original_wire);
    const auto limits = Limits();
    auto work = Work();
    work.reload = moving::ReadWork();
    const auto captured = ic::ReadGltfFileAsset(
        input / kEntry,
        {input, ic::GltfExtensionFilePolicy::kRejectOpaqueExtensions, {}},
        {limits.files, limits.reload, limits.container},
        {work.files, work.reload, work.container}, context);
    Require(captured.asset && captured.asset->Format() ==
                                  (glb ? ic::GltfFileFormat::kGlb
                                       : ic::GltfFileFormat::kGltfJson),
            "actual content-detected independent original file capture");
    file = *captured.asset;
    const auto extraction = moving::Extract(file->Asset());
    const auto transfer = ic::MaterializeGltfPrimitive(
        extraction, moving::TransferLimits(), moving::GeometryWork());
    Require(transfer.primitive.has_value(), "real moving file materialization");
    materialized = *transfer.primitive;
    candidate = moving::Candidate(*materialized, identity);
    const auto written = ic::WriteGltfTopologySelection(
        {*materialized, *candidate, moving::Faces(identity),
         moving::Corners(identity)},
        moving::WriteLimits(), moving::WriteWork());
    Require(written.bundle.has_value(),
            "real moving writer before publication");
    const auto reloaded = moving::Reload(*written.bundle, context);
    const auto selected = moving::Extract(reloaded);
    const auto inspected = ic::InspectGltfTopologyOutput(
        {*materialized, *candidate, moving::Faces(identity),
         moving::Corners(identity), *written.bundle, reloaded, selected},
        moving::InspectionLimits(), moving::InspectionWork(true));
    Require(inspected.inspection.has_value(),
            "genuine independent output authority before file publication");
    parent = *inspected.inspection;
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ic::GltfFilePublicationRequest Request() const { return {*file, *parent}; }
  fs::path Destination(std::string_view name = "published") const {
    return root / name;
  }
};
inline void CheckInspection(const ic::GltfTopologyOutputInspection& inspection,
                            bool identity, bool glb) {
  const auto& output = inspection.Output();
  const auto& extracted = inspection.ReloadedExtraction();
  constexpr std::array<std::uint32_t, 6> kIdentity{0, 1, 2, 3, 4, 5};
  constexpr std::array<std::uint32_t, 3> kChanged{3, 4, 5};
  const auto topology = identity ? std::span<const std::uint32_t>(kIdentity)
                                 : std::span<const std::uint32_t>(kChanged);
  const auto proof = extracted.DirectTranslation();
  Require(
      extracted.ProcessingProfile() ==
              ic::GltfProcessingProfile::kDirectTranslationTrianglesV1 &&
          extracted.MaterialIndex() == 0 && extracted.Channels().size() == 2 &&
          std::ranges::equal(extracted.TriangleIndices(), topology) &&
          std::ranges::equal(output.SourceFaces(), moving::Faces(identity)) &&
          std::ranges::equal(output.SourceCorners(), moving::Corners(identity)),
      "literal selected triangles, channels, material and ordered maps");
  Require(proof &&
              std::ranges::equal(proof->mesh_users,
                                 std::array<std::uint64_t, 2>{1, 2}) &&
              std::ranges::equal(proof->node_membership,
                                 std::array<std::uint8_t, 3>{2, 1, 1}) &&
              proof->tracks.size() == 2 && proof->tracks[0].key_count == 2 &&
              proof->tracks[1].sampler_index == 0,
          "actual shared mesh users and translation tracks survive reload");
  Require(
      output.Resources().size() ==
              (identity ? std::size_t{1} : std::size_t{2}) &&
          std::ranges::equal(output.Resources()[0].bytes, moving::Bytes()) &&
          output.Resources()[0].logical_bytes ==
              (glb ? 156 : moving::kLogicalBytes),
      "every original key, UV, position and unused binary byte remains exact");
  Require(extracted.DirectTranslationFacts() &&
              extracted.DirectTranslationFacts()->complete &&
              inspection.DirectTranslationFacts() &&
              inspection.DirectTranslationFacts()->complete,
          "actual successful staged E/V owners retain their own facts");
}
inline void CheckContainer(const GlbContainer* actual, const Fixture& source) {
  if (!source.glb) {
    Require(actual == nullptr, "JSON publication has no invented container");
    return;
  }
  const auto* original = source.file->Container();
  Require(original && actual && original->ChunkCount() == 3 &&
              actual->ChunkCount() == 3,
          "real GLB JSON, BIN and independent opaque chunks retained");
  for (std::size_t index = 1; index < 3; ++index)
    Require(actual->Chunk(index).type == original->Chunk(index).type &&
                std::ranges::equal(actual->Chunk(index).bytes,
                                   original->Chunk(index).bytes),
            "full non-JSON chunk headers and payload preserved");
  Require(actual->Chunk(2).type == kOpaqueType &&
              std::ranges::equal(actual->Chunk(2).bytes, kOpaque),
          "independently authored opaque type and bytes");
}
inline void Positive(bool glb) {
  geo::ExecutionContext context({4, 1, kRequestBytes});
  std::optional<ic::GltfPublishedBundle> retained;
  std::optional<ic::GltfTopologyOutputInspection> callback_owner;
  for (bool step : {false, true})
    for (bool identity : {false, true}) {
      Fixture source(context, glb, identity, step);
      fs::remove_all(source.input);
      bool verified = false;
      const auto limits = Limits();
      const auto work = Work();
      ic::GltfFilePublicationOptions options;
      options.on_verified = [&](const ic::GltfFileVerifiedContent& content) {
        verified = true;
        Require(!fs::exists(source.Destination()),
                "actual verified callback precedes destination commit");
        CheckInspection(content.inspection, identity, glb);
        CheckContainer(content.container, source);
        const auto* e =
            content.inspection.ReloadedExtraction().DirectTranslationFacts();
        const auto* v = content.inspection.DirectTranslationFacts();
        Require(
            e && v && SameWork(e->work, work.reload.direct_translation) &&
                SameWork(v->work, work.inspection.direct_translation) &&
                e->execution.launched_workers == 0 &&
                v->execution.launched_workers > 0 &&
                v->execution.launched_workers == v->execution.joined_workers,
            "staged E uses captured reload work and V uses explicit own work");
        if (!callback_owner) callback_owner = content.inspection;
        return std::vector<ic::GltfFileSupplement>{};
      };
      const auto result = ic::PublishGltfTopologyBundle(
          source.Request(), source.Destination(), options, limits, work);
      Require(
          result.bundle && verified &&
              result.outcome == ic::GltfFilePublicationOutcome::kPublished &&
              result.diagnostic.status == ic::GltfFileStatus::kSuccess &&
              result.translation_extraction &&
              result.translation_extraction->complete &&
              result.translation_inspection &&
              result.translation_inspection->complete &&
              SameWork(result.translation_extraction->work,
                       work.reload.direct_translation) &&
              SameWork(result.translation_inspection->work,
                       work.inspection.direct_translation),
          "published result preserves actual completed staged E/V facts");
      CheckInspection(result.bundle->Inspection(), identity, glb);
      CheckContainer(result.bundle->Container(), source);
      Require(result.bundle->Files().size() ==
                  (glb ? std::size_t{1} : std::size_t{2}),
              "inline changed indices do not invent physical files");
      for (const auto& file : result.bundle->Files())
        Require(std::ranges::equal(ReadBytes(source.Destination() / file.path),
                                   file.bytes),
                "every final physical byte independently reopened");
      if (identity)
        Require(std::ranges::equal(result.bundle->EntryBytes(),
                                   source.original_wire),
                "identity preserves complete JSON or GLB original wire");
      if (!retained) retained = *result.bundle;
    }
  Require(
      retained && callback_owner && context.ActiveTrackedPayload() > 0,
      "published and callback copies retain real owners after inputs vanish");
  CheckInspection(retained->Inspection(), false, glb);
  CheckInspection(*callback_owner, false, glb);
  retained.reset();
  callback_owner.reset();
  Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
          "all translation publication owners and workers release");
}
}  // namespace meshvale::interchange::test_translation_publication
#endif  // MESHVALE_INTERCHANGE_TESTS_GLTF_FILE_PUBLISH_TRANSLATION_FIXTURE_H_
