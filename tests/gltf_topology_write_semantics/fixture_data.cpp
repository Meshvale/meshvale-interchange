// SPDX-License-Identifier: Apache-2.0
// Original wire fixtures; regenerate/check using check_fixtures.py.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fixture_support.h"

namespace gltf_topology_write_semantics {
namespace {
std::vector<std::byte> DecodeHex(std::string_view text) {
  std::vector<std::byte> bytes;
  bytes.reserve(text.size() / 2);
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const auto digit = [](char c) -> unsigned {
      return c <= '9' ? static_cast<unsigned>(c - '0')
                      : static_cast<unsigned>(c - 'a' + 10);
    };
    bytes.push_back(
        static_cast<std::byte>((digit(text[i]) << 4) | digit(text[i + 1])));
  }
  return bytes;
}
}  // namespace
Fixture MakeFixture(FixtureKind kind) {
  switch (kind) {
    case FixtureKind::kTriangles:
      return {
          std::string(R"GLTF({
  "ass\u0065t" : {"version":"2.0"},
  "extras" : {"fake":{"buffers":[],"accessors":[],"indices":3,"mode":5},"decimal":1.00e+01,"integer":9007199254740995,"escaped":"\ud83d\ude00"},
  "buffers" : [
	{"uri":"memory.bin","byteLength":132},
	{"uri":"data:application/gltf-buffer;base64,iVBORw0KGgo=","byteLength":8}
  ],
  "bufferViews" : [
	{"buffer":0,"byteOffset":0,"byteLength":64,"byteStride":16},
	{"buffer":0,"byteOffset":64,"byteLength":16},
	{"buffer":0,"byteOffset":80,"byteLength":2},
	{"buffer":0,"byteOffset":84,"byteLength":4},
	{"buffer":0,"byteOffset":88,"byteLength":12},
	{"buffer":0,"byteOffset":100,"byteLength":8},
	{"buffer":0,"byteOffset":108,"byteLength":24},
	{"buffer":1,"byteOffset":0,"byteLength":8}
  ],
  "accessors" : [
	{"bufferView":0,"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[1,1,0]},
	{"bufferView":1,"componentType":5122,"type":"VEC2","count":4,"normalized":true,"min":[-32768,-32768],"max":[32767,32767]},
	{"componentType":5121,"type":"VEC2","count":4,"normalized":true,"sparse":{"count":2,"indices":{"bufferView":2,"componentType":5121},"values":{"bufferView":3}}},
	{"bufferView":4,"componentType":5123,"type":"SCALAR","count":6},
	{"componentType":5126,"type":"VEC3","count":4},
	{"bufferView":5,"componentType":5126,"type":"SCALAR","count":2,"min":[0],"max":[1]},
	{"bufferView":6,"componentType":5126,"type":"VEC3","count":2}
  ],
  "images":[{"uri":"data:image/png;base64,iVBORw0KGgo="},{"bufferView":7,"mimeType":"image/png"}],
  "materials":[{"name":"untouched","extras":{"number":12345678901234567}}],
  "meshes" : [{"primitives":[{"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indices":3,"mode":4,"material":0,"extras":{"other":"unrelated primitive"}},{"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indi\u0063es" : 3, "mo\u0064e" : 4,"material":0,"extras":{"indices":9007199254740993,"mode":-0.0,"braces":"} [ \"indices\": 71 {","huge":1e400}}]},{"weights":[0],"primitives":[{"attributes":{"POSITION":0},"indices":3,"mode":4,"targets":[{"POSITION":4}]}]}],
  "nodes" : [{"mesh":0,"name":"first\u0394","translation":[1,2,3]},{"mesh":0,"name":"second"},{"mesh":1,"skin":0,"weights":[0]},{"name":"unrelated joint"}],
  "skins" : [{"joints":[3],"skeleton":3}],
  "animations" : [{"samplers":[{"input":5,"output":6}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}],
  "scenes" : [{"name":"shared","nodes":[0,1,2,3]},{"nodes":[0]}],
  "scene":0
}
)GLTF"),
          DecodeHex("000000800000000000000000efbeadde010000000000000000000000ef"
                    "beadde0000803f0000000000000000efbeadde000000000000803f0000"
                    "0000efbeadde0080ff7fffff000000000080ff7f01000103a2a3ff0001"
                    "fe030001000200000002000200000000000000803f0000000000000000"
                    "00000000000000000000000000000000fafbfcfdfe"),
          kind,
          1,
          4,
          4,
          {3, 1, 2, 0, 2, 2},
          {0, 1, 2, 3, 4, 5},
          {R"KEEP({"uri":"memory.bin","byteLength":132})KEEP",
           R"KEEP({"uri":"data:application/gltf-buffer;base64,iVBORw0KGgo=","byteLength":8})KEEP",
           R"KEEP({"buffer":0,"byteOffset":0,"byteLength":64,"byteStride":16})KEEP",
           R"KEEP({"buffer":0,"byteOffset":64,"byteLength":16})KEEP",
           R"KEEP({"buffer":0,"byteOffset":80,"byteLength":2})KEEP",
           R"KEEP({"buffer":0,"byteOffset":84,"byteLength":4})KEEP",
           R"KEEP({"buffer":0,"byteOffset":88,"byteLength":12})KEEP",
           R"KEEP({"buffer":0,"byteOffset":100,"byteLength":8})KEEP",
           R"KEEP({"buffer":0,"byteOffset":108,"byteLength":24})KEEP",
           R"KEEP({"buffer":1,"byteOffset":0,"byteLength":8})KEEP",
           R"KEEP({"bufferView":0,"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[1,1,0]})KEEP",
           R"KEEP({"bufferView":1,"componentType":5122,"type":"VEC2","count":4,"normalized":true,"min":[-32768,-32768],"max":[32767,32767]})KEEP",
           R"KEEP({"componentType":5121,"type":"VEC2","count":4,"normalized":true,"sparse":{"count":2,"indices":{"bufferView":2,"componentType":5121},"values":{"bufferView":3}}})KEEP",
           R"KEEP({"bufferView":4,"componentType":5123,"type":"SCALAR","count":6})KEEP",
           R"KEEP({"componentType":5126,"type":"VEC3","count":4})KEEP",
           R"KEEP({"bufferView":5,"componentType":5126,"type":"SCALAR","count":2,"min":[0],"max":[1]})KEEP",
           R"KEEP({"bufferView":6,"componentType":5126,"type":"VEC3","count":2})KEEP",
           R"KEEP({"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indices":3,"mode":4,"material":0,"extras":{"other":"unrelated primitive"}})KEEP",
           R"KEEP({"attributes":{"POSITION":0},"indices":3,"mode":4,"targets":[{"POSITION":4}]})KEEP",
           R"KEEP([{"mesh":0,"name":"first\u0394","translation":[1,2,3]},{"mesh":0,"name":"second"},{"mesh":1,"skin":0,"weights":[0]},{"name":"unrelated joint"}])KEEP",
           R"KEEP([{"joints":[3],"skeleton":3}])KEEP",
           R"KEEP([{"samplers":[{"input":5,"output":6}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}])KEEP",
           R"KEEP([{"name":"shared","nodes":[0,1,2,3]},{"nodes":[0]}])KEEP",
           R"KEEP({"fake":{"buffers":[],"accessors":[],"indices":3,"mode":5},"decimal":1.00e+01,"integer":9007199254740995,"escaped":"\ud83d\ude00"})KEEP",
           R"KEEP("extras":{"indices":9007199254740993,"mode":-0.0,"braces":"} [ \"indices\": 71 {","huge":1e400}})KEEP"}};
    case FixtureKind::kStrip:
      return {
          std::string(R"GLTF({
  "ass\u0065t" : {"version":"2.0"},
  "extras" : {"fake":{"buffers":[],"accessors":[],"indices":3,"mode":5},"decimal":1.00e+01,"integer":9007199254740995,"escaped":"\ud83d\ude00"},
  "buffers" : [
	{"uri":"memory.bin","byteLength":132},
	{"uri":"data:application/gltf-buffer;base64,iVBORw0KGgo=","byteLength":8}
  ],
  "bufferViews" : [
	{"buffer":0,"byteOffset":0,"byteLength":64,"byteStride":16},
	{"buffer":0,"byteOffset":64,"byteLength":16},
	{"buffer":0,"byteOffset":80,"byteLength":2},
	{"buffer":0,"byteOffset":84,"byteLength":4},
	{"buffer":0,"byteOffset":88,"byteLength":8},
	{"buffer":0,"byteOffset":100,"byteLength":8},
	{"buffer":0,"byteOffset":108,"byteLength":24},
	{"buffer":1,"byteOffset":0,"byteLength":8}
  ],
  "accessors" : [
	{"bufferView":0,"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[1,1,0]},
	{"bufferView":1,"componentType":5122,"type":"VEC2","count":4,"normalized":true,"min":[-32768,-32768],"max":[32767,32767]},
	{"componentType":5121,"type":"VEC2","count":4,"normalized":true,"sparse":{"count":2,"indices":{"bufferView":2,"componentType":5121},"values":{"bufferView":3}}},
	{"bufferView":4,"componentType":5123,"type":"SCALAR","count":4},
	{"componentType":5126,"type":"VEC3","count":4},
	{"bufferView":5,"componentType":5126,"type":"SCALAR","count":2,"min":[0],"max":[1]},
	{"bufferView":6,"componentType":5126,"type":"VEC3","count":2}
  ],
  "images":[{"uri":"data:image/png;base64,iVBORw0KGgo="},{"bufferView":7,"mimeType":"image/png"}],
  "materials":[{"name":"untouched","extras":{"number":12345678901234567}}],
  "meshes" : [{"primitives":[{"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indices":3,"mode":5,"material":0,"extras":{"other":"unrelated primitive"}},{"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indi\u0063es" : 3, "mo\u0064e" : 5,"material":0,"extras":{"indices":9007199254740993,"mode":-0.0,"braces":"} [ \"indices\": 71 {","huge":1e400}}]},{"weights":[0],"primitives":[{"attributes":{"POSITION":0},"indices":3,"mode":5,"targets":[{"POSITION":4}]}]}],
  "nodes" : [{"mesh":0,"name":"first\u0394","translation":[1,2,3]},{"mesh":0,"name":"second"},{"mesh":1,"skin":0,"weights":[0]},{"name":"unrelated joint"}],
  "skins" : [{"joints":[3],"skeleton":3}],
  "animations" : [{"samplers":[{"input":5,"output":6}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}],
  "scenes" : [{"name":"shared","nodes":[0,1,2,3]},{"nodes":[0]}],
  "scene":0
}
)GLTF"),
          DecodeHex("000000800000000000000000efbeadde010000000000000000000000ef"
                    "beadde0000803f0000000000000000efbeadde000000000000803f0000"
                    "0000efbeadde0080ff7fffff000000000080ff7f01000103a2a3ff0001"
                    "fe030001000200000000000000000000000000803f0000000000000000"
                    "00000000000000000000000000000000fafbfcfdfe"),
          kind,
          1,
          5,
          4,
          {3, 1, 2, 2, 1, 0},
          {0, 1, 2, 2, 1, 3},
          {R"KEEP({"uri":"memory.bin","byteLength":132})KEEP",
           R"KEEP({"uri":"data:application/gltf-buffer;base64,iVBORw0KGgo=","byteLength":8})KEEP",
           R"KEEP({"buffer":0,"byteOffset":0,"byteLength":64,"byteStride":16})KEEP",
           R"KEEP({"buffer":0,"byteOffset":64,"byteLength":16})KEEP",
           R"KEEP({"buffer":0,"byteOffset":80,"byteLength":2})KEEP",
           R"KEEP({"buffer":0,"byteOffset":84,"byteLength":4})KEEP",
           R"KEEP({"buffer":0,"byteOffset":88,"byteLength":8})KEEP",
           R"KEEP({"buffer":0,"byteOffset":100,"byteLength":8})KEEP",
           R"KEEP({"buffer":0,"byteOffset":108,"byteLength":24})KEEP",
           R"KEEP({"buffer":1,"byteOffset":0,"byteLength":8})KEEP",
           R"KEEP({"bufferView":0,"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[1,1,0]})KEEP",
           R"KEEP({"bufferView":1,"componentType":5122,"type":"VEC2","count":4,"normalized":true,"min":[-32768,-32768],"max":[32767,32767]})KEEP",
           R"KEEP({"componentType":5121,"type":"VEC2","count":4,"normalized":true,"sparse":{"count":2,"indices":{"bufferView":2,"componentType":5121},"values":{"bufferView":3}}})KEEP",
           R"KEEP({"bufferView":4,"componentType":5123,"type":"SCALAR","count":4})KEEP",
           R"KEEP({"componentType":5126,"type":"VEC3","count":4})KEEP",
           R"KEEP({"bufferView":5,"componentType":5126,"type":"SCALAR","count":2,"min":[0],"max":[1]})KEEP",
           R"KEEP({"bufferView":6,"componentType":5126,"type":"VEC3","count":2})KEEP",
           R"KEEP({"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indices":3,"mode":5,"material":0,"extras":{"other":"unrelated primitive"}})KEEP",
           R"KEEP({"attributes":{"POSITION":0},"indices":3,"mode":5,"targets":[{"POSITION":4}]})KEEP",
           R"KEEP([{"mesh":0,"name":"first\u0394","translation":[1,2,3]},{"mesh":0,"name":"second"},{"mesh":1,"skin":0,"weights":[0]},{"name":"unrelated joint"}])KEEP",
           R"KEEP([{"joints":[3],"skeleton":3}])KEEP",
           R"KEEP([{"samplers":[{"input":5,"output":6}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}])KEEP",
           R"KEEP([{"name":"shared","nodes":[0,1,2,3]},{"nodes":[0]}])KEEP",
           R"KEEP({"fake":{"buffers":[],"accessors":[],"indices":3,"mode":5},"decimal":1.00e+01,"integer":9007199254740995,"escaped":"\ud83d\ude00"})KEEP",
           R"KEEP("extras":{"indices":9007199254740993,"mode":-0.0,"braces":"} [ \"indices\": 71 {","huge":1e400}})KEEP"}};
    case FixtureKind::kFan:
      return {
          std::string(R"GLTF({
  "ass\u0065t" : {"version":"2.0"},
  "extras" : {"fake":{"buffers":[],"accessors":[],"indices":3,"mode":5},"decimal":1.00e+01,"integer":9007199254740995,"escaped":"\ud83d\ude00"},
  "buffers" : [
	{"uri":"memory.bin","byteLength":132},
	{"uri":"data:application/gltf-buffer;base64,iVBORw0KGgo=","byteLength":8}
  ],
  "bufferViews" : [
	{"buffer":0,"byteOffset":0,"byteLength":64,"byteStride":16},
	{"buffer":0,"byteOffset":64,"byteLength":16},
	{"buffer":0,"byteOffset":80,"byteLength":2},
	{"buffer":0,"byteOffset":84,"byteLength":4},
	{"buffer":0,"byteOffset":88,"byteLength":8},
	{"buffer":0,"byteOffset":100,"byteLength":8},
	{"buffer":0,"byteOffset":108,"byteLength":24},
	{"buffer":1,"byteOffset":0,"byteLength":8}
  ],
  "accessors" : [
	{"bufferView":0,"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[1,1,0]},
	{"bufferView":1,"componentType":5122,"type":"VEC2","count":4,"normalized":true,"min":[-32768,-32768],"max":[32767,32767]},
	{"componentType":5121,"type":"VEC2","count":4,"normalized":true,"sparse":{"count":2,"indices":{"bufferView":2,"componentType":5121},"values":{"bufferView":3}}},
	{"bufferView":4,"componentType":5123,"type":"SCALAR","count":4},
	{"componentType":5126,"type":"VEC3","count":4},
	{"bufferView":5,"componentType":5126,"type":"SCALAR","count":2,"min":[0],"max":[1]},
	{"bufferView":6,"componentType":5126,"type":"VEC3","count":2}
  ],
  "images":[{"uri":"data:image/png;base64,iVBORw0KGgo="},{"bufferView":7,"mimeType":"image/png"}],
  "materials":[{"name":"untouched","extras":{"number":12345678901234567}}],
  "meshes" : [{"primitives":[{"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indices":3,"mode":6,"material":0,"extras":{"other":"unrelated primitive"}},{"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indi\u0063es" : 3, "mo\u0064e" : 6,"material":0,"extras":{"indices":9007199254740993,"mode":-0.0,"braces":"} [ \"indices\": 71 {","huge":1e400}}]},{"weights":[0],"primitives":[{"attributes":{"POSITION":0},"indices":3,"mode":6,"targets":[{"POSITION":4}]}]}],
  "nodes" : [{"mesh":0,"name":"first\u0394","translation":[1,2,3]},{"mesh":0,"name":"second"},{"mesh":1,"skin":0,"weights":[0]},{"name":"unrelated joint"}],
  "skins" : [{"joints":[3],"skeleton":3}],
  "animations" : [{"samplers":[{"input":5,"output":6}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}],
  "scenes" : [{"name":"shared","nodes":[0,1,2,3]},{"nodes":[0]}],
  "scene":0
}
)GLTF"),
          DecodeHex("000000800000000000000000efbeadde010000000000000000000000ef"
                    "beadde0000803f0000000000000000efbeadde000000000000803f0000"
                    "0000efbeadde0080ff7fffff000000000080ff7f01000103a2a3ff0001"
                    "fe030001000200000000000000000000000000803f0000000000000000"
                    "00000000000000000000000000000000fafbfcfdfe"),
          kind,
          1,
          6,
          4,
          {3, 1, 2, 3, 2, 0},
          {0, 1, 2, 0, 2, 3},
          {R"KEEP({"uri":"memory.bin","byteLength":132})KEEP",
           R"KEEP({"uri":"data:application/gltf-buffer;base64,iVBORw0KGgo=","byteLength":8})KEEP",
           R"KEEP({"buffer":0,"byteOffset":0,"byteLength":64,"byteStride":16})KEEP",
           R"KEEP({"buffer":0,"byteOffset":64,"byteLength":16})KEEP",
           R"KEEP({"buffer":0,"byteOffset":80,"byteLength":2})KEEP",
           R"KEEP({"buffer":0,"byteOffset":84,"byteLength":4})KEEP",
           R"KEEP({"buffer":0,"byteOffset":88,"byteLength":8})KEEP",
           R"KEEP({"buffer":0,"byteOffset":100,"byteLength":8})KEEP",
           R"KEEP({"buffer":0,"byteOffset":108,"byteLength":24})KEEP",
           R"KEEP({"buffer":1,"byteOffset":0,"byteLength":8})KEEP",
           R"KEEP({"bufferView":0,"componentType":5126,"type":"VEC3","count":4,"min":[0,0,0],"max":[1,1,0]})KEEP",
           R"KEEP({"bufferView":1,"componentType":5122,"type":"VEC2","count":4,"normalized":true,"min":[-32768,-32768],"max":[32767,32767]})KEEP",
           R"KEEP({"componentType":5121,"type":"VEC2","count":4,"normalized":true,"sparse":{"count":2,"indices":{"bufferView":2,"componentType":5121},"values":{"bufferView":3}}})KEEP",
           R"KEEP({"bufferView":4,"componentType":5123,"type":"SCALAR","count":4})KEEP",
           R"KEEP({"componentType":5126,"type":"VEC3","count":4})KEEP",
           R"KEEP({"bufferView":5,"componentType":5126,"type":"SCALAR","count":2,"min":[0],"max":[1]})KEEP",
           R"KEEP({"bufferView":6,"componentType":5126,"type":"VEC3","count":2})KEEP",
           R"KEEP({"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},"indices":3,"mode":6,"material":0,"extras":{"other":"unrelated primitive"}})KEEP",
           R"KEEP({"attributes":{"POSITION":0},"indices":3,"mode":6,"targets":[{"POSITION":4}]})KEEP",
           R"KEEP([{"mesh":0,"name":"first\u0394","translation":[1,2,3]},{"mesh":0,"name":"second"},{"mesh":1,"skin":0,"weights":[0]},{"name":"unrelated joint"}])KEEP",
           R"KEEP([{"joints":[3],"skeleton":3}])KEEP",
           R"KEEP([{"samplers":[{"input":5,"output":6}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}])KEEP",
           R"KEEP([{"name":"shared","nodes":[0,1,2,3]},{"nodes":[0]}])KEEP",
           R"KEEP({"fake":{"buffers":[],"accessors":[],"indices":3,"mode":5},"decimal":1.00e+01,"integer":9007199254740995,"escaped":"\ud83d\ude00"})KEEP",
           R"KEEP("extras":{"indices":9007199254740993,"mode":-0.0,"braces":"} [ \"indices\": 71 {","huge":1e400}})KEEP"}};
    case FixtureKind::kImplicit:
      return {
          std::string(
              R"GLTF({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":6,"min":[0,0,0],"max":[0,0,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})GLTF"),
          DecodeHex(""),
          kind,
          0,
          4,
          6,
          {0, 1, 2, 3, 4, 5},
          {0, 1, 2, 3, 4, 5},
          {R"KEEP({"componentType":5126,"type":"VEC3","count":6,"min":[0,0,0],"max":[0,0,0]})KEEP"}};
    case FixtureKind::kLargeIndex:
      return {
          std::string(
              R"GLTF({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"type":"VEC3","count":16777218,"min":[0,0,0],"max":[0,0,0]},{"bufferView":0,"componentType":5125,"type":"SCALAR","count":6}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"buffers":[{"uri":"memory.bin","byteLength":24}],"bufferViews":[{"buffer":0,"byteLength":24}]})GLTF"),
          DecodeHex("010000010000000001000000000000010000000001000000fafbfc"),
          kind,
          0,
          4,
          16777218,
          {16777217, 0, 1, 16777216, 0, 1},
          {0, 1, 2, 3, 4, 5},
          {R"KEEP({"componentType":5126,"type":"VEC3","count":16777218,"min":[0,0,0],"max":[0,0,0]})KEEP"}};
  }
  throw std::runtime_error("Unknown topology writer semantic fixture");
}
}  // namespace gltf_topology_write_semantics
