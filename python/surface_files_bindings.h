// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PYTHON_SURFACE_FILES_BINDINGS_H_
#define MESHVALE_INTERCHANGE_PYTHON_SURFACE_FILES_BINDINGS_H_

#include <nanobind/nanobind.h>

#include <stop_token>

namespace meshvale::interchange::surface_python_detail {

// Read the existing owning extension's Cancellation under the GIL. No foreign
// extension object, replacement Cancellation registration or stop state is
// used.
using SurfaceCancellationReader = std::stop_token (*)(nanobind::handle);

// Register once under the GIL, including the genuine native view types before
// any lazy result framing. Root's facade supplies the public keyword defaults.
void RegisterSurfaceFiles(nanobind::module_&, SurfaceCancellationReader);

}  // namespace meshvale::interchange::surface_python_detail
#endif  // MESHVALE_INTERCHANGE_PYTHON_SURFACE_FILES_BINDINGS_H_
