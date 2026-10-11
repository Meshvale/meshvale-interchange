# SPDX-License-Identifier: Apache-2.0
"""Independent struct/Fraction oracle for actual Read->Extract->Materialize.

Generates source bytes and expected canonical/normalized values without importing
production metadata, decoder helpers, or adapter mappings. --prepare-only writes
fixtures without claiming execution. All execution commands/results are retained.
"""

import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def f32_fraction(bits):
    sign = -1 if bits >> 31 else 1
    exponent = (bits >> 23) & 255
    fraction = bits & ((1 << 23) - 1)
    assert exponent != 255
    if exponent:
        return sign * Fraction((1 << 23) + fraction) * Fraction(2) ** (exponent - 150)
    return sign * Fraction(fraction) * Fraction(2) ** -149


def f64_bits(value):
    return struct.unpack("<Q", struct.pack("<d", float(value)))[0]


def widened(bits):
    return 1 << 63 if bits == 1 << 31 else f64_bits(f32_fraction(bits))


def normalized(values, width, signed):
    denominator = (1 << (width - int(signed))) - 1
    return [f64_bits(max(Fraction(-1), Fraction(v, denominator))) for v in values]


def fixture(mode, *, high_channels=False):
    positions = [
        0x3F800000, 0x80000000, 1,
        0x3F800001, 0xC0000000, 0x7F7FFFFF,
        0xBF800000, 0x40000000, 0x80800000,
        0, 0x3F000000, 0xBF000000,
    ]
    count = len(positions) // 3
    storage = bytearray()
    views, accessors = [], []
    attributes = {}
    expected_attributes, expected_normalized = {}, {}

    def view(data, stride=None):
        while len(storage) % 4:
            storage.append(0)
        record = {"buffer": 0, "byteOffset": len(storage), "byteLength": len(data)}
        if stride is not None:
            record["byteStride"] = stride
        index = len(views)
        views.append(record)
        storage.extend(data)
        return index

    def channel(name, code, shape, values, fmt, canonical, *, normalize=False,
                width=0, signed=False, stride=None):
        if stride is None:
            packed = struct.pack("<" + fmt * len(values), *values)
        else:
            packed = b"".join(struct.pack("<" + fmt, v) + bytes(stride - 1)
                              for v in values)
        record = {"bufferView": view(packed, stride), "componentType": code,
                  "type": shape, "count": count}
        if normalize:
            record["normalized"] = True
        attributes[name] = len(accessors)
        accessors.append(record)
        expected_attributes[name] = [canonical, values]
        expected_normalized[name] = normalized(values, width, signed) if normalize else []

    position_view = view(struct.pack("<" + "I" * len(positions), *positions))
    numeric_positions = [struct.unpack("<f", struct.pack("<I", b))[0] for b in positions]
    accessors.append({"bufferView": position_view, "componentType": 5126,
                      "type": "VEC3", "count": count,
                      "min": [min(numeric_positions[axis::3]) for axis in range(3)],
                      "max": [max(numeric_positions[axis::3]) for axis in range(3)]})
    attributes["POSITION"] = 0
    expected_normalized["POSITION"] = []
    channel("_I8", 5120, "SCALAR", [-128, -1, 0, 127], "b", "i32",
            normalize=True, width=8, signed=True, stride=4)
    channel("_I16", 5122, "VEC2", [-32768, -1, 0, 1, 32767, -32768, 7, -7],
            "h", "i32", normalize=True, width=16, signed=True)
    channel("TEXCOORD_0", 5123, "VEC2", [0, 65535, 1, 65534, 32768, 32767, 7, 11],
            "H", "u16", normalize=True, width=16)
    channel("_FLOAT", 5126, "VEC2", [0, 0x80000000, 1, 0x3F800001, 0xC0200000,
                                      0x40000000, 0xBF000000, 0x7F7FFFFF], "I", "f32")
    # Sparse unsigned bytes retain zero base and exact replacement selectors.
    sparse_values = [255, 254, 1, 2]
    sparse_accessor = {"componentType": 5121, "type": "VEC2", "count": count,
                       "normalized": True,
                       "sparse": {"count": 2,
                                  "indices": {"bufferView": view(bytes([1, 3])),
                                              "componentType": 5121},
                                  "values": {"bufferView": view(bytes(sparse_values))}}}
    attributes["_SPARSE"] = len(accessors)
    accessors.append(sparse_accessor)
    sparse_output = [0, 0, 255, 254, 0, 0, 1, 2]
    expected_attributes["_SPARSE"] = ["u8", sparse_output]
    expected_normalized["_SPARSE"] = normalized(sparse_output, 8, False)
    if high_channels:
        # Functional large descriptor inventory assessment; no timing claim.
        for ordinal in range(256):
            name = f"_CHANNEL_{ordinal}"
            attributes[name] = len(accessors)
            accessors.append({"componentType": 5123, "type": "SCALAR", "count": count})
            expected_attributes[name] = ["u16", [0] * count]
            expected_normalized[name] = []
    index_stream = [0, 1, 2] if mode == 4 else [0, 1, 1, 3]
    index_accessor = len(accessors)
    accessors.append({"bufferView": view(struct.pack("<" + "I" * len(index_stream), *index_stream)), "componentType": 5125,
                      "type": "SCALAR", "count": len(index_stream)})
    if mode == 4:
        corners = index_stream
    elif mode == 5:
        corners = [0, 1, 1, 1, 1, 3]
    else:
        corners = [0, 1, 1, 0, 1, 3]
    document = {"asset": {"version": "2.0"},
                "buffers": [{"uri": "memory.bin", "byteLength": len(storage)}],
                "bufferViews": views, "accessors": accessors,
                "meshes": [{"primitives": [{"mode": mode, "indices": index_accessor,
                                               "attributes": attributes}]}]}
    expected = {"positions": [widened(b) for b in positions],
                "offsets": list(range(0, len(corners) + 1, 3)), "corners": corners,
                "attributes": expected_attributes, "normalized": expected_normalized,
                "mapping": [[ordinal, "positions" if name == "POSITION" else str(ordinal - 1)]
                            for ordinal, name in enumerate(attributes)],
                "identity": [0, 0], "indices": ["u32", index_stream]}
    return document, bytes(storage), expected


