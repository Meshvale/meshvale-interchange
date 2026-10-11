// SPDX-License-Identifier: Apache-2.0
#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string_view>
#include <system_error>

#include "fixture_support.h"

namespace {
constexpr std::string_view kLargeCaseArgument = "--large-source-index";
constexpr std::string_view kResourceBudgetArgument = "--resource-budget-mib";
constexpr int kLargeArgumentCount = 4;
constexpr int kDefaultArgumentCount = 1;
}  // namespace
int main(int argc, char** argv) {
  using namespace gltf_topology_write_semantics;
  try {
    if (argc == kLargeArgumentCount && argv[1] == kLargeCaseArgument &&
        argv[2] == kResourceBudgetArgument) {
      const auto spelling = std::string_view(argv[3]);
      std::uint64_t budget = 0;
      const auto parsed = std::from_chars(
          spelling.data(), spelling.data() + spelling.size(), budget);
      Require(parsed.ec == std::errc{} &&
                  parsed.ptr == spelling.data() + spelling.size(),
              "Resource budget must be an exact unsigned decimal MiB count");
      CheckLargeIndex(budget);
      std::cout << "Required public topology writer UInt32 index 16777217 "
                   "qualification passed\n";
      return 0;
    }
    Require(
        argc == kDefaultArgumentCount,
        "Expected no arguments or the explicit large-index budget invocation");
    CheckSelectionAndReload();
    CheckIdentityAndLifetimes();
    CheckRejectedCandidates();
    std::cout << "Independent glTF topology writer semantic fixtures passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
