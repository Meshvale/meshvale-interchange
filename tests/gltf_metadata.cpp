// SPDX-License-Identifier: Apache-2.0
#include "gltf_metadata.h"

#include <array>
#include <bit>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "gltf_parser.h"
#include "meshvale/interchange/gltf.h"

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

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
  GltfReadLimits limits{65536, 65536, 131072, 8192, 64,   65536, 256,
                        256,   32,    8192,   8192, 8192, 8192,  131072};
  AssetState state(context, {}, limits, {8, 8, 8, 8}, *capture.snapshot);
  try {
    AdmitMetadata(state);
  } catch (const GltfFailure& failure) {
    Require(failure.Diagnostic().status == expected);
    return;
  }
  Require(expected == GltfStatus::kAccepted);
}
void Hex(std::string_view text) {
  constexpr std::string_view kHex = "0123456789abcdef";
  for (const auto c : text) {
    const auto value = static_cast<unsigned char>(c);
    std::cout << kHex[value >> 4] << kHex[value & 0xf];
  }
}
void Dump(const char* path) {
  std::ifstream file(path, std::ios::binary);
  Require(file.good());
  const std::string source{std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>()};
  meshvale::geometry::ExecutionContext context({2, 1, 67108864});
  const AllocationContext allocation(context, {});
  auto captured =
      CaptureJsonSnapshot(source, {1048576, 65536, 1048576, 128}, allocation);
  if (!captured.snapshot) {
    std::cout << "status\t" << static_cast<int>(GltfStatus::kMalformed) << '\n';
    return;
  }
  GltfReadLimits limits{1048576, 1048576, 2097152, 65536,  128,
                        1048576, 1024,    1024,    256,    65536,
                        65536,   65536,   65536,   2097152};
  AssetState state(context, {}, limits, {4096, 4096, 4096, 4096},
                   *captured.snapshot);
  try {
    AdmitMetadata(state);
  } catch (const GltfFailure& failure) {
    std::cout << "status\t" << static_cast<int>(failure.Diagnostic().status)
              << '\n';
    for (const auto rule : state.guard_rules) {
      const auto guard_path = GuardPath(rule);
      if (rule == std::numeric_limits<std::uint32_t>::max()) continue;
      std::cout << "guard\t" << rule << '\t';
      Hex(guard_path);
      std::cout << '\n';
    }
    return;
  }
  const auto projection = BuildCoreProjection(state);
  const auto parsed =
      InspectCoreProjection({projection.data(), projection.size()}, allocation);
  Require(parsed.accepted);
  // Core parser has already been destroyed before observing retained values.
  std::cout << "status\t" << static_cast<int>(GltfStatus::kAccepted) << '\n';
  std::cout << "source\t";
  Hex(state.document.Original());
  std::cout << '\n';
  std::cout << "projection\t";
  Hex({projection.data(), projection.size()});
  std::cout << '\n';
  for (std::size_t node = 0; node < state.document.NodeCount(); ++node) {
    std::cout << "node\t" << node << '\t'
              << static_cast<int>(state.document.Kind(node)) << '\t';
    if (const auto parent = state.document.Parent(node))
      std::cout << *parent;
    else
      std::cout << '-';
    std::cout << '\t';
    Hex(state.document.Key(node));
    std::cout << '\t';
    const auto kind = state.document.Kind(node);
    if (kind == JsonKind::kString) Hex(state.document.String(node));
    if (kind == JsonKind::kNumber) Hex(state.document.Number(node));
    if (kind == JsonKind::kBool)
      std::cout << (state.document.Boolean(node) ? "1" : "0");
    std::cout << '\t' << state.guard_rules[node] << '\t';
    Hex(GuardPath(state.guard_rules[node]));
    std::cout << '\n';
  }
}

class FloatEnvironmentRestore {
 public:
  FloatEnvironmentRestore() {
    Require(std::fegetenv(&saved_) == 0);
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    saved_mxcsr_ = _mm_getcsr();
#endif
  }
  ~FloatEnvironmentRestore() {
    (void)std::fesetenv(&saved_);
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    _mm_setcsr(saved_mxcsr_);
#endif
  }

