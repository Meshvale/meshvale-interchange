// SPDX-License-Identifier: Apache-2.0
#include <charconv>
#include <cstddef>
#include <limits>
#include <new>
#include <string_view>
#include <system_error>

#include "gltf_allocation.h"
#include "gltf_parser.h"

namespace meshvale::interchange::gltf_detail {
namespace {
float ProjectedFloat(const char* text) noexcept {
  // The admitted projection contains finite binary32 decimal spellings. Parse
  // directly to that representation without locale or ambient rounding state.
  const std::string_view number(text);
  float value = 0;
  const auto result =
      std::from_chars(number.data(), number.data() + number.size(), value);
  return result.ec == std::errc{} ? value : 0;
}
}  // namespace
}  // namespace meshvale::interchange::gltf_detail

#define CGLTF_ATOF(text) \
  ::meshvale::interchange::gltf_detail::ProjectedFloat(text)
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#undef CGLTF_ATOF

namespace meshvale::interchange::gltf_detail {
namespace {
// Intrusive tracking is inside the same explicit parser request, including its
// alignment padding. It allocates no separate/default-allocator list. cgltf is
// compiled as C++ in this translation unit: an allocation callback exception
// unwinds directly to this owner, bypassing unsafe partial-graph SDK cleanup.
// On MSVC this TU requires /EHsc-: cgltf's extern-C declarations still allow
// these exceptions, so the compiler must not assume C calls cannot throw.
struct alignas(std::max_align_t) ParserBlock {
  ParserBlock* previous;
  ParserBlock* next;
};
static_assert(sizeof(ParserBlock) % alignof(std::max_align_t) == 0);
class Memory {
 public:
  explicit Memory(const AllocationContext& allocation)
      : allocation_(allocation) {}
  Memory(const Memory&) = delete;
  Memory& operator=(const Memory&) = delete;
  ~Memory() noexcept {
    while (head_) Free(head_ + 1);
  }
  [[nodiscard]] void* Allocate(std::size_t bytes) {
    if (bytes > std::numeric_limits<std::size_t>::max() - sizeof(ParserBlock))
      throw AdmissionFailure(AdmissionCode::kSizeLimit);
    auto* storage =
        allocation_.Allocate(sizeof(ParserBlock) + bytes, alignof(ParserBlock));
    auto* block = new (storage) ParserBlock{nullptr, head_};
    if (head_) head_->previous = block;
    head_ = block;
    return block + 1;
  }
  void Free(void* pointer) noexcept {
    if (!pointer) return;
    auto* block = static_cast<ParserBlock*>(pointer) - 1;
    if (block->previous)
      block->previous->next = block->next;
    else
      head_ = block->next;
    if (block->next) block->next->previous = block->previous;
    block->~ParserBlock();
    AllocationContext::Deallocate(block);
  }

 private:
  AllocationContext allocation_;
  ParserBlock* head_ = nullptr;
};
void* Allocate(void* user, cgltf_size bytes) {
  return static_cast<Memory*>(user)->Allocate(bytes);
}
void Free(void* user, void* pointer) noexcept {
  static_cast<Memory*>(user)->Free(pointer);
}
}  // namespace
CoreParserSummary InspectCoreProjection(std::string_view input,
                                        const AllocationContext& allocation,
                                        CoreProjectionObserver observer,
                                        void* observer_state) {
  Memory memory(allocation);
  cgltf_options options{};
  options.memory = {Allocate, Free, &memory};
  cgltf_data* data = nullptr;
  const auto result = cgltf_parse(&options, input.data(), input.size(), &data);
  const CoreParserSummary summary{
      result == cgltf_result_success,
      data ? static_cast<std::size_t>(data->meshes_count) : 0,
      data ? static_cast<std::size_t>(data->nodes_count) : 0};
  if (result == cgltf_result_success && observer)
    observer(*data, observer_state);
  // This wrapper only parses supplied JSON: it never loads external buffers or
  // files. Every parser block is owned by Memory, including data, tokens and
  // nested tables. Its destructor frees blocks without following graph fields
  // on success, observer failure, or allocation/cancellation unwinding.
  return summary;
}
}  // namespace meshvale::interchange::gltf_detail
