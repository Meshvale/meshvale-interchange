// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace fs = std::filesystem;
constexpr std::size_t kWorkerBudget = 2;
constexpr std::size_t kMinimumParallelVertices = 1;
constexpr std::size_t kPayloadBudget = 16777216;
constexpr std::uint64_t kCountLimit = 4096;
constexpr std::uint64_t kByteLimit = 1048576;
constexpr std::uint64_t kRequestLimit = 4194304;
constexpr std::uint64_t kChannelLimit = 64;
constexpr std::uint64_t kJsonDepthLimit = 128;
constexpr std::uint64_t kParallelMinimumBytes = 1;
constexpr std::uint64_t kParallelMinimumComponents = 1;
constexpr std::uint64_t kChannelPartitionElements = 16;
constexpr std::uint64_t kPartitionBytes = 64;
constexpr std::uint64_t kCancellationBlockBytes = 16;
constexpr std::uint64_t kCancellationBlockScalars = 16;
constexpr std::uint64_t kMaximumJobs = 8;
constexpr std::string_view kDocument =
    R"({"asset":{"version":"2.0"},"extras":{"exact":9007199254740993},"buffers":[{"byteLength":72}],"bufferViews":[{"buffer":0,"byteLength":72}],"accessors":[{"bufferView":0,"componentType":5126,"type":"VEC3","count":6,"min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
constexpr std::array<geo::index_t, 1> kKeptFaces{1};
constexpr std::array<geo::index_t, 3> kKeptCorners{3, 4, 5};
constexpr std::array<std::uint32_t, 3> kExpectedIndices{3, 4, 5};
constexpr std::array<std::byte, 12> kExpectedBytes{
    std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{4}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{5}, std::byte{0}, std::byte{0}, std::byte{0}};
constexpr std::string_view kExpectedUri =
    "data:application/octet-stream;base64,AwAAAAQAAAAFAAAA";
void Require(bool value) {
  if (!value)
    throw std::runtime_error("installed verified GLB publication failed");
}
mv::GltfReadLimits ReadLimits() {
  return {kByteLimit,    kByteLimit,   kByteLimit,    kCountLimit,
          kChannelLimit, kByteLimit,   kChannelLimit, kChannelLimit,
          kChannelLimit, kCountLimit,  kCountLimit,   kCountLimit,
          kCountLimit,   kRequestLimit};
}
constexpr std::string_view kEntry = "source.glb";
constexpr std::size_t kPhysicalPositionBytes = 72;
constexpr std::uint32_t kOpaqueChunkType = 0x74657374;
constexpr std::size_t kExpectedChunks = 3;
constexpr std::size_t kMaximumJsonPadding = 3;
constexpr std::array<std::byte, 4> kOpaque{std::byte{0xde}, std::byte{0xad},
                                           std::byte{0xbe}, std::byte{0xef}};
constexpr std::array<std::byte, 3> kStatic{std::byte{0x10}, std::byte{0x20},
                                           std::byte{0x30}};
constexpr std::array<std::string_view, 1> kSourceOpaque{"opaque.bin"};
constexpr std::array<std::string_view, 3> kFinalOpaque{
    "opaque.bin", "receipts/static.bin", "checks/indices.bin"};
constexpr std::size_t kInitialFiles = 3;
constexpr std::size_t kPublishedFiles = 4;
constexpr std::array kPhases{mv::GltfFilePublicationPhase::kPreflight,
                             mv::GltfFilePublicationPhase::kStaging,
                             mv::GltfFilePublicationPhase::kVerification,
                             mv::GltfFilePublicationPhase::kPublication};
class Fixture {
 public:
  Fixture() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = fs::canonical(fs::temp_directory_path()) /
            ("meshvale-installed-glb-publication-" + std::to_string(stamp));
    Require(fs::create_directory(root_));
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ~Fixture() noexcept {
    std::error_code error;
    fs::remove_all(root_, error);
  }
  const fs::path& Root() const noexcept { return root_; }
  fs::path Input() const { return root_ / "input"; }

