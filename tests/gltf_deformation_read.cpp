// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
constexpr std::uint64_t kMebibyte = 1024 * 1024;
constexpr std::uint64_t kReadBytes = 16 * kMebibyte;
constexpr std::uint64_t kTableRows = 65536;
constexpr std::uint64_t kGraphDepth = 64;
constexpr std::uint64_t kNumericDigits = 1024;
constexpr std::uint64_t kExponentDigits = 32;
constexpr std::uint64_t kInfluenceSets = 16;
constexpr std::uint64_t kInfluenceLanes = 4 * kInfluenceSets;
constexpr std::uint64_t kRelationshipBlock = 16;
constexpr std::uint64_t kWorkBlock = 64;
constexpr std::size_t kWorkers = 4;
constexpr std::uint64_t kSharedBytes = 64 * kMebibyte;
constexpr mv::GltfReadOperationLimits kRead{
    {kMebibyte, kMebibyte, 4 * kMebibyte, kTableRows, kGraphDepth, kMebibyte,
     kNumericDigits, kNumericDigits, kExponentDigits, kTableRows, kTableRows,
     kTableRows, kMebibyte, 4 * kMebibyte},
    kReadBytes};
constexpr mv::GltfWorkPolicy kWork{kWorkBlock, kWorkBlock, kWorkBlock,
                                   kWorkBlock};
constexpr mv::GltfDeformationPolicy kDeformation{
    {kTableRows, kTableRows, kTableRows, kTableRows, kInfluenceSets,
     kInfluenceLanes, kTableRows, kTableRows, kTableRows, kTableRows,
     kTableRows, kMebibyte, kTableRows, kTableRows, kTableRows, kMebibyte,
     kMebibyte, 4 * kMebibyte},
    {kWorkBlock, kWorkBlock, kWorkBlock, kWorkBlock, kWorkBlock, kWorkBlock,
     kWorkBlock, kRelationshipBlock}};
constexpr mv::GltfReadRequest kRequest{
    mv::GltfAdmissionProfile::kRetainedCoreDeformationV1, kDeformation};
