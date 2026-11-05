# SPDX-License-Identifier: Apache-2.0
"""Literal caller records and independently decoded Surface payloads."""
from array import array
from dataclasses import replace
import json
import struct

from meshvale_interchange import surface_files as sf


ENCODINGS = ("gltf-json", "glb", "stl-ascii", "stl-binary")
REPRESENTATIONS = ("strict-bare", "annotated-bundle-v1")
POSITIONS = (-0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
BINARY_HEADER = bytes(range(80))
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d494844520000000200000002080600000072b60d24"
    "000000184944415478da63f8cfc0f0ff7f03c37f860610e33fc37f004ccf08f9"
    "7caa21c60000000049454e44ae426082")
CATALOG = (
    b'{"asset":{"version":"2.0"},"samplers":[{"magFilter":9728,"minFilter":9728,'
    b'"wrapS":10497,"wrapT":10497}],"images":[{"uri":"albedo.png",'
    b'"mimeType":"image/png","name":"literal-png"}],"textures":[{"sampler":0,"source":0}],'
    b'"materials":[{"name":"literal-material","pbrMetallicRoughness":{'
    b'"baseColorFactor":[1,1,1,1],"baseColorTexture":{"index":0,"texCoord":2},'
    b'"metallicFactor":0,"roughnessFactor":1},"alphaMode":"OPAQUE","doubleSided":false}]}')


def record(normals=False):
    attributes = []
    if normals:
        attributes.append(dict(
            domain="corner", name="authored-normal", semantic="NORMAL",
            set_index=None, components=3, scalar_type="float32",
            values=array("f", [0, 0, 1] * 3), offsets=None, present=None,
            metadata={}))
    return dict(schema="meshvale.mesh/1", positions=array("d", POSITIONS),
                face_offsets=array("Q", [0, 3]),
                corner_vertices=array("Q", [0, 1, 2]), attributes=attributes)


def options(encoding="stl-binary", representation="strict-bare"):
    stl = encoding.startswith("stl-")
    return sf.SurfaceOptions(
        encoding=encoding, representation=representation,
        acknowledge_triangle_conversion=True, acknowledge_corner_expansion=True,
        acknowledge_unused_vertices=False,
        channels=(sf.SurfaceChannel(0, "stl-corner-normal", False),) if stl else (),
        positions="exact64" if encoding == "stl-ascii" else "exact32",
        max_position_linf_error=0.0, max_uv_component_errors=(),
        coordinates="stl-units-unspecified" if stl else "declared-meters-right-handed-y-up",
        uv=(), materials=(), catalog_uv=(), corner_normal_attribute=0 if stl else None,
        declared_unit="mm" if stl else "",
        ascii_name="literal-normal" if encoding == "stl-ascii" else "",
        binary_header=BINARY_HEADER if encoding == "stl-binary" else b"")


def catalog_record():
    source = record()
    source["attributes"] = [
        dict(domain="vertex", name="uv5", semantic="TEXCOORD", set_index=5,
             components=2, scalar_type="float32", values=array("f", [0, 0, 1, 0, 0, 1]),
             offsets=None, present=None, metadata={}),
        dict(domain="face", name="material_id", semantic="MATERIAL_ID", set_index=None,
             components=1, scalar_type="int32", values=array("i", [-7]),
             offsets=None, present=None, metadata={})]
    return source


def catalog_options(encoding):
    return replace(options(encoding, "annotated-bundle-v1"),
                   channels=(sf.SurfaceChannel(0, "uv", False),
                             sf.SurfaceChannel(1, "material-id", False)),
                   max_uv_component_errors=(0.0,), uv=(sf.SurfaceUv(0, 5, 0),),
                   materials=(sf.SurfaceMaterial(-7, 0),),
                   catalog_uv=(sf.SurfaceCatalogUv(2, 5),))


def gltf_payload(encoding, members):
    if encoding == "gltf-json":
        wire, = (value for name, value in members.items() if name.endswith(".gltf"))
        return json.loads(wire), None
    wire, = (value for name, value in members.items() if name.endswith(".glb"))
    assert wire[:4] == b"glTF" and struct.unpack_from("<II", wire, 4) == (2, len(wire))
    size, kind = struct.unpack_from("<II", wire, 12)
    assert kind == 0x4E4F534A
    document = json.loads(wire[20:20 + size])
    bin_offset = 20 + size
    length, kind = struct.unpack_from("<II", wire, bin_offset)
    assert kind == 0x004E4942
    return document, wire[bin_offset + 8:bin_offset + 8 + length]


def payload_positions(encoding, members):
    """Decode actual protocol fields without using a Meshvale reader."""
    if encoding == "stl-binary":
        wire, = (value for name, value in members.items() if name.endswith(".stl"))
        assert wire[:80] == BINARY_HEADER
        assert len(wire) == 134 and struct.unpack_from("<I", wire, 80) == (1,)
        fields = struct.unpack_from("<12fH", wire, 84)
        assert fields[:3] == (0.0, 0.0, 1.0) and fields[-1] == 0
        return fields[3:12]
    if encoding == "stl-ascii":
        wire, = (value for name, value in members.items() if name.endswith(".stl"))
        lines = wire.decode("ascii").splitlines()
        assert lines[0].split() == ["solid", "literal-normal"]
        normal, = (line.split()[2:] for line in lines if line.strip().startswith("facet normal"))
        assert tuple(map(float, normal)) == (0.0, 0.0, 1.0)
        vertices = [line.split()[1:] for line in lines if line.strip().startswith("vertex ")]
        assert len(vertices) == 3
        return tuple(float(value) for vertex in vertices for value in vertex)
    document, buffer = gltf_payload(encoding, members)
    primitive = document["meshes"][0]["primitives"][0]
    accessor = document["accessors"][primitive["attributes"]["POSITION"]]
    view = document["bufferViews"][accessor["bufferView"]]
    if buffer is None:
        buffer = members[document["buffers"][view["buffer"]]["uri"]]
    assert accessor["componentType"] == 5126 and accessor["type"] == "VEC3"
    assert accessor["count"] == 3 and primitive.get("mode", 4) == 4
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", 12)
    return tuple(value for vertex in range(3)
                 for value in struct.unpack_from("<3f", buffer, start + stride * vertex))
