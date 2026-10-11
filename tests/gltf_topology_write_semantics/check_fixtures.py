#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check/generate original wire fixtures without importing Meshvale runtime."""

import argparse
import base64
import json
from pathlib import Path
import re
import struct

PNG_SIGNATURE = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
PNG_BUFFER_INDEX = 1
PNG_BUFFER_VIEW_INDEX = 7


def compact(value):
    return json.dumps(value, separators=(",", ":"), ensure_ascii=True)


def make_shared(mode):
    position_bits = [0x80000000, 0, 0, 0xDEADBEEF, 1, 0, 0, 0xDEADBEEF,
                     0x3F800000, 0, 0, 0xDEADBEEF,
                     0, 0x3F800000, 0, 0xDEADBEEF]
    payload = bytearray(struct.pack("<16I", *position_bits))
    payload.extend(struct.pack("<8h", -32768, 32767, -1, 0, 0, -32768, 32767, 1))
    payload.extend(bytes([1, 3, 0xA2, 0xA3, 255, 0, 1, 254]))
    stream = [3, 1, 2, 0, 2, 2] if mode == 4 else [3, 1, 2, 0]
    payload.extend(struct.pack("<" + "H" * len(stream), *stream))
    payload.extend(bytes(100 - len(payload)))
    payload.extend(struct.pack("<2f", 0, 1))
    payload.extend(bytes(24))
    payload.extend(bytes([0xFA, 0xFB, 0xFC, 0xFD, 0xFE]))
    assert len(payload) == 137
    views = [dict(buffer=0, byteOffset=0, byteLength=64, byteStride=16),
             dict(buffer=0, byteOffset=64, byteLength=16),
             dict(buffer=0, byteOffset=80, byteLength=2),
             dict(buffer=0, byteOffset=84, byteLength=4),
             dict(buffer=0, byteOffset=88, byteLength=len(stream) * 2),
             dict(buffer=0, byteOffset=100, byteLength=8),
             dict(buffer=0, byteOffset=108, byteLength=24),
             dict(buffer=PNG_BUFFER_INDEX, byteOffset=0,
                  byteLength=len(PNG_SIGNATURE))]
    accessors = [dict(bufferView=0, componentType=5126, type="VEC3", count=4,
                      min=[0, 0, 0], max=[1, 1, 0]),
                 dict(bufferView=1, componentType=5122, type="VEC2", count=4,
                      normalized=True, min=[-32768, -32768], max=[32767, 32767]),
                 dict(componentType=5121, type="VEC2", count=4, normalized=True,
                      sparse=dict(count=2, indices=dict(bufferView=2, componentType=5121),
                                  values=dict(bufferView=3))),
                 dict(bufferView=4, componentType=5123, type="SCALAR", count=len(stream)),
                 dict(componentType=5126, type="VEC3", count=4),
                 dict(bufferView=5, componentType=5126, type="SCALAR", count=2,
                      min=[0], max=[1]),
                 dict(bufferView=6, componentType=5126, type="VEC3", count=2)]
    buffers = [dict(uri="memory.bin", byteLength=132),
               dict(uri="data:application/gltf-buffer;base64," +
                        base64.b64encode(PNG_SIGNATURE).decode("ascii"),
                    byteLength=len(PNG_SIGNATURE))]
    other = compact(dict(attributes={"POSITION": 0, "_SIGNED": 1, "TEXCOORD_0": 2},
                         indices=3, mode=mode, material=0,
                         extras={"other": "unrelated primitive"}))
    selected = ('{"attributes":{"POSITION":0,"_SIGNED":1,"TEXCOORD_0":2},'
                '"indi\\u0063es" : 3, "mo\\u0064e" : ' + str(mode) +
                ',"material":0,'
                '"extras":{"indices":9007199254740993,"mode":-0.0,'
                '"braces":"} [ \\"indices\\": 71 {","huge":1e400}}')
    morph = compact(dict(attributes={"POSITION": 0}, indices=3, mode=mode,
                         targets=[{"POSITION": 4}]))
    meshes = '[{"primitives":[' + other + ',' + selected + ']},' + \
             '{"weights":[0],"primitives":[' + morph + ']}]'
    nodes = '[{"mesh":0,"name":"first\\u0394","translation":[1,2,3]},' + \
            '{"mesh":0,"name":"second"},{"mesh":1,"skin":0,"weights":[0]},' + \
            '{"name":"unrelated joint"}]'
    skins = '[{"joints":[3],"skeleton":3}]'
    animations = '[{"samplers":[{"input":5,"output":6}],' + \
                 '"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}]'
    scenes = '[{"name":"shared","nodes":[0,1,2,3]},{"nodes":[0]}]'
    extras = '{"fake":{"buffers":[],"accessors":[],"indices":3,"mode":5},' + \
             '"decimal":1.00e+01,"integer":9007199254740995,"escaped":"\\ud83d\\ude00"}'
    lines = ['  "ass\\u0065t" : {"version":"2.0"}',
             '  "extras" : ' + extras,
             '  "buffers" : [\n\t' + ',\n\t'.join(map(compact, buffers)) + '\n  ]',
             '  "bufferViews" : [\n\t' + ',\n\t'.join(map(compact, views)) + '\n  ]',
             '  "accessors" : [\n\t' + ',\n\t'.join(map(compact, accessors)) + '\n  ]',
             '  "images":[{"uri":"data:image/png;base64,iVBORw0KGgo="},'
             '{"bufferView":' + str(PNG_BUFFER_VIEW_INDEX) +
             ',"mimeType":"image/png"}]',
             '  "materials":[{"name":"untouched","extras":{"number":12345678901234567}}]',
             '  "meshes" : ' + meshes, '  "nodes" : ' + nodes,
             '  "skins" : ' + skins, '  "animations" : ' + animations,
             '  "scenes" : ' + scenes, '  "scene":0']
    text = '{\n' + ',\n'.join(lines) + '\n}\n'
    parsed = json.loads(text)
    assert parsed["meshes"][0]["primitives"][1]["indices"] == 3
    assert parsed["meshes"][0]["primitives"][1]["mode"] == mode
    triangles = {4: [3, 1, 2, 0, 2, 2], 5: [3, 1, 2, 2, 1, 0],
                 6: [3, 1, 2, 3, 2, 0]}[mode]
    corners = {4: [0, 1, 2, 3, 4, 5], 5: [0, 1, 2, 2, 1, 3],
               6: [0, 1, 2, 0, 2, 3]}[mode]
    preserved = list(map(compact, buffers + views + accessors)) + \
                [other, morph, nodes, skins, animations, scenes, extras,
                 '"extras":{"indices":9007199254740993,"mode":-0.0,'
                 '"braces":"} [ \\"indices\\": 71 {","huge":1e400}}']
    return text, bytes(payload), triangles, corners, preserved


