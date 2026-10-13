// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_FILES_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_FILES_TEST_INTERNAL_H_

#include <meshvale/interchange/gltf_files.h>

#include <cstdint>
#include <filesystem>
#include <stop_token>

#include "glb_detail.h"
#include "gltf_allocation.h"
#include "gltf_operation_workers.h"

namespace meshvale::interchange::gltf_files_detail {
// Per-call hooks only; mutable hook state is synchronized by the caller. Every
// native handle observer is final/nonthrowing. Hooks live through all joins;
// allocation backend state additionally outlives retained file-owner blocks.
struct GltfFileHooks {
  void (*before_open)(std::uint64_t file_index, void* state) = nullptr;
  void (*before_read)(std::uint64_t file_index, std::uint64_t byte_offset,
                      void* state) = nullptr;
  void (*opened)(std::uint64_t file_index, void* state) noexcept = nullptr;
  void (*closed)(std::uint64_t file_index, void* state) noexcept = nullptr;
  void* state = nullptr;
};
struct GltfFileTestOptions {
  gltf_detail::AllocationBackend allocation_backend =
      gltf_detail::DefaultAllocationBackend();
  gltf_detail::NativeExecutionOptions execution;
  GltfFileHooks hooks;
  // Container requests have their own declared scope on the shared ledger.
  // This backend state outlives the last retained file-owner copy.
  glb_detail::Hooks container;
};
[[nodiscard]] GltfFileReadResult TestReadGltfFileAsset(
    const std::filesystem::path& input, const GltfFileReadOptions& options,
    const GltfFileReadLimits& limits, const GltfFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const GltfFileTestOptions& testing);
}  // namespace meshvale::interchange::gltf_files_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_FILES_TEST_INTERNAL_H_
