// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/stl.h>
#include <meshvale/interchange/stl_files.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "stl_files_fixture.h"

namespace {
namespace ic = meshvale::interchange;
namespace geo = meshvale::geometry;
namespace f = ic::stl_files_fixture;
using f::Require;
template <class T>
constexpr bool kStrongOwner =
    !std::is_default_constructible_v<T> &&
    std::is_nothrow_copy_constructible_v<T> &&
    std::is_nothrow_copy_assignable_v<T> && std::is_nothrow_destructible_v<T>;
static_assert(kStrongOwner<ic::StlCapturedFile> &&
              kStrongOwner<ic::StlFileAsset> &&
              kStrongOwner<ic::StlVerifiedRewrite> &&
              kStrongOwner<ic::StlPublishedBundle>);
void Consumer(bool unsupported) {
  f::TempTree tree;
  const auto input = tree.root / std::filesystem::path(u8"\u90e8\u54c1.stl");
  const auto destination = tree.root / std::filesystem::path(u8"\u516c\u958b");
  std::optional<ic::StlPublishedBundle> retained;
  std::optional<ic::StlFileAsset> retained_source;
  {
    geo::ExecutionContext context({4, 1, f::kContextBytes});
    f::Write(input, f::Bytes(f::kAscii));
    const auto nonempty =
        ic::ReadStlFileAsset(input, ic::StlEncoding::kAscii, f::ReadLimits(),
                             f::ReadWork(), context);
    const bool use_empty = f::Unsupported(nonempty, unsupported);
    if (use_empty) f::Write(input, f::Bytes(f::kEmpty));
    const auto read =
        ic::ReadStlFileAsset(input, ic::StlEncoding::kAscii, f::ReadLimits(),
                             f::ReadWork(), context);
    Require(read.asset && !read.captured,
            "public path convenience returns genuine owner");
    retained_source = read.asset;
    Require(std::filesystem::remove(input),
            "remove actual source after retained capture");
    const auto result = ic::RewriteStlFile(
        *read.asset, destination, f::Retain(ic::StlEncoding::kAscii),
        f::RewriteLimits(), f::RewriteWork());
    f::Published(result);
    retained = result.bundle;
  }
  // Caller context and every initial result wrapper have gone away.
  const auto& document = retained->Verified().Document();
  f::CheckDocument(document,
                   unsupported ? std::span<const f::Facet>{} : f::kFacetBits);
  const auto bytes =
      f::Read(destination / std::filesystem::path(u8"\u90e8\u54c1.stl"));
  Require(
      std::ranges::equal(bytes, retained->Verified().EntryBytes()),
      "public retained bundle survives session destruction and source removal");
  const auto shared = document.Mesh().ShareExecutionContext();
  const auto decoded =
      ic::ReadStl(bytes, ic::StlEncoding::kAscii, f::CodecReadLimits(),
                  f::CodecWork(), shared);
  Require(decoded.document.has_value(),
          "installed public actual physical reload");
  f::CheckDocument(*decoded.document,
                   unsupported ? std::span<const f::Facet>{} : f::kFacetBits);
  const auto immutable = f::Inventory(destination);
  const auto repeat = ic::RewriteStlFile(*retained_source, destination,
                                         f::Retain(ic::StlEncoding::kAscii),
                                         f::RewriteLimits(), f::RewriteWork());
  Require(repeat.status == ic::StlFileStatus::kDestinationExists &&
              !repeat.bundle && f::Inventory(destination) == immutable,
          "public no-replace preserves published inventory");
  auto copy = *retained;
  retained.reset();
  Require(copy.NewRequestedBytes() > 0 &&
              copy.Verified().Source().Sha256() == retained_source->Sha256(),
          "immutable copy retains complete charged lineage");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    const bool unsupported =
        argc == 2 && std::string_view(argv[1]) == "--unsupported-profile";
    Require(argc == 1 || unsupported, "known consumer profile argument");
    Consumer(unsupported);
    std::cout << "STL files public owner consumer passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
