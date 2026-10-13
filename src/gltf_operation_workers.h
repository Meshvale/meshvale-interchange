// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_OPERATION_WORKERS_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_OPERATION_WORKERS_H_

#include <meshvale/geometry/execution.h>

#include <cstddef>
#include <cstdint>

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_detail {
struct NativeWorkerRecord {
  void (*entry)(NativeWorkerRecord&) noexcept = nullptr;
  void* state = nullptr;
  std::size_t ordinal = 0;
};
// Per-call native backend. State remains alive through all synchronous joins;
// allocation-backend state separately remains alive through retained frees.
// A successful launch returns its handle. A throwing launch leaves no started
// thread whose handle has not been returned; join is nonthrowing and final.
struct NativeWorkerBackend {
  std::uintptr_t (*launch)(NativeWorkerRecord*, void* state);
  void (*join)(std::uintptr_t, void* state) noexcept;
  void* state;
};
[[nodiscard]] NativeWorkerBackend DefaultNativeWorkerBackend() noexcept;
struct OperationWorkerObserver {
  void (*launched)(std::size_t ordinal, void* state) noexcept = nullptr;
  void (*joined)(std::size_t ordinal, void* state) noexcept = nullptr;
  void (*job_entered)(std::size_t ordinal, void* state) noexcept = nullptr;
  void* state = nullptr;
};
struct NativeExecutionOptions {
  NativeWorkerBackend backend = DefaultNativeWorkerBackend();
  const OperationWorkerObserver* observer = nullptr;
};
struct OperationWorkerStats {
  std::size_t launched = 0;
  std::size_t joined = 0;
};
using OperationJob = void (*)(void* state, std::size_t ordinal) noexcept;
// The caller owns charged per-job diagnostic/exception slots and catches every
// callback exception there. A positive live lease is held through this call.
// Native records and handle capacity are admitted before the first launch.
// No callable heap or optional writer-module dependency is introduced.
void RunOperationJobs(const AllocationContext& allocation,
                      const geometry::WorkerLease& lease, std::size_t jobs,
                      OperationJob operation, void* state,
                      const NativeExecutionOptions& execution,
                      OperationWorkerStats& stats,
                      void (*before_launch)(std::size_t, void*) = nullptr,
                      void* launch_state = nullptr);
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_OPERATION_WORKERS_H_
