// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "../src/gltf_files_test_internal.h"
#include "../src/gltf_operation_workers.h"
#include "gltf_file_publish_glb_fixture.h"
#include "gltf_file_publish_translation_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace detail = ic::gltf_files_detail;
namespace fixture = ic::test_glb_publication;
namespace fs = std::filesystem;
using fixture::Bytes;
using fixture::Require;
constexpr std::string_view kUnsupportedArgument = "--unsupported-profile";
constexpr std::size_t kPaddingCases = 4;
constexpr std::size_t kPaddingSourceFaces = 335;
constexpr std::array<std::size_t, kPaddingCases> kSelectedFaceCounts{1, 4, 84,
                                                                     334};
constexpr std::array<std::string_view, 6> kPaths{"a.bin",
                                                 "opaque-a.bin",
                                                 "opaque-b.bin",
                                                 fixture::kDynamicPath,
                                                 fixture::kStaticPath,
                                                 fixture::kEntry};
constexpr std::size_t kSourceImages = 3;
constexpr std::size_t kAliasImageOrdinal = 2;
constexpr std::size_t kImageViewOrdinal = 3;
constexpr std::uint64_t kNormalizationMaximum = 255;
void CheckChannels(const ic::GltfPrimitiveExtraction& extraction) {
  bool color = false;
  for (const auto& channel : extraction.Channels()) {
    if (channel.semantic != fixture::kColorSemantic) continue;
    color = true;
    const auto raw = std::get<std::span<const std::uint8_t>>(channel.raw);
    Require(channel.normalized && raw.size() == fixture::kColors.size() &&
                channel.normalized_values.size() == fixture::kColors.size(),
            "literal normalized color descriptor and complete raw channel");
    for (std::size_t i = 0; i < raw.size(); ++i) {
      const auto expected = std::to_integer<std::uint8_t>(fixture::kColors[i]);
      Require(
          raw[i] == expected &&
              channel.normalized_values[i] ==
                  static_cast<double>(expected) / kNormalizationMaximum,
          "literal raw/normalized values preserved through GLB publication");
    }
  }
  Require(color, "actual normalized channel remains present");
}
void CheckResourceAliases(const ic::GltfAsset& asset,
                          const fixture::Fixture& source) {
  const ic::GltfResourceView* buffer = nullptr;
  const ic::GltfResourceView* alias = nullptr;
  std::size_t images = 0;
  constexpr std::uint64_t kDataBufferOrdinal = 3;
  constexpr std::uint64_t kChangedIndexBufferOrdinal = 4;
  constexpr std::uint64_t kDataImageOrdinal = 1;
  bool data_buffer = false;
  bool data_image = false;
  bool changed_indices = false;
  for (const auto& resource : asset.Resources()) {
    if (resource.kind == ic::GltfResourceKind::kBuffer &&
        resource.table_index == 0)
      buffer = &resource;
    if (resource.kind == ic::GltfResourceKind::kImage) {
      ++images;
      if (resource.table_index == kAliasImageOrdinal) alias = &resource;
    }
    if (resource.kind == ic::GltfResourceKind::kBuffer &&
        (resource.table_index == 1 || resource.table_index == 2))
      Require(std::ranges::equal(resource.bytes, fixture::kBinary),
              "shared external core URIs preserve complete physical tails");
    if (resource.kind == ic::GltfResourceKind::kBuffer &&
        resource.table_index == kDataBufferOrdinal) {
      data_buffer = true;
      Require(resource.origin == ic::GltfResourceOrigin::kDecodedDataUri &&
                  resource.bytes.size() == fixture::kPngBytes,
              "literal inline source buffer remains decoded data URI");
      for (std::size_t i = 0; i < resource.bytes.size(); ++i)
        Require(resource.bytes[i] == static_cast<std::byte>(i),
                "literal inline source bytes zero through seven");
    }
    if (resource.kind == ic::GltfResourceKind::kImage &&
        resource.table_index == kDataImageOrdinal) {
      data_image = true;
      Require(resource.origin == ic::GltfResourceOrigin::kDecodedDataUri &&
                  std::ranges::equal(
                      resource.bytes,
                      std::span(fixture::kBinary).first(fixture::kPngBytes)),
              "literal data image survives container publication");
    }
    if (resource.kind == ic::GltfResourceKind::kBuffer &&
        resource.table_index == kChangedIndexBufferOrdinal &&
        source.inspection->Output().ChangedBinding()) {
      changed_indices = true;
      Require(resource.origin == ic::GltfResourceOrigin::kDecodedDataUri &&
                  resource.bytes.size() ==
                      source.selected_triangles.size() * fixture::kWordBytes,
              "changed UInt32 indices remain inline with exact literal extent");
      for (std::size_t i = 0; i < source.selected_triangles.size(); ++i)
        for (std::size_t byte = 0; byte < fixture::kWordBytes; ++byte)
          Require(
              resource.bytes[i * fixture::kWordBytes + byte] ==
                  static_cast<std::byte>((source.selected_triangles[i] >>
                                          (byte * fixture::kWordBitsPerByte)) &
                                         fixture::kByteMask),
              "every inline index byte follows independently supplied "
              "selection");
    }
  }
  Require(
      data_buffer && data_image &&
          changed_indices ==
              source.inspection->Output().ChangedBinding().has_value(),
      "source data resources and changed-only inline index binding retained");
  const auto image_offset =
      fixture::kIndexOffset +
      source.source_triangles.size() * fixture::kSourceIndexBytes;
  Require(
      buffer && alias && images == kSourceImages &&
          std::ranges::equal(buffer->bytes,
                             source.chunks[fixture::kBinChunkOrdinal].bytes) &&
          alias->origin == ic::GltfResourceOrigin::kBufferView &&
          alias->buffer_index == 0 &&
          alias->buffer_view_index == kImageViewOrdinal &&
          alias->bytes.data() == buffer->bytes.data() + image_offset &&
          std::ranges::equal(
              alias->bytes,
              std::span(fixture::kBinary).first(fixture::kPngBytes)),
      "actual embedded BIN/image alias retains full source payload and image "
      "bytes");
}
void CheckContainer(const ic::GlbContainer* container,
                    const fixture::Fixture& source,
                    std::span<const std::byte> wire,
                    std::span<const std::byte> prefix) {
  Require(container && container->ChunkCount() == source.chunks.size() &&
              std::ranges::equal(container->Bytes(), wire),
          "actual retained container owns complete independently encoded wire");
  const auto json = container->Chunk(0);
  Require(json.type == fixture::kJsonType &&
              json.bytes.size() ==
                  prefix.size() + fixture::Padding(prefix.size()) &&
              std::ranges::equal(json.bytes.first(prefix.size()), prefix) &&
              std::ranges::all_of(json.bytes.subspan(prefix.size()),
                                  [](auto b) { return b == std::byte{' '}; }),
          "exact entire writer prefix and only independently derived space "
          "padding");
  for (std::size_t i = 1; i < source.chunks.size(); ++i) {
    const auto chunk = container->Chunk(i);
    Require(chunk.type == source.chunks[i].type &&
                std::ranges::equal(chunk.bytes, source.chunks[i].bytes),
            "literal BIN/full padding and ordered duplicate opaque payloads");
  }
}
void CheckFiles(std::span<const ic::GltfFileView> files,
                const fixture::Fixture& source, bool supplemental,
                bool dynamic) {
  const auto wire = source.ExpectedWire();
  std::size_t ordinal = 0;
  for (const auto path : kPaths) {
    if ((path == fixture::kStaticPath && !supplemental) ||
        (path == fixture::kDynamicPath && !dynamic))
      continue;
    Require(ordinal < files.size() && files[ordinal].path == path,
            "independent literal sorted GLB physical inventory");
    const auto expected =
        path == fixture::kEntry ? std::span<const std::byte>(wire)
        : path == "a.bin"       ? std::span<const std::byte>(fixture::kBinary)
        : path == fixture::kStaticPath ? Bytes(fixture::kStaticBytes)
        : path == fixture::kDynamicPath
            ? Bytes(ic::test_provider::kDynamicBytes)
            : std::span<const std::byte>(source.opaque);
    Require(std::ranges::equal(files[ordinal++].bytes, expected),
            "full literal wire/resource/opaque/supplemental bytes");
  }
  Require(ordinal == files.size(),
          "no generated index externalization or extra member");
}
void Reopen(const fixture::Fixture& source,
            const ic::GltfPublishedBundle& bundle, bool supplemental = false,
            bool dynamic = false) {
  for (const auto& file : bundle.Files())
    Require(
        std::ranges::equal(fixture::ReadBytes(source.Destination() / file.path),
                           file.bytes),
        "every finalized physical byte independently reopened");
  std::vector<std::string_view> opaque(fixture::kOpaquePaths.begin(),
                                       fixture::kOpaquePaths.end());
  if (supplemental) opaque.push_back(fixture::kStaticPath);
  if (dynamic) opaque.push_back(fixture::kDynamicPath);
  auto read = ic::ReadGltfFileAsset(
      source.Destination() / fixture::kEntry,
      {{}, ic::GltfExtensionFilePolicy::kExplicitOpaqueInventory, opaque},
      {fixture::FileLimits(),
       fixture::ReadLimits(),
       {fixture::kPhysicalBytes, fixture::kCountLimit, fixture::kRequestBytes}},
      {fixture::Work(true).files, fixture::ReadWork(true),
       fixture::Work(true).container},
      source.context);
  Require(read.asset && read.asset->Format() == ic::GltfFileFormat::kGlb &&
              read.asset->Container() &&
              std::ranges::equal(read.asset->Asset().OriginalJson(),
                                 read.asset->Container()->Chunk(0).bytes),
          "ordinary final capture retains full padded JSON chunk again");
  auto extracted = ic::ExtractGltfPrimitiveCapped(
      read.asset->Asset(), *read.asset->Asset().Primitive(0),
      {ic::GltfTriangleRequest::kRequireTriangles,
       ic::GltfValueRequest::kRawAndNormalized},
      {fixture::kCountLimit, fixture::kCountLimit, fixture::kRequestBytes});
  Require(extracted.extraction &&
              std::ranges::equal(extracted.extraction->TriangleIndices(),
                                 source.selected_triangles),
          "literal selected topology independently decoded from actual "
          "finalized wire");
  CheckChannels(*extracted.extraction);
}
void Success(const fixture::Fixture& source,
             const ic::GltfFilePublicationResult& result,
             bool supplemental = false, bool dynamic = false) {
  Require(result.bundle &&
              result.outcome == ic::GltfFilePublicationOutcome::kPublished &&
              result.diagnostic.status == ic::GltfFileStatus::kSuccess &&
              result.bundle->Format() == ic::GltfFileFormat::kGlb,
          "actual verified GLB native publication");
  const auto expected = source.ExpectedWire();
  Require(
      std::ranges::equal(result.bundle->EntryBytes(), expected) &&
          std::ranges::equal(
              result.bundle->Inspection().ReloadedAsset().OriginalJson(),
              source.inspection->Output().Json()),
      "actual inspected prefix is separate from full physical container bytes");
  CheckContainer(result.bundle->Container(), source, expected,
                 source.inspection->Output().Json());
  CheckChannels(result.bundle->Inspection().ReloadedExtraction());
  CheckResourceAliases(result.bundle->Inspection().ReloadedAsset(), source);
  CheckFiles(result.bundle->Files(), source, supplemental, dynamic);
  Reopen(source, *result.bundle, supplemental, dynamic);
}
struct Stage {
  fs::path path;
  detail::GltfFilePublicationTestOptions Testing() {
    detail::GltfFilePublicationTestOptions testing;
    testing.hooks.state = this;
    testing.hooks.stage_ready = [](detail::TestNativeView path, void* state) {
      static_cast<Stage*>(state)->path = fs::path(path);
    };
    return testing;
  }
};
void Removed(const Stage& stage, const fixture::Fixture& source,
             const ic::GltfFilePublicationResult& result) {
  Require(!result.bundle &&
              result.cleanup == ic::GltfFileCleanupOutcome::kRemoved &&
              !stage.path.empty() && !fs::exists(stage.path) &&
              !fs::exists(source.Destination()) &&
              source.context.ActiveWorkers() == 0,
          "GLB verification failure joins then removes actual staging root");
}
void IdentityAndChangedPadding() {
  for (std::size_t tail = 0; tail < kPaddingCases; ++tail) {
    fixture::Fixture source(false, 2, 1, tail);
    fs::remove_all(source.SourceRoot());
    const auto result = ic::PublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), {},
        fixture::Limits(), fixture::Work());
    Success(source, result);
    Require(std::ranges::equal(result.bundle->EntryBytes(), source.wire),
            "no-op retains exact original authored whitespace and full wire");
  }
  std::array<bool, kPaddingCases> observed{};
  for (const auto faces : kSelectedFaceCounts) {
    fixture::Fixture source(true, kPaddingSourceFaces, faces);
    const auto padding =
        fixture::Padding(source.inspection->Output().Json().size());
    observed[padding] = true;
    fs::remove_all(source.SourceRoot());
    const auto result = ic::PublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), {},
        fixture::Limits(), fixture::Work());
    Success(source, result);
  }
  Require(std::ranges::all_of(observed, [](bool value) { return value; }),
          "real changed prefixes exercise every zero-to-three padding extent");
}
struct Producer {
  explicit Producer(const fixture::Fixture& fixture_source)
      : source(fixture_source) {}
  const fixture::Fixture& source;
  std::size_t calls = 0;
  bool empty = false;
  std::optional<ic::GltfTopologyOutputInspection> retained;
  std::array<ic::GltfFileView, 1> views{
      {{fixture::kDynamicPath, Bytes(ic::test_provider::kDynamicBytes)}}};
  static std::span<const ic::GltfFileView> Provide(
      const ic::GltfFileVerifiedContent& content, void* state) {
    auto& self = *static_cast<Producer*>(state);
    ++self.calls;
    Require(content.staged_deformation == nullptr,
            "legacy physical GLB providers have no deformation observation");
    Require(content.format == ic::GltfFileFormat::kGlb && content.container &&
                content.entry_path == fixture::kEntry,
            "both producers receive actual staged GLB format and container");
    CheckContainer(content.container, self.source, self.source.ExpectedWire(),
                   self.source.inspection->Output().Json());
    CheckFiles(content.files, self.source, true, false);
    self.retained = content.inspection;
    return self.empty ? std::span<const ic::GltfFileView>{} : self.views;
  }
};
void BothProducers() {
  for (const bool borrowed : {false, true}) {
    fixture::Fixture source;
    Producer producer{source};
    const std::array<ic::GltfFileView, 1> initial{
        {{fixture::kStaticPath, Bytes(fixture::kStaticBytes)}}};
    ic::GltfFilePublicationOptions options{};
    options.supplemental_files = initial;
    if (borrowed)
      options.verified_files_provider = {Producer::Provide, &producer};
    else
      options.on_verified = [&](const ic::GltfFileVerifiedContent& content) {
        (void)Producer::Provide(content, &producer);
        return std::vector<ic::GltfFileSupplement>{
            {std::string(fixture::kDynamicPath),
             {Bytes(ic::test_provider::kDynamicBytes).begin(),
              Bytes(ic::test_provider::kDynamicBytes).end()}}};
      };
    fs::remove_all(source.SourceRoot());
    const auto result = ic::PublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work());
    Success(source, result, true, true);
    Require(producer.calls == 1 && producer.retained,
            "synchronous producer can copy real staged inspection owner");
  }
}
void ConfigurationPriority() {
  fixture::Fixture source;
  Producer producer{source};
  std::size_t phases = 0;
  std::size_t legacy = 0;
  ic::GltfFilePublicationOptions options{};
  options.verified_files_provider = {Producer::Provide, &producer};
  options.on_verified = [&](const auto&) {
    ++legacy;
    return std::vector<ic::GltfFileSupplement>{};
  };
  options.on_phase = [&](auto) { ++phases; };
  std::stop_source stop;
  stop.request_stop();
  const auto result = ic::PublishGltfTopologyBundle(
      {*source.source, *source.inspection}, source.Destination(), options,
      fixture::Limits(), fixture::Work(), stop.get_token());
  Require(!result.bundle &&
              result.diagnostic.status ==
                  ic::GltfFileStatus::kInvalidConfiguration &&
              result.diagnostic.code ==
                  ic::GltfFileDiagnosticCode::kInvalidRequest &&
              result.diagnostic.stage == ic::GltfFileStage::kConfiguration &&
              result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired &&
              phases == 0 && legacy == 0 && producer.calls == 0,
          "both producers fail configuration before GLB work and initial stop");
}
void JsonOnlyAndEntryAlias() {
  for (const bool changed : {false, true}) {
    ic::test_provider::Fixture source(changed, true, ic::GltfFileFormat::kGlb);
    const auto* original = source.source->Container();
    Require(
        original && original->ChunkCount() == 2,
        "literal JSON-only GLB has external resources and one opaque chunk");
    std::vector<fixture::Chunk> expected_chunks;
    for (std::size_t i = 0; i < original->ChunkCount(); ++i) {
      const auto chunk = original->Chunk(i);
      expected_chunks.push_back(
          {chunk.type, {chunk.bytes.begin(), chunk.bytes.end()}});
    }
    const auto prefix = source.inspection->Output().Json();
    if (changed) {
      expected_chunks[0].bytes.assign(prefix.begin(), prefix.end());
      expected_chunks[0].bytes.resize(
          prefix.size() + fixture::Padding(prefix.size()), std::byte{' '});
    }
    const auto expected =
        changed ? fixture::Wire(expected_chunks) : source.entry_bytes;
    fs::remove_all(source.SourceRoot());
    auto result = ic::PublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), {},
        fixture::Limits(), fixture::Work());
    Require(
        result.bundle && result.bundle->Format() == ic::GltfFileFormat::kGlb &&
            result.bundle->Container() &&
            result.bundle->Container()->ChunkCount() == 2 &&
            std::ranges::equal(result.bundle->EntryBytes(), expected) &&
            std::ranges::equal(
                fixture::ReadBytes(source.Destination() / fixture::kEntry),
                expected),
        "actual JSON-only GLB publishes unchanged/changed independent wire");
  }
  fixture::Fixture alias(false, 2, 1, 0, true, true);
  fs::remove_all(alias.SourceRoot());
  const auto result = ic::PublishGltfTopologyBundle(
      {*alias.source, *alias.inspection}, alias.Destination(), {},
      fixture::Limits(), fixture::Work());
  Success(alias, result);
  Require(result.bundle->Files().size() == alias.source->Files().size(),
          "entry used as a resource remains one physical file on exact no-op");
}
void EmptyProducerReverification() {
  enum class Change { kNone, kBytes, kExtra };
  for (const bool borrowed : {false, true}) {
    for (const auto change : {Change::kNone, Change::kBytes, Change::kExtra}) {
      fixture::Fixture source;
      Stage stage;
      auto testing = stage.Testing();
      std::size_t calls = 0;
      const auto callback = [&](const ic::GltfFileVerifiedContent& content) {
        ++calls;
        Require(content.format == ic::GltfFileFormat::kGlb && content.container,
                "empty producer observes actual staged GLB container");
        if (change == Change::kBytes) {
          auto altered = source.opaque;
          altered[0] ^= std::byte{1};
          fixture::Write(stage.path / "opaque-a.bin", altered);
        } else if (change == Change::kExtra) {
          fixture::Write(stage.path / "unexpected.bin", Bytes("extra"));
        }
      };
      struct State {
        const decltype(callback)* invoke;
      } state{&callback};
      ic::GltfFilePublicationOptions options;
      if (borrowed)
        options.verified_files_provider = {
            [](const ic::GltfFileVerifiedContent& content,
               void* opaque) -> std::span<const ic::GltfFileView> {
              (*static_cast<State*>(opaque)->invoke)(content);
              return {};
            },
            &state};
      else
        options.on_verified = [&](const auto& content) {
          callback(content);
          return std::vector<ic::GltfFileSupplement>{};
        };
      const auto result = detail::TestPublishGltfTopologyBundle(
          {*source.source, *source.inspection}, source.Destination(), options,
          fixture::Limits(), fixture::Work(), {}, testing);
      Require(calls == 1, "empty producer invoked exactly once");
      if (change == Change::kNone) {
        Success(source, result);
      } else {
        Require(result.diagnostic.status ==
                        ic::GltfFileStatus::kPreservationMismatch &&
                    result.diagnostic.stage == ic::GltfFileStage::kInventory &&
                    result.diagnostic.code ==
                        (change == Change::kBytes
                             ? ic::GltfFileDiagnosticCode::kResourceBytes
                             : ic::GltfFileDiagnosticCode::kStagedInventory),
                "empty producer still triggers full byte and physical "
                "inventory reverify");
        if (change == Change::kBytes)
          Require(result.diagnostic.file_index == 1 &&
                      result.diagnostic.byte_offset == 0,
                  "literal first changed opaque byte has sorted ordinal one");
        Removed(stage, source, result);
      }
    }
  }
}
void SupplementalCollisions() {
  for (const auto path : {fixture::kEntry, std::string_view("a.bin"),
                          fixture::kStaticPath, fixture::kDynamicPath}) {
    fixture::Fixture source;
    Stage stage;
    auto testing = stage.Testing();
    const std::array<ic::GltfFileView, 1> initial{
        {{fixture::kStaticPath, Bytes(fixture::kStaticBytes)}}};
    const std::array<ic::GltfFileView, 2> dynamic{
        {{path, Bytes("first")}, {path, Bytes("second")}}};
    ic::GltfFilePublicationOptions options;
    options.supplemental_files = initial;
    struct State {
      std::span<const ic::GltfFileView> files;
    } state{dynamic};
    options.verified_files_provider = {
        [](const auto&, void* value) {
          return static_cast<State*>(value)->files;
        },
        &state};
    const auto result = detail::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work(), {}, testing);
    Require(result.diagnostic.status == ic::GltfFileStatus::kInvalidRequest &&
                result.diagnostic.stage == ic::GltfFileStage::kPathPlanning &&
                result.diagnostic.code ==
                    ic::GltfFileDiagnosticCode::kPathCollision &&
                result.diagnostic.file_index ==
                    (path == fixture::kDynamicPath ? 6 : 5),
            "entry/resource/static/prior-dynamic collisions retain literal "
            "incoming ordinal");
    Removed(stage, source, result);
  }
}
enum class Mutation {
  kPrefixByte,
  kPrefixLength,
  kJsonPadding,
  kOpaqueByte,
  kOpaqueType,
  kOpaqueLength,
  kOpaqueOrder,
  kBinPadding,
  kBinTooShort,
  kBinExcessPadding
};
std::size_t PayloadOffset(const fixture::Fixture& source, std::size_t chunk) {
  auto offset = fixture::kHeaderBytes;
  const auto prefix_bytes = source.inspection->Output().Json().size();
  for (std::size_t i = 0; i < chunk; ++i)
    offset += fixture::kChunkHeaderBytes +
              (i == 0 ? prefix_bytes + fixture::Padding(prefix_bytes)
                      : source.chunks[i].bytes.size());
  return offset + fixture::kChunkHeaderBytes;
}
void StagedMutations() {
  for (const auto mutation :
       {Mutation::kPrefixByte, Mutation::kPrefixLength, Mutation::kJsonPadding,
        Mutation::kOpaqueByte, Mutation::kOpaqueType, Mutation::kOpaqueLength,
        Mutation::kOpaqueOrder, Mutation::kBinPadding, Mutation::kBinTooShort,
        Mutation::kBinExcessPadding}) {
    fixture::Fixture source(true, 2, 1, 1);
    Stage stage;
    auto testing = stage.Testing();
    std::size_t callbacks = 0;
    auto expected_offset = fixture::kJsonPayloadOffset;
    ic::GltfFilePublicationOptions options{};
    options.on_verified = [&](const auto&) {
      ++callbacks;
      return std::vector<ic::GltfFileSupplement>{};
    };
    options.on_phase = [&](auto phase) {
      if (phase != ic::GltfFilePublicationPhase::kVerification) return;
      auto wire = source.ExpectedWire();
      if (mutation == Mutation::kPrefixByte)
        wire[expected_offset] = std::byte{'!'};
      else if (mutation == Mutation::kPrefixLength) {
        auto chunks = source.chunks;
        const auto prefix = source.inspection->Output().Json();
        chunks[0].bytes.assign(prefix.begin(), prefix.end());
        chunks[0].bytes.resize(prefix.size() + fixture::Padding(prefix.size()) +
                                   fixture::kAlignmentBytes,
                               std::byte{' '});
        expected_offset += prefix.size();
        wire = fixture::Wire(chunks);
      } else if (mutation == Mutation::kOpaqueByte) {
        expected_offset = PayloadOffset(source, fixture::kFirstOpaqueOrdinal);
        wire[expected_offset] ^= std::byte{1};
      } else if (mutation == Mutation::kJsonPadding) {
        const auto prefix = source.inspection->Output().Json();
        Require(fixture::Padding(prefix.size()) > 0,
                "fixture has actual added JSON padding to corrupt");
        expected_offset += prefix.size();
        wire[expected_offset] = std::byte{'\t'};
      } else if (mutation == Mutation::kOpaqueType ||
                 mutation == Mutation::kOpaqueLength ||
                 mutation == Mutation::kOpaqueOrder) {
        auto chunks = source.chunks;
        const auto prefix = source.inspection->Output().Json();
        chunks[0].bytes.assign(prefix.begin(), prefix.end());
        chunks[0].bytes.resize(prefix.size() + fixture::Padding(prefix.size()),
                               std::byte{' '});
        expected_offset = PayloadOffset(source, fixture::kFirstOpaqueOrdinal);
        if (mutation == Mutation::kOpaqueLength) {
          expected_offset += chunks[fixture::kFirstOpaqueOrdinal].bytes.size();
          chunks[fixture::kFirstOpaqueOrdinal].bytes.resize(
              chunks[fixture::kFirstOpaqueOrdinal].bytes.size() +
              fixture::kAlignmentBytes);
        } else {
          expected_offset -= fixture::kWordBytes;
          if (mutation == Mutation::kOpaqueType)
            chunks[fixture::kFirstOpaqueOrdinal].type =
                fixture::kOtherOpaqueType;
          else
            std::swap(chunks[fixture::kFirstOpaqueOrdinal],
                      chunks[fixture::kFirstOpaqueOrdinal + 1]);
        }
        wire = fixture::Wire(chunks);
      } else {
        const auto logical =
            fixture::kIndexOffset +
            source.source_triangles.size() * fixture::kSourceIndexBytes +
            fixture::kPngBytes + 1;
        expected_offset =
            PayloadOffset(source, fixture::kBinChunkOrdinal) + logical;
        if (mutation == Mutation::kBinPadding) {
          wire[expected_offset] = std::byte{1};
        } else {
          auto chunks = source.chunks;
          const auto prefix = source.inspection->Output().Json();
          chunks[0].bytes.assign(prefix.begin(), prefix.end());
          chunks[0].bytes.resize(
              prefix.size() + fixture::Padding(prefix.size()), std::byte{' '});
          auto& bin = chunks[fixture::kBinChunkOrdinal].bytes;
          if (mutation == Mutation::kBinTooShort) {
            bin.resize(bin.size() - fixture::kAlignmentBytes);
            expected_offset =
                PayloadOffset(source, fixture::kBinChunkOrdinal) + bin.size();
          } else {
            bin.resize(bin.size() + fixture::kAlignmentBytes);
            constexpr std::size_t kMaximumPaddingBytes = 3;
            expected_offset += kMaximumPaddingBytes;
          }
          wire = fixture::Wire(chunks);
        }
      }
      fixture::Write(stage.path / fixture::kEntry, wire);
    };
    const auto result = detail::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), fixture::Work(), {}, testing);
    const bool padding = mutation == Mutation::kBinPadding ||
                         mutation == Mutation::kBinExcessPadding;
    const bool binding = mutation == Mutation::kBinTooShort;
    const bool json_padding = mutation == Mutation::kJsonPadding;
    Require(
        result.diagnostic.status ==
                (padding || binding
                     ? ic::GltfFileStatus::kMalformed
                     : ic::GltfFileStatus::kPreservationMismatch) &&
            result.diagnostic.stage == ic::GltfFileStage::kContainer &&
            result.diagnostic.code ==
                (padding || json_padding
                     ? ic::GltfFileDiagnosticCode::kContainerPadding
                 : binding
                     ? ic::GltfFileDiagnosticCode::kContainerBinding
                     : ic::GltfFileDiagnosticCode::kContainerPreservation) &&
            result.diagnostic.byte_offset == expected_offset && callbacks == 0,
        "actual staged GLB prefix/BIN/opaque failure precedes producer and "
        "reader fallback");
    Removed(stage, source, result);
  }
}
void KnownPrefixBeforeLateStop() {
  struct JoinedStop {
    ic::gltf_detail::NativeWorkerBackend original =
        ic::gltf_detail::DefaultNativeWorkerBackend();
    std::stop_source stop;
    std::size_t launches = 0;
    std::size_t joins = 0;
    static std::uintptr_t Launch(ic::gltf_detail::NativeWorkerRecord* record,
                                 void* state) {
      auto& self = *static_cast<JoinedStop*>(state);
      const auto handle = self.original.launch(record, self.original.state);
      ++self.launches;
      return handle;
    }
    static void Join(std::uintptr_t handle, void* state) noexcept {
      auto& self = *static_cast<JoinedStop*>(state);
      self.original.join(handle, self.original.state);
      ++self.joins;
      self.stop.request_stop();
    }
  };
  for (const bool mismatch : {false, true}) {
    fixture::Fixture source;
    Stage stage;
    JoinedStop joined;
    auto testing = stage.Testing();
    testing.verification_execution.backend = {JoinedStop::Launch,
                                              JoinedStop::Join, &joined};
    auto work = fixture::Work();
    constexpr std::uint64_t kPrefixPartitionBytes = 64;
    work.files.partition_bytes = kPrefixPartitionBytes;
    ic::GltfFilePublicationOptions options;
    options.on_phase = [&](auto phase) {
      if (phase != ic::GltfFilePublicationPhase::kVerification || !mismatch)
        return;
      auto wire = source.ExpectedWire();
      wire[fixture::kJsonPayloadOffset] = std::byte{'!'};
      fixture::Write(stage.path / fixture::kEntry, wire);
    };
    const auto result = detail::TestPublishGltfTopologyBundle(
        {*source.source, *source.inspection}, source.Destination(), options,
        fixture::Limits(), work, joined.stop.get_token(), testing);
    Require(
        joined.launches > 0 && joined.joins == joined.launches &&
            joined.stop.stop_requested(),
        "late stop follows an actual joined prefix-comparison native worker");
    if (mismatch)
      Require(
          result.diagnostic.status ==
                  ic::GltfFileStatus::kPreservationMismatch &&
              result.diagnostic.stage == ic::GltfFileStage::kContainer &&
              result.diagnostic.code ==
                  ic::GltfFileDiagnosticCode::kContainerPreservation &&
              result.diagnostic.byte_offset == fixture::kJsonPayloadOffset,
          "known literal first prefix mismatch survives late cooperative stop");
    else
      Require(result.outcome == ic::GltfFilePublicationOutcome::kCanceled,
              "matching prefix still honors actual late cancellation");
    Removed(stage, source, result);
  }
}
void PhaseStopsAndDestinationConflicts() {
  constexpr std::array kPhases{ic::GltfFilePublicationPhase::kPreflight,
                               ic::GltfFilePublicationPhase::kStaging,
                               ic::GltfFilePublicationPhase::kVerification,
                               ic::GltfFilePublicationPhase::kPublication};
  static constexpr std::string_view kOtherBytes =
      "existing destination owner\n";
  static constexpr std::string_view kOtherPath = "owner.txt";
  for (const bool changed : {false, true}) {
    fixture::Fixture source(changed);
    const auto retained = source.context.ActiveTrackedPayload();
    for (const auto phase : kPhases) {
      Stage stage;
      auto testing = stage.Testing();
      std::stop_source stop;
      std::size_t publication_phases = 0;
      ic::GltfFilePublicationOptions options;
      options.on_phase = [&](auto actual) {
        if (actual == ic::GltfFilePublicationPhase::kPublication)
          ++publication_phases;
        if (actual == phase) stop.request_stop();
      };
      const auto result = detail::TestPublishGltfTopologyBundle(
          {*source.source, *source.inspection}, source.Destination(), options,
          fixture::Limits(), fixture::Work(), stop.get_token(), testing);
      Require(
          !result.bundle &&
              result.outcome == ic::GltfFilePublicationOutcome::kCanceled &&
              result.diagnostic.status == ic::GltfFileStatus::kCanceled &&
              result.diagnostic.code ==
                  ic::GltfFileDiagnosticCode::kStopRequested &&
              result.phase == phase && !fs::exists(source.Destination()) &&
              publication_phases ==
                  (phase == ic::GltfFilePublicationPhase::kPublication ? 1 : 0),
          "changed/no-op GLB stop in each actual phase prevents commit");
      const bool before_staging =
          phase == ic::GltfFilePublicationPhase::kPreflight ||
          phase == ic::GltfFilePublicationPhase::kStaging;
      if (before_staging)
        Require(stage.path.empty() &&
                    result.cleanup == ic::GltfFileCleanupOutcome::kNotRequired,
                "early GLB phase stop creates no staging root");
      else
        Removed(stage, source, result);
      Require(source.context.ActiveTrackedPayload() == retained &&
                  source.context.ActiveWorkers() == 0,
              "each GLB phase stop restores retained ledger after joins");
    }
    for (const bool racing : {false, true}) {
      for (const bool ordinary_file : {false, true}) {
        struct Conflict {
          Stage stage;
          fs::path destination;
          bool ordinary_file;
          std::size_t commits = 0;
        } conflict{{}, source.Destination(), ordinary_file};
        const auto create = [&] {
          if (!ordinary_file)
            Require(fs::create_directory(conflict.destination),
                    "independent existing destination directory created");
          fixture::Write(ordinary_file ? conflict.destination
                                       : conflict.destination / kOtherPath,
                         Bytes(kOtherBytes));
        };
        auto testing = conflict.stage.Testing();
        testing.hooks.state = &conflict;
        testing.hooks.stage_ready = [](detail::TestNativeView path,
                                       void* state) {
          static_cast<Conflict*>(state)->stage.path = fs::path(path);
        };
        if (racing)
          testing.hooks.before_commit = [](detail::TestNativeView,
                                           void* state) {
            auto& target = *static_cast<Conflict*>(state);
            ++target.commits;
            if (!target.ordinary_file)
              Require(fs::create_directory(target.destination),
                      "independent racing destination directory created");
            fixture::Write(target.ordinary_file
                               ? target.destination
                               : target.destination / kOtherPath,
                           Bytes(kOtherBytes));
          };
        else
          create();
        const auto result = detail::TestPublishGltfTopologyBundle(
            {*source.source, *source.inspection}, source.Destination(), {},
            fixture::Limits(), fixture::Work(), {}, testing);
        Require(
            !result.bundle &&
                result.outcome == ic::GltfFilePublicationOutcome::kFailed &&
                result.diagnostic.status ==
                    ic::GltfFileStatus::kDestinationExists &&
                result.diagnostic.code ==
                    ic::GltfFileDiagnosticCode::kDestinationExists &&
                result.phase ==
                    (racing ? ic::GltfFilePublicationPhase::kPublication
                            : ic::GltfFilePublicationPhase::kPreflight) &&
                conflict.commits == (racing ? 1 : 0) &&
                std::ranges::equal(
                    fixture::ReadBytes(ordinary_file
                                           ? conflict.destination
                                           : conflict.destination / kOtherPath),
                    Bytes(kOtherBytes)),
            "changed/no-op GLB preserves existing/racing directory/file bytes");
        Require(
            result.cleanup ==
                    (racing ? ic::GltfFileCleanupOutcome::kRemoved
                            : ic::GltfFileCleanupOutcome::kNotRequired) &&
                (racing ? !conflict.stage.path.empty() &&
                              !fs::exists(conflict.stage.path)
                        : conflict.stage.path.empty()) &&
                source.context.ActiveTrackedPayload() == retained &&
                source.context.ActiveWorkers() == 0,
            "GLB no-replace failure removes only owned staging after joins");
        if (!ordinary_file)
          Require(std::distance(fs::directory_iterator(conflict.destination),
                                fs::directory_iterator{}) == 1,
                  "other destination directory retains exactly its own file");
        fs::remove_all(conflict.destination);
      }
    }
    Require(
        std::ranges::equal(
            fixture::ReadBytes(source.SourceRoot() / fixture::kEntry),
            source.wire),
        "all canceled/conflicting GLB attempts preserve original full wire");
  }
}
void UnsupportedDependency() {
  fixture::Fixture source(true, 2, 1, 0, false);
  Require(source.source->Format() == ic::GltfFileFormat::kGlb &&
              source.source->Container() &&
              source.source->Container()->ChunkCount() == fixture::kChunkCount,
          "actual retained rich GLB owner before numerical-profile refusal");
  source.Prepare(true, 1, false);
  Require(
      !source.materialization.primitive &&
          source.materialization.status ==
              ic::GltfMaterializationStatus::kGeometryFailure &&
          source.materialization.diagnostic.geometry_status ==
              geo::MaterializationStatus::kUnsupportedAllocationProfile,
      "genuine unsupported materialization creates no fake publication input");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == kUnsupportedArgument;
    Require(argc == 1 || unsupported,
            "known GLB publication semantic fixture invocation");
    if (unsupported)
      UnsupportedDependency();
    else {
      ic::test_translation_publication::Positive(true);
      IdentityAndChangedPadding();
      BothProducers();
      ConfigurationPriority();
      StagedMutations();
      JsonOnlyAndEntryAlias();
      EmptyProducerReverification();
      SupplementalCollisions();
      KnownPrefixBeforeLateStop();
      PhaseStopsAndDestinationConflicts();
    }
    std::cout << "independent GLB publication semantics passed" << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
