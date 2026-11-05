// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_DELIVERY_H_
#define MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_DELIVERY_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/surface_write.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <vector>

#include "../src/gltf_allocation.h"
#include "../src/gltf_operation_workers.h"

namespace meshvale::interchange::surface_python_detail {

// Binding copy work, separate from native encoder/inspection facts. This object
// must outlive its CapturedSurfaceMembers, including exceptional construction.
struct SurfaceDeliveryObservation {
  std::uint64_t allocated_payload_bytes = 0;
  std::uint64_t allocated_request_bytes = 0;
  std::uint64_t active_payload_bytes = 0;
  std::uint64_t copied_bytes = 0;
  std::uint64_t byte_checkpoints = 0;
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t caller_jobs = 0;
};

// Copies actual encoder bytes into distinct charged storage before independent
// inspection. Paths borrow the retained genuine output. No Python owner, proof
// constructor, filesystem write or inspection is hidden in this owner.
//
// Construction and Capture require exclusive access. Capture is single-use and
// synchronous; every started helper joins even on exceptions. Members is usable
// only after successful capture, and its spans live through this owner's
// destruction. Caller must release the GIL around Capture's native-only work.
class CapturedSurfaceMembers {
 public:
  CapturedSurfaceMembers(const SurfaceOutput&,
                         const gltf_detail::AllocationContext& capped_bridge,
                         SurfaceDeliveryObservation&);
  ~CapturedSurfaceMembers();
  CapturedSurfaceMembers(const CapturedSurfaceMembers&) = delete;
  CapturedSurfaceMembers& operator=(const CapturedSurfaceMembers&) = delete;
  void Capture(const geometry::MeshMaterializationWorkPolicy&,
               std::uint64_t cancellation_block_bytes, std::stop_token,
               const gltf_detail::NativeExecutionOptions& = {});
  std::span<const SuppliedSurfaceMember> Members() const;

 private:
  struct CopyJob;
  SurfaceOutput output_;
  gltf_detail::AllocationContext bridge_;
  SurfaceDeliveryObservation& reached_;
  std::vector<SuppliedSurfaceMember,
              gltf_detail::Allocator<SuppliedSurfaceMember>>
      members_;
  std::byte* payload_ = nullptr;
  std::size_t payload_bytes_ = 0;
  bool started_ = false;
  bool captured_ = false;
};

}  // namespace meshvale::interchange::surface_python_detail
#endif  // MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_DELIVERY_H_
