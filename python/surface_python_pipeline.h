// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_PIPELINE_H_
#define MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_PIPELINE_H_

#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <stop_token>

#include "surface_python_delivery.h"
#include "surface_python_options.h"
#include "surface_python_publication.h"
#include "surface_record_capture.h"

namespace meshvale::interchange::surface_python_detail {

enum class SurfaceNativePhase {
  kMaterialization,
  kSourceInspection,
  kTriangulation,
  kCatalogRead,
  kEncode,
  kInspection,
  kPublication,
  kCount
};
enum class SurfaceNativeCall { kNotCalled, kReturned, kThrew };
enum class SurfaceDeliveryCall { kNotCalled, kCaptured, kRefused, kThrew };

// Raw catalog bytes must already have passed the shared ingress. They are not a
// catalog proof and are always submitted to the actual capped native reader.
struct SurfaceCatalogInput {
  std::span<const std::byte> json;
  std::span<const SuppliedGltfResource> resources;
};
// The enclosing charged operation precreates this reachable holder. Every
// optional contains only an actually returned native result; nothing later is
// synthesized on refusal/exception. Delivery work is separate from inspection.
// The enclosing single-use admission owns concurrency; do not read while Run
// mutates it. All returned strong native owners survive subsequent failures.
struct SurfaceNativeResult {
  std::array<SurfaceNativeCall,
             static_cast<std::size_t>(SurfaceNativePhase::kCount)>
      calls{};
  std::optional<SurfaceNativePhase> terminal_phase;
  bool started = false;
  bool complete = false;
  std::optional<geometry::MeshMaterializationResult> materialization;
  std::optional<geometry::MeshInspectionResult> source_inspection;
  std::optional<geometry::ChargedTriangulationResult> triangulation;
  std::optional<GltfReadResult> catalog_read;
  std::optional<SurfaceEncodeResult> encode;
  SurfaceDeliveryCall delivery_call = SurfaceDeliveryCall::kNotCalled;
  SurfaceDeliveryObservation delivery;
  std::optional<gltf_detail::AdmissionCode> delivery_admission;
  std::optional<gltf_detail::AdmissionProvenance> delivery_provenance;
  std::optional<SurfaceInspectionResult> inspection;
  std::optional<SurfaceFilePublicationResult> publication;
};

// Concrete native continuation after successful record AND catalog ingress.
// Sources must have been released under the GIL. Caller releases the GIL around
// this entire synchronous function and keeps input spans/options/paths alive.
// Materializes the original exactly once, then frees record staging after that
// call has joined; raw catalog staging remains owned by the caller. Uses one
// actual ledger throughout. No Python callback, proof constructor or path
// loader. The caller validates all closed configuration before entering this
// function. Repeated attempts reject before touching capture input. Workers
// always join before native delegates return or propagate an exception. A
// failed delegate leaves later calls kNotCalled. Only native publication can
// establish commit.
void RunCapturedSurface(SurfaceRecordCapture&, SharedSurfaceIngress&,
                        const OwnedSurfaceOptions&,
                        const SurfacePresetProjection&,
                        const geometry::ExecutionContext&,
                        std::optional<SurfaceCatalogInput>,
                        std::optional<SurfacePublicationInput>, std::stop_token,
                        SurfaceNativeResult&);

}  // namespace meshvale::interchange::surface_python_detail
#endif  // MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_PIPELINE_H_
