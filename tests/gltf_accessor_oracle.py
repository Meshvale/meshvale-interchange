"""Independent struct/Decimal/Fraction oracle for actual native extraction.

Usage: python gltf_accessor_oracle.py NATIVE_TEST_EXECUTABLE NEW_OUTPUT_DIRECTORY [read]
The test driver constructs immutable snapshots through the private retention
seam by default; pass read to exercise actual ReadGltfAsset before extraction.
"""

from copy import deepcopy
from decimal import Decimal
from fractions import Fraction
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

FORMATS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
DIVISORS = {5120: 127, 5121: 255, 5122: 32767, 5123: 65535}
COMPONENT_TAGS = {5120: 0, 5121: 1, 5122: 2, 5123: 3, 5125: 4, 5126: 5}
SHAPE_TAGS = {"SCALAR": 0, "VEC2": 1, "VEC3": 2, "VEC4": 3}


def oracle(document, payload):
    def decode(index):
        accessor = document["accessors"][index]
        component = accessor["componentType"]
        fmt = FORMATS[component]
        size = struct.calcsize(fmt)
        width = COMPONENTS[accessor["type"]]
        rows = [[0] * width for _ in range(accessor["count"])]

        def read(view_index, offset):
            view = document["bufferViews"][view_index]
            address = view.get("byteOffset", 0) + offset
            return list(struct.unpack_from("<" + fmt * width, payload, address))

        if "bufferView" in accessor:
            view = document["bufferViews"][accessor["bufferView"]]
            for row in range(accessor["count"]):
                offset = accessor.get("byteOffset", 0) + row * view.get("byteStride", width * size)
                rows[row] = read(accessor["bufferView"], offset)
        destinations = []
        if "sparse" in accessor:
            sparse = accessor["sparse"]
            indices, values = sparse["indices"], sparse["values"]
            view = document["bufferViews"][indices["bufferView"]]
            selector_format = FORMATS[indices["componentType"]]
            selector_bytes = struct.calcsize(selector_format)
            for ordinal in range(sparse["count"]):
                address = view.get("byteOffset", 0) + indices.get("byteOffset", 0) + ordinal * selector_bytes
                destination, = struct.unpack_from("<" + selector_format, payload, address)
                destinations.append(destination)
                rows[destination] = read(values["bufferView"], values.get("byteOffset", 0) + ordinal * width * size)
        raw = [value for row in rows for value in row]
        normalized = []
        if accessor.get("normalized", False):
            for value in raw:
                exact = Fraction(value, DIVISORS[component])
                if component in (5120, 5122):
                    exact = max(Fraction(-1), exact)
                normalized.append(struct.unpack("<Q", struct.pack("<d", float(exact)))[0])
        result = {"R": list(struct.pack("<" + fmt * len(raw), *raw)), "N": normalized, "P": destinations}
        for key, tag in (("min", "L"), ("max", "H")):
            if key in accessor:
                values = [float(Decimal(str(v))) if component == 5126 else int(Decimal(str(v))) for v in accessor[key]]
                result[tag] = list(struct.pack("<" + fmt * len(values), *values))
        return result, raw

    primitive = document["meshes"][0]["primitives"][0]
    channels = {semantic: decode(index)[0] for semantic, index in primitive["attributes"].items()}
    for semantic, index in primitive["attributes"].items():
        accessor = document["accessors"][index]
        packed = COMPONENTS[accessor["type"]] * struct.calcsize(FORMATS[accessor["componentType"]])
        view = document.get("bufferViews", [])[accessor["bufferView"]] if "bufferView" in accessor else {}
        set_index = int(semantic.rsplit("_", 1)[1]) if semantic.startswith(("TEXCOORD_", "COLOR_")) else -1
        channels[semantic]["M"] = [index, COMPONENT_TAGS[accessor["componentType"]], SHAPE_TAGS[accessor["type"]],
                                    int(accessor.get("normalized", False)), accessor["count"], set_index,
                                    accessor.get("bufferView", -1), accessor.get("byteOffset", 0),
                                    view.get("byteStride", packed), packed]
        if "sparse" in accessor:
            sparse = accessor["sparse"]
            indices, values = sparse["indices"], sparse["values"]
            channels[semantic]["Q"] = [indices["bufferView"], indices.get("byteOffset", 0),
                                        COMPONENT_TAGS[indices["componentType"]], values["bufferView"], values.get("byteOffset", 0)]
    result = {"channels": channels}
    if "indices" in primitive:
        encoded, stream = decode(primitive["indices"])
        result["I"] = encoded["R"]
    else:
        count = document["accessors"][primitive["attributes"]["POSITION"]]["count"]
        stream = list(range(count))
    mode = primitive.get("mode", 4)
    if mode == 4:
        source = list(range(len(stream)))
    elif mode == 5:
        source = [corner for tri in range(len(stream)-2)
                  for corner in ((tri, tri+1, tri+2) if tri % 2 == 0 else (tri+1, tri, tri+2))]
    else:
        source = [corner for tri in range(len(stream)-2) for corner in (0, tri+1, tri+2)]
    result["T"] = [stream[corner] for corner in source]
    result["S"] = [value for corner in source for value in (corner, stream[corner])]
    return result


