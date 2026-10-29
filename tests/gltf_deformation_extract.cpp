// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_deformation.h>
#include <meshvale/interchange/gltf_materialization.h>
#if defined(MESHVALE_TEST_DEFORMATION_FILES)
#include <meshvale/interchange/gltf_files.h>
#endif
#if defined(MESHVALE_TEST_DEFORMATION_OUTPUT)
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "gltf_materialization_test_internal.h"
#if defined(MESHVALE_TEST_DEFORMATION_OUTPUT)
#include "gltf_allocation.h"
#include "gltf_workflow_support_internal.h"
#endif
#include "gltf_numerical_profile.h"
#include "gltf_operation_workers.h"
#if defined(_MSC_VER) && defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace {
namespace mv = meshvale::interchange;
namespace geo = meshvale::geometry;
constexpr std::uint64_t kMebibyte = 1024 * 1024;
constexpr std::uint64_t kRootBytes = 128 * kMebibyte;
constexpr std::size_t kWorkerBudget = 4;
constexpr std::uint64_t kTableRows = 65536;
constexpr mv::GltfReadOperationLimits kRead{
    {kMebibyte, kMebibyte, 4 * kMebibyte, kTableRows, 64, kMebibyte, 1024, 1024,
     32, kTableRows, kTableRows, kTableRows, kMebibyte, 4 * kMebibyte},
    16 * kMebibyte};
constexpr mv::GltfWorkPolicy kReadWork{64, 64, 64, 64};
constexpr mv::GltfDeformationPolicy kPolicy{
    {kTableRows, kTableRows, kTableRows, kTableRows, 16, 64, kTableRows,
     kTableRows, kTableRows, kTableRows, kTableRows, kMebibyte, kTableRows,
     kTableRows, 8 * kMebibyte, 8 * kMebibyte, 8 * kMebibyte, 16 * kMebibyte},
    {64, 64, 64, 64, 64, 64, 64, 16}};
constexpr mv::GltfExtractionLimits kLimits{kMebibyte, kMebibyte,
                                           32 * kMebibyte};
constexpr mv::GltfExtractionRequest kRequest{
    mv::GltfTriangleRequest::kRequireTriangles,
    mv::GltfValueRequest::kRawAndNormalized,
    mv::GltfProcessingProfile::kDeformationRetainedTrianglesV1,
    kPolicy,
    {}};
constexpr mv::GltfMaterializationLimits kMaterialization{
    1024,
    8 * kMebibyte,
    {kTableRows, kTableRows, kTableRows, kTableRows, 1024, kMebibyte,
     kTableRows, kTableRows, kTableRows, 4 * kMebibyte, 16 * kMebibyte},
    {1024, 4 * kMebibyte}};
constexpr geo::MeshMaterializationWorkPolicy kMaterializationWork{1, 64, 64,
                                                                  64};
