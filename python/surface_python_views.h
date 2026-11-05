// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_VIEWS_H_
#define MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_VIEWS_H_

#include <meshvale/interchange/surface_files.h>
#include <meshvale/interchange/surface_write.h>
#include <nanobind/nanobind.h>

namespace meshvale::interchange::surface_python_detail {

// Register once under the GIL before any lazy view construction. The registered
// types have no Python constructor, mutable attributes or proof/map setters.
void RegisterSurfaceViews(nanobind::module_& module);

// GIL required. Each wrapper keeps an allocation-free strong native owner copy;
// Python wrappers/tuples/diagnostic framing may allocate and propagate failure
// without changing native facts. Buffer exports are read-only and retain their
// own exporting strong owner, independently of the result or outer wrapper.
// Caller supplies genuine native owners, never caller-authored proof flags.
nanobind::object SurfaceOutputView(const SurfaceOutput&);
nanobind::object SurfaceInspectionView(const SurfaceOutputInspection&);
nanobind::object SurfacePublishedView(const SurfacePublishedBundle&);
nanobind::object SurfaceFactsView(const SurfaceFacts&);

// Output encoding/representation use the existing lowercase Python selectors.
// Other native enums use enumerator names. Published receipt.sha256 is exactly
// 32 immutable raw bytes from SurfaceFileReceiptView, not a hexadecimal string.

}  // namespace meshvale::interchange::surface_python_detail
#endif  // MESHVALE_INTERCHANGE_PYTHON_SURFACE_PYTHON_VIEWS_H_