void Require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
std::span<const std::byte> Bytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}
mv::GltfReadResult Read(std::string_view json,
                        const geo::ExecutionContext& context) {
  return mv::ReadGltfAssetCapped(Bytes(json), {}, kRequest, kRead, kWork,
                                 context);
}
std::vector<char> File(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  Require(static_cast<bool>(stream), "fixture file open");
  stream.seekg(0, std::ios::end);
  const auto size = stream.tellg();
  Require(size >= 0 && static_cast<std::uint64_t>(size) <= kMebibyte,
          "bounded literal fixture bytes");
  std::vector<char> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  Require(stream.gcount() == static_cast<std::streamsize>(bytes.size()) &&
              stream.peek() == std::char_traits<char>::eof(),
          "complete unchanged literal fixture read");
  return bytes;
}
std::vector<char> WireFile(const std::filesystem::path& path) {
  constexpr unsigned kNibbleBits = 4;
  constexpr unsigned kAlphabeticDigit = 10;
  constexpr std::size_t kNibblesPerByte = 2;
  const auto literal = File(path);
  std::vector<char> bytes;
  bytes.reserve(literal.size() / kNibblesPerByte);
  std::optional<unsigned> high;
  for (const auto c : literal) {
    if (c == '\n' || c == '\r') continue;
    Require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
            "literal GLB hexadecimal digit");
    const auto digit =
        static_cast<unsigned>(c <= '9' ? c - '0' : c - 'a' + kAlphabeticDigit);
    if (!high) {
      high = digit;
    } else {
      bytes.push_back(static_cast<char>((*high << kNibbleBits) | digit));
      high.reset();
    }
  }
  Require(!high, "complete literal GLB byte");
  return bytes;
}
void MatrixInputs(const std::filesystem::path& directory,
                  const geo::ExecutionContext& context) {
  constexpr std::array<std::string_view, 6> kCases{
      "weights-linear",     "weights-step",    "weights-cubic",
      "matrix-translation", "matrix-rotation", "matrix-scale"};
  for (std::size_t index = 0; index < kCases.size(); ++index) {
    for (const bool binary : {false, true}) {
      const auto path = directory / (std::string(kCases[index]) +
                                     (binary ? ".glb.hex" : ".gltf"));
      const auto input = binary ? WireFile(path) : File(path);
      std::span<const std::byte> json = std::as_bytes(std::span(input));
      mv::GlbResult container;
      std::array<mv::SuppliedGltfResource, 1> bindings{};
      std::span<const mv::SuppliedGltfResource> supplied;
      if (binary) {
        container =
            mv::ReadGlbContainer(json, {kMebibyte, kGraphDepth, kReadBytes},
                                 {kWorkBlock, kWorkBlock}, context);
        Require(container.status == mv::GlbStatus::kSuccess &&
                    container.container.ChunkCount() == 2,
                "genuine GLB container input");
        Require(container.container.Chunk(0).type == mv::kGlbJsonChunkType &&
                    container.container.Chunk(1).type == mv::kGlbBinChunkType,
                "actual JSON/BIN chunk roles");
        json = container.container.Chunk(0).bytes;
        bindings[0] = {mv::GltfResourceKind::kBuffer, 0,
                       container.container.Chunk(1).bytes};
        supplied = bindings;
      }
      const auto result = mv::ReadGltfAssetCapped(json, supplied, kRequest,
                                                  kRead, kWork, context);
      const auto legacy =
          mv::ReadGltfAssetCapped(json, supplied, kRead, kWork, context);
      Require(!legacy.asset &&
                  legacy.diagnostic.status == mv::GltfStatus::kMalformed &&
                  legacy.diagnostic.code ==
                      mv::GltfDiagnosticCode::kGraphRelationship &&
                  !legacy.deformation,
              "legacy matrix animation interpretation remains unchanged");
      if (index >= 3) {
        Require(!result.asset && result.deformation &&
                    result.diagnostic.status == mv::GltfStatus::kMalformed &&
                    result.diagnostic.code ==
                        mv::GltfDiagnosticCode::kGraphRelationship,
                "new read refuses matrix+TRS animation");
        continue;
      }
      Require(
          result.asset && result.deformation && result.deformation->complete &&
              result.deformation->predicates.source_admission == true &&
              !result.deformation->weight_sums,
          "new read admits real matrix+weights; no fabricated payload fold");
      Require(result.asset->Nodes().size() == 1 &&
                  result.asset->Nodes()[0].transform_kind ==
                      mv::GltfTransformKind::kMatrix &&
                  result.asset->Nodes()[0].local_transform_bits[12] ==
                      std::bit_cast<std::uint32_t>(2.0F),
              "nonidentity source matrix retained");
      const auto primitive = result.asset->Primitive(0);
      Require(primitive.has_value(), "source primitive owner");
      mv::GltfExtractionRequest request;
      request.triangles = mv::GltfTriangleRequest::kRequireTriangles;
      request.values = mv::GltfValueRequest::kRaw;
      const auto refusal =
          mv::ExtractGltfPrimitiveCapped(*result.asset, *primitive, request,
                                         {kMebibyte, kTableRows, kReadBytes});
      Require(
          !refusal.extraction &&
              refusal.diagnostic.status == mv::GltfStatus::kInvalidRequest &&
              refusal.diagnostic.code ==
                  mv::GltfDiagnosticCode::kAdmissionProfileMismatch &&
              !refusal.direct_translation && !refusal.deformation,
          "old processing refuses new source before helper work");
      std::cout << kCases[index] << (binary ? "/GLB" : "/JSON")
                << " actual read/cross-profile PASS\n";
    }
  }
}
void RotationSources(const geo::ExecutionContext& context) {
  constexpr std::string_view kSource =
      R"({"asset":{"version":"2.0"},"nodes":[{"rotation":[-0,0e0,0.0,1.000]},{}]})";
  auto result = Read(kSource, context);
  Require(result.asset && result.deformation &&
              result.asset->AdmissionProfile() ==
                  mv::GltfAdmissionProfile::kRetainedCoreDeformationV1,
          "real authored node quaternion source");
  const auto sources = result.asset->NodeRotationSources();
  Require(sources.size() == 2 && sources[0].authored &&
              sources[0].authored_components.size() == 4 &&
              sources[0].authored_components[0].lexeme == "-0" &&
              sources[0].authored_components[3].lexeme == "1.000" &&
              sources[0].projected_components &&
              (*sources[0].projected_components)[0] == 0x80000000U &&
              !sources[1].authored && sources[1].authored_components.empty() &&
              !sources[1].projected_components,
          "original lexemes, projected signed zero and authored absence");
  Require(result.deformation->quaternions &&
              result.deformation->quaternions->complete &&
              result.deformation->quaternions->node_values_checked == 1 &&
              result.deformation->quaternions->rows_admitted == 1 &&
              !result.deformation->weight_sums,
          "actual one-row quaternion summary; absent default is not a row");
  auto copy = *result.asset;
  result.asset.reset();
  Require(
      copy.NodeRotationSources()[0].authored_components[3].lexeme == "1.000" &&
          copy.SharesExecutionBudget(context) &&
          copy.DeformationRevision()->commit ==
              "1c84b58ba3338be4731ea546fa4138dcbb93d42d",
      "source views/revision retain genuine copied owner");
  const auto nonunit =
      Read(R"({"asset":{"version":"2.0"},"nodes":[{"rotation":[0,0,0,0]}]})",
           context);
  Require(!nonunit.asset && nonunit.deformation &&
              nonunit.diagnostic.status == mv::GltfStatus::kMalformed &&
              nonunit.diagnostic.code == mv::GltfDiagnosticCode::kCoreRange &&
              nonunit.deformation->quaternions &&
              nonunit.deformation->quaternions->first_refusal &&
              nonunit.deformation->quaternions->first_refusal->node == 0 &&
              nonunit.deformation->quaternions->first_refusal->reason ==
                  mv::GltfQuaternionSourceRefusalReason::kNonunit,
          "real original nonunit refusal retains actual called facts");
}
void DeclarationProfiles(const geo::ExecutionContext& context) {
  constexpr std::string_view kTargetlessSibling =
      R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":3},{"componentType":5126,"type":"VEC3","count":3}],"meshes":[{"weights":[0.25],"primitives":[{"attributes":{"POSITION":0},"targets":[{"POSITION":1}]},{"attributes":{"POSITION":0}}]}]})";
  const auto new_sibling = Read(kTargetlessSibling, context);
  const auto old_sibling = mv::ReadGltfAssetCapped(Bytes(kTargetlessSibling),
                                                   {}, kRead, kWork, context);
  Require(new_sibling.asset && new_sibling.asset->PrimitiveCount() == 2 &&
              !old_sibling.asset &&
              old_sibling.diagnostic.code ==
                  mv::GltfDiagnosticCode::kGraphRelationship,
          "only explicit new read permits a targetless sibling");
  constexpr std::string_view kMissingCustomBase =
      R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":3},{"componentType":5126,"type":"VEC3","count":3}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"targets":[{"_CUSTOM":1}]}]}]})";
  const auto new_missing = Read(kMissingCustomBase, context);
  const auto old_missing = mv::ReadGltfAssetCapped(Bytes(kMissingCustomBase),
                                                   {}, kRead, kWork, context);
  Require(!new_missing.asset &&
              new_missing.diagnostic.status == mv::GltfStatus::kMalformed &&
              new_missing.diagnostic.code ==
                  mv::GltfDiagnosticCode::kGraphRelationship &&
              old_missing.asset && !old_missing.deformation,
          "new read requires every morph base without upgrading old read");
  auto tiny = kRequest;
  tiny.deformation.limits.max_new_requested_bytes = 1;
  const auto refused = mv::ReadGltfAssetCapped(
      Bytes(R"({"asset":{"version":"2.0"}})"), {}, tiny, kRead, kWork, context);
  Require(
      !refused.asset && refused.deformation &&
          refused.diagnostic.status == mv::GltfStatus::kUnsupportedProfile &&
          refused.diagnostic.code == mv::GltfDiagnosticCode::kLimitExceeded &&
          refused.deformation->effective_request_cap == 1 &&
          !refused.deformation->numeric && !refused.deformation->quaternions,
      "tiny real deformation owner cap refuses before projection/helper");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    Require(argc == 2, "fixture directory argument");
    geo::ExecutionContext context({kWorkers, 1, kSharedBytes});
    {
      const auto range = Read(
          R"({"asset":{"version":"2.0"},"nodes":[{"rotation":[2,0,0,1]}]})",
          context);
      Require(
          !range.asset && range.deformation &&
              range.diagnostic.status == mv::GltfStatus::kMalformed &&
              range.diagnostic.code == mv::GltfDiagnosticCode::kCoreRange &&
              !range.deformation->numeric && !range.deformation->quaternions,
          "exact original range refuses before gate/projection/unit helper");
      const auto baseline = Read(R"({"asset":{"version":"2.0"}})", context);
      if (!baseline.asset) {
        Require(baseline.deformation && baseline.deformation->numeric &&
                    baseline.deformation->numeric->refusal &&
                    baseline.deformation->numeric->refusal->reason ==
                        mv::GltfDeformationNumericRefusalReason::
                            kCompiledUnsupported,
                "only genuine compiled-unsupported gate may limit this test");
        std::cout
            << "exact read range PASS; compiled-unsupported refusal PASS; "
               "matrix/quaternion read cohort NOT EXERCISED\n";
      } else {
        MatrixInputs(std::filesystem::path(argv[1]), context);
        RotationSources(context);
        DeclarationProfiles(context);
      }
    }
    Require(context.ActiveTrackedPayload() == 0 && context.ActiveWorkers() == 0,
            "read owners/children release original shared ledger");
    std::cout << "deformation read controls PASS; payload=0 workers=0\n";
  } catch (const std::exception& error) {
    std::cerr << "deformation read failure: " << error.what() << '\n';
    return 1;
  }
}
