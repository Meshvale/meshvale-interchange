// SPDX-License-Identifier: Apache-2.0
#include "gltf_metadata.h"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace meshvale::interchange;
using namespace meshvale::interchange::gltf_detail;
void Require(bool result) {
  if (!result) throw std::runtime_error("exact metadata expectation failed");
}
void Rejected(std::string_view text, GltfStatus expected) {
  try {
    (void)ExactUnsigned(text);
  } catch (const GltfFailure& failure) {
    Require(failure.Diagnostic().status == expected);
    return;
  }
  throw std::runtime_error("expected exact metadata rejection");
}
}  // namespace
int main() {
  try {
    Require(ExactUnsigned("18446744073709551615") ==
            std::numeric_limits<std::uint64_t>::max());
    Require(ExactUnsigned("1.8446744073709551615e19") ==
            std::numeric_limits<std::uint64_t>::max());
    Require(ExactSigned("-9223372036854775808") ==
            std::numeric_limits<std::int64_t>::min());
    Require(ExactUnsigned("0e99999999999999999999999999") == 0);
    Require(ExactUnsigned("-0.0000e-99999999999999999999999999") == 0);
    Require(ExactUnsigned("100.00e-2") == 1);
    Require(ExactUnsigned("16777217") == 16777217);
    Rejected("1.00000000000000000000001", GltfStatus::kMalformed);
    Rejected("-1", GltfStatus::kMalformed);
    Rejected("18446744073709551616", GltfStatus::kUnsupportedProfile);
    Require(CompareDecimal("-1e3", "-999") < 0);
    Require(CompareDecimal("100.000e-2", "1") == 0);
    Require(CompareDecimal("1.00000000000000000001", "1") > 0);
    Require(CompareDecimal("-0.00e99999999999999999", "0") == 0);
    Require(CompareDecimal("1.234e-20", "1.233999999999e-20") > 0);
    meshvale::geometry::ExecutionContext context({2, 1, 1048576});
    const AllocationContext allocation(context, {});
    auto capture = CaptureJsonSnapshot(
        "[-0e99999999999999999999,1.401298464324817e-45,16777217]",
        {1024, 128, 1024, 16}, allocation);
    Require(capture.snapshot.has_value());
    const auto& document = *capture.snapshot;
    Require(ReadFloatBits(document, *ChildAt(document, 0, 0)) == 0x80000000u);
    Require(ReadFloatBits(document, *ChildAt(document, 0, 1)) == 1);
    Require(ReadExactUnsigned(document, *ChildAt(document, 0, 2)) == 16777217);
    std::cout << "exact metadata boundaries passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
