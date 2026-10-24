// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "off_files_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::off_files_fixture;
namespace fs = std::filesystem;
using f::Require;
static_assert(std::is_nothrow_copy_constructible_v<ic::OffFileAsset>);
static_assert(std::is_nothrow_copy_assignable_v<ic::OffFileAsset>);
static_assert(std::is_nothrow_copy_constructible_v<ic::OffPublishedBundle>);
static_assert(std::is_nothrow_copy_assignable_v<ic::OffPublishedBundle>);

struct Receipt {
  std::array<std::byte, 7> bytes{
      std::byte{'O'},  std::byte{'F'},  std::byte{'F'}, std::byte{0},
      std::byte{0xff}, std::byte{'\n'}, std::byte{'!'}};
  std::size_t calls = 0;
  bool changed = false;
  bool empty = false;
  bool verified_alias = false;
  std::optional<ic::OffDocument> retained;
  static std::span<const std::byte> Provide(
      const ic::OffFileVerifiedContent& content, void* state) {
    auto& self = *static_cast<Receipt*>(state);
    ++self.calls;
    f::Check(content.document.Mesh(), self.changed, self.empty);
    Require(content.document.Inspection().IsStorageValid() &&
                content.document.DeclaredEdgeCount() == 0,
            "provider sees actual inspected canonical staged document");
    self.verified_alias =
        content.entry_bytes.data() == content.document.OriginalBytes().data() &&
        std::ranges::equal(content.entry_bytes,
                           content.document.OriginalBytes()) &&
        !content.entry_path.empty();
    self.retained.emplace(content.document);
    return self.bytes;
  }
};

void RoundTrip(bool empty, bool changed, bool receipt) {
  f::Root root;
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  const auto input = root.Path() / fs::path(u8"原始-🌱.off");
  const auto destination = root.Path() / fs::path(u8"输出-🌿");
  const auto raw = empty ? f::kEmpty : f::kSource;
  f::Write(input, raw);
  {
    auto capture =
        ic::ReadOffFileAsset(input, f::ReadLimits(), f::ReadWork(), context);
    Require(capture.status == ic::OffFileStatus::kSuccess && capture.asset,
            "actual Unicode regular-file capture");
    f::Joined(capture.execution);
    auto& source = *capture.asset;
    Require(source.EntryPath() == f::Utf8(u8"原始-🌱.off") &&
                std::ranges::equal(source.EntryBytes(), f::Bytes(raw)) &&
                source.EntryBytes().data() ==
                    source.Document().OriginalBytes().data() &&
                source.Document().DeclaredEdgeCount() == 17 &&
                source.NewRequestedBytes() > 0,
            "one retained raw document, portable UTF8 name and original edge");
    f::Check(source.Document().Mesh(), false, empty);
    auto expected = changed ? f::Candidate(source) : source.Document().Mesh();
    auto output = f::Output(source, expected);
    const auto original_output =
        std::vector<std::byte>(output.Bytes().begin(), output.Bytes().end());
    Require(fs::remove(input), "actual external source removal");
    f::Check(source.Document().Mesh(), false, empty);
    Receipt provider;
    provider.changed = changed;
    provider.empty = empty;
    const auto original_receipt = provider.bytes;
    ic::OffFilePublicationOptions options;
    if (receipt)
      options.receipt = ic::OffFileReceiptProvider{
          "verified.receipt", &Receipt::Provide, &provider};
    auto published =
        ic::PublishOffBundle({source, expected, output}, destination, options,
                             f::PublishLimits(), f::PublishWork());
    Require(published.outcome == ic::OffFilePublicationOutcome::kPublished &&
                published.bundle &&
                published.phase == ic::OffFileStage::kComplete &&
                published.diagnostic.status == ic::OffFileStatus::kSuccess &&
                published.cleanup == ic::OffFileCleanupOutcome::kNotRequired &&
                !published.cleanup_error,
            "real native commit exposes one complete bundle");
    f::Joined(published.execution);
    auto& bundle = *published.bundle;
    f::Check(bundle.ExpectedMesh(), changed, empty);
    f::Check(bundle.Document().Mesh(), changed, empty);
    f::Check(bundle.Source().Document().Mesh(), false, empty);
    Require(
        bundle.EntryPath() == source.EntryPath() &&
            bundle.Document().DeclaredEdgeCount() == 0 &&
            bundle.EntryBytes().data() ==
                bundle.Document().OriginalBytes().data() &&
            std::ranges::equal(bundle.EntryBytes(), original_output) &&
            std::ranges::equal(bundle.Source().EntryBytes(), f::Bytes(raw)) &&
            bundle.NewRequestedBytes() > 0,
        "published owner aliases real staged bytes and retains exact source");
    Require(f::Read(destination / input.filename()) == original_output,
            "independent physical entry bytes equal actual serialized bytes");
    auto reload = ic::ReadOffFileAsset(destination / input.filename(),
                                       f::ReadLimits(), f::ReadWork(), context);
    Require(reload.asset.has_value(),
            "independent published-path native reload");
    f::Check(reload.asset->Document().Mesh(), changed, empty);
    if (receipt) {
      Require(
          provider.calls == 1 && provider.verified_alias && provider.retained &&
              bundle.ReceiptPath() == "verified.receipt" &&
              bundle.ReceiptBytes() &&
              std::ranges::equal(*bundle.ReceiptBytes(), original_receipt) &&
              std::ranges::equal(f::Read(destination / "verified.receipt"),
                                 original_receipt),
          "provider called once after verification; imported exact receipt");
      provider.bytes[0] = std::byte{'X'};
      Require(std::ranges::equal(*bundle.ReceiptBytes(), original_receipt),
              "receipt owner does not borrow caller backing after return");
      f::Check(provider.retained->Mesh(), changed, empty);
      f::Inventory(destination, source.EntryPath(), "verified.receipt");
    } else {
      Require(provider.calls == 0 && !bundle.ReceiptPath() &&
                  !bundle.ReceiptBytes(),
              "receipt-free public operation creates no fabricated supplement");
      f::Inventory(destination, source.EntryPath());
    }
    const auto before = f::Read(destination / input.filename());
    const auto second =
        ic::PublishOffBundle({source, expected, output}, destination, options,
                             f::PublishLimits(), f::PublishWork());
    Require(
        second.outcome == ic::OffFilePublicationOutcome::kFailed &&
            !second.bundle &&
            second.diagnostic.status == ic::OffFileStatus::kDestinationExists &&
            second.diagnostic.code ==
                ic::OffFileDiagnosticCode::kDestinationExists &&
            f::Read(destination / input.filename()) == before &&
            provider.calls == static_cast<std::size_t>(receipt),
        "existing destination is never replaced and provider is not replayed");
  }
  f::Empty(context);
}

