// SPDX-License-Identifier: Apache-2.0
#include "surface_python_pipeline.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace meshvale::interchange::surface_python_detail {
namespace geo = geometry;
namespace alloc = gltf_detail;
namespace {
constexpr auto kNativeSuccess = SurfaceStatus::kSuccess;

// Direct non-list initialization through this conversion constructs the native
// return value in its final optional slot. C++20 guaranteed copy elision avoids
// any intervening move/copy, including for GltfAsset's non-noexcept copy API.
// The callable is borrowed only through the synchronous emplace expression.
template <class Result, class Delegate>
struct NativeResultFactory {
  Delegate& delegate;
  operator Result() const { return delegate(); }
};

// Mark an actually entering delegate before it can throw. Native publication
// also keeps its existing nonthrowing owner-transfer contract independently of
// the in-place construction used for every phase here.
template <class Result, class Delegate>
void Call(SurfaceNativeResult& state, SurfaceNativePhase phase,
          std::optional<Result>& destination, Delegate delegate) {
  static_assert(
      std::is_nothrow_move_constructible_v<SurfaceFilePublicationResult>);
  state.terminal_phase = phase;
  auto& call = state.calls[static_cast<std::size_t>(phase)];
  call = SurfaceNativeCall::kThrew;
  destination.emplace(NativeResultFactory<Result, Delegate>{delegate});
  call = SurfaceNativeCall::kReturned;
}

class ReleaseRecordStorage {
 public:
  explicit ReleaseRecordStorage(SurfaceRecordCapture& record) noexcept
      : record_(record) {}
  ~ReleaseRecordStorage() { record_.ReleaseCapturedStorage(); }
  ReleaseRecordStorage(const ReleaseRecordStorage&) = delete;
  ReleaseRecordStorage& operator=(const ReleaseRecordStorage&) = delete;

 private:
  SurfaceRecordCapture& record_;
};

bool ExpectedDeliveryRefusal(const alloc::AdmissionFailure& error) noexcept {
  return error.Code() == alloc::AdmissionCode::kBudgetExceeded ||
         error.Code() == alloc::AdmissionCode::kCanceled ||
         (error.Code() == alloc::AdmissionCode::kSizeLimit &&
          error.Provenance() ==
              alloc::AdmissionProvenance::kOperationRequestLimit);
}
}  // namespace

