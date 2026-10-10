# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 CarlYaki and Meshvale contributors.

"""Generate original glTF/GLB contract fixtures in a new directory (Python 3.10+)."""

import argparse
import base64
import copy
import json
import struct
import zlib
from pathlib import Path
from urllib.parse import quote


def png_bytes():
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"\x00\xff\x40\x80\xff"))
            + chunk(b"IEND", b""))


def serialize(document):
    text = json.dumps(document, ensure_ascii=False, indent=2)
    # Authored opaque numeric fixtures are emitted as JSON numbers, never doubles.
    for marker, number in [("__integer__", "18446744073709551616"),
                           ("__decimal__", "0.12345678901234567890123456789"),
                           ("__exponent__", "1e400")]:
        token = json.dumps(marker)
        if text.count(token) != 1:
            raise ValueError("Opaque fixture number marker must appear exactly once")
        text = text.replace(token, number)
    return text.encode("utf-8")


def glb_bytes(document, logical_buffer):
    json_bytes = serialize(document)
    json_bytes += b" " * (-len(json_bytes) % 4)
    bin_bytes = logical_buffer + b"\x00" * (-len(logical_buffer) % 4)
    length = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    return (struct.pack("<III", 0x46546C67, 2, length)
            + struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes
            + struct.pack("<II", len(bin_bytes), 0x004E4942) + bin_bytes)


