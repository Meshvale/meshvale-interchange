# SPDX-License-Identifier: Apache-2.0
"""Independent little-endian glTF wire fixtures, not production state builders.

--write prepares fixture_data.cpp. The default checks its JSON and byte literals
against this independent Python builder, including after C++ formatting. No
Meshvale runtime is loaded or executed by this check.
"""

import argparse
import json
import re
import struct
from pathlib import Path
from fractions import Fraction

WIRE_INT8 = 5120
WIRE_UINT8 = 5121
WIRE_INT16 = 5122
WIRE_UINT16 = 5123
WIRE_UINT32 = 5125
WIRE_FLOAT32 = 5126
MODE_TRIANGLES = 4
MODE_STRIP = 5
MODE_FAN = 6
SMALL_VERTEX_COUNT = 4
LARGE_SOURCE_INDEX = 16777217
LARGE_VERTEX_COUNT = LARGE_SOURCE_INDEX + 1
POSITION_BITS = [0x80000000, 0, 0, 1, 0, 0, 0x3F800000, 0, 0,
                 0, 0x3F800000, 0]
TRAILER = bytes([42, 43, 44])


def document(mode, index_type, raw_channels=False, large=False):
    blob = bytearray()
    views = []
    accessors = []
    attributes = {}

    def view(data, stride=None):
        blob.extend(bytes((-len(blob)) % 4))
        result = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if stride is not None:
            result["byteStride"] = stride
        views.append(result)
        blob.extend(data)
        return len(views) - 1

    def channel(name, component, shape, data=None, normalized=False,
                stride=None, minimum=None, maximum=None, sparse=None):
        item = {"componentType": component, "type": shape,
                "count": LARGE_VERTEX_COUNT if large else SMALL_VERTEX_COUNT}
        if data is not None:
            item["bufferView"] = view(data, stride)
        if normalized:
            item["normalized"] = True
        if minimum is not None:
            item["min"] = minimum
        if maximum is not None:
            item["max"] = maximum
        if sparse is not None:
            item["sparse"] = sparse
        attributes[name] = len(accessors)
        accessors.append(item)

    position_data = None if large else struct.pack("<12I", *POSITION_BITS)
    channel("POSITION", WIRE_FLOAT32, "VEC3", position_data,
            minimum=[0, 0, 0], maximum=[0, 0, 0] if large else [1, 1, 0])
    if raw_channels:
        channel("_SIGNED_BYTE", WIRE_INT8, "SCALAR",
                bytes([128, 91, 92, 93, 255, 91, 92, 93,
                       0, 91, 92, 93, 127, 91, 92, 93]),
                normalized=True, stride=4, minimum=[-128], maximum=[127])
        signed_short = [-32768, 32767, -1, 0, 0, -32768, 32767, 1]
        channel("_SIGNED_SHORT", WIRE_INT16, "VEC2",
                struct.pack("<8h", *signed_short), normalized=True,
                minimum=[-32768, -32768], maximum=[32767, 32767])
        selector_view = view(bytes([1, 3]))
        sparse_view = view(bytes([255, 0, 1, 254]))
        channel("TEXCOORD_0", WIRE_UINT8, "VEC2", normalized=True,
                minimum=[0, 0], maximum=[255, 254],
                sparse={"count": 2,
                        "indices": {"bufferView": selector_view,
                                    "componentType": WIRE_UINT8},
                        "values": {"bufferView": sparse_view}})
        channel("TEXCOORD_1", WIRE_UINT16, "VEC2",
                struct.pack("<8H", 0, 65535, 32768, 1, 65535, 0, 1, 32768),
                normalized=True)
        channel("COLOR_0", WIRE_UINT8, "VEC4",
                bytes([0, 255, 1, 254, 255, 0, 128, 127,
                       1, 2, 3, 4, 255, 255, 255, 255]), normalized=True)
        channel("COLOR_1", WIRE_FLOAT32, "VEC3",
                struct.pack("<12f", 0, 1, 0.5, 1, 0, 0.25,
                            0.5, 0.25, 1, 0, 0, 0))
        channel("NORMAL", WIRE_FLOAT32, "VEC3",
                struct.pack("<12f", *([0, 0, 1] * SMALL_VERTEX_COUNT)))
        channel("TANGENT", WIRE_FLOAT32, "VEC4",
                struct.pack("<16f", 1, 0, 0, 1, 1, 0, 0, -1,
                            1, 0, 0, 1, 1, 0, 0, -1))
        channel("_FLOAT4", WIRE_FLOAT32, "VEC4",
                struct.pack("<16I", 0x80000000, 1, 0xBF800000, 0x3F800000,
                            0, 0, 0x40000000, 0xC0000000,
                            0x3F800000, 0x40000000, 0x40400000, 0x40800000,
                            0, 0, 0, 0))
        channel("_IMPLICIT", WIRE_UINT16, "VEC3")

    stream = [0, LARGE_SOURCE_INDEX, 1] if large else (
        [3, 1, 2, 0, 2, 2] if mode == MODE_TRIANGLES else [3, 1, 2, 0])
    format_code = {WIRE_UINT8: "B", WIRE_UINT16: "H", WIRE_UINT32: "I"}[index_type]
    index_view = view(struct.pack("<" + format_code * len(stream), *stream))
    index_accessor = len(accessors)
    accessors.append({"bufferView": index_view, "componentType": index_type,
                      "type": "SCALAR", "count": len(stream)})
    primitive = {"mode": mode, "indices": index_accessor,
                 "material": 0, "attributes": attributes,
                 "extras": {"opaque": {"selected": "kept"}}}
    logical_bytes = len(blob)
    blob.extend(TRAILER)
    result = {"asset": {"version": "2.0"},
              "extras": {"exactInteger": 9007199254740993, "zero": -0.0},
              "buffers": [{"uri": "memory.bin", "byteLength": logical_bytes}],
              "bufferViews": views, "accessors": accessors,
              "materials": [{"name": "retained material"}],
              "meshes": [{"primitives": [primitive, primitive]},
                         {"primitives": [primitive, primitive]}],
              "nodes": [{"mesh": 1, "name": "first", "translation": [1, 2, 3]},
                        {"mesh": 1, "name": "second"}, {"mesh": 0}],
              "scenes": [{"nodes": [0, 1]}, {"nodes": [2]}]}
    return json.dumps(result, separators=(",", ":")), bytes(blob)