 private:
  fs::path root_;
};
void Write(const fs::path& path, std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  Require(static_cast<bool>(stream));
}
mv::GltfFileLimits FileLimits() {
  constexpr std::uint64_t kFiles = 16;
  constexpr std::uint64_t kPathBytes = 1024;
  return {kFiles,     kFiles,      kByteLimit,  kByteLimit,
          kPathBytes, kCountLimit, kCountLimit, kRequestLimit};
}
mv::GltfFileReadLimits FileReadLimits() {
  return {FileLimits(),
          {ReadLimits(), kRequestLimit},
          {kByteLimit, kCountLimit, kRequestLimit}};
}
mv::GltfFileReadWorkPolicy FileReadWork() {
  return {{kPartitionBytes, kParallelMinimumBytes, kPartitionBytes,
           kCancellationBlockBytes, kMaximumJobs},
          {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumComponents,
           kChannelPartitionElements},
          {kParallelMinimumBytes, kPartitionBytes}};
}
void CheckContainer(const mv::GlbContainer& container,
                    std::span<const std::byte> json) {
  Require(container.ChunkCount() == kExpectedChunks);
  const auto prefix = container.Chunk(0);
  Require(prefix.type == mv::kGlbJsonChunkType &&
          prefix.bytes.size() >= json.size() &&
          prefix.bytes.size() - json.size() <= kMaximumJsonPadding &&
          std::ranges::equal(prefix.bytes.first(json.size()), json));
  Require(std::ranges::all_of(
      prefix.bytes.subspan(json.size()),
      [](std::byte byte) { return byte == std::byte{' '}; }));
  const auto bin = container.Chunk(1);
  Require(bin.type == mv::kGlbBinChunkType &&
          bin.bytes.size() == kPhysicalPositionBytes &&
          std::ranges::all_of(
              bin.bytes, [](std::byte byte) { return byte == std::byte{0}; }));
  const auto opaque = container.Chunk(2);
  Require(opaque.type == kOpaqueChunkType &&
          std::ranges::equal(opaque.bytes, kOpaque));
}
struct VerifiedFilesState {
  bool called = false;
  std::array<mv::GltfFileView, 1> files{
      mv::GltfFileView{"checks/indices.bin", kExpectedBytes}};
};
std::span<const mv::GltfFileView> ProvideVerifiedFiles(
    const mv::GltfFileVerifiedContent& content, void* state) {
  auto& files = *static_cast<VerifiedFilesState*>(state);
  Require(content.format == mv::GltfFileFormat::kGlb && content.container &&
          content.entry_path == kEntry &&
          content.files.size() == kInitialFiles &&
          std::ranges::equal(
              content.inspection.ReloadedExtraction().TriangleIndices(),
              kExpectedIndices));
  files.called = true;
  return files.files;
}
}  // namespace

