// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_FILES_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_FILES_TEST_INTERNAL_H_

#include <meshvale/interchange/gltf_files.h>

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string_view>
#include <system_error>

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
using TestNativeView =
    std::basic_string_view<std::filesystem::path::value_type>;
struct GltfFilePublicationHooks {
  void (*before_write)(std::uint64_t file_index, std::uint64_t byte_offset,
                       void* state) = nullptr;
  void (*stage_ready)(TestNativeView stage, void* state) = nullptr;
  void (*before_inventory)(TestNativeView stage, void* state) = nullptr;
  void (*before_commit)(TestNativeView stage, void* state) = nullptr;
  std::error_code (*cleanup_error)(TestNativeView stage,
                                   void* state) noexcept = nullptr;
  void (*cleanup_finished)(GltfFileCleanupOutcome outcome,
                           std::error_code error,
                           void* state) noexcept = nullptr;
  void* state = nullptr;
};
struct GltfFilePublicationTestOptions {
  gltf_detail::AllocationBackend allocation_backend =
      gltf_detail::DefaultAllocationBackend();
  gltf_detail::NativeExecutionOptions capture_execution;
  gltf_detail::NativeExecutionOptions verification_execution;
  // Charged expected-byte imports are a separate private role from writes.
  gltf_detail::NativeExecutionOptions import_execution;
  // Private witnesses only: staging writes are not public capture statistics.
  gltf_detail::NativeExecutionOptions write_execution;
  GltfFileHooks file_hooks;
  GltfFilePublicationHooks hooks;
  // Assembly and actual staged read retain distinct fresh container caps;
  // backend state remains borrowed through every join and retained owner free.
  glb_detail::Hooks container;
};
[[nodiscard]] GltfFilePublicationResult TestPublishGltfTopologyBundle(
    const GltfFilePublicationRequest& request,
    const std::filesystem::path& destination,
    const GltfFilePublicationOptions& options,
    const GltfFilePublicationLimits& limits,
    const GltfFilePublicationWorkPolicy& work, std::stop_token stop,
    const GltfFilePublicationTestOptions& testing);
[[nodiscard]] GltfFileReadResult TestReadGltfFileAsset(
    const std::filesystem::path& input, const GltfFileReadOptions& options,
    const GltfFileReadLimits& limits, const GltfFileReadWorkPolicy& work,
    const geometry::ExecutionContext& context, std::stop_token stop,
    const GltfFileTestOptions& testing);
}  // namespace meshvale::interchange::gltf_files_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_FILES_TEST_INTERNAL_H_