void OutliveContext() {
  f::Root root;
  const auto input = root.Path() / "original.off";
  f::Write(input, f::kSource);
  std::optional<ic::OffFileAsset> retained;
  {
    geo::ExecutionContext original({4, 1, f::kContextBytes});
    auto capture =
        ic::ReadOffFileAsset(input, f::ReadLimits(), f::ReadWork(), original);
    Require(capture.asset.has_value(), "lifetime fixture captures actual file");
    retained.emplace(*capture.asset);
  }
  Require(fs::remove(input), "source is removed after original context dies");
  f::Check(retained->Document().Mesh());
  std::optional<ic::OffPublishedBundle> final;
  {
    auto candidate = f::Candidate(*retained);
    auto output = f::Output(*retained, candidate);
    auto published = ic::PublishOffBundle({*retained, candidate, output},
                                          root.Path() / "after-context", {},
                                          f::PublishLimits(), f::PublishWork());
    Require(
        published.bundle.has_value(),
        "retained source pins original ledger for later writer/publication");
    final.emplace(*published.bundle);
  }
  retained.reset();
  f::Check(final->Document().Mesh(), true);
  Require(
      std::ranges::equal(final->Source().EntryBytes(), f::Bytes(f::kSource)),
      "published source survives original asset/output/mesh/context death");
  const auto entry = final->EntryBytes();
  std::optional<ic::OffPublishedBundle> copy(*final);
  final.reset();
  Require(copy->EntryBytes().data() == entry.data(),
          "strong copies share original bytes without a counter-derived copy");
  auto ledger = copy->ExpectedMesh().ShareExecutionContext();
  {
    auto last = *copy;
    f::Check(last.ExpectedMesh(), true);
  }
  // The ledger is acquired only after the original context and every producing
  // argument have died; it cannot mask the owner's original-context lifetime.
  Require(ledger.ActiveTrackedPayload() > 0 && ledger.ActiveWorkers() == 0,
          "retained bundle owns actual live payload and no worker");
  copy.reset();
  f::Empty(ledger);
}

void Unsupported() {
  f::Root root;
  geo::ExecutionContext context({4, 1, f::kContextBytes});
  const auto input = root.Path() / "nonempty.off";
  f::Write(input, f::kSource);
  const auto capture =
      ic::ReadOffFileAsset(input, f::ReadLimits(), f::ReadWork(), context);
  Require(
      capture.status == ic::OffFileStatus::kOffFailure && !capture.asset &&
          capture.diagnostic.off_status == ic::OffStatus::kGeometryFailure &&
          capture.diagnostic.off_diagnostic &&
          capture.diagnostic.off_diagnostic->geometry_status ==
              geo::MaterializationStatus::kUnsupportedAllocationProfile,
      "nonempty unsupported numerical capture reports its real delegate");
  f::Joined(capture.execution);
  f::Empty(context);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "known source-selected profile only");
    RoundTrip(true, false, false);
    RoundTrip(true, false, true);
    if (unsupported) {
      Unsupported();
    } else {
      for (const bool changed : {false, true})
        for (const bool receipt : {false, true})
          RoundTrip(false, changed, receipt);
      OutliveContext();
    }
    std::cout
        << "OFF files: literal geometry, raw capture, Unicode, receipts and "
           "physical publication passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
