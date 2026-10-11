// SPDX-License-Identifier: Apache-2.0
#include <exception>
#include <iostream>
#include <string_view>

#include "fixture_support.h"

namespace {
constexpr std::string_view kLargeFixtureArgument = "--large-source-index";
constexpr std::string_view kResourceArgument = "--resource-budget-mib";
constexpr std::string_view kLargeBudgetMebibytes = "2048";
constexpr int kResourceAcknowledgmentArguments = 4;
}  // namespace

int main(int argc, char** argv) {
  namespace tests = gltf_materialization_semantics;
  try {
    if (argc == 1) {
      tests::CheckRawChannels();
      tests::CheckTopologyAndSelection();
      tests::CheckSourceAndMeshLifetimes();
      std::cout
          << "glTF transfer semantics passed: raw encodings, topology/maps, "
             "face selection and independent source/mesh lifetimes\n";
    } else {
      tests::Require(argc == kResourceAcknowledgmentArguments &&
                         argv[1] == kLargeFixtureArgument &&
                         argv[2] == kResourceArgument &&
                         argv[3] == kLargeBudgetMebibytes,
                     "Large fixture requires --large-source-index "
                     "--resource-budget-mib 2048 (2 GiB ledger, approximately "
                     "576 MiB numerical payload; sanitizer RSS is additional)");
      tests::CheckLargeSourceIndex();
      std::cout << "Actual source index 16777217 preserved through 16777218 "
                   "physical decoded/materialized XYZ rows; ledger cap 2 GiB\n";
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