def fixtures():
    return {"Raw": document(MODE_TRIANGLES, WIRE_UINT32, raw_channels=True),
            "Triangles": document(MODE_TRIANGLES, WIRE_UINT8),
            "Strip": document(MODE_STRIP, WIRE_UINT16),
            "Fan": document(MODE_FAN, WIRE_UINT32),
            "Large": document(MODE_TRIANGLES, WIRE_UINT32, large=True)}


def write_cpp(path, data):
    text = ['// SPDX-License-Identifier: Apache-2.0',
            '// Golden wire input generated by check_fixtures.py; no runtime state.',
            '#include "fixture_support.h"', '#include <array>',
            '#include <cstddef>', '#include <cstdint>', '#include <string>', '#include <string_view>',
            '#include <vector>',
            'namespace gltf_materialization_semantics {', 'namespace {',
            'constexpr std::uint64_t kSelectedPrimitiveOrdinal = 3;']
    for name, (document_text, blob) in data.items():
        text.append(f'constexpr std::string_view k{name}Json = R"GLTF({document_text})GLTF";')
        text.append(f'constexpr std::array<std::byte, {len(blob)}> k{name}Bytes{{')
        for offset in range(0, len(blob), 12):
            text.append('  ' + ', '.join(f'std::byte{{0x{v:02x}}}'
                                        for v in blob[offset:offset + 12]) + ',')
        text.append('};')
    text.extend(['}  // namespace', 'Fixture RawChannelsFixture() {',
                 'return {std::string(kRawJson), std::vector<std::byte>(kRawBytes.begin(), kRawBytes.end()), kSelectedPrimitiveOrdinal, false, false};',
                 '}', 'Fixture MakeTopologyFixture(TopologyFixture kind) {',
                 'switch (kind) {'])
    for name in ['Triangles', 'Strip', 'Fan']:
        converted = 'false' if name == 'Triangles' else 'true'
        text.extend([f'case TopologyFixture::k{name}:',
                     f'return {{std::string(k{name}Json), std::vector<std::byte>(k{name}Bytes.begin(), k{name}Bytes.end()), kSelectedPrimitiveOrdinal, {converted}, false}};'])
    text.extend(['}', 'throw std::runtime_error("Unknown topology fixture");', '}',
                 'Fixture LargeSourceIndexFixture() {',
                 'return {std::string(kLargeJson), std::vector<std::byte>(kLargeBytes.begin(), kLargeBytes.end()), kSelectedPrimitiveOrdinal, false, true};',
                 '}', '}  // namespace gltf_materialization_semantics', ''])
    text.insert(7, '#include <stdexcept>')
    path.write_text('\n'.join(text), encoding='utf-8')


