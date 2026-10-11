// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <ostream>
#include <source_location>
#include <span>
#include <string_view>
#include <vector>

#include "fixture_support.h"

namespace gltf_topology_write_semantics {
namespace {
constexpr std::array<geo::index_t, 1> kSelectedFaces{1};
constexpr std::array<geo::index_t, 3> kSelectedCorners{3, 4, 5};
constexpr std::array<geo::index_t, 2> kIdentityFaces{0, 1};
constexpr std::array<geo::index_t, 6> kIdentityCorners{0, 1, 2, 3, 4, 5};
constexpr std::array<FixtureKind, 4> kSmallFixtures{
    FixtureKind::kTriangles, FixtureKind::kStrip, FixtureKind::kFan,
    FixtureKind::kImplicit};
constexpr std::size_t kWorkerBudget = 4;
constexpr std::uint64_t kMinimumLargeResourceBudgetMib = 2048;

void RequireWriteSuccess(
    const mv::GltfTopologyWriteResult& result, std::string_view message,
    std::source_location source = std::source_location::current()) {
  const bool succeeded =
      result.status == mv::GltfTopologyWriteStatus::kSuccess && result.bundle;
  if (!succeeded) {
    std::cerr << "Topology writer failure: status="
              << static_cast<int>(result.status)
              << " stage=" << static_cast<int>(result.diagnostic.stage)
              << " code=" << static_cast<int>(result.diagnostic.code);
    if (result.diagnostic.attribute)
      std::cerr << " attribute=" << *result.diagnostic.attribute;
    if (result.diagnostic.element)
      std::cerr << " element=" << *result.diagnostic.element;
    if (result.diagnostic.json_span) std::cerr << " json_span_present=true";
    if (result.diagnostic.storage_issue)
      std::cerr << " storage_issue_present=true";
    std::cerr << '\n' << std::flush;
  }
  Require(succeeded, message, source);
}

void CheckExecution(const mv::GltfTopologyWriteExecution& execution,
                    bool serial) {
  const std::array phases{execution.verification, execution.index_encoding,
                          execution.base64_encoding, execution.output_copy};
  for (const auto& phase : phases) {
    Require(phase.launched_workers == phase.joined_workers,
            "Published writer phase contains unjoined workers");
    if (serial)
      Require(phase.launched_workers == 0,
              "Serial or occupied writer policy launched a worker");
  }
}
geo::MaterializedFaceSelection Select(
    const mv::GltfMaterializedPrimitive& source,
    std::span<const geo::index_t> faces, bool large = false) {
  const auto result = geo::SelectMeshFaces(
      source.Mesh(), faces, TransferLimits(large).materialization,
      GeometryWork(), {});
  Require(
      result.selection && result.status == geo::MaterializationStatus::kSuccess,
      "Independent selected candidate must be constructible");
  return *result.selection;
}
}  // namespace

void CheckSelectionAndReload() {
  for (const auto kind : kSmallFixtures) {
    const auto fixture = MakeFixture(kind);
    const auto context = Context();
    const auto source = Transfer(fixture, context);
    const auto selection = Select(source, kSelectedFaces);
    const auto candidate = Inspect(selection.Candidate());
    const mv::GltfTopologyWriteRequest request{
        source, candidate, kSelectedFaces, kSelectedCorners};
    const auto baseline = context.ActiveTrackedPayload();
    auto serial = mv::WriteGltfTopologySelection(request, WriteLimits(),
                                                 WriteWork(true), {});
    RequireWriteSuccess(
        serial, "Checked face selection must publish a complete bundle");
    CheckExecution(serial.execution, true);
    CheckWrittenBundle(fixture, *serial.bundle, true);
    auto parallel =
        mv::WriteGltfTopologySelection(request, WriteLimits(), WriteWork(), {});
    RequireWriteSuccess(
        parallel, "Parallel face selection must publish a complete bundle");
    CheckExecution(parallel.execution, false);
    RequireBits<std::byte>(parallel.bundle->Json(), serial.bundle->Json(),
                           "Serial/parallel assembly bytes differ");
    RequireBits<std::byte>(parallel.bundle->Resources().back().bytes,
                           serial.bundle->Resources().back().bytes,
                           "Serial/parallel index bytes differ");
    if (kind != FixtureKind::kImplicit)
      Require(parallel.execution.output_copy.launched_workers > 0,
              "Multi-kilobyte golden JSON must use real admitted copy workers");
    {
      auto occupied = geo::TryReserveWorkers(context, kWorkerBudget);
      Require(occupied.Count() == kWorkerBudget,
              "Independent fixture must occupy the complete worker budget");
      const auto fallback = mv::WriteGltfTopologySelection(
          request, WriteLimits(), WriteWork(), {});
      RequireWriteSuccess(fallback,
                          "Occupied worker slots must permit caller fallback");
      CheckExecution(fallback.execution, true);
      RequireBits<std::byte>(fallback.bundle->Json(), serial.bundle->Json(),
                             "Occupied fallback changed output bytes");
    }
    parallel.bundle.reset();
    serial.bundle.reset();
    Require(context.ActiveTrackedPayload() == baseline &&
                context.ActiveWorkers() == 0,
            "Writer output cleanup changed retained input charges or slots");
  }
}

void CheckIdentityAndLifetimes() {
  for (const auto kind : kSmallFixtures) {
    const auto fixture = MakeFixture(kind);
    std::optional<mv::GltfTopologyWriteBundle> retained;
    {
      const auto context = Context();
      const auto source = Transfer(fixture, context);
      const auto result = mv::WriteGltfTopologySelection(
          {source, source.Inspection(), kIdentityFaces, kIdentityCorners},
          WriteLimits(), WriteWork(), {});
      RequireWriteSuccess(result, "Verified identity selection must succeed");
      retained = *result.bundle;
    }
    // All caller contexts, source/extraction wrappers and original result died.
    CheckWrittenBundle(fixture, *retained, false);
    const auto copy = *retained;
    retained.reset();
    CheckWrittenBundle(fixture, copy, false);
  }
  const auto fixture = MakeFixture(FixtureKind::kStrip);
  std::optional<mv::GltfTopologyWriteBundle> retained;
  {
    const auto context = Context();
    const auto source = Transfer(fixture, context);
    const auto selection = Select(source, kSelectedFaces);
    const auto candidate = Inspect(selection.Candidate());
    std::vector<geo::index_t> faces(kSelectedFaces.begin(),
                                    kSelectedFaces.end());
    std::vector<geo::index_t> corners(kSelectedCorners.begin(),
                                      kSelectedCorners.end());
    const auto result = mv::WriteGltfTopologySelection(
        {source, candidate, faces, corners}, WriteLimits(), WriteWork(), {});
    RequireWriteSuccess(result,
                        "Changed output must publish owned maps/resources");
    retained = *result.bundle;
    for (auto& face : faces) face = 999;
    for (auto& corner : corners) corner = 999;
  }
  CheckWrittenBundle(fixture, *retained, true);
  const auto copied = *retained;
  retained.reset();
  CheckWrittenBundle(fixture, copied, true);
  const auto retained_source = copied.Source();
  CheckSourceExtraction(fixture, retained_source.SourceExtraction());
}

void CheckLargeIndex(std::uint64_t resource_budget_mib) {
  Require(
      resource_budget_mib >= kMinimumLargeResourceBudgetMib,
      "Required large-index qualification needs an explicit 2048 MiB budget");
  const auto fixture = MakeFixture(FixtureKind::kLargeIndex);
  const auto context = Context(true);
  const auto source = Transfer(fixture, context);
  constexpr std::array<geo::index_t, 1> kFirstFace{0};
  constexpr std::array<geo::index_t, 3> kFirstCorners{0, 1, 2};
  const auto selection = Select(source, kFirstFace, true);
  const auto inspection = Inspect(selection.Candidate(), true);
  const auto result = mv::WriteGltfTopologySelection(
      {source, inspection, kFirstFace, kFirstCorners}, WriteLimits(true),
      WriteWork(), {});
  RequireWriteSuccess(
      result, "Public-path UINT32 index above 2^24 must remain writable");
  CheckExecution(result.execution, false);
  Require(result.execution.verification.launched_workers > 0,
          "Large complete position verification must launch real workers");
  CheckWrittenBundle(fixture, *result.bundle, true, 0);
  const auto candidate = result.bundle->Source().Mesh().View();
  Require(candidate.positions.size() == fixture.vertex_count * 3,
          "Large qualification truncated original vertex rows");
}
}  // namespace gltf_topology_write_semantics
