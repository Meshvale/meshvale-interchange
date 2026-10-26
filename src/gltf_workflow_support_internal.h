// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_SUPPORT_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_SUPPORT_INTERNAL_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_workflow_support.h>

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_workflow_support_detail {
using Status = GltfTopologyOutputInspectionStatus;
using Code = GltfTopologyOutputInspectionDiagnosticCode;
using Stage = GltfTopologyOutputInspectionStage;
using Diagnostic = GltfTopologyOutputInspectionDiagnostic;
using Stats = GltfTopologyOutputInspectionExecutionStats;
template <class T>
using Vector = std::vector<T, gltf_detail::Allocator<T>>;
inline constexpr std::size_t kTriangleCorners = 3;
inline constexpr std::uint64_t kTrianglesMode = 4;
inline constexpr std::size_t kUint32Bytes = 4;
inline constexpr std::string_view kBuffers = "buffers";
inline constexpr std::string_view kBufferViews = "bufferViews";
inline constexpr std::string_view kAccessors = "accessors";
inline constexpr std::string_view kMeshes = "meshes";
inline constexpr std::string_view kPrimitives = "primitives";
inline constexpr std::string_view kIndices = "indices";
inline constexpr std::string_view kMode = "mode";
inline constexpr std::string_view kDataUriPrefix =
    "data:application/octet-stream;base64,";

class Failure : public std::exception {
 public:
  Failure(Status status, Diagnostic diagnostic) noexcept
      : status(status), diagnostic(diagnostic) {}
  const char* what() const noexcept override {
    return "glTF output inspection failed";
  }
  Status status;
  Diagnostic diagnostic;
};
[[noreturn]] void Fail(Status status, Stage stage, Code code,
                       std::optional<geometry::index_t> attribute = {},
                       std::optional<geometry::index_t> element = {},
                       std::optional<std::uint64_t> resource = {},
                       std::optional<GltfSourceSpan> span = {});
std::size_t Add(std::size_t first, std::size_t second);
void Count(std::uint64_t actual, std::uint64_t maximum);

struct InspectionState {
  InspectionState(const gltf_detail::AllocationContext& allocation,
                  const GltfTopologyOutputInspectionRequest& request);
  gltf_detail::AllocationContext allocation;
  GltfMaterializedPrimitive source;
  geometry::MeshInspection candidate;
  GltfTopologyWriteBundle output;
  GltfAsset reloaded_asset;
  GltfPrimitiveExtraction reloaded_extraction;
  Vector<geometry::index_t> faces;
  Vector<geometry::index_t> corners;
  Vector<GltfSourceCorner> original_corners;
  GltfTopologyOutputCoverage coverage;
  std::optional<GltfDirectTranslationObservation> direct_translation;
};
struct Access {
  static GltfTopologyOutputInspection Make(
      std::shared_ptr<const InspectionState> state);
};
struct Operation {
  const GltfTopologyOutputInspectionRequest& request;
  const GltfTopologyOutputInspectionLimits& limits;
  const GltfTopologyOutputInspectionWorkPolicy& work;
  const geometry::ExecutionContext& context;
  const gltf_detail::AllocationContext& allocation;
  std::stop_token stop;
  gltf_detail::NativeExecutionOptions execution;
  bool changed = false;
};
using RangeCheck = std::optional<Diagnostic> (*)(void*, std::size_t,
                                                 std::size_t, bool);
// Callback ranges are disjoint and ordinal ordered. A finalization callback
// ignores stop and allocates nothing; it checks only the prefix needed to find
// the first failure after all native handles have joined.
void RunRanges(const Operation& operation, std::size_t bytes, RangeCheck check,
               void* state, Stats& stats, std::size_t unit_bytes = 1);
struct ByteCheck {
  std::span<const std::byte> expected;
  std::span<const std::byte> actual;
  Diagnostic diagnostic;
  std::size_t scalar_bytes = 0;
  std::size_t prefix = 0;
};
class BytePlan {
 public:
  explicit BytePlan(const Operation& operation);
  void Compare(std::span<const std::byte> expected,
               std::span<const std::byte> actual, Diagnostic diagnostic,
               std::size_t scalar_bytes = 0);
  void Compare(std::string_view expected, std::string_view actual,
               Diagnostic diagnostic);
  void Require(bool condition, Diagnostic diagnostic);
  void Checkpoint();
  void Run(Stats& stats);

 private:
  static std::optional<Diagnostic> Check(void*, std::size_t, std::size_t, bool);
  [[noreturn]] void FinalizeKnownFailure();
  const Operation& operation_;
  Vector<ByteCheck> checks_;
  std::size_t bytes_ = 0;
  std::optional<Diagnostic> known_failure_;
  std::size_t known_prefix_ = 0;
  std::optional<ByteCheck> known_pending_;
};
Diagnostic Location(Stage stage, Code code,
                    std::optional<geometry::index_t> attribute = {},
                    std::optional<geometry::index_t> element = {});
void AdmitInputs(const GltfTopologyOutputInspectionRequest& request,
                 const GltfTopologyOutputInspectionLimits& limits,
                 const GltfTopologyOutputInspectionWorkPolicy& work,
                 std::stop_token stop);
void VerifyGeometry(const Operation& operation, Stats& stats);
void VerifyCorrespondence(Operation& operation, Stats& stats);
void VerifyLexical(const Operation& operation, Stats& stats);
void VerifyResources(const Operation& operation, Stats& stats);
void VerifyReload(const Operation& operation, Stats& stats);
void VerifyProvenance(const Operation& operation, InspectionState& state,
                      Stats& stats);
}  // namespace meshvale::interchange::gltf_workflow_support_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_WORKFLOW_SUPPORT_INTERNAL_H_
