// SPDX-License-Identifier: Apache-2.0
#include "../src/surface_capsule.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/surface_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../src/gltf_allocation.h"
#include "../src/surface_output_internal.h"
#include "surface_fixture.h"

namespace {
namespace geom = meshvale::geometry;
namespace mv = meshvale::interchange;
namespace detail = mv::surface_detail;
namespace fixture = mv::surface_fixture;
constexpr std::uint64_t kMiB = 1024 * 1024;
constexpr std::uint64_t kScopeBytes = 64 * kMiB;
constexpr std::uint64_t kAllowanceBytes = 512 * kMiB;
constexpr geom::MeshMaterializationWorkPolicy kWork{4096, 8192, 64, 65536};
constexpr geom::MeshInspectionLimits kInspect{1024, kScopeBytes};
constexpr std::array<geom::index_t, 1> kEmptyOffsets{0};
constexpr std::array<std::byte, 80> kHeader{};
constexpr std::array<mv::SurfaceUvBinding, 3> kUv{
    {{15, 0, 0}, {16, 2, 1}, {17, 5, 2}}};
constexpr std::array<mv::SurfaceMaterialBinding, 3> kMaterials{
    {{17, 0}, {-4, 1}, {1000003, 2}}};
constexpr std::array<mv::SurfaceCatalogUvBinding, 3> kCatalogUv{
    {{0, 0}, {1, 2}, {2, 5}}};
constexpr std::array<double, 3> kUvBounds{};
constexpr std::array<mv::SurfaceEncoding, 4> kEncodings{
    mv::SurfaceEncoding::kGltfJson, mv::SurfaceEncoding::kGlb,
    mv::SurfaceEncoding::kStlAscii, mv::SurfaceEncoding::kStlBinary};
void Require(bool good, std::string_view message) {
  if (!good) throw std::runtime_error(std::string(message));
}
geom::MeshMaterializationLimits MeshLimits() {
  return {1024,  1024,  8192, 1025, 64,         65536,
          65536, 65536, 4096, kMiB, kScopeBytes};
}
mv::SurfaceCountLimits Counts() {
  mv::SurfaceCountLimits l;
  l.max_vertices = 1024;
  l.max_faces = 1024;
  l.max_corners = 8192;
  l.max_face_offsets = 1025;
  l.max_attributes = 64;
  l.max_attribute_scalars = 65536;
  l.max_attribute_bytes = kMiB;
  l.max_attribute_offsets = 65536;
  l.max_presence_bytes = 65536;
  l.max_metadata_entries = 4096;
  l.max_descriptor_bytes = kMiB;
  l.max_wire_primitives = 1024;
  l.max_wire_vertices = 8192;
  l.max_wire_corners = 8192;
  l.max_map_entries = 65536;
  l.max_members = 1024;
  l.max_member_bytes = 4 * kMiB;
  l.max_total_member_bytes = 16 * kMiB;
  l.max_resources = 1024;
  l.max_resource_bytes = 4 * kMiB;
  l.max_json_bytes = 4 * kMiB;
  l.max_binary_bytes = 4 * kMiB;
  l.max_capsule_json_bytes = 4 * kMiB;
  l.max_capsule_binary_bytes = 4 * kMiB;
  l.max_material_bindings = 1024;
  l.max_numeric_token_bytes = 4096;
  l.max_json_nodes = 262144;
  l.max_json_depth = 128;
  l.max_numeric_integer_bits = 65536;
  l.max_coincidence_comparisons = 1048576;
  l.max_relationship_steps = 4194304;
  return l;
}
bool Stl(mv::SurfaceEncoding encoding) {
  return encoding == mv::SurfaceEncoding::kStlAscii ||
         encoding == mv::SurfaceEncoding::kStlBinary;
}
mv::SurfaceWorkPolicy Work(mv::SurfaceEncoding encoding, bool read = false) {
  mv::SurfaceWorkPolicy p;
  p.surface = kWork;
  if (encoding == mv::SurfaceEncoding::kGlb) p.container = {4096, 8192};
  if (Stl(encoding))
    p.stl = {4096, 64, 16, kWork};
  else if (read)
    p.gltf = {4096, 8192, 4096, 8192, {}};
  return p;
}
mv::GltfReadOperationLimits ReadLimits() {
  return {{4 * kMiB, 4 * kMiB, 4 * kMiB, 262144, 128, 4 * kMiB, 4096, 4096, 16,
           65536, 65536, 65536, 65536, 4 * kMiB},
          kScopeBytes};
}
mv::SurfaceEncodeLimits EncodeLimits(mv::SurfaceEncoding encoding) {
  mv::SurfaceEncodeLimits l;
  l.counts = Counts();
  l.max_new_requested_bytes = kScopeBytes;
  l.max_total_requested_allowance = kAllowanceBytes;
  if (Stl(encoding))
    l.delegates =
        mv::StlSurfaceEncodeLimits{MeshLimits(),
                                   {4 * kMiB, 4096, 1024, 65536, 4096,
                                    kScopeBytes, 2 * kScopeBytes, kInspect}};
  else {
    mv::GltfSurfaceEncodeLimits d;
    if (encoding == mv::SurfaceEncoding::kGlb)
      d.container = {4 * kMiB, 4, kScopeBytes};
    l.delegates = d;
  }
  return l;
}
mv::SurfaceInspectLimits InspectLimits(mv::SurfaceEncoding encoding) {
  mv::SurfaceInspectLimits l;
  l.counts = Counts();
  l.max_new_requested_bytes = kScopeBytes;
  l.max_total_requested_allowance = kAllowanceBytes;
  if (Stl(encoding))
    l.delegates = mv::StlSurfaceInspectLimits{
        {4 * kMiB, 4096, 1024, 65536, 4096, kScopeBytes, 3 * kScopeBytes,
         MeshLimits(), kInspect}};
  else {
    mv::GltfSurfaceInspectLimits d;
    if (encoding == mv::SurfaceEncoding::kGlb)
      d.container = {4 * kMiB, 4, kScopeBytes};
    d.read = ReadLimits();
    d.extraction = {65536, 8192, kScopeBytes, {}};
    l.delegates = d;
  }
  return l;
}
std::string Hex(std::span<const std::byte> bytes) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  for (auto b : bytes) {
    const auto value = std::to_integer<unsigned>(b);
    result += digits[value >> 4];
    result += digits[value & 15];
  }
  return result;
}
mv::SurfaceDigest Hash(std::span<const std::byte> bytes,
                       const geom::ExecutionContext& context) {
  mv::gltf_detail::AllocationContext base(context, {});
  auto allocation = base.WithRequestLimit(kScopeBytes);
  detail::FactsState facts(allocation);
  const auto limits = Counts();
  const auto work = Work(mv::SurfaceEncoding::kGltfJson);
  const detail::TestOptions options;
  detail::Run run(context, allocation, limits, work, facts, {}, options);
  return detail::Digest(bytes, run);
}
void HashVectors(const geom::ExecutionContext& context) {
  constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
      vectors{
          {{"",
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
           {"abc",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
           {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c"
            "1"}}};
  for (const auto& [source, expected] : vectors)
    Require(Hex(Hash(std::as_bytes(
                         std::span<const char>{source.data(), source.size()}),
                     context)) == expected,
            "independent standard SHA-256 vector");
  const std::vector<std::byte> million(1000000, std::byte{0x61});
  Require(
      Hex(Hash(million, context)) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
      "million-byte SHA-256 vector");
  mv::gltf_detail::AllocationContext base(context, {});
  auto allocation = base.WithRequestLimit(kScopeBytes);
  detail::FactsState facts(allocation);
  const auto limits = Counts();
  const auto work = Work(mv::SurfaceEncoding::kGltfJson);
  const detail::TestOptions options;
  std::stop_source stop;
  detail::Run run(context, allocation, limits, work, facts, stop.get_token(),
                  options);
  stop.request_stop();
  bool canceled = false;
  try {
    (void)detail::Digest(million, run);
  } catch (const detail::Failure& f) {
    canceled = f.Status() == mv::SurfaceStatus::kCanceled;
  }
  Require(canceled, "actual hash stop checkpoint");
}
void CopyControls(const mv::SurfaceOutput& output,
                  const geom::ExecutionContext& context) {
  constexpr std::uint64_t kCopyPartitionBytes = 256;
  constexpr std::uint64_t kCopyBlockBytes = 64;
  constexpr std::size_t kLargeSidecarBytes = 8192;
  const auto members = output.Members();
  const auto json =
      std::find_if(members.begin(), members.end(), [](const auto& member) {
        return member.kind == mv::SurfaceMemberKind::kCapsuleJson;
      });
  Require(json != members.end() && json->bytes.size() >= kLargeSidecarBytes,
          "actual rich encoded JSON is a large sidecar");
  const auto source = json->bytes;
  const auto jobs = source.size() / kCopyPartitionBytes +
                    (source.size() % kCopyPartitionBytes != 0);
  Require(jobs > 1, "actual sidecar requires multiple copy jobs");
  mv::gltf_detail::AllocationContext base(context, {});
  auto allocation = base.WithRequestLimit(kScopeBytes);
  const auto limits = Counts();
  auto copy_work = Work(output.Encoding());
  copy_work.surface.parallel_min_bytes = 1;
  copy_work.surface.partition_bytes = kCopyPartitionBytes;
  copy_work.surface.cancellation_block_scalars = kCopyBlockBytes;
  copy_work.surface.max_jobs = jobs;
  const detail::TestOptions options;
  // Hold actual slots on this same context, forcing Bulk's genuine caller
  // fallback. No new executor or manufactured worker observation is used.
  auto occupied = geom::TryReserveWorkers(context, context.WorkerBudget());
  Require(occupied.Count() == context.WorkerBudget(),
          "actual same-context lease isolates deterministic copy controls");
  {
    detail::FactsState facts(allocation);
    detail::Member copied(allocation);
    copied.kind = mv::SurfaceMemberKind::kCapsuleJson;
    detail::Run run(context, allocation, limits, copy_work, facts, {}, options);
    run.stage = mv::SurfaceStage::kEncode;
    detail::CopyCapsuleJson(source, copied, run);
    Require(copied.bytes.size() == source.size() &&
                std::equal(source.begin(), source.end(), copied.bytes.begin()),
            "genuine actual JSON copied byte-exact through disjoint jobs");
    Require(facts.execution.planned_jobs == jobs &&
                facts.execution.caller_jobs == jobs &&
                facts.execution.launched_workers == 0 &&
                facts.execution.joined_workers == 0,
            "copy records actual same-executor planned/caller work");
    Require(!facts.positions && facts.uv.empty(),
            "private payload copy creates no V numeric fact or proof");
  }
  {
    auto denied_work = copy_work;
    denied_work.surface.max_jobs = jobs - 1;
    detail::FactsState facts(allocation);
    detail::Member denied(allocation);
    denied.kind = mv::SurfaceMemberKind::kCapsuleJson;
    detail::Run run(context, allocation, limits, denied_work, facts, {},
                    options);
    run.stage = mv::SurfaceStage::kEncode;
    bool bounded = false;
    try {
      detail::CopyCapsuleJson(source, denied, run);
    } catch (const detail::Failure& failure) {
      bounded = failure.Status() == mv::SurfaceStatus::kLimitExceeded &&
                failure.Diagnostic().stage == mv::SurfaceStage::kEncode &&
                failure.Diagnostic().code == mv::SurfaceCode::kCountLimit;
    }
    Require(bounded && facts.execution.planned_jobs == 0 &&
                facts.execution.caller_jobs == 0 &&
                facts.execution.launched_workers == 0 &&
                facts.execution.joined_workers == 0,
            "copy-specific job ceiling denies before any actual copy job");
  }
  {
    struct CopyStop {
      std::stop_source stop;
      bool interior = false;
    } observation;
    detail::TestOptions observed;
    observed.checkpoint_state = &observation;
    observed.checkpoint = [](mv::SurfaceStage stage, std::uint64_t completed,
                             void* opaque) {
      auto& s = *static_cast<CopyStop*>(opaque);
      if (stage == mv::SurfaceStage::kEncode && completed == kCopyBlockBytes) {
        s.interior = true;
        s.stop.request_stop();
      }
    };
    detail::FactsState facts(allocation);
    detail::Member partial(allocation);
    partial.kind = mv::SurfaceMemberKind::kCapsuleJson;
    detail::Run run(context, allocation, limits, copy_work, facts,
                    observation.stop.get_token(), observed);
    run.stage = mv::SurfaceStage::kEncode;
    bool canceled = false;
    try {
      detail::CopyCapsuleJson(source, partial, run);
    } catch (const detail::Failure& failure) {
      canceled = failure.Status() == mv::SurfaceStatus::kCanceled &&
                 failure.Diagnostic().stage == mv::SurfaceStage::kEncode &&
                 failure.Diagnostic().code == mv::SurfaceCode::kStopRequested;
    }
    Require(
        canceled && observation.interior &&
            partial.bytes.size() == source.size() &&
            source[kCopyBlockBytes] != std::byte{} &&
            std::equal(source.begin(), source.begin() + kCopyBlockBytes,
                       partial.bytes.begin()) &&
            std::all_of(partial.bytes.begin() + kCopyBlockBytes,
                        partial.bytes.end(),
                        [](std::byte value) { return value == std::byte{}; }),
        "actual interior stop preserves exactly the completed copy prefix");
    Require(facts.execution.planned_jobs == jobs &&
                facts.execution.caller_jobs == 1 &&
                facts.execution.launched_workers == 0 &&
                facts.execution.joined_workers == 0 && !facts.positions &&
                facts.uv.empty(),
            "partial copy retains actual joined/caller facts without proof");
  }
  // This exercises only the private final copy using genuine already encoded
  // bytes. It does not substitute for full Encode/Inspect cancellation
  // controls.
}
struct Captured {
  explicit Captured(const mv::SurfaceOutput& output) {
    for (const auto& m : output.Members()) {
      paths.emplace_back(m.path);
      bytes.emplace_back(m.bytes.begin(), m.bytes.end());
    }
    Refresh();
  }
  void Refresh() {
    views.clear();
    for (std::size_t i = 0; i < paths.size(); ++i)
      views.push_back({paths[i], bytes[i]});
  }
  std::size_t Index(std::string_view path) const {
    for (std::size_t i = 0; i < paths.size(); ++i)
      if (paths[i] == path) return i;
    throw std::runtime_error("actual member absent");
  }
  std::string Json() const {
    const auto& b = bytes[Index("surface.json")];
    return {reinterpret_cast<const char*>(b.data()), b.size()};
  }
  void SetJson(std::string_view json) {
    auto& b = bytes[Index("surface.json")];
    const auto span =
        std::as_bytes(std::span<const char>{json.data(), json.size()});
    b.assign(span.begin(), span.end());
    Refresh();
  }
  std::vector<std::string> paths;
  std::vector<std::vector<std::byte>> bytes;
  std::vector<mv::SuppliedSurfaceMember> views;
};
void Replace(std::string& text, std::string_view from, std::string_view to) {
  const auto found = text.find(from);
  Require(found != std::string::npos, "required actual JSON field");
  text.replace(found, from.size(), to);
}
void RehashBinary(Captured& captured, const geom::ExecutionContext& context,
                  std::string_view old_digest) {
  auto json = captured.Json();
  Replace(json, old_digest,
          Hex(Hash(captured.bytes[captured.Index("surface.bin")], context)));
  captured.SetJson(json);
}
void Reject(const mv::SurfaceOutput& expected, Captured& captured,
            mv::SurfaceEncoding encoding, bool capsule_stage = true) {
  captured.Refresh();
  const auto result = mv::InspectSurfaceOutput(
      expected, captured.views, InspectLimits(encoding), Work(encoding, true));
  Require(result.status != mv::SurfaceStatus::kSuccess && !result.inspection,
          "real mutated bytes cannot create public proof");
  if (capsule_stage)
    Require(result.diagnostic.stage == mv::SurfaceStage::kCapsuleRead,
            "capsule defect fails during actual capsule read");
}
enum class TriangleGrowthCase { kNotRequired, kRichFixture };
void ClaimsAreNotFacts(const mv::SurfaceOutput& output,
                       const geom::ExecutionContext& context,
                       TriangleGrowthCase triangle_growth_case) {
  Captured copied(output);
  std::vector<mv::SurfaceMemberView> views;
  for (std::size_t i = 0; i < output.Members().size(); ++i) {
    const auto& member = output.Members()[i];
    views.push_back({copied.paths[i], member.kind, copied.bytes[i],
                     Hash(copied.bytes[i], context)});
  }
  mv::gltf_detail::AllocationContext base(context, {});
  auto allocation = base.WithRequestLimit(kScopeBytes);
  detail::FactsState facts(allocation);
  const auto limits = Counts();
  const auto work = Work(output.Encoding(), true);
  const detail::TestOptions options;
  detail::Run run(context, allocation, limits, work, facts, {}, options);
  run.stage = mv::SurfaceStage::kCapsuleRead;
  const auto& state = detail::Access::State(output);
  Require(state.payload.has_value(), "genuine output payload");
  const auto claims = detail::InspectCapsule(*state.payload, views, run);
  Require(claims.positions && claims.positions->complete,
          "actual parsed private claim");
  Require(!facts.positions && facts.uv.empty(),
          "capsule JSON cannot manufacture actual V numeric facts");
  const auto public_facts = output.Facts();
  const auto public_uv = public_facts.UvNumeric();
  Require(claims.uv.size() == public_uv.size(),
          "actual parsed private UV field count");
  for (std::size_t i = 0; i < claims.uv.size(); ++i) {
    Require(claims.uv[i].source_attribute == public_uv[i].source_attribute &&
                claims.uv[i].source_row == public_uv[i].source_row &&
                claims.uv[i].component == public_uv[i].component,
            "parsed outer UV identity survives canonical witness projection");
  }
  if (state.payload->position_bound_bits != 0) {
    auto tiny_limits = Counts();
    tiny_limits.max_numeric_integer_bits = 1;
    detail::Run tiny(context, allocation, tiny_limits, work, facts, {},
                     options);
    tiny.stage = mv::SurfaceStage::kCapsuleRead;
    bool bounded = false;
    std::optional<detail::CapsuleNumericClaims> denied_claims;
    try {
      denied_claims.emplace(
          detail::InspectCapsule(*state.payload, views, tiny));
    } catch (const detail::Failure& failure) {
      bounded = failure.Status() == mv::SurfaceStatus::kLimitExceeded &&
                failure.Diagnostic().stage == mv::SurfaceStage::kCapsuleRead &&
                failure.Diagnostic().code == mv::SurfaceCode::kCountLimit;
    }
    Require(bounded && !denied_claims,
            "intrinsic integer ceiling retains actual CapsuleRead phase");
    Require(!facts.positions && facts.uv.empty(),
            "failed integer claim parse creates no V facts");
  }
  const auto reject_private = [&](std::span<const mv::SurfaceMemberView> input,
                                  const mv::SurfaceCountLimits& caps,
                                  mv::SurfaceStatus status,
                                  mv::SurfaceCode code) {
    detail::FactsState denied_facts(allocation);
    detail::Run denied(context, allocation, caps, work, denied_facts, {},
                       options);
    denied.stage = mv::SurfaceStage::kCapsuleRead;
    bool matched = false;
    std::optional<detail::CapsuleNumericClaims> denied_claims;
    try {
      denied_claims.emplace(
          detail::InspectCapsule(*state.payload, input, denied));
    } catch (const detail::Failure& failure) {
      matched = failure.Status() == status &&
                failure.Diagnostic().stage == mv::SurfaceStage::kCapsuleRead &&
                failure.Diagnostic().code == code;
    }
    Require(matched && !denied_claims && !denied_facts.positions &&
                denied_facts.uv.empty(),
            "actual private refusal leaves no claim or V numeric facts");
  };
  {
    // InspectCapsule's first scan polls with the actual completed member
    // ordinal. Stop after its first admitted record, before the next record.
    struct InventoryStop {
      std::stop_source stop;
      std::uint64_t checkpoints = 0;
      bool ordered = true;
    } observation;
    detail::TestOptions observed;
    observed.checkpoint_state = &observation;
    observed.checkpoint = [](mv::SurfaceStage stage, std::uint64_t completed,
                             void* opaque) {
      auto& s = *static_cast<InventoryStop*>(opaque);
      s.ordered = s.ordered && stage == mv::SurfaceStage::kCapsuleRead &&
                  completed == s.checkpoints;
      ++s.checkpoints;
      if (completed == 1) s.stop.request_stop();
    };
    Require(views.size() > 1, "genuine multi-member capsule inventory");
    detail::FactsState stopped_facts(allocation);
    detail::Run stopped(context, allocation, limits, work, stopped_facts,
                        observation.stop.get_token(), observed);
    stopped.stage = mv::SurfaceStage::kCapsuleRead;
    bool canceled = false;
    std::optional<detail::CapsuleNumericClaims> stopped_claims;
    try {
      stopped_claims.emplace(
          detail::InspectCapsule(*state.payload, views, stopped));
    } catch (const detail::Failure& failure) {
      canceled = failure.Status() == mv::SurfaceStatus::kCanceled &&
                 failure.Diagnostic().stage == mv::SurfaceStage::kCapsuleRead &&
                 failure.Diagnostic().code == mv::SurfaceCode::kStopRequested;
    }
    Require(canceled && observation.ordered && observation.checkpoints == 2 &&
                !stopped_claims && !stopped_facts.positions &&
                stopped_facts.uv.empty(),
            "real stop inside member admission creates no private claim");
  }
  if (triangle_growth_case == TriangleGrowthCase::kRichFixture) {
    const auto original_faces =
        state.payload->original.Source().View().face_offsets.size() - 1;
    const auto triangle_faces =
        state.payload->triangles.Mesh().View().face_offsets.size() - 1;
    Require(triangle_faces > original_faces,
            "actual rich source increases triangle face count");
    auto triangle_caps = Counts();
    triangle_caps.max_faces = original_faces;
    reject_private(views, triangle_caps, mv::SurfaceStatus::kLimitExceeded,
                   mv::SurfaceCode::kTriangleCount);
  }
  if (!output.WireTriangleSources().empty()) {
    Captured damaged(output);
    const auto json = damaged.Json();
    const auto triangulation = json.find("\"triangulation\":");
    const auto face_sources = json.find("\"face_sources\":", triangulation);
    const auto offset = json.find("\"offset\":\"", face_sources);
    Require(triangulation != std::string::npos &&
                face_sources != std::string::npos &&
                offset != std::string::npos,
            "actual Geometry face-source range");
    const auto first = offset + std::string_view("\"offset\":\"").size();
    const auto last = json.find('"', first);
    Require(last != std::string::npos, "actual range offset terminator");
    const auto byte_offset = std::stoull(json.substr(first, last - first));
    auto& bytes = damaged.bytes[damaged.Index("surface.bin")];
    Require(byte_offset < bytes.size(), "actual map byte is in supplied BIN");
    const auto old_digest = Hex(Hash(bytes, context));
    bytes[static_cast<std::size_t>(byte_offset)] ^= std::byte{1};
    RehashBinary(damaged, context, old_digest);
    std::vector<mv::SurfaceMemberView> damaged_views;
    for (std::size_t i = 0; i < output.Members().size(); ++i) {
      const auto& member = output.Members()[i];
      damaged_views.push_back({damaged.paths[i], member.kind, damaged.bytes[i],
                               Hash(damaged.bytes[i], context)});
    }
    // Generous admission really reaches and rejects this paired map scalar.
    reject_private(damaged_views, Counts(),
                   mv::SurfaceStatus::kVerificationFailed,
                   mv::SurfaceCode::kBinarySlice);
    auto map_caps = Counts();
    map_caps.max_map_entries = 1;
    // The same damaged bytes must instead fail the aggregate admission first.
    reject_private(damaged_views, map_caps, mv::SurfaceStatus::kLimitExceeded,
                   mv::SurfaceCode::kMapLimit);
  }
  // This private parser result is not a native inspection or public proof.
}
void Mutations(const mv::SurfaceOutput& output, mv::SurfaceEncoding encoding,
               const geom::ExecutionContext& context) {
  {
    Captured data(output);
    data.paths.pop_back();
    data.bytes.pop_back();
    Reject(output, data, encoding, false);
  }
  {
    Captured data(output);
    data.paths.emplace_back("unknown.bin");
    data.bytes.push_back({std::byte{1}});
    Reject(output, data, encoding, false);
  }
  {
    Captured data(output);
    auto json = data.Json();
    json.insert(1, "\"unknown\":0,");
    data.SetJson(json);
    Reject(output, data, encoding);
  }
  {
    Captured data(output);
    auto json = data.Json();
    json.insert(1, "\"schema\":\"meshvale.surface-bundle/1\",");
    data.SetJson(json);
    Reject(output, data, encoding);
  }
  {
    Captured data(output);
    auto json = data.Json();
    Replace(json, "\"offset\":\"0\"", "\"offset\":\"00\"");
    data.SetJson(json);
    Reject(output, data, encoding);
  }
  {
    Captured data(output);
    auto json = data.Json();
    Replace(json, "\"scalar\":\"f64\"", "\"scalar\":\"u64\"");
    data.SetJson(json);
    Reject(output, data, encoding);
  }
  {
    Captured data(output);
    const auto old = Hex(Hash(data.bytes[data.Index("surface.bin")], context));
    data.bytes[data.Index("surface.bin")].front() ^= std::byte{1};
    RehashBinary(data, context, old);
    Reject(output, data, encoding);
  }
  {
    Captured data(output);
    const auto old = Hex(Hash(data.bytes[data.Index("surface.bin")], context));
    std::uint64_t old_listed = 0;
    for (const auto& member : output.Members())
      if (member.kind != mv::SurfaceMemberKind::kCapsuleJson)
        old_listed += member.bytes.size();
    data.bytes[data.Index("surface.bin")].push_back(std::byte{});
    RehashBinary(data, context, old);
    // Keep both internal hash and declared BIN byte count coherent. The actual
    // canonical range walk must still reject unexplained trailing storage.
    auto json = data.Json();
    const auto bin = json.find("\"path\":\"surface.bin\"");
    Require(bin != std::string::npos, "actual binary row");
    const auto length = json.find("\"bytes\":\"", bin);
    Require(length != std::string::npos, "actual binary length");
    const auto first = length + std::string_view("\"bytes\":\"").size();
    const auto last = json.find('"', first);
    json.replace(first, last - first,
                 std::to_string(data.bytes[data.Index("surface.bin")].size()));
    Replace(
        json, "\"listed_member_bytes\":\"" + std::to_string(old_listed) + "\"",
        "\"listed_member_bytes\":\"" + std::to_string(old_listed + 1) + "\"");
    data.SetJson(json);
    Reject(output, data, encoding);
  }
  if (!output.WireTriangleSources().empty()) {
    // Independent prefix: 12 XYZ/F64 + 4 offsets/U64 + 12 corners/U64 = 416.
    // Attribute00 F32 values (96 bytes) + presence (12) end at 524. Next F64
    // starts at 528 after 8-byte alignment; bytes 524..527 are the first gap.
    constexpr std::size_t kFirstPadding = 524;
    Captured data(output);
    auto& bin = data.bytes[data.Index("surface.bin")];
    Require(bin.size() > kFirstPadding && bin[kFirstPadding] == std::byte{},
            "literal zero alignment padding");
    const auto old = Hex(Hash(bin, context));
    bin[kFirstPadding] = std::byte{1};
    RehashBinary(data, context, old);
    Reject(output, data, encoding);
  }
  if (!output.WireTriangleSources().empty()) {
    Captured data(output);
    auto json = data.Json();
    // Original rich positions have12 rows x3 components.37 is malformed,
    // independently of the generous configured resource ceilings.
    Replace(json, "\"examined_values\":\"33\"", "\"examined_values\":\"37\"");
    data.SetJson(json);
    const auto result = mv::InspectSurfaceOutput(
        output, data.views, InspectLimits(encoding), Work(encoding, true));
    Require(result.status == mv::SurfaceStatus::kVerificationFailed &&
                result.diagnostic.stage == mv::SurfaceStage::kCapsuleRead &&
                result.diagnostic.code == mv::SurfaceCode::kSchema &&
                !result.inspection,
            "impossible summary count is a malformed claim, not a cap denial");
  }
  {
    Captured data(output);
    auto json = data.Json();
    Replace(json, "decoded-wire", "prepared-fields");
    data.SetJson(json);
    Reject(output, data, encoding);
  }
  if (!output.WireTriangleSources().empty()) {
    Captured data(output);
    auto json = data.Json();
    Replace(json, "\"key_hex\":\"7a657461\"", "\"key_hex\":\"7A657461\"");
    data.SetJson(json);
    Reject(output, data, encoding);
  }
  {
    Captured data(output);
    auto limits = InspectLimits(encoding);
    limits.counts.max_capsule_binary_bytes = 1;
    const auto result = mv::InspectSurfaceOutput(output, data.views, limits,
                                                 Work(encoding, true));
    Require(result.status == mv::SurfaceStatus::kLimitExceeded &&
                !result.inspection,
            "actual sidecar byte ceiling");
  }
}
void UvProjection(const mv::SurfaceOutput& output,
                  const mv::SurfaceOutputInspection& inspection,
                  std::span<const std::uint64_t> attributes, bool nonzero,
                  const geom::ExecutionContext& context) {
  const auto written_facts = output.Facts();
  const auto measured_facts = inspection.Facts();
  const auto written = written_facts.UvNumeric();
  const auto measured = measured_facts.UvNumeric();
  Require(written.size() == attributes.size() &&
              measured.size() == attributes.size(),
          "actual reached E/V UV field inventory");
  Captured captured(output);
  const auto json = captured.Json();
  const auto numeric = json.find("\"numeric\":");
  const auto uv = json.find("\"uv\":[", numeric);
  Require(numeric != std::string::npos && uv != std::string::npos,
          "actual numeric UV wrapper");
  std::size_t first_wrapper = std::string::npos;
  std::size_t first_witness = std::string::npos;
  for (std::size_t i = 0; i < attributes.size(); ++i) {
    const auto attribute = attributes[i];
    for (const auto* summary : {&written[i], &measured[i]}) {
      Require(
          summary->complete &&
              summary->kind == mv::SurfaceNumericSummary::Kind::kDecodedWire &&
              summary->source_attribute == attribute,
          "public actual UV identity is retained for every reached field");
      if (nonzero) {
        Require(summary->examined_values == 6 && summary->changed_values == 1 &&
                    summary->max_absolute_error.used_words == 1 &&
                    summary->max_absolute_error.magnitude[0] == 1 &&
                    summary->max_absolute_error.exponent2 == -30 &&
                    summary->source_row == 0 && summary->component == 0,
                "literal nonzero UV error and row/component witness retained");
      } else {
        Require(summary->changed_values == 0 &&
                    summary->max_absolute_error.used_words == 0 &&
                    !summary->source_row && !summary->component,
                "zero error retains public field identity without a maximum "
                "witness");
      }
    }
    const auto outer = "{\"source_attribute\":\"" + std::to_string(attribute) +
                       "\",\"source_set\":";
    const auto wrapper = json.find(outer, uv);
    const auto observed = json.find("\"observed\":{", wrapper);
    const auto witness = json.find("\"source_attribute\":", observed);
    const auto next = json.find("{\"source_attribute\":", wrapper + 1);
    Require(wrapper != std::string::npos && observed != std::string::npos &&
                witness != std::string::npos &&
                (next == std::string::npos || witness < next),
            "independent outer identity and nested observed witness locations");
    const std::string expected =
        nonzero ? "\"source_attribute\":\"" + std::to_string(attribute) +
                      "\",\"source_row\":\"0\",\"component\":0"
                : "\"source_attribute\":null,\"source_row\":null,\"component\":"
                  "null";
    Require(json.compare(witness, expected.size(), expected) == 0,
            "independent literal canonical nested UV witness projection");
    if (i == 0) {
      first_wrapper = wrapper;
      first_witness = witness;
    }
  }
  if (attributes.empty()) return;
  if (!nonzero) {
    auto malformed = json;
    const std::string absent = "\"source_attribute\":null";
    malformed.replace(
        first_witness, absent.size(),
        "\"source_attribute\":\"" + std::to_string(attributes.front()) + "\"");
    Captured bad(output);
    bad.SetJson(malformed);
    // JSON is excluded from its own member hash DAG. BIN/ordinary hashes
    // remain coherent; V recaptures and hashes the supplied changed JSON.
    const auto rejected = mv::InspectSurfaceOutput(
        output, bad.views, InspectLimits(output.Encoding()),
        Work(output.Encoding(), true));
    Require(rejected.status == mv::SurfaceStatus::kVerificationFailed &&
                rejected.diagnostic.stage == mv::SurfaceStage::kCapsuleRead &&
                rejected.diagnostic.code == mv::SurfaceCode::kSchema &&
                !rejected.inspection,
            "nonnull zero witness cannot manufacture an accepted proof");
    std::vector<mv::SurfaceMemberView> rehashed;
    for (std::size_t i = 0; i < output.Members().size(); ++i)
      rehashed.push_back({bad.paths[i], output.Members()[i].kind, bad.bytes[i],
                          Hash(bad.bytes[i], context)});
    mv::gltf_detail::AllocationContext base(context, {});
    auto allocation = base.WithRequestLimit(kScopeBytes);
    detail::FactsState facts(allocation);
    const auto limits = Counts();
    const auto work = Work(output.Encoding(), true);
    const detail::TestOptions options;
    detail::Run run(context, allocation, limits, work, facts, {}, options);
    run.stage = mv::SurfaceStage::kCapsuleRead;
    bool rejected_claim = false;
    std::optional<detail::CapsuleNumericClaims> claim;
    try {
      claim.emplace(detail::InspectCapsule(
          *detail::Access::State(output).payload, rehashed, run));
    } catch (const detail::Failure& failure) {
      rejected_claim =
          failure.Status() == mv::SurfaceStatus::kVerificationFailed &&
          failure.Diagnostic().stage == mv::SurfaceStage::kCapsuleRead &&
          failure.Diagnostic().code == mv::SurfaceCode::kSchema;
    }
    Require(rejected_claim && !claim && !facts.positions && facts.uv.empty(),
            "actual rehashed malformed zero witness yields no claim or V fact");
  }
  {
    auto wrong_identity = json;
    const auto first =
        first_wrapper + std::string_view("{\"source_attribute\":\"").size();
    const auto last = wrong_identity.find('"', first);
    Require(last != std::string::npos, "actual outer identity terminator");
    wrong_identity.replace(first, last - first,
                           std::to_string(attributes.front() + 1));
    Captured bad(output);
    bad.SetJson(wrong_identity);
    const auto rejected = mv::InspectSurfaceOutput(
        output, bad.views, InspectLimits(output.Encoding()),
        Work(output.Encoding(), true));
    Require(rejected.status == mv::SurfaceStatus::kVerificationFailed &&
                rejected.diagnostic.stage == mv::SurfaceStage::kCapsuleRead &&
                rejected.diagnostic.code == mv::SurfaceCode::kUvMap &&
                !rejected.inspection,
            "actual parsed outer identity must match the retained binding");
  }
}
void InspectionCapControls(const mv::SurfaceOutput& output,
                           const Captured& captured,
                           const mv::SurfaceFacts& measured,
                           const geom::ExecutionContext& context,
                           bool catalog) {
  struct CapCase {
    std::uint64_t mv::SurfaceCountLimits::* field;
    mv::SurfaceStage stage;
    mv::SurfaceCode code;
    std::string_view label;
  };
  constexpr std::array<CapCase, 4> kBareCases{{
      {&mv::SurfaceCountLimits::max_json_bytes, mv::SurfaceStage::kCapture,
       mv::SurfaceCode::kOutputLimit, "ordinary JSON bytes"},
      {&mv::SurfaceCountLimits::max_wire_vertices, mv::SurfaceStage::kSource,
       mv::SurfaceCode::kCountLimit, "wire vertices"},
      {&mv::SurfaceCountLimits::max_wire_corners, mv::SurfaceStage::kSource,
       mv::SurfaceCode::kCountLimit, "wire corners"},
      {&mv::SurfaceCountLimits::max_map_entries, mv::SurfaceStage::kSource,
       mv::SurfaceCode::kMapLimit, "aggregate maps"},
  }};
  constexpr std::array<CapCase, 4> kCatalogCases{{
      {&mv::SurfaceCountLimits::max_wire_primitives, mv::SurfaceStage::kSource,
       mv::SurfaceCode::kCountLimit, "wire primitives"},
      {&mv::SurfaceCountLimits::max_resources, mv::SurfaceStage::kSource,
       mv::SurfaceCode::kCountLimit, "selected images"},
      {&mv::SurfaceCountLimits::max_resource_bytes, mv::SurfaceStage::kSource,
       mv::SurfaceCode::kCountLimit, "selected image bytes"},
      {&mv::SurfaceCountLimits::max_material_bindings,
       mv::SurfaceStage::kSource, mv::SurfaceCode::kCountLimit,
       "selected material bindings"},
  }};
  const auto wire = measured.WireCounts();
  Require(wire && measured.PositionNumeric() &&
              measured.PositionNumeric()->complete,
          "actual successful native inspection precedes low-cap controls");
  if (catalog) {
    Require(wire->primitives > 1 && wire->images > 1 && wire->materials > 1,
            "actual nonempty catalog extents exceed each requested cap");
  } else {
    Require(output.Representation() == mv::SurfaceRepresentation::kStrictBare &&
                wire->vertices == 3 && wire->corners == 3 &&
                wire->map_entries > 1,
            "genuine bare native triangle and actual map extents");
    bool ordinary_json = false;
    for (const auto& member : output.Members()) {
      Require(member.kind != mv::SurfaceMemberKind::kCapsuleJson &&
                  member.kind != mv::SurfaceMemberKind::kCapsuleBinary,
              "bare cap controls do not depend on capsule parsing");
      if (member.kind == mv::SurfaceMemberKind::kGltf)
        ordinary_json = member.bytes.size() > 1;
    }
    Require(ordinary_json, "actual ordinary JSON exceeds one byte");
  }
  const auto baseline = context.ActiveTrackedPayload();
  bool all_admitted_before_work = true;
  const auto cases = catalog ? std::span<const CapCase>{kCatalogCases}
                             : std::span<const CapCase>{kBareCases};
  for (const auto& test : cases) {
    auto limits = InspectLimits(mv::SurfaceEncoding::kGltfJson);
    limits.counts.*test.field = 1;
    {
      const auto refused =
          mv::InspectSurfaceOutput(output, captured.views, limits,
                                   Work(mv::SurfaceEncoding::kGltfJson, true));
      const bool early = refused.status == mv::SurfaceStatus::kLimitExceeded &&
                         refused.diagnostic.stage == test.stage &&
                         refused.diagnostic.code == test.code &&
                         !refused.inspection && refused.facts &&
                         !refused.facts->WireCounts() &&
                         !refused.facts->PositionNumeric() &&
                         refused.facts->UvNumeric().empty() &&
                         refused.facts->Delegates().empty();
      if (!early)
        std::cerr << "V cap " << test.label << ": status "
                  << static_cast<std::uint32_t>(refused.status) << ", stage "
                  << static_cast<std::uint32_t>(refused.diagnostic.stage)
                  << ", code "
                  << static_cast<std::uint32_t>(refused.diagnostic.code)
                  << '\n';
      all_admitted_before_work = early && all_admitted_before_work;
    }
    Require(context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "low-cap inspection releases its actual scope and workers");
  }
  Require(all_admitted_before_work,
          "independent V caps refuse before payload/native work");
}
void FocusedUv(mv::SurfaceEncoding encoding, bool zero_domain,
               const geom::ExecutionContext& context) {
  // Original tiny control: the first UV coordinate is exactly1+2^-30 in F64.
  // RNE32 is1, giving error2^-30 at attribute0/row0/component0. Empty domains
  // use the same real typed descriptor and zero scalar backing.
  constexpr std::array<double, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
  constexpr std::array<double, 6> uv{0x1.00000004p0, 0, 0, 1, 0, 0};
  constexpr std::array<geom::index_t, 2> offsets{0, 3};
  constexpr std::array<geom::index_t, 3> corners{0, 1, 2};
  const std::span<const double> position_values =
      zero_domain ? std::span<const double>{}
                  : std::span<const double>{positions};
  const std::span<const double> uv_values =
      zero_domain ? std::span<const double>{} : std::span<const double>{uv};
  const std::span<const geom::index_t> face_offsets =
      zero_domain ? std::span<const geom::index_t>{kEmptyOffsets}
                  : std::span<const geom::index_t>{offsets};
  const std::span<const geom::index_t> corner_vertices =
      zero_domain ? std::span<const geom::index_t>{}
                  : std::span<const geom::index_t>{corners};
  std::array<geom::AttributeMaterializationInput, 1> attributes{};
  attributes[0].domain = geom::AttributeDomain::vertex;
  attributes[0].name = "uv";
  attributes[0].semantic = "TEXCOORD";
  attributes[0].set_index = 0;
  attributes[0].components = 2;
  attributes[0].values = uv_values;
  auto source = geom::MaterializeMesh(
      {position_values,
       geom::PolygonTopologyInput{face_offsets, corner_vertices}, attributes},
      MeshLimits(), kWork, context);
  if (source.status ==
      geom::MaterializationStatus::kUnsupportedAllocationProfile) {
    Require(!source.mesh, "actual focused UV constructor profile refusal");
    return;
  }
  Require(source.status == geom::MaterializationStatus::kSuccess && source.mesh,
          "genuine focused UV materialized owner");
  const auto original =
      geom::InspectMaterializedMesh(*source.mesh, kInspect, kWork);
  const auto triangles =
      geom::TriangulateCharged(*source.mesh, {64, 1},
                               {MeshLimits(), kInspect, MeshLimits(), kInspect,
                                65536, kScopeBytes, kScopeBytes},
                               kWork);
  Require(original.inspection && original.inspection->IsStorageValid() &&
              triangles.status == geom::ChargedTriangulationStatus::kAccepted &&
              triangles.triangulation,
          "genuine focused UV dependencies");
  constexpr std::array<mv::SurfaceChannelRule, 1> channels{
      {{0, mv::SurfaceChannelRole::kUv, true}}};
  constexpr std::array<mv::SurfaceUvBinding, 1> binding{{{0, 0, 0}}};
  const std::array<double, 1> bounds{zero_domain ? 0 : 0x1p-29};
  mv::SurfaceEncodeRequest request{
      .original = *original.inspection,
      .triangles = *triangles.triangulation,
      .representation = mv::SurfaceRepresentation::kInvalid,
      .acknowledge_triangle_conversion = false,
      .acknowledge_corner_expansion = false,
      .acknowledge_unused_vertices = false,
      .channels = {},
      .numeric = {},
      .destination = {}};
  request.representation = mv::SurfaceRepresentation::kAnnotatedBundleV1;
  request.acknowledge_triangle_conversion = true;
  request.acknowledge_corner_expansion = true;
  request.acknowledge_unused_vertices = true;
  request.channels = channels;
  request.numeric.positions = zero_domain ? mv::SurfaceNarrowing::kExact32
                                          : mv::SurfaceNarrowing::kBoundedRne32;
  request.numeric.max_uv_component_errors = bounds;
  mv::GltfSurfaceOptions destination;
  destination.encoding = encoding;
  destination.coordinates =
      mv::SurfaceCoordinates::kDeclaredMetersRightHandedYUp;
  destination.uv = binding;
  request.destination = destination;
  const auto encoded =
      mv::EncodeSurface(request, EncodeLimits(encoding), Work(encoding));
  Require(encoded.status == mv::SurfaceStatus::kSuccess && encoded.output,
          "actual focused UV annotated encoding");
  Captured captured(*encoded.output);
  const auto verified =
      mv::InspectSurfaceOutput(*encoded.output, captured.views,
                               InspectLimits(encoding), Work(encoding, true));
  if (zero_domain && verified.status == mv::SurfaceStatus::kUnsupported &&
      verified.diagnostic.code == mv::SurfaceCode::kNumericProfile) {
    Require(!verified.inspection && verified.facts,
            "actual zero-domain parser profile refusal, no fabricated proof");
    return;
  }
  Require(verified.status == mv::SurfaceStatus::kSuccess && verified.inspection,
          "actual focused UV encode/inspect roundtrip");
  constexpr std::array<std::uint64_t, 1> identity{0};
  UvProjection(*encoded.output, *verified.inspection, identity, !zero_domain,
               context);
  const auto measured = verified.inspection->Facts();
  Require(measured.UvNumeric().size() == 1 &&
              measured.UvNumeric()[0].examined_values == (zero_domain ? 0 : 6),
          "actual empty/nonempty UV domain examination");
  ClaimsAreNotFacts(*encoded.output, context, TriangleGrowthCase::kNotRequired);
  if (!zero_domain && encoding == mv::SurfaceEncoding::kGltfJson) {
    auto bare_request = request;
    bare_request.representation = mv::SurfaceRepresentation::kStrictBare;
    const auto bare =
        mv::EncodeSurface(bare_request, EncodeLimits(encoding), Work(encoding));
    Require(bare.status == mv::SurfaceStatus::kSuccess && bare.output,
            "genuine nonempty strict-bare UV encoding");
    Captured bare_captured(*bare.output);
    const auto bare_verified =
        mv::InspectSurfaceOutput(*bare.output, bare_captured.views,
                                 InspectLimits(encoding), Work(encoding, true));
    Require(bare_verified.status == mv::SurfaceStatus::kSuccess &&
                bare_verified.inspection,
            "actual strict-bare native inspection before low-cap variants");
    InspectionCapControls(*bare.output, bare_captured,
                          bare_verified.inspection->Facts(), context, false);
  }
}
void Example(mv::SurfaceEncoding encoding, fixture::Variant variant,
             fixture::MetadataRoute route,
             const geom::ExecutionContext& context) {
  auto source = geom::MaterializeMesh(fixture::RichInput(variant, route),
                                      MeshLimits(), kWork, context);
  const bool empty = source.status ==
                     geom::MaterializationStatus::kUnsupportedAllocationProfile;
  if (empty) {
    Require(!source.mesh, "actual nonempty Geometry refusal");
    source =
        geom::MaterializeMesh({std::span<const double>{},
                               geom::PolygonTopologyInput{kEmptyOffsets, {}},
                               {}},
                              MeshLimits(), kWork, context);
  }
  Require(source.mesh && source.status == geom::MaterializationStatus::kSuccess,
          "genuine source owner");
  const auto inspection =
      geom::InspectMaterializedMesh(*source.mesh, kInspect, kWork);
  Require(inspection.inspection && inspection.inspection->IsStorageValid(),
          "genuine original inspection");
  const auto triangles =
      geom::TriangulateCharged(*source.mesh, {64, 1},
                               {MeshLimits(), kInspect, MeshLimits(), kInspect,
                                65536, kScopeBytes, kScopeBytes},
                               kWork);
  Require(triangles.triangulation &&
              triangles.status == geom::ChargedTriangulationStatus::kAccepted,
          "genuine charged result");
  std::optional<mv::GltfAsset> catalog;
  if (!Stl(encoding) && !empty) {
    const auto json = fixture::CatalogJson();
    const auto read = mv::ReadGltfAssetCapped(
        std::as_bytes(std::span<const char>{json.data(), json.size()}),
        fixture::CatalogResources(), ReadLimits(), {4096, 8192, 4096, 8192, {}},
        context);
    Require(read.asset.has_value(), "actual original catalog read");
    catalog = read.asset;
  }
  std::array<mv::SurfaceChannelRule, 19> channels{};
  for (std::size_t i = 0; i < channels.size(); ++i)
    channels[i] = {i, mv::SurfaceChannelRole::kBundleOnly, true};
  if (Stl(encoding))
    channels[14].role = mv::SurfaceChannelRole::kStlCornerNormal;
  else {
    for (std::size_t i = 15; i < 18; ++i)
      channels[i].role = mv::SurfaceChannelRole::kUv;
    channels[18].role = mv::SurfaceChannelRole::kMaterialId;
  }
  mv::SurfaceEncodeRequest request{
      .original = *inspection.inspection,
      .triangles = *triangles.triangulation,
      .representation = mv::SurfaceRepresentation::kInvalid,
      .acknowledge_triangle_conversion = false,
      .acknowledge_corner_expansion = false,
      .acknowledge_unused_vertices = false,
      .channels = {},
      .numeric = {},
      .destination = {}};
  request.representation = mv::SurfaceRepresentation::kAnnotatedBundleV1;
  request.acknowledge_triangle_conversion = true;
  request.acknowledge_corner_expansion = true;
  request.acknowledge_unused_vertices = true;
  if (!empty) request.channels = channels;
  request.numeric.positions = encoding == mv::SurfaceEncoding::kStlAscii
                                  ? mv::SurfaceNarrowing::kExact64
                                  : (variant == fixture::Variant::kTranslated
                                         ? mv::SurfaceNarrowing::kBoundedRne32
                                         : mv::SurfaceNarrowing::kExact32);
  request.numeric.max_position_linf_error =
      variant == fixture::Variant::kTranslated ? 0x1p-29 : 0;
  if (Stl(encoding)) {
    mv::StlSurfaceOptions options;
    options.encoding = encoding;
    options.coordinates = mv::SurfaceCoordinates::kStlUnitsUnspecified;
    if (!empty) options.corner_normal_attribute = 14;
    if (encoding == mv::SurfaceEncoding::kStlAscii)
      options.ascii_name = "capsule";
    else
      options.binary_header = kHeader;
    request.destination = options;
  } else {
    mv::GltfSurfaceOptions options;
    options.encoding = encoding;
    options.coordinates = mv::SurfaceCoordinates::kDeclaredMetersRightHandedYUp;
    if (!empty) {
      options.uv = kUv;
      options.catalog = &*catalog;
      options.materials = kMaterials;
      options.catalog_uv = kCatalogUv;
      request.numeric.max_uv_component_errors = kUvBounds;
    }
    request.destination = options;
  }
  const auto encoded =
      mv::EncodeSurface(request, EncodeLimits(encoding), Work(encoding));
  if (empty && Stl(encoding)) {
    Require(encoded.status == mv::SurfaceStatus::kUnsupported &&
                encoded.diagnostic.code == mv::SurfaceCode::kNormalAbsent &&
                !encoded.output,
            "real absent-normal empty STL refusal");
    return;
  }
  Require(encoded.status == mv::SurfaceStatus::kSuccess && encoded.output,
          "actual annotated encoder");
  Captured captured(*encoded.output);
  const auto verified =
      mv::InspectSurfaceOutput(*encoded.output, captured.views,
                               InspectLimits(encoding), Work(encoding, true));
  if (empty && verified.status == mv::SurfaceStatus::kUnsupported &&
      verified.diagnostic.code == mv::SurfaceCode::kNumericProfile) {
    Require(!verified.inspection && verified.facts,
            "actual parser profile refusal");
    for (const auto& call : verified.facts->Delegates())
      Require(call.kind != mv::SurfaceDelegateKind::kGltfRead &&
                  call.kind != mv::SurfaceDelegateKind::kGltfExtract,
              "no fabricated native calls after profile refusal");
    return;
  }
  Require(verified.status == mv::SurfaceStatus::kSuccess && verified.inspection,
          "actual independent capsule/native proof");
  const auto numeric = verified.inspection->Facts().PositionNumeric();
  Require(numeric && numeric->complete &&
              numeric->kind == mv::SurfaceNumericSummary::Kind::kDecodedWire,
          "fresh actual decoded facts");
  Require(numeric->examined_values == (empty ? 0 : 33),
          "eleven used source vertices, no fabricated unused conversion");
  if (!empty && encoding != mv::SurfaceEncoding::kStlAscii)
    Require(numeric->changed_values ==
                (variant == fixture::Variant::kTranslated ? 11 : 0),
            "literal translated changed-x count");
  if (!empty && !Stl(encoding)) {
    constexpr std::array<std::uint64_t, 3> identities{15, 16, 17};
    UvProjection(*encoded.output, *verified.inspection, identities, false,
                 context);
  }
  if (!empty) CopyControls(*encoded.output, context);
  ClaimsAreNotFacts(*encoded.output, context,
                    empty ? TriangleGrowthCase::kNotRequired
                          : TriangleGrowthCase::kRichFixture);
  if (!empty && encoding == mv::SurfaceEncoding::kGltfJson &&
      variant == fixture::Variant::kOriginal &&
      route == fixture::MetadataRoute::kRetained)
    InspectionCapControls(*encoded.output, captured,
                          verified.inspection->Facts(), context, true);
  Mutations(*encoded.output, encoding, context);
  if (!empty) {
    request.representation = mv::SurfaceRepresentation::kStrictBare;
    const auto bare =
        mv::EncodeSurface(request, EncodeLimits(encoding), Work(encoding));
    Require(bare.status != mv::SurfaceStatus::kSuccess && !bare.output,
            "rich extra storage cannot masquerade as strict bare");
  }
}
}  // namespace

int main() {
  try {
    geom::ExecutionContext context({4, 1, 2048 * kMiB});
    HashVectors(context);
    for (const auto encoding :
         {mv::SurfaceEncoding::kGltfJson, mv::SurfaceEncoding::kGlb}) {
      FocusedUv(encoding, true, context);
      FocusedUv(encoding, false, context);
      Require(
          context.ActiveWorkers() == 0 && context.ActiveTrackedPayload() == 0,
          "focused UV owners and actual work released");
    }
    for (const auto encoding : kEncodings)
      for (const auto variant :
           {fixture::Variant::kOriginal, fixture::Variant::kTranslated})
        for (const auto route : {fixture::MetadataRoute::kRetained,
                                 fixture::MetadataRoute::kLegacy}) {
          Example(encoding, variant, route, context);
          Require(context.ActiveWorkers() == 0 &&
                      context.ActiveTrackedPayload() == 0,
                  "all real joins/owners released");
        }
    std::cout << "surface capsule actual-byte controls completed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
