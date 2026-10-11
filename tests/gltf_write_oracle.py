# SPDX-License-Identifier: Apache-2.0
"""Original byte/Decimal/struct oracle for public Read -> Write -> Read.

Usage: python gltf_write_oracle.py DRIVER NEW_OUTPUT_DIRECTORY
The native driver owns emission and actual reader reload; this oracle computes
expected document tokens, resources, graph identities and raw numerical bits.
"""
import base64
from copy import deepcopy
from decimal import Decimal
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

spec = importlib.util.spec_from_file_location("accessor_reference", Path(__file__).with_name("gltf_accessor_oracle.py"))
accessor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(accessor)
NUMBER = re.compile(rb"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?")
PNG = bytes([137, 80, 78, 71, 13, 10, 26, 10])
JPEG = bytes([255, 216, 255, 217])


def tokens(source):
    result, at = [], 0
    while at < len(source):
        if source[at] == 34:
            at += 1
            while source[at] != 34:
                if source[at] == 92: at += 1
                at += 1
            at += 1
        elif source[at] == 45 or 48 <= source[at] <= 57:
            match = NUMBER.match(source, at)
            result.append([str(at), str(match.end() - at), match.group().decode("ascii")])
            at = match.end()
        else: at += 1
    return result


def graph(document):
    nodes, scenes = document.get("nodes", []), document.get("scenes", [])
    parents = {child: index for index, node in enumerate(nodes) for child in node.get("children", [])}
    records = {"V": [], "S": [], "P": [], "I": []}
    pairs = [(mesh, primitive) for mesh, value in enumerate(document.get("meshes", [])) for primitive in range(len(value["primitives"]))]
    for index, (mesh, primitive) in enumerate(pairs): records["P"].append([str(index), str(mesh), str(primitive)])
    for index, node in enumerate(nodes):
        values, kind, mask = [0.0]*16, 0, 0
        for slot in (6, 7, 8, 9): values[slot] = 1.0
        if "matrix" in node: values, kind, mask = node["matrix"], 2, 8
        else:
            for field, offset, flag in (("translation", 0, 1), ("rotation", 3, 2), ("scale", 7, 4)):
                if field in node:
                    kind, mask = 1, mask | flag
                    values[offset:offset+len(node[field])] = node[field]
        bits = [struct.unpack("<I", struct.pack("<f", float(value)))[0] for value in values]
        children = node.get("children", [])
        records["V"].append(list(map(str, [index, parents.get(index, -1), node.get("mesh", -1), node.get("skin", -1), kind, mask])) +
                            [node.get("name", "").encode().hex() or "-", str(len(children))] + list(map(str, children + bits)))
    for index, scene in enumerate(scenes):
        roots = scene.get("nodes", [])
        records["S"].append([str(index), scene.get("name", "").encode().hex() or "-", str(len(roots))] + list(map(str, roots)))
        stack = list(reversed(roots))
        while stack:
            node_index = stack.pop()
            node = nodes[node_index]
            if "mesh" in node:
                for mesh, primitive in pairs:
                    if mesh == node["mesh"]:
                        records["I"].append(list(map(str, [len(records["I"]), index, node_index, mesh, primitive])))
            stack += list(reversed(node.get("children", [])))
    return records


def resources(document, bindings):
    output, buffers = [], []
    for index, value in enumerate(document.get("buffers", [])):
        uri = value["uri"]
        embedded = uri.startswith("data:")
        data = base64.b64decode(uri.split(",", 1)[1], validate=True) if embedded else bindings[("buffer", index)]
        buffers.append(data)
        output.append((["1", str(index), str(int(embedded)), str(value["byteLength"]), str(len(data)), "-1", "-1", uri.encode().hex()], data))
    for index, value in enumerate(document.get("images", [])):
        uri = value.get("uri", "")
        buffer, view = -1, -1
        if "bufferView" in value:
            view = value["bufferView"]
            layout = document["bufferViews"][view]
            buffer = layout["buffer"]
            start = layout.get("byteOffset", 0)
            data = buffers[buffer][start:start+layout["byteLength"]]
            origin = 2
        elif uri.startswith("data:"):
            data, origin = base64.b64decode(uri.split(",", 1)[1], validate=True), 1
        else: data, origin = bindings[("image", index)], 0
        output.append((["2", str(index), str(origin), str(len(data)), str(len(data)), str(buffer), str(view), uri.encode().hex() or "-"], data))
    return output


