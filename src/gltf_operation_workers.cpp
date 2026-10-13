// SPDX-License-Identifier: Apache-2.0
#include "gltf_operation_workers.h"

#include <meshvale/geometry/execution.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <system_error>
#include <type_traits>
#include <vector>

#include "gltf_allocation.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <process.h>
#include <windows.h>

#include <cerrno>
#else
#include <pthread.h>
#endif

namespace meshvale::interchange::gltf_detail {
namespace {
#if defined(_WIN32)
unsigned __stdcall NativeEntry(void* argument) noexcept {
  auto& record = *static_cast<NativeWorkerRecord*>(argument);
  record.entry(record);
  return 0;
}
std::uintptr_t LaunchDefault(NativeWorkerRecord* record, void*) {
  const auto handle =
      _beginthreadex(nullptr, 0, NativeEntry, record, 0, nullptr);
  if (!handle) throw std::system_error(errno, std::generic_category());
  return handle;
}
void JoinDefault(std::uintptr_t handle, void*) noexcept {
  const auto native = reinterpret_cast<HANDLE>(handle);
  if (WaitForSingleObject(native, INFINITE) != WAIT_OBJECT_0) std::terminate();
  if (!CloseHandle(native)) std::terminate();
}
#else
template <class Thread>
std::uintptr_t EncodeThreadHandle(Thread thread) noexcept {
  static_assert(std::is_pointer_v<Thread> || std::is_integral_v<Thread>);
  static_assert(sizeof(Thread) <= sizeof(std::uintptr_t));
  if constexpr (std::is_pointer_v<Thread>)
    return reinterpret_cast<std::uintptr_t>(thread);
  else
    return static_cast<std::uintptr_t>(thread);
}
template <class Thread>
Thread DecodeThreadHandle(std::uintptr_t handle) noexcept {
  static_assert(std::is_pointer_v<Thread> || std::is_integral_v<Thread>);
  static_assert(sizeof(Thread) <= sizeof(std::uintptr_t));
  if constexpr (std::is_pointer_v<Thread>)
    return reinterpret_cast<Thread>(handle);
  else
    return static_cast<Thread>(handle);
}
void* NativeEntry(void* argument) noexcept {
  auto& record = *static_cast<NativeWorkerRecord*>(argument);
  record.entry(record);
  return nullptr;
}
std::uintptr_t LaunchDefault(NativeWorkerRecord* record, void*) {
  pthread_t thread;
  const auto error = pthread_create(&thread, nullptr, NativeEntry, record);
  if (error != 0) throw std::system_error(error, std::generic_category());
  return EncodeThreadHandle(thread);
}
void JoinDefault(std::uintptr_t handle, void*) noexcept {
  if (pthread_join(DecodeThreadHandle<pthread_t>(handle), nullptr) != 0)
    std::terminate();
}
#endif
struct Team {
  std::size_t jobs;
  std::size_t workers;
  OperationJob operation;
  void* state;
  const OperationWorkerObserver* observer;
};
void WorkerEntry(NativeWorkerRecord& record) noexcept {
  auto& team = *static_cast<Team*>(record.state);
  for (auto job = record.ordinal; job < team.jobs;) {
    if (team.observer && team.observer->job_entered)
      team.observer->job_entered(job, team.observer->state);
    team.operation(team.state, job);
    // Avoid an overflowing final round-robin increment at the host ceiling.
    if (team.workers >= team.jobs - job) break;
    job += team.workers;
  }
}
}  // namespace
NativeWorkerBackend DefaultNativeWorkerBackend() noexcept {
  return {LaunchDefault, JoinDefault, nullptr};
}
void RunOperationJobs(const AllocationContext& allocation,
                      const geometry::WorkerLease& lease, std::size_t jobs,
                      OperationJob operation, void* state,
                      const NativeExecutionOptions& execution,
                      OperationWorkerStats& stats,
                      void (*before_launch)(std::size_t, void*),
                      void* launch_state) {
  allocation.Checkpoint();
  std::vector<NativeWorkerRecord, Allocator<NativeWorkerRecord>> records{
      Allocator<NativeWorkerRecord>(allocation)};
  std::vector<std::uintptr_t, Allocator<std::uintptr_t>> handles{
      Allocator<std::uintptr_t>(allocation)};
  records.resize(lease.Count());
  handles.reserve(lease.Count());
  Team team{jobs, lease.Count(), operation, state, execution.observer};
  auto join = [&]() noexcept {
    for (std::size_t ordinal = 0; ordinal < handles.size(); ++ordinal) {
      execution.backend.join(handles[ordinal], execution.backend.state);
      ++stats.joined;
      if (execution.observer && execution.observer->joined)
        execution.observer->joined(ordinal, execution.observer->state);
    }
  };
  try {
    for (std::size_t ordinal = 0; ordinal < lease.Count(); ++ordinal) {
      if (before_launch) before_launch(ordinal, launch_state);
      records[ordinal] = {WorkerEntry, &team, ordinal};
      // Capacity was reserved before any launch: a successful native handle
      // cannot be lost to a subsequent vector allocation.
      handles.push_back(
          execution.backend.launch(&records[ordinal], execution.backend.state));
      ++stats.launched;
      if (execution.observer && execution.observer->launched)
        execution.observer->launched(ordinal, execution.observer->state);
    }
  } catch (...) {
    join();
    throw;
  }
  join();
}
}  // namespace meshvale::interchange::gltf_detail