def base(count=3, mode=4):
    return {"asset": {"version": "2.0"},
            "accessors": [{"componentType": 5126, "type": "VEC3", "count": count,
                           "min": [0, 0, 0], "max": [0, 0, 0]}],
            "bufferViews": [], "buffers": [],
            "meshes": [{"primitives": [{"mode": mode, "attributes": {"POSITION": 0}}]}]}


def channel_case(component, shape="SCALAR", normalized=False, sparse=False, semantic="_RAW"):
    document = base()
    fmt = FORMATS[component]
    size = struct.calcsize(fmt)
    width = COMPONENTS[shape]
    stride = ((width * size + 3) // 4) * 4
    endpoints = {5120: [-128, 127, -1], 5121: [0, 255, 1],
                 5122: [-32768, 32767, -1], 5123: [0, 65535, 1],
                 5126: [-0.0, struct.unpack("<f", bytes([1, 0, 0, 0]))[0], 2.0]}[component]
    values = [[endpoints[row]] * width for row in range(3)]
    if semantic.startswith("TEXCOORD_") or semantic.startswith("COLOR_"):
        values = [[v] * width for v in ([0.0, 0.5, 1.0] if component == 5126 else [0, 1, endpoints[1]])]
    payload = bytearray(stride * 3)
    for row, value in enumerate(values):
        struct.pack_into("<" + fmt * width, payload, row * stride, *value)
    document["bufferViews"].append({"buffer": 0, "byteLength": len(payload), "byteStride": stride})
    accessor = {"componentType": component, "type": shape, "count": 3,
                "bufferView": 0, "normalized": normalized,
                "min": [min(row[c] for row in values) for c in range(width)],
                "max": [max(row[c] for row in values) for c in range(width)]}
    if sparse:
        selector_offset = len(payload)
        payload += bytes([1, 2])
        while len(payload) % 4: payload += bytes([222])
        values_offset = len(payload)
        replacements = values[2] + values[1]
        payload += struct.pack("<" + fmt * len(replacements), *replacements)
        document["bufferViews"] += [{"buffer": 0, "byteOffset": selector_offset, "byteLength": 2},
                                    {"buffer": 0, "byteOffset": values_offset, "byteLength": len(replacements)*size}]
        accessor["sparse"] = {"count": 2, "indices": {"bufferView": 1, "componentType": 5121},
                              "values": {"bufferView": 2}}
    document["accessors"].append(accessor)
    document["meshes"][0]["primitives"][0]["attributes"][semantic] = 1
    document["buffers"] = [{"uri": "fixture.bin", "byteLength": len(payload)}]
    return document, bytes(payload)


def cases():
    generator = random.Random(0x4E4F524D)
    for component, minimum, maximum in (
        (5120, -128, 127), (5121, 0, 255),
        (5122, -32768, 32767), (5123, 0, 65535)):
        for sample in range(20):
            doc, original = channel_case(component, normalized=True)
            data = bytearray(original)
            values = [generator.randint(minimum, maximum) for _ in range(3)]
            for row, value in enumerate(values):
                struct.pack_into("<" + FORMATS[component], data, row * 4, value)
            doc["accessors"][1].update({"min": [min(values)], "max": [max(values)]})
            yield f"normalized-fraction-{component}-{sample}", doc, bytes(data), 0
    for component in (5120, 5121, 5122, 5123, 5126):
        for sparse in (False, True):
            for normalized in ((False, True) if component != 5126 else (False,)):
                doc, data = channel_case(component, normalized=normalized, sparse=sparse)
                yield f"raw-{component}-{sparse}-{normalized}", doc, data, 0
    for semantic, shape in (("TEXCOORD_0", "VEC2"), ("COLOR_0", "VEC3"), ("COLOR_0", "VEC4")):
        for component in (5121, 5123, 5126):
            for sparse in (False, True):
                doc, data = channel_case(component, shape, component != 5126, sparse, semantic)
                yield f"{semantic}-{shape}-{component}-{sparse}", doc, data, 0
    for mode in (4, 5, 6):
        for component in (5121, 5123, 5125):
            doc = base(6, mode)
            stream = [0, 2, 2, 4, 1, 5]
            fmt = FORMATS[component]
            data = struct.pack("<" + fmt * len(stream), *stream)
            doc["buffers"] = [{"uri": "fixture.bin", "byteLength": len(data)}]
            doc["bufferViews"] = [{"buffer": 0, "byteLength": len(data)}]
            doc["accessors"].append({"componentType": component, "type": "SCALAR", "count": 6, "bufferView": 0})
            doc["meshes"][0]["primitives"][0]["indices"] = 1
            yield f"indices-{mode}-{component}", doc, data, 0
            restart = struct.pack("<" + fmt, {5121: 255, 5123: 65535, 5125: 4294967295}[component])
            yield f"restart-{mode}-{component}", doc, restart + data[len(restart):], 3
            for selector_component in (5121, 5123, 5125):
                for with_base in (False, True):
                    sparse_doc = deepcopy(doc)
                    payload = bytearray(bytes(len(data)) if with_base else b"")
                    sparse_doc["bufferViews"] = ([{"buffer": 0, "byteLength": len(data)}] if with_base else [])
                    source = sparse_doc["accessors"][1]
                    if not with_base: source.pop("bufferView")
                    while len(payload) % 4: payload += bytes([99])
                    selector_offset = len(payload)
                    selectors = struct.pack("<" + FORMATS[selector_component]*5, 1, 2, 3, 4, 5)
                    payload += selectors
                    selector_view = len(sparse_doc["bufferViews"])
                    sparse_doc["bufferViews"].append({"buffer": 0, "byteOffset": selector_offset, "byteLength": len(selectors)})
                    while len(payload) % 4: payload += bytes([99])
                    value_offset = len(payload)
                    replacements = struct.pack("<" + fmt*5, *stream[1:])
                    payload += replacements
                    value_view = len(sparse_doc["bufferViews"])
                    sparse_doc["bufferViews"].append({"buffer": 0, "byteOffset": value_offset, "byteLength": len(replacements)})
                    source["sparse"] = {"count": 5,
                        "indices": {"bufferView": selector_view, "componentType": selector_component},
                        "values": {"bufferView": value_view}}
                    source["min"], source["max"] = [0], [5]
                    sparse_doc["buffers"][0]["byteLength"] = len(payload)
                    yield f"indices-sparse-{mode}-{component}-{selector_component}-{with_base}", sparse_doc, bytes(payload), 0
    # Fraction establishes the profile boundary from actual binary32 values.
    for bits in (0x3f80003f, 0x3f800040, 0x3f7fff80, 0x3f7fff7f):
        value, = struct.unpack("<f", struct.pack("<I", bits))
        doc, data = channel_case(5126, "VEC3", semantic="NORMAL")
        doc["accessors"][1].pop("min")
        doc["accessors"][1].pop("max")
        data = struct.pack("<fff", value, 0, 0) * 3
        exact_square = Fraction(value) ** 2
        accepted = abs(exact_square - 1) <= Fraction(1, 65536)
        yield f"normal-boundary-{bits}", doc, data, 0 if accepted else 4
    doc, data = channel_case(5126, "VEC3", semantic="COLOR_0")
    yield "color-outside-range", doc, struct.pack("<f", 1.5) + data[4:], 3
    doc, data = channel_case(5126)
    yield "nonfinite", doc, struct.pack("<I", 0x7f800000) + data[4:], 3
    # Fully populated animation relationships let the public reader reach the
    # extractor's global usage check, rather than fail missing metadata first.
    doc, data = channel_case(5126)
    doc["nodes"] = [{}]
    doc["accessors"].append({"componentType": 5126, "type": "VEC3", "count": 3})
    doc["animations"] = [{"samplers": [{"input": 1, "output": 2}],
                          "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]}]
    yield "complete-animation-accessor-role", doc, data, 3
    doc, data = channel_case(5126, "VEC3")
    doc["nodes"] = [{}]
    doc["accessors"].append({"componentType": 5126, "type": "SCALAR", "count": 3, "bufferView": 0})
    doc["accessors"].append({"componentType": 5126, "type": "VEC3", "count": 3})
    doc["animations"] = [{"samplers": [{"input": 2, "output": 3}],
                          "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]}]
    yield "complete-animation-shared-view-role", doc, data, 3
    doc, data = channel_case(5121, "VEC2", True, True, "TEXCOORD_0")
    multiple = deepcopy(doc)
    multiple["meshes"][0]["primitives"][0]["attributes"].update({"TEXCOORD_1": 1, "TEXCOORD_2": 1})
    yield "three-complete-uv-sets", multiple, data, 0
    for name, modification in (
        ("duplicate-sparse", lambda d, b: (d, b[:12] + bytes([1, 1]) + b[14:])),
        ("descending-sparse", lambda d, b: (d, b[:12] + bytes([2, 1]) + b[14:])),
        ("out-of-range-sparse", lambda d, b: (d, b[:12] + bytes([1, 3]) + b[14:]))):
        yield name, *modification(deepcopy(doc), data), 3
    for name, mutate in (
        ("uv-gap", lambda d: d["meshes"][0]["primitives"][0]["attributes"].update({"TEXCOORD_2": 1})),
        ("uv-leading-zero", lambda d: d["meshes"][0]["primitives"][0]["attributes"].update({"TEXCOORD_01": 1})),
        ("raw-integer-uv", lambda d: d["accessors"][1].update({"normalized": False})),
        ("sparse-view-stride", lambda d: d["bufferViews"][2].update({"byteStride": 4})),
        ("sparse-view-target", lambda d: d["bufferViews"][2].update({"target": 34962})),
        ("different-count", lambda d: d["accessors"][1].update({"count": 2})),
        ("mixed-accessor-role", lambda d: d["meshes"][0]["primitives"].append({"attributes": {"POSITION": 0}, "indices": 1})),
        ("animation-view-role", lambda d: d.update({"animations": [{"samplers": [{"input": 1, "output": 1}]}]}))):
        changed = deepcopy(doc); mutate(changed)
        yield name, changed, data, 3


def main():
    executable, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=False)
    observations = []
    for name, document, payload, expected_status in cases():
        source, binary = output / f"{name}.json", output / f"{name}.bin"
        source.write_text(json.dumps(document), encoding="utf-8")
        binary.write_bytes(payload)
        request = "triangles" if document["meshes"][0]["primitives"][0].get("mode", 4) == 4 else "convert"
        command = [str(executable), str(source), str(binary), request]
        if len(sys.argv) == 4:
            if sys.argv[3] != "read": raise ValueError("Unknown capture mode")
            command.append("read")
        run = subprocess.run(command, capture_output=True, text=True)
        (output / f"{name}.stdout").write_text(run.stdout, encoding="utf-8")
        (output / f"{name}.stderr").write_text(run.stderr, encoding="utf-8")
        if run.returncode: raise AssertionError((name, run.returncode, run.stderr))
        lines = run.stdout.splitlines()
        status = int(lines[0].split()[1])
        if status != expected_status: raise AssertionError((name, status, expected_status))
        if status == 0:
            actual = {"channels": {}}
            target = actual
            for line in lines[1:]:
                tag, *values = line.split()
                if tag == "C":
                    target = actual["channels"].setdefault(values[0], {})
                else:
                    if tag in ("I", "T", "S"): target = actual
                    target[tag] = list(map(int, values))
            expected = oracle(document, payload)
            if actual != expected: raise AssertionError((name, actual, expected))
        observations.append({"name": name, "status": status, "returncode": run.returncode})
    (output / "results.json").write_text(json.dumps(observations, indent=2), encoding="utf-8")
    print(f"Independent extraction oracle passed: {len(observations)} cases")


if __name__ == "__main__":
    main()