void RunCapturedSurface(SurfaceRecordCapture& record,
                        SharedSurfaceIngress& ingress,
                        const OwnedSurfaceOptions& options,
                        const SurfacePresetProjection& preset,
                        const geo::ExecutionContext& context,
                        std::optional<SurfaceCatalogInput> catalog,
                        std::optional<SurfacePublicationInput> publication,
                        std::stop_token stop, SurfaceNativeResult& state) {
  if (state.started)
    throw std::logic_error("surface native continuation is single-use");
  state.started = true;
  if (ingress.Observation().status != RecordIngressStatus::kCaptured)
    throw std::logic_error(
        "surface native continuation requires captured input");
  const alloc::AllocationContext root(context, {});
  if (!ingress.Bridge().SharesExecutionBudget(root))
    throw std::logic_error("surface native continuation requires one ledger");
  const auto encoding = options.Encoding();
  if (catalog && (encoding == SurfaceEncoding::kStlAscii ||
                  encoding == SurfaceEncoding::kStlBinary))
    throw std::invalid_argument("STL surface output has no catalog input");
  const auto& format = preset.For(encoding);
  (void)preset.OperationAllowance(encoding, catalog.has_value(),
                                  publication.has_value());
  const auto input = record.Input();
  {
    // Native materialization joins before return/throw. The input's staged
    // storage is no longer read after that boundary, including on exceptions.
    ReleaseRecordStorage release(record);
    Call(state, SurfaceNativePhase::kMaterialization, state.materialization,
         [&] {
           return geo::MaterializeMesh(input, preset.source_materialization,
                                       preset.mesh_work, context, stop);
         });
  }
  const auto& materialized = *state.materialization;
  if (materialized.status != geo::MaterializationStatus::kSuccess ||
      !materialized.mesh)
    return;
  Call(state, SurfaceNativePhase::kSourceInspection, state.source_inspection,
       [&] {
         return geo::InspectMaterializedMesh(*materialized.mesh,
                                             preset.source_inspection,
                                             preset.mesh_work, stop);
       });
  const auto& original = *state.source_inspection;
  if (original.status != geo::MaterializationStatus::kSuccess ||
      !original.inspection || !original.inspection->IsStorageValid())
    return;
  Call(state, SurfaceNativePhase::kTriangulation, state.triangulation, [&] {
    return geo::TriangulateCharged(
        *materialized.mesh, preset.triangulation_options,
        preset.triangulation_limits, preset.mesh_work, stop);
  });
  const auto& triangulated = *state.triangulation;
  if (triangulated.status != geo::ChargedTriangulationStatus::kAccepted ||
      !triangulated.triangulation)
    return;
  if (catalog) {
    Call(state, SurfaceNativePhase::kCatalogRead, state.catalog_read, [&] {
      // The explicit default request selects retained core metadata. No
      // deformation/translation interpretation is admitted for a material
      // catalog, and no filesystem or URI provider is installed.
      return ReadGltfAssetCapped(catalog->json, catalog->resources,
                                 GltfReadRequest{}, preset.catalog_read_limits,
                                 preset.catalog_read_work, context, stop);
    });
    if (!state.catalog_read->asset) return;
  }
  const auto request =
      options.Request(*original.inspection, *triangulated.triangulation,
                      catalog ? &*state.catalog_read->asset : nullptr);
  Call(state, SurfaceNativePhase::kEncode, state.encode, [&] {
    return EncodeSurface(request, format.encode_limits, format.encode_work,
                         stop);
  });
  if (state.encode->status != kNativeSuccess || !state.encode->output) return;
  state.terminal_phase = SurfaceNativePhase::kInspection;
  state.delivery_call = SurfaceDeliveryCall::kThrew;
  // This block catches only actually classified admission failures. Checked
  // arithmetic, invalid objects, bad_alloc and unrelated exceptions propagate
  // with earlier returned owners intact and inspection still not called.
  try {
    CapturedSurfaceMembers actual(*state.encode->output, ingress.Bridge(),
                                  state.delivery);
    actual.Capture(preset.mesh_work,
                   preset.ingress_work.cancellation_block_bytes, stop);
    state.delivery_call = SurfaceDeliveryCall::kCaptured;
    Call(state, SurfaceNativePhase::kInspection, state.inspection, [&] {
      return InspectSurfaceOutput(*state.encode->output, actual.Members(),
                                  format.inspect_limits, format.inspect_work,
                                  stop);
    });
  } catch (const alloc::AdmissionFailure& error) {
    // A delegate throw must remain that delegate's throw, not be recast as a
    // normal delivery refusal just because it uses the same private exception.
    if (state.delivery_call == SurfaceDeliveryCall::kCaptured ||
        !ExpectedDeliveryRefusal(error))
      throw;
    state.delivery_call = SurfaceDeliveryCall::kRefused;
    state.delivery_admission = error.Code();
    state.delivery_provenance = error.Provenance();
    return;
  }
  if (state.inspection->status != kNativeSuccess ||
      !state.inspection->inspection)
    return;
  if (publication) {
    const SurfaceFilePublicationOptions publish_options{
        publication->receipt_path, {}};
    Call(state, SurfaceNativePhase::kPublication, state.publication, [&] {
      return PublishSurfaceBundleNative(
          *state.inspection->inspection, publication->destination,
          publish_options, format.publication_limits, format.publication_work,
          stop);
    });
    if (state.publication->outcome !=
            SurfaceFilePublicationOutcome::kPublished ||
        !state.publication->bundle)
      return;
  }
  state.complete = true;
  state.terminal_phase.reset();
}
}  // namespace meshvale::interchange::surface_python_detail
