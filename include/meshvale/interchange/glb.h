// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_GLB_H_
#define MESHVALE_INTERCHANGE_GLB_H_

#include <meshvale/geometry/execution.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

namespace meshvale::interchange {

// GLB 2 chunk identifiers, specified by Khronos; other identifiers stay opaque.
constexpr std::uint32_t kGlbJsonChunkType = 0x4e4f534a;
constexpr std::uint32_t kGlbBinChunkType = 0x004e4942;

struct GlbLimits {
  std::size_t max_container_bytes = 0;
  std::size_t max_chunks = 0;
  std::size_t max_requested_bytes = 0;
};
struct GlbWorkPolicy {
  std::size_t parallel_min_bytes = 0;
  std::size_t partition_bytes = 0;
};
struct GlbChunk {
  std::uint32_t type;
  std::span<const std::byte> bytes;
};
enum class GlbStatus {
  kSuccess,
  kInvalidOptions,
  kMalformed,
  kUnsupportedVersion,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled
};
namespace glb_detail {
struct State;
struct Access;
}  // namespace glb_detail

// Spans borrow this immutable owner; see docs/glb.md for operation guarantees.
class GlbContainer {
 public:
  GlbContainer() noexcept;
  ~GlbContainer() noexcept;
  GlbContainer(GlbContainer&& other) noexcept;
  GlbContainer& operator=(GlbContainer&& other) noexcept;
  GlbContainer(const GlbContainer&) = delete;
  GlbContainer& operator=(const GlbContainer&) = delete;
  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
  [[nodiscard]] std::size_t ChunkCount() const noexcept;
  [[nodiscard]] GlbChunk Chunk(std::size_t index) const;

 private:
  friend struct glb_detail::Access;
  explicit GlbContainer(glb_detail::State* state) noexcept;
  glb_detail::State* state_ = nullptr;
};
struct GlbResult {
  GlbStatus status = GlbStatus::kInvalidOptions;
  std::size_t error_byte_offset = 0;
  std::size_t workers_used = 0;
  GlbContainer container;
};

[[nodiscard]] GlbResult ReadGlbContainer(
    std::span<const std::byte> bytes, const GlbLimits& limits,
    const GlbWorkPolicy& work, const geometry::ExecutionContext& context,
    std::stop_token stop = {});
// JSON/BIN may be unaligned and receive canonical padding; opaque chunks must
// already be aligned. Inputs borrow their owners through every worker join.
[[nodiscard]] GlbResult WriteGlbContainer(
    std::span<const GlbChunk> chunks, const GlbLimits& limits,
    const GlbWorkPolicy& work, const geometry::ExecutionContext& context,
    std::stop_token stop = {});

}  // namespace meshvale::interchange
#endif  // MESHVALE_INTERCHANGE_GLB_H_
