// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/stl.h>
#include <meshvale/interchange/stl_files.h>

#include <cstdint>
#include <filesystem>

#include "stl_files_alias_witness.h"
#include "stl_files_internal.h"

#ifndef MESHVALE_STL_ALIAS_INTERNAL_PROBE
#error This source-test-only supplement requires its explicit fixture build mode.
#endif

namespace meshvale::interchange::stl_files_alias_fixture {
namespace {
void Opened(std::uint64_t, void* state) {
  static_cast<HookCounters*>(state)->opened.fetch_add(1);
}
void Closed(std::uint64_t, void* state) noexcept {
  static_cast<HookCounters*>(state)->closed.fetch_add(1);
}
stl_files_detail::TestOptions ObserveHooks(HookCounters& counters) {
  stl_files_detail::TestOptions hooks;
  hooks.files.opened = &Opened;
  hooks.files.closed = &Closed;
  hooks.files.state = &counters;
  return hooks;
}
}  // namespace
StlFileCaptureResult CaptureObserved(const fs::path& input,
                                     const geo::ExecutionContext& context,
                                     HookCounters& counters) {
  return stl_files_detail::CaptureForTesting(input, base::FileLimits(),
                                             base::FileWork(), context, {},
                                             ObserveHooks(counters));
}
StlFileReadResult ReadObserved(const fs::path& input,
                               const geo::ExecutionContext& context,
                               HookCounters& counters) {
  return stl_files_detail::ReadForTesting(input, StlEncoding::kAscii,
                                          base::ReadLimits(), base::ReadWork(),
                                          context, {}, ObserveHooks(counters));
}
}  // namespace meshvale::interchange::stl_files_alias_fixture