 private:
  std::fenv_t saved_{};
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  unsigned int saved_mxcsr_ = 0;
#endif
};
void FloatRequirement(bool value, std::string_view label, std::string_view mode,
                      std::string_view seam) {
  if (!value)
    throw std::runtime_error(std::string("float metadata ") +
                             std::string(label) + " / " + std::string(mode) +
                             " / " + std::string(seam));
}
void FloatMetadataEnvironments() {
  constexpr std::size_t kBytesPerFloat = sizeof(std::uint32_t);
  constexpr std::size_t kBitsPerByte = 8;
  constexpr std::uint32_t kByteMask = 0xff;
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  constexpr unsigned int kDenormalsAreZero = 0x0040;
  constexpr unsigned int kFlushToZero = 0x8000;
  constexpr unsigned int kMxcsrControls = 0xffc0;
#endif
  struct Mode {
    std::string_view name;
    int rounding;
    unsigned int flags;
  };
  const auto modes = std::to_array<Mode>({
      {"ordinary", FE_TONEAREST, 0},
      {"rounding", FE_UPWARD, 0},
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
      {"DAZ", FE_TONEAREST, kDenormalsAreZero},
      {"FTZ", FE_TONEAREST, kFlushToZero},
      {"DAZ+FTZ+rounding", FE_UPWARD, kDenormalsAreZero | kFlushToZero},
#endif
  });
  struct Positive {
    std::string_view text;
    std::uint32_t bits;
  };
  constexpr std::array<Positive, 10> kPositive{{
      {"0", 0},
      {"-0", 0x80000000U},
      {"1e-45", 1},
      {"-1e-45", 0x80000001U},
      {"1."
       "40129846432481707092372958328991613128026194187651577175706828388979108"
       "268586060148663818836212158203125e-45",
       1},
      {"1.1754942106924411e-38", 0x007fffffU},
      {"1.1754943508222875e-38", 0x00800000U},
      {"16777217", 0x4b800000U},
      {"-1.5", 0xbfc00000U},
      {"340282346638528859811704183484516925440", 0x7f7fffffU},
  }};
  constexpr std::array<std::string_view, 3> kUnsupported{
      "1e-999", "-1e-999", "340282346638528859811704183484516925441"};
  for (const auto& mode : modes) {
    FloatEnvironmentRestore restore;
    Require(std::fesetround(mode.rounding) == 0);
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    _mm_setcsr((_mm_getcsr() & ~(kDenormalsAreZero | kFlushToZero)) |
               mode.flags);
    const auto caller_mxcsr = _mm_getcsr();
#endif
    for (const auto& test : kPositive) {
      meshvale::geometry::ExecutionContext context({2, 1, 1048576});
      const AllocationContext allocation(context, {});
      const auto text = std::string("[") + std::string(test.text) + "]";
      const auto captured =
          CaptureJsonSnapshot(text, {65536, 4096, 65536, 64}, allocation);
      FloatRequirement(captured.snapshot.has_value(), test.text, mode.name,
                       "numeric snapshot");
      const auto source =
          std::string(
              R"({"asset":{"version":"2.0"},"buffers":[{"uri":"value.bin","byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":4}],"accessors":[{"bufferView":0,"componentType":5126,"count":1,"type":"SCALAR","min":[)") +
          std::string(test.text) + R"(],"max":[)" + std::string(test.text) +
          "]}]}";
      std::array<std::byte, kBytesPerFloat> bytes{};
      for (std::size_t byte = 0; byte < kBytesPerFloat; ++byte)
        bytes[byte] =
            std::byte((test.bits >> (byte * kBitsPerByte)) & kByteMask);
      const std::array<SuppliedGltfResource, 1> resources{
          {{GltfResourceKind::kBuffer, 0, bytes}}};
      const GltfReadLimits limits{65536, 65536, 131072, 8192,  64,
                                  65536, 256,   256,    32,    8192,
                                  8192,  8192,  8192,   131072};
      const auto read = ReadGltfAssetCapped(
          std::as_bytes(std::span(source.data(), source.size())), resources,
          {limits, 1048576}, {8192, 8192, 8192, 8192}, context);
      FloatRequirement(read.asset.has_value(), test.text, mode.name,
                       "actual reader");
      FloatRequirement(
          ReadFloatBits(*captured.snapshot,
                        *ChildAt(*captured.snapshot, 0, 0)) == test.bits,
          test.text, mode.name, "exact conversion bits");
      FloatRequirement(std::fegetround() == mode.rounding, test.text, mode.name,
                       "rounding preserved");
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
      FloatRequirement(
          (_mm_getcsr() & kMxcsrControls) == (caller_mxcsr & kMxcsrControls),
          test.text, mode.name, "MXCSR controls preserved");
#endif
    }
    for (const auto text : kUnsupported) {
      meshvale::geometry::ExecutionContext context({2, 1, 1048576});
      const AllocationContext allocation(context, {});
      const auto captured =
          CaptureJsonSnapshot(std::string("[") + std::string(text) + "]",
                              {65536, 4096, 65536, 64}, allocation);
      Require(captured.snapshot.has_value());
      bool refused = false;
      try {
        (void)ReadFloatBits(*captured.snapshot,
                            *ChildAt(*captured.snapshot, 0, 0));
      } catch (const GltfFailure& failure) {
        refused =
            failure.Diagnostic().status == GltfStatus::kUnsupportedProfile &&
            failure.Diagnostic().code ==
                GltfDiagnosticCode::kUnsupportedNumberRepresentation;
      }
      FloatRequirement(refused, text, mode.name,
                       "nonzero underflow/overflow refusal");
    }
  }
}

}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--dump") {
      Dump(argv[2]);
      return 0;
    }
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
    FloatMetadataEnvironments();
    std::cout << "exact metadata boundaries passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