def make_simple(large):
    count = 16777218 if large else 6
    triangles = [16777217, 0, 1, 16777216, 0, 1] if large else [0, 1, 2, 3, 4, 5]
    position = dict(componentType=5126, type="VEC3", count=count,
                    min=[0, 0, 0], max=[0, 0, 0])
    document = dict(asset={"version": "2.0"}, accessors=[position],
                    meshes=[dict(primitives=[dict(attributes={"POSITION": 0})])])
    payload = b""
    if large:
        document["buffers"] = [dict(uri="memory.bin", byteLength=24)]
        document["bufferViews"] = [dict(buffer=0, byteLength=24)]
        document["accessors"].append(dict(bufferView=0, componentType=5125,
                                            type="SCALAR", count=6))
        document["meshes"][0]["primitives"][0]["indices"] = 1
        payload = struct.pack("<6I", *triangles) + b"\xfa\xfb\xfc"
    text = compact(document)
    return text, payload, triangles, list(range(6)), [compact(position)]


def fixture_source():
    prefix = '''// SPDX-License-Identifier: Apache-2.0
// Original wire fixtures; regenerate/check using check_fixtures.py.
#include "fixture_support.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
    bytes.push_back(static_cast<std::byte>((digit(text[i]) << 4) |
                                         digit(text[i + 1])));
  }
  return bytes;
}
}  // namespace
Fixture MakeFixture(FixtureKind kind) {
  switch (kind) {
'''
    cases = [("kTriangles", 4, 4, make_shared(4)),
             ("kStrip", 5, 4, make_shared(5)),
             ("kFan", 6, 4, make_shared(6)),
             ("kImplicit", 4, 6, make_simple(False)),
             ("kLargeIndex", 4, 16777218, make_simple(True))]
    for name, mode, vertices, (text, payload, triangles, corners, preserved) in cases:
        strings = ',\n'.join('R"KEEP(' + s + ')KEEP"' for s in preserved)
        prefix += ('    case FixtureKind::' + name + ':\n      return {\n'
                   'std::string(R"GLTF(' + text + ')GLTF"),\n'
                   'DecodeHex("' + payload.hex() + '"),kind,' +
                   str(1 if name in ("kTriangles", "kStrip", "kFan") else 0) + ',' +
                   str(mode) + ',' + str(vertices) + ',\n{' +
                   ','.join(map(str, triangles)) + '},{' + ','.join(map(str, corners)) +
                   '},\n{' + strings + '}};\n')
    return prefix + '''  }
  throw std::runtime_error("Unknown topology writer semantic fixture");
}
}  // namespace gltf_topology_write_semantics
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    target = Path(__file__).with_name("fixture_data.cpp")
    expected = fixture_source()
    if args.write:
        target.write_text(expected, encoding="utf-8")
    else:
        # Format-independent checks bind all literal JSON, payload and map cases.
        actual = target.read_text(encoding="utf-8")
        raw_jsons = re.findall(r'R"GLTF\((.*?)\)GLTF"', actual, re.DOTALL)
        raw_hexes = ["".join(json.loads(s) for s in re.findall(r'"[^"]*"', group))
                     for group in re.findall(r'DecodeHex\(\s*((?:"[^"]*"\s*)+)\)', actual)]
        maps = re.findall(r'\{\s*((?:\d+\s*,\s*)*\d+)\s*\},\s*'
                          r'\{\s*((?:\d+\s*,\s*)*\d+)\s*\},', actual)
        parsed_maps = [(list(map(int, first.split(','))), list(map(int, second.split(','))))
                       for first, second in maps]
        cases = [make_shared(mode) for mode in (4, 5, 6)] + \
                [make_simple(large) for large in (False, True)]
        assert raw_jsons == [case[0] for case in cases]
        assert raw_hexes == [case[1].hex() for case in cases]
        assert parsed_maps == [(case[2], case[3]) for case in cases]
        for mode in (4, 5, 6):
            text, payload, triangles, corners, preserved = make_shared(mode)
            assert all(s in text for s in preserved)
            assert len(triangles) == len(corners) == 6
            assert [struct.unpack_from("<H", payload, 88 + c * 2)[0]
                    for c in corners] == triangles
            selected = struct.pack("<3I", *triangles[3:])
            assert len(selected) == 12
            document = json.loads(text)
            assert len(document["bufferViews"]) == PNG_BUFFER_VIEW_INDEX + 1
            png_buffer = document["buffers"][PNG_BUFFER_INDEX]
            decoded_buffer = base64.b64decode(png_buffer["uri"].split(",", 1)[1],
                                             validate=True)
            assert decoded_buffer == PNG_SIGNATURE
            assert png_buffer["byteLength"] == len(PNG_SIGNATURE)
            embedded_image, aliased_image = document["images"]
            assert base64.b64decode(embedded_image["uri"].split(",", 1)[1],
                                    validate=True) == PNG_SIGNATURE
            assert aliased_image["mimeType"] == "image/png"
            assert aliased_image["bufferView"] == PNG_BUFFER_VIEW_INDEX
            png_view = document["bufferViews"][aliased_image["bufferView"]]
            assert png_view == dict(buffer=PNG_BUFFER_INDEX, byteOffset=0,
                                    byteLength=len(PNG_SIGNATURE))
            assert decoded_buffer[png_view["byteOffset"]:
                                  png_view["byteOffset"] + png_view["byteLength"]] == PNG_SIGNATURE
        for large in (False, True):
            text, payload, triangles, corners, preserved = make_simple(large)
            if large:
                assert struct.unpack_from("<I", payload)[0] == 16777217
                assert struct.pack("<I", triangles[0]).hex() == "01000001"
        checks = target.with_name("written_checks.cpp").read_text(encoding="utf-8")
        for ordinal, case in enumerate(cases):
            selected = case[2][:3] if ordinal == 4 else case[2][3:]
            uri = "data:application/octet-stream;base64," + \
                  base64.b64encode(struct.pack("<3I", *selected)).decode("ascii")
            assert uri in checks
    print("Independent input fixtures passed: three indexed modes, PNG resource/view signatures, absent tables, UInt32 above 2^24")


if __name__ == "__main__":
    main()
