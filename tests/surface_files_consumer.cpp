// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/surface_files.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>
#include <vector>

#include "surface_files_fixture.h"

namespace {
namespace mv = meshvale::interchange;
namespace geom = meshvale::geometry;
namespace test = mv::surface_files_fixture;
using test::Require;
static_assert(!std::is_default_constructible_v<mv::SurfacePublishedBundle>);
static_assert(std::is_nothrow_copy_constructible_v<mv::SurfacePublishedBundle>);
static_assert(std::is_nothrow_copy_assignable_v<mv::SurfacePublishedBundle>);
static_assert(std::is_nothrow_destructible_v<mv::SurfacePublishedBundle>);

void Consumer(mv::SurfaceEncoding encoding,
              const geom::ExecutionContext& context) {
  test::TempTree tree;
  const auto destination = tree.root / "application-output";
  std::optional<mv::SurfacePublishedBundle> owner;
  {
    const auto proof =
        test::MakeProof(encoding, mv::SurfaceRepresentation::kStrictBare,
                        test::Shape::kTriangle, context);
    if (!proof.inspection)
      return;  // Asserted real unsupported profile/normal refusal.
    const auto result = mv::PublishSurfaceBundle(*proof.inspection, destination,
                                                 {}, test::FileLimits(encoding),
                                                 test::FileWork(encoding));
    test::Published(result);
    owner = result.bundle;
  }
  // All original local E/V wrappers and the publication result have expired.
  const auto& retained = owner->Inspection();
  const auto members = retained.CapturedMembers();
  std::vector<std::string> paths;
  std::vector<std::vector<std::byte>> bytes;
  std::vector<mv::SuppliedSurfaceMember> views;
  for (const auto& member : members) {
    paths.emplace_back(member.path);
    bytes.push_back(test::Read(destination / member.path));
    Require(std::ranges::equal(bytes.back(), member.bytes),
            "consumer physically reopens every retained payload");
  }
  for (std::size_t i = 0; i < paths.size(); ++i)
    views.push_back({paths[i], bytes[i]});
  const auto verified = mv::InspectSurfaceOutput(retained.Expected(), views,
                                                 test::InspectLimits(encoding),
                                                 test::Work(encoding, true));
  Require(
      verified.status == mv::SurfaceStatus::kSuccess && verified.inspection,
      "public consumer obtains another real inspection from physical files");
  const auto receipt = owner->Receipt();
  Require(receipt.path == "publication.json" &&
              std::ranges::equal(test::Read(destination / receipt.path),
                                 receipt.bytes),
          "default receipt name and complete actual receipt bytes");
  const auto immutable = test::Inventory(destination);
  const auto repeat = mv::PublishSurfaceBundle(retained, destination, {},
                                               test::FileLimits(encoding),
                                               test::FileWork(encoding));
  test::NoOwner(repeat);
  Require(
      repeat.diagnostic.status == mv::SurfaceFileStatus::kDestinationExists &&
          test::Inventory(destination) == immutable,
      "public no-replace preserves successful output");
  std::stop_source stopped;
  stopped.request_stop();
  const auto canceled = mv::PublishSurfaceBundle(
      retained, tree.root / "canceled", {}, test::FileLimits(encoding),
      test::FileWork(encoding), stopped.get_token());
  Require(
      !canceled.bundle &&
          canceled.outcome == mv::SurfaceFilePublicationOutcome::kCanceled &&
          !std::filesystem::exists(tree.root / "canceled"),
      "public stopped request has no output");
  auto alias = *owner;
  owner.reset();
  Require(alias.Inspection().CapturedMembers().size() == members.size() &&
              alias.NewRequestedBytes() != 0,
          "copy-only public published owner retains content");
}
}  // namespace
int main() {
  try {
    geom::ExecutionContext context({4, 1, 1024 * test::kMiB});
    for (auto encoding : test::kEncodings) Consumer(encoding, context);
    Require(context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
            "public consumer releases all root charges and workers");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
