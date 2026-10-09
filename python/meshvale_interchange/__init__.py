# SPDX-License-Identifier: Apache-2.0
"""Owned OBJ documents, resources and verified output bundles."""
from . import _interchange
from ._version import __version__
from dataclasses import dataclass, replace
import os
from pathlib import Path
from meshvale_geometry import Mesh

Cancellation = _interchange.Cancellation


def _require(value, kind):
    if not isinstance(value, kind):
        raise TypeError(f"expected {kind.__name__}")
    return value


def _tuple(value, kind):
    if isinstance(value, (str, bytes)):
        raise TypeError("expected iterable of elements")
    return tuple(_require(item, kind) for item in value)


def _binary(value):
    if not isinstance(value, (bytes, bytearray, memoryview)):
        raise TypeError("expected bytes-like value")
    return bytes(value)


def _path(value):
    value = os.fspath(value)
    if "\0" in os.fsdecode(value):
        raise ValueError("path contains NUL")
    return Path(os.fsdecode(value))


@dataclass(frozen=True)
class ObjPart:
    object: str = ""
    groups: tuple[str, ...] = ()

    def __post_init__(self):
        _require(self.object, str)
        object.__setattr__(self, "groups", _tuple(self.groups, str))


@dataclass(frozen=True)
class ObjDocument:
    mesh: Mesh
    parts: tuple[ObjPart, ...] = ()
    material_names: tuple[str, ...] = ()
    material_library: bytes = b""

    def __post_init__(self):
        _require(self.mesh, Mesh)
        object.__setattr__(self, "parts", _tuple(self.parts, ObjPart))
        object.__setattr__(self, "material_names", _tuple(self.material_names, str))
        object.__setattr__(self, "material_library", _binary(self.material_library))

    def with_mesh(self, mesh):
        return replace(self, mesh=mesh)


@dataclass(frozen=True)
class ObjResource:
    path: Path
    bytes: bytes

    def __post_init__(self):
        object.__setattr__(self, "path", _path(self.path))
        object.__setattr__(self, "bytes", _binary(self.bytes))


@dataclass(frozen=True)
class ObjFileAsset:
    document: ObjDocument
    obj_path: Path
    material_libraries: tuple[Path, ...] = ()
    resources: tuple[ObjResource, ...] = ()

    def __post_init__(self):
        _require(self.document, ObjDocument)
        object.__setattr__(self, "obj_path", _path(self.obj_path))
        if isinstance(self.material_libraries, (str, bytes)):
            raise TypeError("expected iterable of paths")
        object.__setattr__(self, "material_libraries", tuple(_path(item) for item in self.material_libraries))
        object.__setattr__(self, "resources", _tuple(self.resources, ObjResource))

    def with_mesh(self, mesh):
        return replace(self, document=self.document.with_mesh(mesh))


@dataclass(frozen=True)
class ObjImportResult:
    document: ObjDocument | None
    diagnostics: tuple[dict, ...]


@dataclass(frozen=True)
class ObjText:
    obj: str
    mtl: bytes


@dataclass(frozen=True)
class ObjExportResult:
    text: ObjText | None
    diagnostics: tuple[dict, ...]


@dataclass(frozen=True)
class ObjFileResult:
    asset: ObjFileAsset | None
    diagnostics: tuple[dict, ...]


@dataclass(frozen=True)
class ObjBundleResult:
    outcome: str
    phase: str
    entry: Path | None
    diagnostics: tuple[dict, ...]


def _document_record(value):
    _require(value, ObjDocument)
    return {"mesh_record": value.mesh.to_record(),
            "parts": [{"object": p.object, "groups": list(p.groups)} for p in value.parts],
            "material_names": list(value.material_names), "material_library": value.material_library}


def _document(value):
    return ObjDocument(Mesh.from_record(value["mesh_record"]),
                       tuple(ObjPart(p["object"], p["groups"]) for p in value["parts"]),
                       value["material_names"], value["material_library"])


def _resource_record(value):
    _require(value, ObjResource)
    return {"path": value.path, "bytes": value.bytes}


def _asset_record(value):
    _require(value, ObjFileAsset)
    return {"document": _document_record(value.document), "obj_path": value.obj_path,
            "material_libraries": list(value.material_libraries),
            "resources": [_resource_record(r) for r in value.resources]}


def read_obj(obj, mtl=b""):
    result = _interchange.read_text(obj, mtl)
    return ObjImportResult(None if result["document"] is None else _document(result["document"]),
                           tuple(result["diagnostics"]))


def write_obj(document):
    result = _interchange.write_text(_document_record(document))
    text = None if result["text"] is None else ObjText(**result["text"])
    return ObjExportResult(text, tuple(result["diagnostics"]))


def read_obj_file(input, *, resource_root=None, cancellation=None):
    result = _interchange.read_file(input, resource_root, cancellation)
    value = result["asset"]
    asset = None if value is None else ObjFileAsset(_document(value["document"]), value["obj_path"],
        value["material_libraries"], tuple(ObjResource(**r) for r in value["resources"]))
    return ObjFileResult(asset, tuple(result["diagnostics"]))


def publish_obj_bundle(asset, destination, *, cancellation=None, on_phase=None, supplemental_files=()):
    record = _asset_record(asset)
    supplemental = [_resource_record(r) for r in supplemental_files]
    success = ObjBundleResult("published", "publication", asset.obj_path, ())
    result = _interchange.publish(record, destination, cancellation, on_phase, supplemental)
    if result["outcome"] == "published":
        return success
    return ObjBundleResult(result["outcome"], result["phase"], result["entry"], tuple(result["diagnostics"]))


__all__ = ["ObjPart", "ObjDocument", "ObjResource", "ObjFileAsset", "ObjImportResult", "ObjText",
           "ObjExportResult", "ObjFileResult", "ObjBundleResult", "Cancellation", "read_obj",
           "write_obj", "read_obj_file", "publish_obj_bundle", "__version__"]