int main() {
  try {
    geo::ExecutionContext ledger(
        {kWorkerBudget, kMinimumParallelVertices, kPayloadBudget});
    {
      Fixture fixture;
      Require(fs::create_directory(fixture.Input()));
      std::array<std::byte, kPhysicalPositionBytes> positions{};
      const std::array chunks{mv::GlbChunk{mv::kGlbJsonChunkType,
                                           std::as_bytes(std::span(kDocument))},
                              mv::GlbChunk{mv::kGlbBinChunkType, positions},
                              mv::GlbChunk{kOpaqueChunkType, kOpaque}};
      auto source_container = mv::WriteGlbContainer(
          chunks, {kByteLimit, kCountLimit, kRequestLimit},
          {kParallelMinimumBytes, kPartitionBytes}, ledger);
      Require(source_container.status == mv::GlbStatus::kSuccess);
      Write(fixture.Input() / kEntry, source_container.container.Bytes());
      Write(fixture.Input() / "opaque.bin", kOpaque);
      std::optional<mv::GltfFileAsset> file_source;
      std::optional<mv::GltfTopologyWriteBundle> retained;
      std::optional<mv::GltfMaterializedPrimitive> original;
      std::optional<geo::MeshInspection> candidate;
      {
        auto context = ledger;
        const geo::MeshMaterializationWorkPolicy geometry_work{
            kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
            kMaximumJobs};
        const geo::MeshMaterializationLimits geometry_limits{
            kCountLimit,   kCountLimit, kCountLimit,  kCountLimit,
            kChannelLimit, kCountLimit, kCountLimit,  kCountLimit,
            kCountLimit,   kByteLimit,  kRequestLimit};
        const mv::GltfMaterializationLimits transfer_limits{
            kChannelLimit,
            kRequestLimit,
            geometry_limits,
            {kCountLimit, kRequestLimit}};
        auto file = mv::ReadGltfFileAsset(
            fixture.Input() / kEntry,
            {fixture.Input(),
             mv::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
             kSourceOpaque},
            FileReadLimits(), FileReadWork(), context);
        Require(file.asset.has_value());
        file_source = *file.asset;
        const auto& asset = file_source->Asset();
        auto extracted = mv::ExtractGltfPrimitiveCapped(
            asset, *asset.Primitive(0),
            {mv::GltfTriangleRequest::kRequireTriangles,
             mv::GltfValueRequest::kRawAndNormalized},
            {kCountLimit, kCountLimit, kRequestLimit}, {});
        Require(extracted.extraction.has_value());
        auto transferred = mv::MaterializeGltfPrimitive(
            *extracted.extraction, transfer_limits, geometry_work);
        Require(transferred.primitive.has_value());
        auto selected =
            geo::SelectMeshFaces(transferred.primitive->Mesh(), kKeptFaces,
                                 geometry_limits, geometry_work);
        Require(selected.selection.has_value());
        auto inspected = geo::InspectMaterializedMesh(
            selected.selection->Candidate(), {kCountLimit, kRequestLimit},
            geometry_work);
        Require(inspected.inspection && inspected.inspection->IsStorageValid());
        const mv::GltfTopologyWriteLimits writer_limits{
            kByteLimit,
            kCountLimit,
            kJsonDepthLimit,
            kCountLimit,
            kChannelLimit,
            kCountLimit,
            kByteLimit,
            kCountLimit,
            kCountLimit,
            kByteLimit,
            {kByteLimit, kChannelLimit, kByteLimit, kByteLimit, kRequestLimit}};
        const mv::GltfTopologyWriteWorkPolicy writer_work{
            kParallelMinimumBytes, kPartitionBytes, kCancellationBlockBytes,
            kMaximumJobs};
        auto written = mv::WriteGltfTopologySelection(
            {*transferred.primitive, *inspected.inspection,
             selected.selection->SourceFaces(),
             selected.selection->SourceCorners()},
            writer_limits, writer_work);
        Require(written.bundle.has_value() && written.bundle->ChangedBinding());
        retained = *written.bundle;
        original = *transferred.primitive;
        candidate = *inspected.inspection;
      }
      Require(
          retained->Resources().size() == 2 &&
          retained->Resources()[0].authored_uri.empty() &&
          std::ranges::equal(retained->Resources()[0].bytes, positions) &&
          retained->Resources()[1].authored_uri == kExpectedUri &&
          std::ranges::equal(retained->Resources()[1].bytes, kExpectedBytes));
      Require(
          retained->SourceFaces().size() == 1 &&
          retained->SourceFaces()[0] == kKeptFaces[0] &&
          retained->Source().Mesh().View().positions.size() == 18 &&
          retained->OriginalAsset().OriginalJson().size() >= kDocument.size() &&
          retained->OriginalAsset().OriginalJson().size() <=
              kDocument.size() + kMaximumJsonPadding &&
          std::ranges::equal(
              retained->OriginalAsset().OriginalJson().first(kDocument.size()),
              std::as_bytes(std::span(kDocument))));
      const auto context = retained->Source().Mesh().ShareExecutionContext();
      Require(file_source->Asset().SharesExecutionBudget(context));
      const std::array<mv::SuppliedGltfResource, 1> bindings{
          mv::SuppliedGltfResource{mv::GltfResourceKind::kBuffer, 0,
                                   positions}};
      auto reloaded = mv::ReadGltfAssetCapped(
          retained->Json(), bindings, {ReadLimits(), kRequestLimit},
          {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumComponents,
           kChannelPartitionElements},
          context, {});
      Require(reloaded.asset.has_value());
      auto extracted = mv::ExtractGltfPrimitiveCapped(
          *reloaded.asset, *reloaded.asset->Primitive(0),
          {mv::GltfTriangleRequest::kRequireTriangles,
           mv::GltfValueRequest::kRawAndNormalized},
          {kCountLimit, kCountLimit, kRequestLimit}, {});
      Require(extracted.extraction &&
              std::ranges::equal(extracted.extraction->TriangleIndices(),
                                 kExpectedIndices));
      const mv::GltfTopologyOutputInspectionLimits inspection_limits{
          kByteLimit,   kCountLimit, kJsonDepthLimit, kChannelLimit,
          kByteLimit,   kCountLimit, kChannelLimit,   kCountLimit,
          kByteLimit,   kCountLimit, kCountLimit,     kByteLimit,
          kRequestLimit};
      auto checked = mv::InspectGltfTopologyOutput(
          {*original, *candidate, kKeptFaces, kKeptCorners, *retained,
           *reloaded.asset, *extracted.extraction},
          inspection_limits,
          {kParallelMinimumBytes, kPartitionBytes, kCancellationBlockBytes,
           kMaximumJobs});
      Require(checked.inspection.has_value());
      const auto& coverage = checked.inspection->Coverage();
      using Coverage = mv::GltfTopologyOutputCheckCoverage;
      Require(coverage.source_candidate == Coverage::kComplete &&
              coverage.correspondence == Coverage::kComplete &&
              coverage.lexical == Coverage::kComplete &&
              coverage.resources == Coverage::kComplete &&
              coverage.reloaded_primitive == Coverage::kComplete &&
              coverage.provenance == Coverage::kComplete);
      Require(fs::remove(fixture.Input() / kEntry) &&
              fs::remove(fixture.Input() / "opaque.bin") &&
              fs::remove(fixture.Input()));
      const mv::GltfFilePublicationLimits publication_limits{
          FileLimits(),
          {ReadLimits(), kRequestLimit},
          {kCountLimit, kCountLimit, kRequestLimit},
          inspection_limits,
          {kByteLimit, kCountLimit, kRequestLimit}};
      const mv::GltfFilePublicationWorkPolicy publication_work{
          FileReadWork().files,
          FileReadWork().asset,
          {kParallelMinimumBytes, kPartitionBytes, kCancellationBlockBytes,
           kMaximumJobs},
          FileReadWork().container};
      const std::array<mv::GltfFileView, 1> supplements{
          mv::GltfFileView{"receipts/static.bin", kStatic}};
      std::size_t next_phase = 0;
      bool verified = false;
      mv::GltfFilePublicationOptions options;
      options.supplemental_files = supplements;
      options.on_phase = [&](mv::GltfFilePublicationPhase phase) {
        Require(next_phase < kPhases.size() && phase == kPhases[next_phase]);
        ++next_phase;
      };
      options.on_verified = [&](const mv::GltfFileVerifiedContent& content) {
        Require(content.format == mv::GltfFileFormat::kGlb &&
                content.container && content.entry_path == kEntry &&
                content.files.size() == kInitialFiles &&
                std::ranges::equal(
                    content.inspection.ReloadedExtraction().TriangleIndices(),
                    kExpectedIndices));
        verified = true;
        return std::vector<mv::GltfFileSupplement>{
            {"checks/indices.bin",
             {kExpectedBytes.begin(), kExpectedBytes.end()}}};
      };
      const auto destination = fixture.Root() / "published";
      auto published = mv::PublishGltfTopologyBundle(
          {*file_source, *checked.inspection}, destination, options,
          publication_limits, publication_work);
      Require(published.outcome == mv::GltfFilePublicationOutcome::kPublished &&
              published.bundle && verified && next_phase == kPhases.size() &&
              published.cleanup == mv::GltfFileCleanupOutcome::kNotRequired &&
              published.bundle->Files().size() == kPublishedFiles &&
              published.bundle->Format() == mv::GltfFileFormat::kGlb &&
              published.bundle->Container() &&
              std::ranges::equal(published.bundle->EntryBytes(),
                                 published.bundle->Container()->Bytes()));
      CheckContainer(*published.bundle->Container(), retained->Json());
      auto copy = *published.bundle;
      published.bundle.reset();
      // This backing and its state survive the entire synchronous publication.
      VerifiedFilesState borrowed_files;
      options.on_verified = {};
      options.verified_files_provider = {ProvideVerifiedFiles, &borrowed_files};
      next_phase = 0;
      const auto borrowed_destination = fixture.Root() / "borrowed";
      auto borrowed = mv::PublishGltfTopologyBundle(
          {*file_source, *checked.inspection}, borrowed_destination, options,
          publication_limits, publication_work);
      Require(borrowed.outcome == mv::GltfFilePublicationOutcome::kPublished &&
              borrowed.bundle && borrowed_files.called &&
              next_phase == kPhases.size());
      auto borrowed_reopened = mv::ReadGltfFileAsset(
          borrowed_destination / kEntry,
          {borrowed_destination,
           mv::GltfExtensionFilePolicy::kExplicitOpaqueInventory, kFinalOpaque},
          FileReadLimits(), FileReadWork(), context);
      Require(borrowed_reopened.asset &&
              borrowed_reopened.asset->Files().size() == kPublishedFiles &&
              borrowed_reopened.asset->Format() == mv::GltfFileFormat::kGlb &&
              borrowed_reopened.asset->Container());
      CheckContainer(*borrowed_reopened.asset->Container(), retained->Json());
      const auto borrowed_capture = borrowed_reopened.asset->Files();
      const auto original_publication = copy.Files();
      for (std::size_t file = 0; file < borrowed_capture.size(); ++file)
        Require(borrowed_capture[file].path ==
                    original_publication[file].path &&
                std::ranges::equal(borrowed_capture[file].bytes,
                                   original_publication[file].bytes));
      const auto rejected = mv::PublishGltfTopologyBundle(
          {*file_source, *checked.inspection}, destination, {},
          publication_limits, publication_work);
      Require(rejected.diagnostic.status ==
                  mv::GltfFileStatus::kDestinationExists &&
              !rejected.bundle &&
              rejected.cleanup == mv::GltfFileCleanupOutcome::kNotRequired);
      auto reopened = mv::ReadGltfFileAsset(
          destination / kEntry,
          {destination, mv::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
           kFinalOpaque},
          FileReadLimits(), FileReadWork(), context);
      Require(reopened.asset &&
              reopened.asset->Files().size() == kPublishedFiles &&
              reopened.asset->Format() == mv::GltfFileFormat::kGlb &&
              reopened.asset->Container());
      CheckContainer(*reopened.asset->Container(), retained->Json());
      const auto captured = reopened.asset->Files();
      const auto preserved = copy.Files();
      for (std::size_t file = 0; file < captured.size(); ++file)
        Require(
            captured[file].path == preserved[file].path &&
            std::ranges::equal(captured[file].bytes, preserved[file].bytes));
      Require(captured[0].path == "checks/indices.bin" &&
              std::ranges::equal(captured[0].bytes, kExpectedBytes) &&
              captured[1].path == "opaque.bin" &&
              std::ranges::equal(captured[1].bytes, kOpaque) &&
              captured[2].path == "receipts/static.bin" &&
              std::ranges::equal(captured[2].bytes, kStatic));
      auto final_extraction = mv::ExtractGltfPrimitiveCapped(
          reopened.asset->Asset(), *reopened.asset->Asset().Primitive(0),
          {mv::GltfTriangleRequest::kRequireTriangles,
           mv::GltfValueRequest::kRawAndNormalized},
          {kCountLimit, kCountLimit, kRequestLimit});
      Require(final_extraction.extraction &&
              std::ranges::equal(final_extraction.extraction->TriangleIndices(),
                                 kExpectedIndices));
      // An identity writer must publish the original complete authored wire.
      constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
      constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
      const geo::MeshMaterializationWorkPolicy identity_work{
          kParallelMinimumBytes, kPartitionBytes, kCancellationBlockScalars,
          kMaximumJobs};
      auto identity_candidate = geo::InspectMaterializedMesh(
          original->Mesh(), {kCountLimit, kRequestLimit}, identity_work);
      Require(identity_candidate.inspection &&
              identity_candidate.inspection->IsStorageValid());
      const mv::GltfTopologyWriteLimits identity_limits{
          kByteLimit,
          kCountLimit,
          kJsonDepthLimit,
          kCountLimit,
          kChannelLimit,
          kCountLimit,
          kByteLimit,
          kCountLimit,
          kCountLimit,
          kByteLimit,
          {kByteLimit, kChannelLimit, kByteLimit, kByteLimit, kRequestLimit}};
      auto identity = mv::WriteGltfTopologySelection(
          {*original, *identity_candidate.inspection, kIdentityFaces,
           kIdentityCorners},
          identity_limits,
          {kParallelMinimumBytes, kPartitionBytes, kCancellationBlockBytes,
           kMaximumJobs});
      Require(identity.bundle && !identity.bundle->ChangedBinding());
      auto identity_reload = mv::ReadGltfAssetCapped(
          identity.bundle->Json(), bindings, {ReadLimits(), kRequestLimit},
          {kParallelMinimumBytes, kPartitionBytes, kParallelMinimumComponents,
           kChannelPartitionElements},
          context);
      Require(identity_reload.asset.has_value());
      auto identity_extract = mv::ExtractGltfPrimitiveCapped(
          *identity_reload.asset, *identity_reload.asset->Primitive(0),
          {mv::GltfTriangleRequest::kRequireTriangles,
           mv::GltfValueRequest::kRawAndNormalized},
          {kCountLimit, kCountLimit, kRequestLimit});
      Require(identity_extract.extraction.has_value());
      auto identity_inspect = mv::InspectGltfTopologyOutput(
          {*original, *identity_candidate.inspection, kIdentityFaces,
           kIdentityCorners, *identity.bundle, *identity_reload.asset,
           *identity_extract.extraction},
          inspection_limits,
          {kParallelMinimumBytes, kPartitionBytes, kCancellationBlockBytes,
           kMaximumJobs});
      Require(identity_inspect.inspection.has_value());
      const auto identity_destination = fixture.Root() / "identity";
      auto identity_published = mv::PublishGltfTopologyBundle(
          {*file_source, *identity_inspect.inspection}, identity_destination,
          {}, publication_limits, publication_work);
      Require(identity_published.outcome ==
                  mv::GltfFilePublicationOutcome::kPublished &&
              identity_published.bundle &&
              identity_published.bundle->Container() &&
              identity_published.bundle->Format() == mv::GltfFileFormat::kGlb &&
              std::ranges::equal(identity_published.bundle->EntryBytes(),
                                 source_container.container.Bytes()));
      auto identity_reopen = mv::ReadGltfFileAsset(
          identity_destination / kEntry,
          {identity_destination,
           mv::GltfExtensionFilePolicy::kExplicitOpaqueInventory,
           kSourceOpaque},
          FileReadLimits(), FileReadWork(), context);
      Require(identity_reopen.asset && identity_reopen.asset->Container() &&
              std::ranges::equal(identity_reopen.asset->EntryBytes(),
                                 source_container.container.Bytes()));
      CheckContainer(*identity_reopen.asset->Container(),
                     file_source->Asset().OriginalJson());
    }
    Require(ledger.ActiveTrackedPayload() == 0 && ledger.ActiveWorkers() == 0);
    std::cout << "installed GLB capture, selection, verified publication, "
                 "source removal, no-replace and final reopen passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