def generate(output):
    document = {"asset": {"version": "2.0"}, "bufferViews": [], "accessors": []}
    buffers = [bytearray(), bytearray(), bytearray()]

    def view(data, buffer=0, target=None, stride=None):
        payload = buffers[buffer]
        payload += b"\x00" * (-len(payload) % 4)
        record = {"buffer": buffer, "byteOffset": len(payload), "byteLength": len(data)}
        if target is not None:
            record["target"] = target
        if stride is not None:
            record["byteStride"] = stride
        payload += data
        document["bufferViews"].append(record)
        return len(document["bufferViews"]) - 1

    def accessor(fmt, values, shape, component=5126, buffer=0, bounds=None,
                 target=34962, normalized=False):
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[shape]
        record = {"bufferView": view(struct.pack("<" + fmt * len(values), *values), buffer, target),
                  "componentType": component, "type": shape, "count": len(values) // width}
        if normalized:
            record["normalized"] = True
        if bounds is not None:
            record.update(min=bounds[0], max=bounds[1])
        document["accessors"].append(record)
        return len(document["accessors"]) - 1

    # Dense stride 16 and two packed sparse FLOAT/VEC3 replacements (step 12).
    base = b"".join(struct.pack("<ffff", 0, 0, 0, 99) for _ in range(3))
    position = len(document["accessors"])
    document["accessors"].append({
        "bufferView": view(base, target=34962, stride=16), "componentType": 5126,
        "type": "VEC3", "count": 3, "min": [0, 0, 0], "max": [1, 1, 0],
        "sparse": {"count": 2,
                   "indices": {"bufferView": view(bytes([1, 2])), "componentType": 5121},
                   "values": {"bufferView": view(struct.pack("<ffffff", 1, 0, 0, 0, 1, 0))}}})
    uv0 = accessor("f", [0, 0, 1, 0, 0, 1], "VEC2", buffer=1)
    uv1 = accessor("H", [0, 65535, 65535, 0, 32768, 1], "VEC2", 5123, normalized=True)
    color = accessor("B", [255, 0, 127, 255] * 3, "VEC4", 5121, normalized=True)
    joints = accessor("B", [0, 0, 0, 0] * 3, "VEC4", 5121)
    weights = accessor("f", [1, 0, 0, 0] * 3, "VEC4")
    indices = accessor("H", [0, 1, 2], "SCALAR", 5123, buffer=2, target=34963)
    morph = accessor("f", [0, 0, 0, 0, 0, 0.25, 0, 0, 0], "VEC3", bounds=([0, 0, 0], [0, 0, 0.25]))
    inverse = accessor("f", [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "MAT4", target=None)
    time = accessor("f", [0, 1], "SCALAR", bounds=([0], [1]), target=None)
    translation = accessor("f", [0, 0, 0, 0, 0, 1], "VEC3", target=None)
    morph_weight = accessor("f", [0, 1], "SCALAR", target=None)
    image = png_bytes()
    image_view = view(image)
    buffers[0] += b"\x81\x82\x83"  # Logical authored bytes, separate from GLB padding.
    buffer_uri = quote("geometry 雪.bin", safe="")
    image_uri = quote("texture 雪.png", safe="")
    document.update(
        buffers=[{"byteLength": len(buffers[0]), "uri": buffer_uri},
                 {"byteLength": len(buffers[1]), "uri": "data:application/octet-stream;base64," + base64.b64encode(buffers[1]).decode()},
                 {"byteLength": len(buffers[2]), "uri": "indices.bin"}],
        images=[{"uri": image_uri}, {"uri": "data:image/png;base64," + base64.b64encode(image).decode()},
                {"bufferView": image_view, "mimeType": "image/png"}],
        textures=[{"source": 0}, {"source": 1}, {"source": 2}],
        materials=[{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        meshes=[{"name": 'mesh "雪" \\ retained', "weights": [0.25], "primitives": [{
            "attributes": {"POSITION": position, "TEXCOORD_0": uv0, "TEXCOORD_1": uv1,
                           "COLOR_0": color, "JOINTS_0": joints, "WEIGHTS_0": weights},
            "indices": indices, "material": 0, "targets": [{"POSITION": morph}]}]}],
        nodes=[{"name": 'instance "A" \\ 雪', "mesh": 0, "skin": 0,
                "extensions": {"ACME_opaque": {"nodeRef": 2}}},
               {"name": "instance B", "mesh": 0, "skin": 0, "weights": [0.75]}, {"name": "joint 雪"}],
        scenes=[{"nodes": [0, 1, 2]}, {"nodes": [0, 2]}], scene=0,
        skins=[{"joints": [2], "skeleton": 2, "inverseBindMatrices": inverse}],
        animations=[{"samplers": [{"input": time, "output": translation}, {"input": time, "output": morph_weight}],
                     "channels": [{"sampler": 0, "target": {"node": 2, "path": "translation"}},
                                  {"sampler": 1, "target": {"node": 0, "path": "weights"}}]}],
        extensionsUsed=["ACME_opaque"], extensions={"ACME_opaque": {"sceneRef": 1, "meshRef": 0}},
        extras={"aboveUint64": "__integer__", "longDecimal": "__decimal__", "hugeExponent": "__exponent__"})
    container_document = copy.deepcopy(document)
    del container_document["buffers"][0]["uri"]
    output.mkdir(parents=True, exist_ok=False)
    (output / "scene.gltf").write_bytes(serialize(document))
    (output / "scene.glb").write_bytes(glb_bytes(container_document, bytes(buffers[0])))
    (output / "geometry 雪.bin").write_bytes(buffers[0])
    (output / "indices.bin").write_bytes(buffers[2])
    (output / "texture 雪.png").write_bytes(image)

    # Generic unused matrix accessors; these are not inverse-bind matrices.
    matrices = {"asset": {"version": "2.0"}, "buffers": [], "bufferViews": [], "accessors": []}
    matrix_data = bytearray()
    for dimension, size, component in [(2, 1, 5121), (3, 1, 5121), (3, 2, 5123)]:
        matrix_data += b"\x00" * (-len(matrix_data) % 4)
        start = len(matrix_data)
        column_step = (dimension * size + 3) // 4 * 4
        payload = bytearray(column_step * (dimension - 1) + dimension * size)
        for i in range(dimension * dimension):
            struct.pack_into("<B" if size == 1 else "<H", payload,
                             i // dimension * column_step + i % dimension * size, i + 1)
        matrix_data += payload
        matrices["bufferViews"].append({"buffer": 0, "byteOffset": start, "byteLength": len(payload)})
        matrices["accessors"].append({"bufferView": len(matrices["bufferViews"]) - 1,
                                    "componentType": component, "type": f"MAT{dimension}", "count": 1})
    matrices["buffers"] = [{"uri": "matrices.bin", "byteLength": len(matrix_data)}]
    (output / "matrices.gltf").write_text(json.dumps(matrices, indent=2), encoding="utf-8")
    (output / "matrices.bin").write_bytes(matrix_data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="New output directory; existing paths are refused")
    generate(parser.parse_args().output)
