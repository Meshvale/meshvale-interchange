// SPDX-License-Identifier: Apache-2.0
#include "gltf_parser.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <exception>
#include <memory>
#include <optional>

namespace meshvale::interchange::gltf_detail {
namespace {
struct Memory {
  AllocationContext allocation;
  std::optional<AdmissionCode> failure;
  std::exception_ptr exception;
};
void* Allocate(void* user, cgltf_size bytes) noexcept {
  auto& memory = *static_cast<Memory*>(user);
  try {
    return memory.allocation.Allocate(bytes, alignof(std::max_align_t));
  } catch (const AdmissionFailure& failure) {
    if (!memory.failure) memory.failure = failure.Code();
  } catch (...) {
    if (!memory.exception) memory.exception = std::current_exception();
  }
  return nullptr;
}
void Free(void*, void* pointer) noexcept {
  AllocationContext::Deallocate(pointer);
}
}  // namespace
CoreParserSummary InspectCoreProjection(std::string_view input,
                                        const AllocationContext& allocation,
                                        CoreProjectionObserver observer,
                                        void* observer_state) {
  Memory memory{allocation, {}, {}};
  cgltf_options options{};
  options.memory = {Allocate, Free, &memory};
  cgltf_data* data = nullptr;
  const auto result = cgltf_parse(&options, input.data(), input.size(), &data);
  std::unique_ptr<cgltf_data, decltype(&cgltf_free)> owner(data, cgltf_free);
  const CoreParserSummary summary{
      result == cgltf_result_success,
      data ? static_cast<std::size_t>(data->meshes_count) : 0,
      data ? static_cast<std::size_t>(data->nodes_count) : 0};
  if (result == cgltf_result_success && observer)
    observer(*data, observer_state);
  owner.reset();
  if (memory.exception) std::rethrow_exception(memory.exception);
  if (memory.failure) throw AdmissionFailure(*memory.failure);
  return summary;
}
}  // namespace meshvale::interchange::gltf_detail
