// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_SURFACE_FILES_TEST_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_SURFACE_FILES_TEST_INTERNAL_H_

#include <meshvale/interchange/surface_files.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string_view>
#include <system_error>

#include "gltf_allocation.h"
#include "gltf_operation_workers.h"
#include "surface_output_internal.h"

namespace meshvale::interchange::surface_files_detail {
#ifdef _WIN32
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
using NativeView = std::basic_string_view<NativeChar>;
enum class IoOperation { kOpen, kRead, kWrite, kClose, kEnumerate };
struct IoProgress {
  SurfaceFileStage stage;
  std::uint64_t member_ordinal;
  std::uint64_t offset;
  std::uint64_t bytes;
};
struct FileHooks {
  void (*before_open)(std::uint64_t, void*) = nullptr;
  void (*opened)(std::uint64_t, void*) = nullptr;
  void (*closed)(std::uint64_t, void*) noexcept = nullptr;
  // Runs only after an actual completed store, on the thread doing that store.
  // Offset is file-local; bytes is this real range's completed transfer.
  void (*after_write)(const IoProgress&, void*) = nullptr;
  std::error_code (*io_error)(IoOperation, SurfaceFileStage, std::uint64_t,
                              std::uint64_t, void*) = nullptr;
  void* state = nullptr;
};
struct TestOptions {
  gltf_detail::AllocationBackend allocation_backend =
      gltf_detail::DefaultAllocationBackend();
  gltf_detail::NativeExecutionOptions workers;
  surface_detail::TestOptions inspection;
  FileHooks files;
  // Each native handle copies its callback table; state stays borrowed.
  // Hooks can throw before commit and are never invoked in the final window.
  void (*before_stage_create)(NativeView, std::uint64_t, void*) = nullptr;
  void (*stage_ready)(NativeView, void*) = nullptr;
  void (*before_inventory)(NativeView, bool, void*) = nullptr;
  void (*before_final_verify)(NativeView, void*) = nullptr;
  // Fault callbacks are evaluated before the final verification window.
  std::error_code (*commit_error)(void*) = nullptr;
  std::error_code (*cleanup_error)(NativeView, void*) noexcept = nullptr;
  void (*cleanup_finished)(SurfaceFileCleanupOutcome, std::error_code,
                           void*) noexcept = nullptr;
  // Private actual-success marker; no postcommit callable is invoked.
  std::atomic<bool>* native_commit_observed = nullptr;
  // Default native final group only: these are atomic stores, never callbacks.
  std::atomic<bool>* final_window_entered = nullptr;
  std::atomic<std::uint64_t>* final_read_bytes = nullptr;
  std::atomic<std::uint64_t>* final_joined_workers = nullptr;
  std::atomic<bool>* final_lease_released = nullptr;
  // Test-owned gate after real read progress. An external thread releases it or
  // requests stop; no caller/injected code runs on a final worker.
  const std::atomic<bool>* final_continue = nullptr;
  // Setup-only failure after N successful default launches; no in-window fault.
  std::optional<std::size_t> final_launch_fail_after;
  void* state = nullptr;
};
[[nodiscard]] SurfaceFilePublicationResult PublishForTest(
    const SurfaceOutputInspection&, const std::filesystem::path&,
    const SurfaceFilePublicationOptions&, const SurfaceFilePublicationLimits&,
    const SurfaceFilePublicationWorkPolicy&, std::stop_token,
    const TestOptions&);
}  // namespace meshvale::interchange::surface_files_detail
#endif  // MESHVALE_INTERCHANGE_SRC_SURFACE_FILES_TEST_INTERNAL_H_