def parse_output(text):
    result = {"attributes": {}, "normalized": {}, "mapping": []}
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "indices":
            assert "indices" not in result
            result["indices"] = [parts[1], [int(v) for v in parts[2:]]]
        elif parts[0] == "attribute":
            assert parts[1] not in result["attributes"]
            result["attributes"][parts[1]] = [parts[2], [int(v) for v in parts[3:]]]
        elif parts[0] == "normalized":
            assert parts[1] not in result["normalized"]
            result["normalized"][parts[1]] = [int(v) for v in parts[2:]]
        elif parts[0] == "mapping":
            result["mapping"].append([int(parts[1]), parts[2]])
        else:
            assert parts[0] in ("positions", "offsets", "corners", "identity")
            assert parts[0] not in result
            result[parts[0]] = [int(v) for v in parts[1:]]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    if not args.prepare_only and args.exe is None:
        parser.error("--exe is required for actual execution")
    args.output.mkdir(parents=True, exist_ok=False)
    reports = []
    for name, mode, high in [("triangles", 4, False), ("strip", 5, False),
                             ("fan", 6, False), ("many-channels", 4, True)]:
        document, raw, expected = fixture(mode, high_channels=high)
        source = args.output / (name + ".gltf")
        buffer = args.output / (name + ".bin")
        source.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")
        buffer.write_bytes(raw)
        (args.output / (name + ".expected.json")).write_text(
            json.dumps(expected, indent=2), encoding="utf-8")
        report = {"case": name, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                  "buffer_sha256": hashlib.sha256(raw).hexdigest(),
                  "scope": "fixture preparation only" if args.prepare_only else "actual public pipeline"}
        if not args.prepare_only:
            command = [str(args.exe.resolve()), "--fixture", str(source.resolve()), str(buffer.resolve()), str(mode)]
            report["argv"] = command
            (args.output / (name + ".command.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
            run = subprocess.run(command, capture_output=True)
            (args.output / (name + ".stdout")).write_bytes(run.stdout)
            (args.output / (name + ".stderr")).write_bytes(run.stderr)
            report["exit"] = run.returncode
            reports.append(report)
            (args.output / "results.json").write_text(json.dumps(reports, indent=2), encoding="utf-8")
            assert run.returncode == 0, (name, run.stderr)
            assert parse_output(run.stdout.decode("utf-8")) == expected, name
            report["correspondence"] = "passed"
        else:
            reports.append(report)
    (args.output / "results.json").write_text(json.dumps(reports, indent=2), encoding="utf-8")
    print(f"{len(reports)} {'prepared fixtures' if args.prepare_only else 'actual pipeline correspondences passed'}")


if __name__ == "__main__":
    main()