void Require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
#if defined(MESHVALE_TEST_DEFORMATION_OUTPUT)
bool PreservedDomains(const mv::GltfDeformationObservation& facts) {
  return facts.predicates.influences == true &&
         facts.predicates.bindings == true && facts.predicates.morphs == true &&
         facts.predicates.affecting_animation == true;
}
#endif
std::vector<char> File(const std::filesystem::path& path, bool hexadecimal) {
  std::ifstream input(path, std::ios::binary);
  Require(bool(input), "literal input open");
  input.seekg(0, std::ios::end);
  const auto size = input.tellg();
  Require(size > 0 && static_cast<std::uint64_t>(size) < kMebibyte,
          "bounded input extent");
  std::vector<char> text(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  Require(input.gcount() == static_cast<std::streamsize>(text.size()) &&
              input.peek() == std::char_traits<char>::eof(),
          "complete literal read");
  if (!hexadecimal) return text;
  std::vector<char> bytes;
  std::optional<unsigned> high;
  for (const auto character : text) {
    if (character == '\r' || character == '\n') continue;
    Require((character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f'),
            "wire hexadecimal");
    const auto nibble = static_cast<unsigned>(
        character <= '9' ? character - '0' : character - 'a' + 10);
    if (!high)
      high = nibble;
    else {
      bytes.push_back(static_cast<char>((*high << 4) | nibble));
      high.reset();
    }
  }
  Require(!high, "complete hexadecimal byte");
  return bytes;
}
const mv::GltfChannelView& Channel(
    const mv::GltfPrimitiveExtraction& extraction, std::string_view semantic) {
  for (const auto& channel : extraction.Channels())
    if (channel.semantic == semantic) return channel;
  throw std::runtime_error("missing authored channel");
}
#if defined(_MSC_VER) && defined(_M_X64)
enum class CanonicalFault { kControl, kSecondLaunch, kStop };
constexpr std::size_t kWorkerSlots = kWorkerBudget;
constexpr std::uint32_t kRoundingBits = 0x6000;
constexpr std::uint32_t kTowardZero = 0x6000;
constexpr std::uint32_t kFlushToZero = 0x8000;
constexpr std::uint32_t kDenormalsAreZero = 0x40;
constexpr unsigned kExceptionMaskOffset = 7;
constexpr unsigned kExceptionMasks = 6;
struct CanonicalBackend {
  struct Record {
    mv::gltf_detail::NativeWorkerRecord wrapper;
    mv::gltf_detail::NativeWorkerRecord* actual = nullptr;
    CanonicalBackend* owner = nullptr;
  };
  std::array<Record, kWorkerSlots> records;
  mv::gltf_detail::NativeWorkerBackend original =
      mv::gltf_detail::DefaultNativeWorkerBackend();
  CanonicalFault fault = CanonicalFault::kControl;
  std::uint32_t set_bits = 0, clear_bits = 0;
  std::stop_source* stop = nullptr;
  std::atomic<std::size_t> launched{0}, joined{0}, restored{0};
  mv::gltf_detail::NativeExecutionOptions Execution() {
    mv::gltf_detail::NativeExecutionOptions execution;
    execution.backend = {
        [](mv::gltf_detail::NativeWorkerRecord* actual,
           void* opaque) -> std::uintptr_t {
          auto& state = *static_cast<CanonicalBackend*>(opaque);
          Require(actual->ordinal < state.records.size(),
                  "bounded real worker record");
          if (state.fault == CanonicalFault::kSecondLaunch &&
              actual->ordinal == 1)
            throw std::bad_alloc();
          auto& record = state.records[actual->ordinal];
          record.actual = actual;
          record.owner = &state;
          record.wrapper.state = &record;
          record.wrapper.ordinal = actual->ordinal;
          record.wrapper.entry =
              [](mv::gltf_detail::NativeWorkerRecord& wrapper) noexcept {
                auto& r = *static_cast<Record*>(wrapper.state);
                struct Restore {
                  std::uint32_t saved;
                  CanonicalBackend& owner;
                  ~Restore() {
                    _mm_setcsr(saved);
                    if (_mm_getcsr() == saved) ++owner.restored;
                  }
                } restore{_mm_getcsr(), *r.owner};
                // Test child thread only. Product code receives no capability
                // override and must query/refuse these actual controls itself.
                if (r.owner->fault == CanonicalFault::kControl)
                  _mm_setcsr((restore.saved & ~r.owner->clear_bits) |
                             r.owner->set_bits);
                if (r.owner->fault == CanonicalFault::kStop)
                  r.owner->stop->request_stop();
                r.actual->entry(*r.actual);
              };
          const auto handle =
              state.original.launch(&record.wrapper, state.original.state);
          ++state.launched;
          return handle;
        },
        [](std::uintptr_t handle, void* opaque) noexcept {
          auto& state = *static_cast<CanonicalBackend*>(opaque);
          state.original.join(handle, state.original.state);
          ++state.joined;
        },
        this};
    return execution;
  }
};
void CanonicalWorkerControls(const mv::GltfPrimitiveExtraction& source,
                             const geo::ExecutionContext& context) {
  namespace materialization = mv::gltf_materialization_detail;
  const auto caller_controls = _mm_getcsr();
  const auto baseline = context.ActiveTrackedPayload();
  std::size_t controls = 0;
  auto run = [&](std::uint32_t set, std::uint32_t clear,
                 mv::GltfDeformationNumericRefusalReason reason) {
    CanonicalBackend backend;
    backend.set_bits = set;
    backend.clear_bits = clear;
    const auto execution = backend.Execution();
    materialization::TestHooks hooks;
    hooks.comparison_execution = &execution;
    auto result = materialization::TestMaterializeGltfPrimitive(
        source, kMaterialization, kMaterializationWork, {}, hooks, &kPolicy);
    Require(!result.primitive && result.deformation &&
                !result.deformation->complete && result.deformation->numeric &&
                result.deformation->numeric->refusal &&
                result.deformation->numeric->refusal->reason == reason &&
                !result.deformation->numeric->refusal->caller &&
                result.deformation->numeric->refusal->partition.has_value() &&
                result.deformation->execution.launched_workers > 0 &&
                result.deformation->execution.launched_workers ==
                    backend.launched &&
                backend.launched == backend.joined &&
                backend.restored == backend.launched,
            "actual unsafe canonical workers refused before arithmetic and all "
            "restored/joined");
    Require(_mm_getcsr() == caller_controls && context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == baseline,
            "worker refusal preserves caller controls, source and "
            "request/lease baseline");
    ++controls;
  };
  run(kTowardZero, kRoundingBits,
      mv::GltfDeformationNumericRefusalReason::kRounding);
  run(kFlushToZero, 0, mv::GltfDeformationNumericRefusalReason::kFlush);
  run(kDenormalsAreZero, 0, mv::GltfDeformationNumericRefusalReason::kFlush);
  for (unsigned bit = 0; bit < kExceptionMasks; ++bit)
    run(0, std::uint32_t{1} << (kExceptionMaskOffset + bit),
        mv::GltfDeformationNumericRefusalReason::kExceptions);
  {
    CanonicalBackend backend;
    backend.fault = CanonicalFault::kSecondLaunch;
    const auto execution = backend.Execution();
    materialization::TestHooks hooks;
    hooks.comparison_execution = &execution;
    bool threw = false;
    try {
      (void)materialization::TestMaterializeGltfPrimitive(
          source, kMaterialization, kMaterializationWork, {}, hooks, &kPolicy);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    Require(threw && backend.launched == 1 && backend.joined == 1 &&
                backend.restored == 1 && context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == baseline,
            "real second native launch failure joins first and rolls back all "
            "owned state");
    ++controls;
  }
  {
    std::stop_source stop;
    CanonicalBackend backend;
    backend.fault = CanonicalFault::kStop;
    backend.stop = &stop;
    const auto execution = backend.Execution();
    materialization::TestHooks hooks;
    hooks.comparison_execution = &execution;
    auto result = materialization::TestMaterializeGltfPrimitive(
        source, kMaterialization, kMaterializationWork, stop.get_token(), hooks,
        &kPolicy);
    Require(!result.primitive &&
                result.status == mv::GltfMaterializationStatus::kCanceled &&
                result.deformation && !result.deformation->complete &&
                backend.launched > 0 && backend.joined == backend.launched &&
                backend.restored == backend.launched &&
                context.ActiveWorkers() == 0 &&
                context.ActiveTrackedPayload() == baseline,
            "real canonical worker stop joins/rolls back and retains actual "
            "failure");
    ++controls;
  }
  {
    auto policy = kPolicy;
    policy.work.max_jobs = 1;
    auto result = mv::MaterializeGltfPrimitive(source, policy, kMaterialization,
                                               kMaterializationWork, {});
    Require(result.primitive && result.deformation &&
                result.deformation->execution.planned_jobs == 1 &&
                result.deformation->execution.caller_jobs == 1 &&
                result.deformation->execution.launched_workers == 0 &&
                result.deformation->execution.joined_workers == 0 &&
                result.execution.capture.launched_workers > 0,
            "canonical maxjobs1 is actual caller plan while Geometry delegate "
            "keeps independent work");
    ++controls;
  }
  Require(_mm_getcsr() == caller_controls && context.ActiveWorkers() == 0 &&
              context.ActiveTrackedPayload() == baseline,
          "all canonical controls released and caller unchanged");
  std::cout << "canonical-worker-controls=" << controls
            << " actual-refusal/launch/stop/hybrid PASS\n";
}
#endif
#if defined(MESHVALE_TEST_DEFORMATION_OUTPUT)
constexpr mv::GltfTopologyWriteLimits kWrite{
    kMebibyte,
    kTableRows,
    64,
    kTableRows,
    1024,
    kMebibyte,
    4 * kMebibyte,
    kTableRows,
    kTableRows,
    kMebibyte,
    {kMebibyte, 1024, kMebibyte, 8 * kMebibyte, 16 * kMebibyte}};
constexpr mv::GltfTopologyWriteWorkPolicy kWriteWork{1, 64, 64, 64};
constexpr mv::GltfTopologyOutputInspectionLimits kInspection{
    kMebibyte,     kTableRows, 64,         1024,
    8 * kMebibyte, kTableRows, 1024,       kMebibyte,
    4 * kMebibyte, kTableRows, kTableRows, kMebibyte,
    16 * kMebibyte};
constexpr mv::GltfTopologyOutputInspectionWorkPolicy kInspectionWork{1, 64, 64,
                                                                     64};
mv::GltfTopologyOutputInspection Output(
    const mv::GltfMaterializedPrimitive& source,
    const geo::MeshInspection& candidate, std::span<const geo::index_t> faces,
    std::span<const geo::index_t> corners, const geo::ExecutionContext& context,
    bool changed) {
  const mv::GltfTopologyWriteRequest request{source, candidate, faces, corners};
  auto old = mv::WriteGltfTopologySelection(request, kWrite, kWriteWork, {});
  Require(
      !old.bundle &&
          old.diagnostic.code ==
              mv::GltfTopologyWriteDiagnosticCode::kDeformationPolicyRequired,
      "no-policy writer refuses before helper");
  auto written =
      mv::WriteGltfTopologySelection(request, kPolicy, kWrite, kWriteWork, {});
  if (!written.bundle)
    std::cerr << "writer status=" << static_cast<int>(written.status)
              << " code=" << static_cast<int>(written.diagnostic.code) << '\n';
  Require(written.bundle && written.deformation &&
              written.deformation->complete &&
              PreservedDomains(*written.deformation) &&
              written.bundle->ChangedBinding().has_value() == changed &&
              !written.deformation->weight_sums &&
              !written.deformation->quaternions,
          "genuine checked identity/edited output with own observations");
  Require(written.deformation->execution.planned_jobs > 0 &&
              written.deformation->execution.launched_workers ==
                  written.deformation->execution.joined_workers &&
              written.deformation->new_requested_bytes <=
                  written.deformation->effective_request_cap,
          "writer owns its actual phase dispatch and request counters");
  std::vector<mv::SuppliedGltfResource> supplied;
  for (const auto& resource : written.bundle->Resources())
    if (resource.origin == mv::GltfResourceOrigin::kCallerBinding)
      supplied.push_back({resource.kind, resource.table_index, resource.bytes});
  auto read = mv::ReadGltfAssetCapped(
      written.bundle->Json(), supplied,
      {mv::GltfAdmissionProfile::kRetainedCoreDeformationV1, kPolicy}, kRead,
      kReadWork, context);
  Require(read.asset.has_value(), "independent actual output reader");
  auto primitive = read.asset->Primitive(0);
  Require(primitive.has_value(), "independent actual primitive");
  auto extracted = mv::ExtractGltfPrimitiveCapped(*read.asset, *primitive,
                                                  kRequest, kLimits, {});
  Require(extracted.extraction.has_value(),
          "independent typed output extraction");
  const mv::GltfTopologyOutputInspectionRequest inspection_request{
      source,
      candidate,
      faces,
      corners,
      *written.bundle,
      *read.asset,
      *extracted.extraction};
  auto verified = mv::InspectGltfTopologyOutput(
      inspection_request, kPolicy, kInspection, kInspectionWork, {});
  if (!verified.inspection)
    std::cerr << "inspection status=" << static_cast<int>(verified.status)
              << " code=" << static_cast<int>(verified.diagnostic.code)
              << " stage=" << static_cast<int>(verified.diagnostic.stage)
              << '\n';
  Require(
      verified.inspection && verified.deformation &&
          verified.deformation->complete &&
          PreservedDomains(*verified.deformation) &&
          verified.deformation->predicates.canonical_association == true &&
          verified.deformation->predicates.output_reload == true &&
          !verified.deformation->predicates.staged_reload &&
          !verified.deformation->weight_sums &&
          !verified.deformation->quaternions,
      "independent typed/raw/catalog output comparison owns memory facts only");
  Require(verified.deformation->execution.planned_jobs > 0 &&
              verified.deformation->execution.launched_workers ==
                  verified.deformation->execution.joined_workers &&
              verified.deformation->new_requested_bytes <=
                  verified.deformation->effective_request_cap,
          "inspector owns its actual phase dispatch and request counters");
  return *verified.inspection;
}
void PredicateFailureControls(const mv::GltfTopologyOutputInspection& verified,
                              const geo::ExecutionContext& context) {
  // A genuine valid candidate with one changed position must fail before the
  // checked writer is called. Missing domain observations stay missing.
  const auto view = verified.Candidate().Source().View();
  std::vector<double> positions(view.positions.begin(), view.positions.end());
  positions[0] =
      std::bit_cast<double>(std::bit_cast<std::uint64_t>(positions[0]) ^ 1u);
  std::vector<geo::AttributeMaterializationInput> attributes;
  for (const auto& a : view.attributes) {
    geo::AttributeMaterializationInput input;
    input.domain = a.domain;
    input.name = a.name;
    input.semantic = a.semantic;
    input.set_index = a.set_index;
    input.components = a.components;
    input.offsets = a.offsets;
    input.present = a.present;
    input.metadata = a.metadata;
    input.values = std::visit(
        [](auto values) -> geo::ScalarIngress { return values; }, a.values);
    attributes.push_back(input);
  }
  geo::MeshMaterializationInput changed;
  changed.positions = std::span<const double>(positions);
  changed.topology =
      geo::PolygonTopologyInput{view.face_offsets, view.corner_vertices};
  changed.attributes = attributes;
  auto mesh = geo::MaterializeMesh(changed, kMaterialization.materialization,
                                   kMaterializationWork, context, {});
  Require(mesh.mesh.has_value(), "real changed candidate capture");
  auto inspected = geo::InspectMaterializedMesh(
      *mesh.mesh, kMaterialization.inspection, kMaterializationWork, {});
  Require(inspected.inspection && inspected.inspection->IsStorageValid(),
          "real changed candidate storage");
  auto refused = mv::WriteGltfTopologySelection(
      {verified.Source(), *inspected.inspection, verified.SourceFaces(),
       verified.SourceCorners()},
      kPolicy, kWrite, kWriteWork, {});
  Require(!refused.bundle && refused.deformation &&
              !refused.deformation->complete &&
              refused.deformation->predicates.canonical_association == false &&
              !refused.deformation->predicates.influences &&
              !refused.deformation->predicates.bindings &&
              !refused.deformation->predicates.morphs &&
              !refused.deformation->predicates.affecting_animation,
          "actual failed association does not manufacture uncalled writer "
          "preservation domains");
  constexpr std::string_view kLegacy =
      R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":3,"min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
  auto legacy = mv::ReadGltfAssetCapped(std::as_bytes(std::span(kLegacy)), {},
                                        kRead, kReadWork, context, {});
  Require(legacy.asset.has_value(), "real legacy source owner");
  auto old = mv::ExtractGltfPrimitiveCapped(
      *legacy.asset, *legacy.asset->Primitive(0),
      {mv::GltfTriangleRequest::kRequireTriangles, mv::GltfValueRequest::kRaw},
      kLimits, {});
  Require(old.extraction.has_value(), "real legacy extraction owner");
  const mv::GltfTopologyOutputInspectionRequest missing{
      verified.Source(),        verified.Candidate(), verified.SourceFaces(),
      verified.SourceCorners(), verified.Output(),    *legacy.asset,
      *old.extraction};
  auto absent = mv::InspectGltfTopologyOutput(missing, kPolicy, kInspection,
                                              kInspectionWork, {});
  Require(!absent.inspection && absent.deformation &&
              !absent.deformation->complete &&
              !absent.deformation->predicates.influences &&
              !absent.deformation->predicates.bindings &&
              !absent.deformation->predicates.morphs &&
              !absent.deformation->predicates.affecting_animation &&
              !absent.deformation->predicates.canonical_association,
          "genuine missing new proof leaves uncalled comparison domains null");
  // Both catalogs below are actually admitted. This private comparison control
  // changes only a legal authored mesh default; it proves domain-by-domain
  // progress independently of earlier public lexical/resource refusals.
  const auto json = verified.Output().Json();
  std::string modified(reinterpret_cast<const char*>(json.data()), json.size());
  constexpr std::string_view kBefore = R"("weights":[0.25])";
  constexpr std::string_view kAfter = R"("weights":[0.375])";
  const auto at = modified.find(kBefore);
  Require(at != std::string::npos, "literal mesh-default source control");
  modified.replace(at, kBefore.size(), kAfter);
  std::vector<mv::SuppliedGltfResource> supplied;
  for (const auto& r : verified.Output().Resources())
    if (r.origin == mv::GltfResourceOrigin::kCallerBinding)
      supplied.push_back({r.kind, r.table_index, r.bytes});
  auto read = mv::ReadGltfAssetCapped(
      std::as_bytes(std::span(modified)), supplied,
      {mv::GltfAdmissionProfile::kRetainedCoreDeformationV1, kPolicy}, kRead,
      kReadWork, context, {});
  Require(read.asset.has_value(), "actual changed-default reader");
  auto extracted = mv::ExtractGltfPrimitiveCapped(
      *read.asset, *read.asset->Primitive(0), kRequest, kLimits, {});
  Require(extracted.extraction.has_value(),
          "actual changed-default typed admission");
  const mv::GltfTopologyOutputInspectionRequest partial{
      verified.Source(),        verified.Candidate(), verified.SourceFaces(),
      verified.SourceCorners(), verified.Output(),    *read.asset,
      *extracted.extraction};
  namespace detail = mv::gltf_workflow_support_detail;
  mv::gltf_detail::AllocationContext root(context, {});
  auto parent = root.WithRequestLimit(kInspection.max_new_requested_bytes);
  const auto cap = std::min(
      kPolicy.limits.max_new_requested_bytes,
      kInspection.max_new_requested_bytes - parent.OperationActiveBytes());
  auto allocation =
      parent.WithAdditionalRequestLimit(static_cast<std::size_t>(cap));
  mv::GltfDeformationObservation observation;
  observation.work = kPolicy.work;
  observation.phase = mv::GltfDeformationPhase::kCompare;
  observation.effective_request_cap = cap;
  auto work = kInspectionWork;
  work.parallel_min_bytes = kPolicy.work.parallel_min_bytes;
  work.partition_bytes = kPolicy.work.partition_bytes;
  work.cancellation_block_bytes = kPolicy.work.cancellation_block_bytes;
  work.max_jobs = kPolicy.work.max_jobs;
  detail::Operation operation{partial,    kInspection, work, context,
                              allocation, {},          {},   &observation};
  detail::Stats stats;
  bool mismatch = false;
  try {
    detail::VerifyDeformationReload(operation, stats, kPolicy, observation);
  } catch (const detail::Failure& error) {
    mismatch =
        error.status == mv::GltfTopologyOutputInspectionStatus::kOutputMismatch;
  }
  Require(mismatch && observation.predicates.influences == true &&
              observation.predicates.bindings == true &&
              observation.predicates.morphs == false &&
              !observation.predicates.affecting_animation &&
              !observation.weight_sums && !observation.quaternions &&
              observation.execution.launched_workers ==
                  observation.execution.joined_workers,
          "actual partial domain failure retains prior complete domains and "
          "leaves later domain uncalled");
  std::cout
      << "preservation-negative-controls=3 actual-missing/false/partial PASS\n";
}
#endif
#if defined(MESHVALE_TEST_DEFORMATION_FILES)
constexpr mv::GltfFileLimits kFileLimits{
    1024, 1024, kMebibyte, 8 * kMebibyte, 4096, 65536, 65536, 16 * kMebibyte};
constexpr mv::GltfFileWorkPolicy kFileWork{4096, 1, 4096, 64, 64};
constexpr mv::GltfFileReadLimits kFileRead{
    kFileLimits, kRead, {kMebibyte, 64, 16 * kMebibyte}};
constexpr mv::GltfFileReadWorkPolicy kFileReadWork{
    kFileWork, kReadWork, {1, 64}};
constexpr mv::GltfFilePublicationLimits kPublish{
    kFileLimits, kRead, kLimits, kInspection, {kMebibyte, 64, 16 * kMebibyte}};
constexpr mv::GltfFilePublicationWorkPolicy kPublishWork{
    kFileWork, kReadWork, kInspectionWork, {1, 64}};
struct TemporaryRoot {
  std::filesystem::path path;
  TemporaryRoot() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto parent =
        std::filesystem::canonical(std::filesystem::temp_directory_path());
    path = parent /
           ("meshvale-deformation-publication-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(sequence.fetch_add(1)));
    Require(
        path.parent_path() == parent && std::filesystem::create_directory(path),
        "exclusive owned publication test root");
  }
  ~TemporaryRoot() noexcept {
    try {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    } catch (...) {
      // Best-effort cleanup must not mask an earlier test failure.
    }
  }
};
void Physical(std::span<const char> input, bool binary,
              const geo::ExecutionContext& context) {
  TemporaryRoot root;
  const auto entry = root.path / "source.gltf";
  {
    std::ofstream file(entry, std::ios::binary);
    file.write(input.data(), static_cast<std::streamsize>(input.size()));
    file.close();
    Require(bool(file), "complete actual literal source file");
  }
  mv::GltfFileReadOptions read_options;
  read_options.read_request = {
      mv::GltfAdmissionProfile::kRetainedCoreDeformationV1, kPolicy};
  auto captured = mv::ReadGltfFileAsset(entry, read_options, kFileRead,
                                        kFileReadWork, context);
  if (!captured.asset)
    std::cerr << "file capture status="
              << static_cast<int>(captured.diagnostic.status)
              << " code=" << static_cast<int>(captured.diagnostic.code) << '\n';
  Require(
      captured.asset && captured.deformation &&
          captured.deformation->complete &&
          captured.asset->DeformationReadFacts() &&
          captured.asset->Format() == (binary ? mv::GltfFileFormat::kGlb
                                              : mv::GltfFileFormat::kGltfJson),
      "real content-detected retained-file new-profile admission");
  auto extraction = mv::ExtractGltfPrimitiveCapped(
      captured.asset->Asset(), *captured.asset->Asset().Primitive(0), kRequest,
      kLimits, {});
  Require(extraction.extraction.has_value(),
          "physical retained source typed admission");
  auto source =
      mv::MaterializeGltfPrimitive(*extraction.extraction, kPolicy,
                                   kMaterialization, kMaterializationWork, {});
  Require(source.primitive.has_value(), "physical source canonical owner");
  const std::array<geo::index_t, 1> faces{0};
  auto selection = geo::SelectMeshFaces(source.primitive->Mesh(), faces,
                                        kMaterialization.materialization,
                                        kMaterializationWork, {});
  Require(selection.selection.has_value(),
          "physical workflow genuine face selection");
  auto candidate = geo::InspectMaterializedMesh(
      selection.selection->Candidate(), kMaterialization.inspection,
      kMaterializationWork, {});
  Require(candidate.inspection && candidate.inspection->IsStorageValid(),
          "physical candidate inspection");
  const bool changed = source.primitive->Mesh().View().face_offsets.size() > 2;
  auto verified =
      Output(*source.primitive, *candidate.inspection,
             selection.selection->SourceFaces(),
             selection.selection->SourceCorners(), context, changed);
  const mv::GltfFilePublicationRequest request{*captured.asset, verified};
  std::optional<mv::GltfDeformationObservation> callback_facts;
  std::size_t callback_calls = 0;
  const auto destination = root.path / "published";
  mv::GltfFilePublicationOptions options;
  options.deformation = {kPolicy, kPolicy, kPolicy};
  options.on_verified = [&](const mv::GltfFileVerifiedContent& content) {
    ++callback_calls;
    Require(!std::filesystem::exists(destination),
            "provider is genuinely precommit");
    Require(
        content.staged_deformation && content.staged_deformation->complete &&
            PreservedDomains(*content.staged_deformation) &&
            content.staged_deformation->predicates.canonical_association ==
                true &&
            content.staged_deformation->predicates.staged_reload == true &&
            !content.staged_deformation->predicates.output_reload &&
            !content.staged_deformation->weight_sums &&
            content.inspection.DeformationFacts() &&
            content.inspection.DeformationFacts()->predicates.output_reload ==
                true &&
            PreservedDomains(*content.inspection.DeformationFacts()) &&
            !content.inspection.DeformationFacts()->predicates.staged_reload,
        "borrowed publisher-owned staged facts stay distinct from generic "
        "inspection");
    callback_facts = *content.staged_deformation;
    return std::vector<mv::GltfFileSupplement>{};
  };
  auto published = mv::PublishGltfTopologyBundle(request, destination, options,
                                                 kPublish, kPublishWork, {});
  if (!published.bundle)
    std::cerr << "publication status="
              << static_cast<int>(published.diagnostic.status)
              << " code=" << static_cast<int>(published.diagnostic.code)
              << " stage=" << static_cast<int>(published.diagnostic.stage)
              << '\n';
  Require(published.bundle && callback_facts && callback_calls == 1 &&
              published.deformation_read && published.deformation_extraction &&
              published.deformation_verification &&
              published.deformation_verification->predicates.staged_reload ==
                  true &&
              std::filesystem::exists(destination / "source.gltf"),
          "actual native staged publisher, own producer receipts and precommit "
          "handoff");
  const auto failed_destination = root.path / "later-failure";
  options.on_verified = [&](const mv::GltfFileVerifiedContent& content)
      -> std::vector<mv::GltfFileSupplement> {
    ++callback_calls;
    Require(content.staged_deformation &&
                content.staged_deformation->complete &&
                PreservedDomains(*content.staged_deformation) &&
                content.staged_deformation->predicates.staged_reload == true,
            "real successful stage facts exist before later provider failure");
    throw std::runtime_error("literal later verified-provider failure");
  };
  const auto failed = mv::PublishGltfTopologyBundle(
      request, failed_destination, options, kPublish, kPublishWork, {});
  Require(
      !failed.bundle &&
          failed.diagnostic.status == mv::GltfFileStatus::kCallbackFailure &&
          failed.deformation_verification &&
          failed.deformation_verification->complete &&
          PreservedDomains(*failed.deformation_verification) &&
          failed.deformation_verification->predicates.staged_reload == true &&
          !std::filesystem::exists(failed_destination) &&
          failed.cleanup == mv::GltfFileCleanupOutcome::kRemoved &&
          callback_calls == 2,
      "later failure preserves real successful staged facts without "
      "publication");
  const auto retained = File(entry, false);
  Require(retained.size() == input.size() &&
              std::equal(retained.begin(), retained.end(), input.begin()),
          "source wire bytes unchanged after success and later failure");
  const auto& source_facts = *published.deformation_extraction;
  const auto& stage_facts = *published.deformation_verification;
  Require(
      source_facts.execution.launched_workers ==
              source_facts.execution.joined_workers &&
          stage_facts.execution.launched_workers ==
              stage_facts.execution.joined_workers &&
          source_facts.new_requested_bytes <=
              source_facts.effective_request_cap &&
          stage_facts.new_requested_bytes <= stage_facts.effective_request_cap,
      "actual phase request bounds and completed worker joins");
  std::cout << "physical=" << (binary ? "GLB" : "JSON")
            << " callbacks=" << callback_calls
            << " capture_cap=" << captured.deformation->effective_request_cap
            << " publication_read_cap="
            << published.deformation_read->effective_request_cap
            << " extraction_cap=" << source_facts.effective_request_cap
            << " extraction_live=" << source_facts.new_requested_bytes
            << " extraction_jobs=" << source_facts.execution.planned_jobs
            << " extraction_caller=" << source_facts.execution.caller_jobs
            << " extraction_launched="
            << source_facts.execution.launched_workers
            << " extraction_joined=" << source_facts.execution.joined_workers
            << " staged_cap=" << stage_facts.effective_request_cap
            << " staged_live=" << stage_facts.new_requested_bytes
            << " staged_jobs=" << stage_facts.execution.planned_jobs
            << " staged_caller=" << stage_facts.execution.caller_jobs
            << " staged_launched=" << stage_facts.execution.launched_workers
            << " staged_joined=" << stage_facts.execution.joined_workers
            << '\n';
}
#endif
void Exercise(const std::filesystem::path& directory, std::string_view name,
              bool binary, const geo::ExecutionContext& context,
              bool missing_stride = false) {
  std::cout << "case=" << name << " format=" << (binary ? "GLB" : "JSON")
            << std::endl;
  const auto stem =
      std::string(name) +
      (!missing_stride && name.starts_with("d0") ? "-stride" : "");
  auto bytes =
      File(directory / (stem + (binary ? ".glb.hex" : ".gltf")), binary);
  auto json = std::as_bytes(std::span(bytes));
  mv::GlbResult glb;
  std::array<mv::SuppliedGltfResource, 1> bin{};
  std::span<const mv::SuppliedGltfResource> resources;
  if (binary) {
    glb = mv::ReadGlbContainer(json, {kMebibyte, 64, 16 * kMebibyte}, {64, 64},
                               context);
    Require(glb.status == mv::GlbStatus::kSuccess &&
                glb.container.ChunkCount() == 2,
            "actual GLB input");
    json = glb.container.Chunk(0).bytes;
    bin[0] = {mv::GltfResourceKind::kBuffer, 0, glb.container.Chunk(1).bytes};
    resources = bin;
  }
  auto read = mv::ReadGltfAssetCapped(
      json, resources,
      {mv::GltfAdmissionProfile::kRetainedCoreDeformationV1, kPolicy}, kRead,
      kReadWork, context);
  Require(read.asset.has_value(), "new-profile actual read");
  auto selected = read.asset->Primitive(0);
  Require(selected.has_value(), "actual primitive");
  auto result = mv::ExtractGltfPrimitiveCapped(*read.asset, *selected, kRequest,
                                               kLimits, {});
  if (missing_stride) {
    Require(!result.extraction && result.deformation &&
                result.diagnostic.status == mv::GltfStatus::kMalformed &&
                result.diagnostic.code == mv::GltfDiagnosticCode::kBinaryLayout,
            "original sub4-byte vertex input without explicit stride refused");
    return;
  }
  const bool refused = name == "d03-cross-width-one-unit-low" ||
                       name == "10-same-stream-linear-max-value";
  if (refused) {
    Require(!result.extraction && result.deformation &&
                !result.deformation->complete &&
                result.diagnostic.status == mv::GltfStatus::kMalformed,
            "actual payload refusal retains its observation");
    if (name.starts_with("10-"))
      Require(
          result.diagnostic.rule_id == 65539 &&
              result.deformation->quaternions &&
              result.deformation->quaternions->first_refusal &&
              result.deformation->quaternions->first_refusal->accessor == 3 &&
              result.deformation->quaternions->first_refusal->row == 0,
          "independent fixture exact accessor3/row0/source rule");
    return;
  }
  if (!result.extraction)
    std::cerr << "status=" << static_cast<int>(result.diagnostic.status)
              << " code=" << static_cast<int>(result.diagnostic.code)
              << " rule=" << result.diagnostic.rule_id << '\n';
  Require(result.extraction && result.deformation &&
              result.deformation->complete &&
              result.extraction->Deformation() &&
              result.extraction->DeformationFacts() &&
              result.extraction->ProcessingProfile() ==
                  mv::GltfProcessingProfile::kDeformationRetainedTrianglesV1,
          "public extraction owns genuine full source proof");
  Require(result.extraction->Deformation()->SharesExecutionBudget(context) &&
              result.extraction->NewRequestedBytes() > 0,
          "proof and facade retain actual shared budget");
  const auto old_materialization = mv::MaterializeGltfPrimitive(
      *result.extraction, kMaterialization, kMaterializationWork, {});
  Require(
      !old_materialization.primitive &&
          old_materialization.diagnostic.code ==
              mv::GltfMaterializationDiagnosticCode::kDeformationPolicyRequired,
      "no-policy materializer refuses new-profile source before delegate");
#if defined(MESHVALE_TEST_DEFORMATION_FILES)
  if (name == "d04-dense-float-0-2-5" || name == "weights-linear")
    Physical(bytes, binary, context);
#endif
  auto materialized = mv::MaterializeGltfPrimitive(
      *result.extraction, kPolicy, kMaterialization, kMaterializationWork, {});
  if (!materialized.primitive)
    std::cerr << "materialization status="
              << static_cast<int>(materialized.status)
              << " code=" << static_cast<int>(materialized.diagnostic.code)
              << '\n';
  Require(
      materialized.primitive && materialized.deformation &&
          materialized.deformation->complete &&
          materialized.deformation->predicates.canonical_association == true &&
          !materialized.deformation->weight_sums &&
          !materialized.deformation->quaternions &&
          materialized.deformation->numeric &&
          materialized.deformation->numeric->established_calls > 0,
      "actual independent canonical comparison owns only its called facts");
  Require(materialized.deformation->execution.planned_jobs > 0 &&
              materialized.deformation->execution.launched_workers > 0 &&
              materialized.deformation->execution.launched_workers ==
                  materialized.deformation->execution.joined_workers &&
              materialized.deformation->numeric->guard_calls > 1 &&
              materialized.deformation->new_requested_bytes <=
                  materialized.deformation->effective_request_cap,
          "eligible canonical comparison uses actual guarded joined workers "
          "within phase cap");
  const auto canonical = materialized.primitive->Mesh().View();
#if defined(_MSC_VER) && defined(_M_X64)
  if (name == "d04-dense-float-0-2-5" && !binary)
    CanonicalWorkerControls(*result.extraction, context);
#endif
  Require(canonical.positions.size() == 9 &&
              canonical.corner_vertices.size() ==
                  result.extraction->TriangleIndices().size(),
          "actual unchanged vertex rows and complete triangles");
  const auto associations = materialized.primitive->DeformationAssociations();
  Require(!associations.empty() &&
              associations.front().destination ==
                  mv::GltfDeformationDestination::kCanonicalPositions,
          "actual source-to-canonical association records");
  if (name == "weights-linear") {
    Require(
        associations.size() == 2 && canonical.attributes.size() == 1 &&
            associations[1].target == 0 &&
            associations[1].declaration_name ==
                "gltf/0/0/morph/0/8:504f534954494f4e" &&
            canonical.attributes[0].semantic == "gltf.morph.delta" &&
            canonical.attributes[0].name == associations[1].declaration_name,
        "morph POSITION is a source-qualified delta, not canonical position");
  }
  if (name == "d04-dense-float-0-2-5") {
    Require(
        associations.size() == 5 && canonical.attributes.size() == 4 &&
            canonical.attributes[0].semantic == "joint_indices" &&
            canonical.attributes[1].semantic == "joint_weights" &&
            canonical.attributes[0].name == "gltf/0/0/base/8:4a4f494e54535f30",
        "all dense influence sets retain truthful joint semantics/names");
  }
  if (name == "d04-dense-float-0-2-5") {
    const auto& extraction = *result.extraction;
    Require(extraction.Channels().size() == 5 &&
                extraction.TriangleIndices().size() == 9 &&
                extraction.Deformation()->InfluenceSets().size() == 2,
            "both four-lane sets and full triangle stream retained");
    const auto weights =
        std::get<std::span<const float>>(Channel(extraction, "WEIGHTS_1").raw);
    constexpr std::array<std::uint32_t, 12> kAuthoredWeightWords{
        0x00000000u, 0x00000000u, 0x80000000u, 0x00000000u,
        0x3f000000u, 0x00000000u, 0x00000000u, 0x80000000u,
        0x3e800000u, 0x3e000000u, 0x00000000u, 0x80000000u};
    Require(weights.size() == kAuthoredWeightWords.size(),
            "every authored weight lane retained");
    for (std::size_t i = 0; i < weights.size(); ++i)
      Require(
          std::bit_cast<std::uint32_t>(weights[i]) == kAuthoredWeightWords[i],
          "exact authored word including each zero sign survives facade");
    const auto& summary = extraction.Deformation()->WeightSums();
    Require(summary.rows_evaluated == 3 && summary.all_zero_rows == 1 &&
                summary.advisory_rows == 2,
            "actual variable0/2/5 observations");
  }
#if defined(MESHVALE_TEST_DEFORMATION_OUTPUT)
  const auto face_count = canonical.face_offsets.size() - 1;
  std::vector<geo::index_t> identity_faces(face_count),
      identity_corners(canonical.corner_vertices.size());
  for (std::size_t i = 0; i < identity_faces.size(); ++i) identity_faces[i] = i;
  for (std::size_t i = 0; i < identity_corners.size(); ++i)
    identity_corners[i] = i;
  const auto identity_output =
      Output(*materialized.primitive, materialized.primitive->Inspection(),
             identity_faces, identity_corners, context, false);
  if (name == "weights-linear" && !binary)
    PredicateFailureControls(identity_output, context);
  if (face_count > 1) {
    const std::array<geo::index_t, 1> kept{0};
    auto selected_faces = geo::SelectMeshFaces(
        materialized.primitive->Mesh(), kept, kMaterialization.materialization,
        kMaterializationWork, {});
    Require(selected_faces.selection.has_value(),
            "actual charged face selection");
    auto inspected = geo::InspectMaterializedMesh(
        selected_faces.selection->Candidate(), kMaterialization.inspection,
        kMaterializationWork, {});
    Require(inspected.inspection && inspected.inspection->IsStorageValid(),
            "actual independent selected storage inspection");
    Output(*materialized.primitive, *inspected.inspection,
           selected_faces.selection->SourceFaces(),
           selected_faces.selection->SourceCorners(), context, true);
  }
#endif
  auto old_request = kRequest;
  old_request.processing_profile =
      mv::GltfProcessingProfile::kStaticLocalTrianglesV1;
  const auto old = mv::ExtractGltfPrimitive(*read.asset, *selected, old_request,
                                            kLimits, {});
  Require(!old.extraction && !old.deformation &&
              old.diagnostic.code ==
                  mv::GltfDiagnosticCode::kAdmissionProfileMismatch,
          "legacy entry rejects new source before helper");
  auto tiny = kLimits;
  tiny.max_output_requested_bytes = 1;
  const auto denied = mv::ExtractGltfPrimitiveCapped(*read.asset, *selected,
                                                     kRequest, tiny, {});
  Require(!denied.extraction,
          "complete extraction cap admitted before storage");
  std::stop_source stopped;
  stopped.request_stop();
  const auto canceled = mv::ExtractGltfPrimitiveCapped(
      *read.asset, *selected, kRequest, kLimits, stopped.get_token());
  Require(!canceled.extraction &&
              canceled.diagnostic.status == mv::GltfStatus::kCanceled,
          "fresh extraction stop controls caller, not retained read token");
  auto copy = *result.extraction;
  result.extraction.reset();
  read.asset.reset();
  Require(copy.Deformation()->Asset().PrimitiveCount() > 0 &&
              !Channel(copy, "POSITION").raw.valueless_by_exception(),
          "copied extraction retains source/proof/channel storage");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    Require(argc == 2 || (argc == 3 &&
                          std::string_view(argv[2]) == "--expect-supported"),
            "fixture directory and optional positive requirement");
    mv::GltfDeformationNumericObservation numeric;
    if (!mv::gltf_detail::EstablishGltfNumericalProfile(numeric, {}, true)) {
      Require(
          argc == 2 && numeric.refusal &&
              numeric.refusal->reason ==
                  mv::GltfDeformationNumericRefusalReason::kCompiledUnsupported,
          "only genuine unsupported compiled profile limits scope");
      std::cout << "compiled-unsupported refusal PASS; public extraction cases "
                   "NOT EXERCISED\n";
      return 0;
    }
    geo::ExecutionContext context({kWorkerBudget, 1, kRootBytes});
    for (const auto name :
         {"d04-dense-float-0-2-5", "weights-linear", "d03-three-float-u16",
          "d03-cross-width-one-unit-low", "10-same-stream-linear-max-value"})
      for (const bool binary : {false, true}) {
        Exercise(argv[1], name, binary, context);
        Require(
            context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
            "all source/result/worker owners released");
      }
    for (const auto name : {"d03-two-float-u8", "d03-three-float-u16"})
      for (const bool binary : {false, true}) {
        Exercise(argv[1], name, binary, context, true);
        Require(
            context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
            "original invalid-stride refusal releases all owners");
      }
    std::cout << "14 public producer controls PASS; workers=0 payload=0\n";
  } catch (const std::exception& error) {
    std::cerr << "deformation extraction failure: " << error.what() << '\n';
    return 1;
  }
}
