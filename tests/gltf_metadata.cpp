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
void Metadata(std::string_view input, GltfStatus expected) {
  meshvale::geometry::ExecutionContext context({2, 1, 1048576});
  AllocationContext allocation(context, {});
  auto capture =
      CaptureJsonSnapshot(input, {65536, 4096, 65536, 64}, allocation);
  Require(capture.snapshot.has_value());
  AssetState state(context, {}, {}, {}, *capture.snapshot);
  try {
    AdmitMetadata(state);
  } catch (const GltfFailure& failure) {
    Require(failure.Diagnostic().status == expected);
    return;
  }
  Require(expected == GltfStatus::kAccepted);
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
    Metadata(R"({"asset":{"version":"2.0"}})", GltfStatus::kAccepted);
    Metadata(
        R"({"asset":{"version":"2.0"},"extras":{"huge":999999999999999999999999999999999999999999999999,"number":1e999999999999999999999999},"extensions":{"CUSTOM":{"v":"opaque"}}})",
        GltfStatus::kAccepted);
    Metadata(
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1e0,"type":"VEC3","min":[-0,0,1],"max":[1,2,3]}]})",
        GltfStatus::kAccepted);
    Metadata(
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1.01,"type":"VEC3"}]})",
        GltfStatus::kMalformed);
    Metadata(
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"future"}]})",
        GltfStatus::kUnsupportedProfile);
    Metadata(
        R"({"asset":{"version":"2.0"},"bufferViews":[{"buffer":0,"byteLength":1,"byteStride":5}]})",
        GltfStatus::kMalformed);
    Metadata(
        R"({"asset":{"version":"2.0"},"images":[{"uri":"a.png","bufferView":0,"mimeType":"image/png"}]})",
        GltfStatus::kMalformed);
    Metadata(R"({"asset":{"version":"2.0"},"nodes":[{"skin":0}]})",
             GltfStatus::kMalformed);
    Metadata(
        R"({"asset":{"version":"2.0"},"nodes":[{"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"translation":[0,0,0]}]})",
        GltfStatus::kMalformed);
    Metadata(R"({"asset":{"version":"2.0"},"skins":[{"joints":[0,0]}]})",
             GltfStatus::kMalformed);
    Metadata(R"({"asset":{"version":"2.0"},"extensionsRequired":["CUSTOM"]})",
             GltfStatus::kUnsupportedProfile);
    Metadata(R"({"asset":{"version":"2.0"},"extensions":{"CUSTOM":7}})",
             GltfStatus::kMalformed);
    Metadata(
        R"({"asset":{"version":"2.0"},"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,2]}}]})",
        GltfStatus::kMalformed);
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
