// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_PUBLICATION_H_
#define MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_PUBLICATION_H_

#include <meshvale/interchange/surface_files.h>
#include <nanobind/nanobind.h>

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "../src/gltf_allocation.h"
#include "surface_python_options.h"

namespace meshvale::interchange::surface_python_detail {

// Construction-only Python caller-input snapshot. Receives the result of the
// one owning os.fspath call; never calls PyOS_FSPath itself. Returns exact
// ready built-in str on Windows or bytes on POSIX, with actual interpreter
// filesystem codec/error handling. Creates no native path/buffer or
// operation/budget.
nanobind::object SnapshotSurfaceDestination(nanobind::handle already_resolved);

struct SurfacePublicationPlan {
  std::size_t native_units = 0;  // Payload units, excluding the final NUL.
  std::size_t receipt_bytes = 0;
};
// GIL required. The caller admits the exact frozen SurfacePublication type.
// Validates its actual _native_destination/receipt content, closed syntax and
// selected caps without native allocation, stop checks, source acquisition or
// filesystem calls. This is input admission, not a publication result/proof.
SurfacePublicationPlan ValidateSurfacePublication(
    nanobind::handle value, const SurfacePresetProjection&, SurfaceEncoding);

struct SurfacePublicationInput {
  SurfaceNativeDestination destination;
  std::string_view receipt_path;
};

// The enclosing controller charges inline/control storage. Every actual native
// backing buffer uses this same capped bridge. GIL required for construction;
// no Python source is retained afterwards. Keep the nonmovable owner alive
// through all Input() readers/joins. No stop checks or filesystem calls occur.
class OwnedSurfacePublication {
 public:
  OwnedSurfacePublication(const SurfacePublicationPlan&, nanobind::handle value,
                          const gltf_detail::AllocationContext& capped_bridge);
  OwnedSurfacePublication(const OwnedSurfacePublication&) = delete;
  OwnedSurfacePublication& operator=(const OwnedSurfacePublication&) = delete;
  OwnedSurfacePublication(OwnedSurfacePublication&&) = delete;
  OwnedSurfacePublication& operator=(OwnedSurfacePublication&&) = delete;
  SurfacePublicationInput Input() const noexcept;

 private:
  using NativeChar = std::filesystem::path::value_type;
  std::vector<NativeChar, gltf_detail::Allocator<NativeChar>> destination_;
  std::vector<char, gltf_detail::Allocator<char>> receipt_;
};

}  // namespace meshvale::interchange::surface_python_detail
#endif  // MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_PUBLICATION_H_
