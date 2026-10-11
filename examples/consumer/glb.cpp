// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/glb.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

int main() {
  using namespace meshvale::interchange;
  const std::string_view json = R"({"asset":{"version":"2.0"}})";
  const std::array chunks{GlbChunk{
      kGlbJsonChunkType, std::as_bytes(std::span(json.data(), json.size()))}};
  const GlbLimits limits{4096, 16, 65536};
  const GlbWorkPolicy work{1024, 256};
  meshvale::geometry::ExecutionContext context({2, 1, 131072});
  const auto written = WriteGlbContainer(chunks, limits, work, context);
  if (written.status != GlbStatus::kSuccess) return 1;
  const auto captured =
      ReadGlbContainer(written.container.Bytes(), limits, work, context);
  return captured.status == GlbStatus::kSuccess &&
                 captured.container.ChunkCount() == 1 &&
                 captured.container.Chunk(0).type == kGlbJsonChunkType
             ? 0
             : 1;
}