def check_cpp(path, data):
    text = path.read_text(encoding='utf-8')
    for name, (json_text, blob) in data.items():
        stored_json = re.search(rf'k{name}Json\s*=\s*R"GLTF\((.*?)\)GLTF"', text, re.S)
        assert stored_json and stored_json.group(1) == json_text, name
        stored_bytes = re.search(rf'k{name}Bytes\s*\{{(.*?)\}};', text, re.S)
        assert stored_bytes
        actual = bytes(int(v, 16) for v in re.findall(r'std::byte\{0x([0-9a-f]+)\}', stored_bytes.group(1)))
        assert actual == blob and actual[-len(TRAILER):] == TRAILER, name
        doc = json.loads(json_text)
        assert doc['buffers'][0]['byteLength'] == len(blob) - len(TRAILER)
        for view in doc['bufferViews']:
            assert view['byteOffset'] % 4 == 0
            assert view['byteOffset'] + view['byteLength'] <= len(blob) - len(TRAILER)
        assert list(doc['meshes'][1]['primitives'][1]['attributes'])[0] == 'POSITION'
    raw_doc = json.loads(data['Raw'][0])
    assert {a['componentType'] for a in raw_doc['accessors']} == {
        WIRE_INT8, WIRE_UINT8, WIRE_INT16, WIRE_UINT16, WIRE_UINT32, WIRE_FLOAT32}
    attributes = raw_doc['meshes'][1]['primitives'][1]['attributes']
    assert all(raw_doc['accessors'][accessor]['componentType'] != WIRE_UINT32
               for accessor in attributes.values())
    assert struct.unpack('<f', struct.pack('<I', 1))[0] == 2.0 ** -149
    assert struct.unpack('<Q', struct.pack('<d', 2.0 ** -149))[0] == 0x36A0000000000000
    assert LARGE_SOURCE_INDEX != int(struct.unpack('<f', struct.pack('<f', LARGE_SOURCE_INDEX))[0])
    for rational, expected_bits in [
        (Fraction(-1, 127), 0xBF80204081020408),
        (Fraction(1, 255), 0x3F70101010101010),
        (Fraction(254, 255), 0x3FEFDFDFDFDFDFE0),
        (Fraction(-1, 32767), 0xBF00002000400080),
        (Fraction(1, 32767), 0x3F00002000400080),
        (Fraction(32768, 65535), 0x3FE0001000100010),
        (Fraction(1, 65535), 0x3EF0001000100010),
    ]:
        assert struct.unpack('<Q', struct.pack('<d', float(rational)))[0] == expected_bits
    # Check every independently authored C++ normalized expectation, rather than
    # validating only representative ratios or using a production decoder.
    normalized_text = path.with_name('raw_channels.cpp').read_text(encoding='utf-8')
    signed_values = [-128, -1, 0, 127]
    short_values = [-32768, 32767, -1, 0, 0, -32768, 32767, 1]
    uv_values = [0, 0, 255, 0, 0, 0, 1, 254]
    second_uv_values = [0, 65535, 32768, 1, 65535, 0, 1, 32768]
    color_values = [0, 255, 1, 254, 255, 0, 128, 127,
                    1, 2, 3, 4, 255, 255, 255, 255]
    for name, values, maximum in [
        ('Signed', signed_values, 127), ('Short', short_values, 32767),
        ('Uv', uv_values, 255), ('SecondUv', second_uv_values, 65535),
        ('ByteColor', color_values, 255),
    ]:
        found = re.search(rf'kNormalized{name}Bits\s*\{{(.*?)\}};', normalized_text, re.S)
        assert found, name
        actual = [int(value, 0) for value in
                  re.findall(r'0x[0-9a-f]+|(?<!\w)0(?!\w)', found.group(1))]
        expected = [struct.unpack('<Q', struct.pack('<d', float(max(Fraction(-1),
                    Fraction(value, maximum)))))[0] for value in values]
        assert actual == expected, name
    print('Independent wire fixtures checked: five JSON/binary inputs, six raw encodings; no native execution')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    path = Path(__file__).with_name('fixture_data.cpp')
    data = fixtures()
    if args.write:
        write_cpp(path, data)
    check_cpp(path, data)


if __name__ == '__main__':
    main()
