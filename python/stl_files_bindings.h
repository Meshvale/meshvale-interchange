// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PYTHON_STL_FILES_BINDINGS_H_
#define MESHVALE_INTERCHANGE_PYTHON_STL_FILES_BINDINGS_H_
#include <nanobind/nanobind.h>

#include <stop_token>
namespace meshvale::interchange::stl_python_detail {
// Reads the existing owning extension's Cancellation; no cross-extension C++
// context/mesh instance or duplicate cancellation registration is introduced.
using StlCancellationReader = std::stop_token (*)(nanobind::handle);
void RegisterStlFiles(nanobind::module_&, StlCancellationReader);
}  // namespace meshvale::interchange::stl_python_detail
#endif  // MESHVALE_INTERCHANGE_PYTHON_STL_FILES_BINDINGS_H_