def cases():
    admitted = [(name, doc, data) for name, doc, data, status in accessor.cases()
                if status == 0 and doc["meshes"][0]["primitives"][0].get("mode", 4) == 4]
    for ordinal, (name, doc, payload) in enumerate(admitted):
        document = deepcopy(doc)
        document["extras"] = {"huge": "@@HUGE@@", "zero": "@@ZERO@@", "decimal": "@@DECIMAL@@", "unicode": "Δ😀", "integer": 9007199254740993, "array": [False, None, 1]}
        document["nodes"] = [{"mesh": 0, "name": "Δ😀", "translation": [1, -0.0, 3], "scale": [0, 2, 3]},
                             {"mesh": 0}, {"children": [0, 1]},
                             {"name": "unused", "matrix": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]}]
        document["scenes"] = [{"name": "shared", "nodes": [2]}, {"nodes": [2]}]
        bindings = {("buffer", 0): payload}
        yield name, document, bindings, payload, False, ordinal % 2 == 0
        if ordinal % 12 == 0:
            richer = deepcopy(document)
            richer["extensionsUsed"] = ["VENDOR_keep"]
            richer["extensions"] = {"VENDOR_keep": {"value": "@@HUGE@@", "reference": 9007199254740995}}
            richer["skins"] = [{"joints": [0, 1], "skeleton": 2}]
            richer["materials"] = [{"normalTexture": {"index": 0}}]
            richer["textures"] = [{"source": 0}]
            richer["buffers"].append({"uri": "image.bin", "byteLength": 8})
            richer["buffers"].append({"uri": "data:application/octet-stream;base64,AQIDBA==", "byteLength": 4})
            view = len(richer["bufferViews"])
            richer["bufferViews"].append({"buffer": 1, "byteLength": 8})
            richer["images"] = [{"bufferView": view, "mimeType": "image/png"},
                                 {"uri": "image.bin", "mimeType": "image/jpeg"},
                                 {"uri": "data:image/png;base64,iVBORw0KGgo="}]
            rich_bindings = dict(bindings)
            rich_bindings[("buffer", 1)] = PNG + bytes([42, 43, 44])
            rich_bindings[("image", 1)] = JPEG
            yield name + "-opaque-images-skin", richer, rich_bindings, payload, True, False


def main():
    executable, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=False)
    observations = []
    for name, original, bindings, payload, opaque, escaped in cases():
        source = (" \n" + json.dumps(original, ensure_ascii=escaped, indent=2) + "\r\n")
        for placeholder, value in (("HUGE", "1e400"), ("ZERO", "-0.0"), ("DECIMAL", "1.00e+01")):
            source = source.replace('"@@' + placeholder + '@@"', value)
        source = source.encode("utf-8")
        document = json.loads(source, parse_float=Decimal)
        folder = output / name
        folder.mkdir()
        source_path, prefix = folder / "source.json", folder / "emitted"
        source_path.write_bytes(source)
        command = [str(executable), str(source_path), str(prefix)]
        originals = {source_path: hashlib.sha256(source).hexdigest()}
        for (kind, index), data in bindings.items():
            path = folder / f"{kind}-{index}.bin"
            path.write_bytes(data)
            originals[path] = hashlib.sha256(data).hexdigest()
            command += [kind, str(index), str(path)]
        run = subprocess.run(command, capture_output=True, text=True)
        (folder / "stdout").write_text(run.stdout, encoding="utf-8")
        (folder / "stderr").write_text(run.stderr, encoding="utf-8")
        if run.returncode: raise AssertionError((name, run.returncode, run.stderr))
        assert all(hashlib.sha256(path.read_bytes()).hexdigest() == digest for path, digest in originals.items()), name
        assert prefix.with_suffix(".json").read_bytes() == source, name
        assert json.loads(prefix.with_suffix(".json").read_bytes(), parse_float=Decimal) == document, name
        actual = {tag: [] for tag in ("R", "L", "V", "S", "P", "I", "X", "A")}
        for line in run.stdout.splitlines():
            tag, *values = line.split()
            actual[tag].append(values)
        expected_resources = resources(document, bindings)
        assert actual["R"] == [record for record, data in expected_resources], name
        for ordinal, (_, data) in enumerate(expected_resources):
            assert Path(str(prefix) + f".resource-{ordinal}").read_bytes() == data, name
        assert actual["L"] == tokens(source), name
        for tag, expected in graph(document).items(): assert actual[tag] == expected, (name, tag)
        assert actual["X"] == [["0", "4" if opaque else "0"]], (name, "processing capability")
        if opaque: assert actual["A"] == [], name
        else:
            expected = accessor.oracle(original, payload)["channels"]
            attributes = document["meshes"][0]["primitives"][0]["attributes"]
            rows = []
            for semantic, index in attributes.items():
                value = document["accessors"][index]
                reference = expected[semantic]
                rows.append(["0", semantic.encode().hex(), str(accessor.COMPONENT_TAGS[value["componentType"]]), str(accessor.SHAPE_TAGS[value["type"]]),
                             str(int(value.get("normalized", False))), str(value["count"]), bytes(reference["R"]).hex(), str(len(reference["N"]))] + list(map(str, reference["N"])))
            assert actual["A"] == rows, (name, "raw and normalized bits")
        observations.append({"name": name, "json_bytes": len(source), "resources": len(expected_resources), "returncode": run.returncode})
    (output / "results.json").write_text(json.dumps(observations, indent=2), encoding="utf-8")
    print(f"Independent retained writer oracle passed: {len(observations)} cases")


if __name__ == "__main__": main()
