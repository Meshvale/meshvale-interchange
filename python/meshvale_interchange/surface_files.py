# SPDX-License-Identifier: Apache-2.0
"""Explicit immutable configuration for the development surface interface.

The native operation and installed qualification are still under development.
These values do not materialize a mesh, create an inspection proof or publish.
"""
from dataclasses import dataclass, field
from importlib import import_module
from importlib.resources import files
import json
import math
import os
import sys
from types import MappingProxyType


_UINT32_MAX = (1 << 32) - 1
_UINT64_MAX = (1 << 64) - 1
_INDEX_MAX = min(_UINT64_MAX, sys.maxsize)
_INT32_MIN, _INT32_MAX = -(1 << 31), (1 << 31) - 1
_ENCODINGS = ("gltf-json", "glb", "stl-ascii", "stl-binary")
_REPRESENTATIONS = ("strict-bare", "annotated-bundle-v1")
_ROLES = ("bundle-only", "uv", "stl-corner-normal", "material-id")


def _integer(value, minimum=0, maximum=_UINT64_MAX):
    if type(value) is not int:
        raise TypeError("expected a built-in integer, not bool")
    if not minimum <= value <= maximum:
        raise OverflowError("integer is outside the declared native range")
    return value


def _text(value):
    if type(value) is not str:
        raise TypeError("expected a built-in string")
    for index, character in enumerate(value):
        if 0xD800 <= ord(character) <= 0xDFFF:
            raise UnicodeEncodeError("utf-8", value, index, index + 1,
                                     "surrogates are not permitted")
    return value


def _choice(value, choices):
    if type(value) is not str:
        raise TypeError("expected a built-in string")
    if value not in choices:
        raise ValueError("unsupported explicit surface selector")
    return value


def _boolean(value):
    if type(value) is not bool:
        raise TypeError("expected bool")


def _bound(value):
    if type(value) is not float:
        raise TypeError("surface error bounds require a built-in float")
    if not math.isfinite(value) or value < 0:
        raise ValueError("surface error bounds must be finite and nonnegative")


def _rows(value, row_type):
    if type(value) is not tuple or any(type(row) is not row_type for row in value):
        raise TypeError("surface rows require a tuple of the declared frozen row type")


@dataclass(frozen=True, slots=True)
class SurfaceChannel:
    source_attribute: int
    role: str
    acknowledge_ordinary_view_loss: bool

    def __post_init__(self):
        _integer(self.source_attribute, maximum=_INDEX_MAX)
        _choice(self.role, _ROLES)
        _boolean(self.acknowledge_ordinary_view_loss)


@dataclass(frozen=True, slots=True)
class SurfaceUv:
    source_attribute: int
    source_set: int
    wire_set: int

    def __post_init__(self):
        _integer(self.source_attribute, maximum=_INDEX_MAX)
        _integer(self.source_set, maximum=_UINT32_MAX)
        _integer(self.wire_set, maximum=_UINT32_MAX)


@dataclass(frozen=True, slots=True)
class SurfaceMaterial:
    source_id: int
    catalog_material: int

    def __post_init__(self):
        _integer(self.source_id, _INT32_MIN, _INT32_MAX)
        _integer(self.catalog_material, maximum=_INDEX_MAX)


@dataclass(frozen=True, slots=True)
class SurfaceCatalogUv:
    catalog_set: int
    source_set: int

    def __post_init__(self):
        _integer(self.catalog_set, maximum=_UINT32_MAX)
        _integer(self.source_set, maximum=_UINT32_MAX)


@dataclass(frozen=True, slots=True, kw_only=True)
class SurfaceOptions:
    encoding: str
    representation: str
    acknowledge_triangle_conversion: bool
    acknowledge_corner_expansion: bool
    acknowledge_unused_vertices: bool
    channels: tuple
    positions: str
    max_position_linf_error: float
    max_uv_component_errors: tuple
    coordinates: str
    uv: tuple
    materials: tuple
    catalog_uv: tuple
    corner_normal_attribute: int | None
    declared_unit: str
    ascii_name: str
    binary_header: bytes

    def __post_init__(self):
        _choice(self.encoding, _ENCODINGS)
        _choice(self.representation, _REPRESENTATIONS)
        for value in (self.acknowledge_triangle_conversion,
                      self.acknowledge_corner_expansion,
                      self.acknowledge_unused_vertices):
            _boolean(value)
        if not self.acknowledge_triangle_conversion or not self.acknowledge_corner_expansion:
            raise ValueError("triangle conversion and corner expansion require acknowledgment")
        _rows(self.channels, SurfaceChannel)
        if any(row.source_attribute != ordinal for ordinal, row in enumerate(self.channels)):
            raise ValueError("channel dispositions must be in complete source ordinal order")
        _choice(self.positions, ("exact32", "bounded-rne32", "exact64"))
        _bound(self.max_position_linf_error)
        if type(self.max_uv_component_errors) is not tuple:
            raise TypeError("UV error bounds require a tuple")
        for value in self.max_uv_component_errors:
            _bound(value)
        _rows(self.uv, SurfaceUv)
        _rows(self.materials, SurfaceMaterial)
        _rows(self.catalog_uv, SurfaceCatalogUv)
        for value in (self.declared_unit, self.ascii_name):
            _text(value)
        if type(self.binary_header) is not bytes:
            raise TypeError("binary STL header requires bytes")
        if self.encoding in ("gltf-json", "glb"):
            _choice(self.coordinates, ("declared-meters-right-handed-y-up",))
            if self.positions == "exact64":
                raise ValueError("exact64 positions are available only for ASCII STL")
            if self.corner_normal_attribute is not None or self.declared_unit or self.ascii_name or self.binary_header:
                raise ValueError("glTF requires canonical inactive STL fields")
            if len(self.max_uv_component_errors) != len(self.uv):
                raise ValueError("each explicit UV binding requires one error bound")
        else:
            _choice(self.coordinates, ("stl-units-unspecified",))
            _integer(self.corner_normal_attribute, maximum=_INDEX_MAX)
            if self.uv or self.materials or self.catalog_uv or self.max_uv_component_errors:
                raise ValueError("STL requires inactive UV and material fields")
            if self.encoding == "stl-ascii":
                if self.binary_header:
                    raise ValueError("ASCII STL requires an empty binary header")
                if any(not 32 <= ord(c) <= 126 for c in self.ascii_name):
                    raise ValueError("ASCII STL name requires printable ASCII")
            else:
                if self.positions == "exact64" or self.ascii_name or len(self.binary_header) != 80:
                    raise ValueError("binary STL requires exact32/bounded positions, empty ASCII name and 80 header bytes")


@dataclass(frozen=True, slots=True)
class SurfaceResource:
    kind: str
    table_index: int
    bytes: object

    def __post_init__(self):
        _choice(self.kind, ("buffer", "image"))
        _integer(self.table_index, maximum=_INDEX_MAX)
        # Export metadata is acquired once by the bounded native ingress.


@dataclass(frozen=True, slots=True)
class SurfaceCatalog:
    json: object
    resources: tuple

    def __post_init__(self):
        _rows(self.resources, SurfaceResource)


@dataclass(frozen=True, slots=True)
class SurfacePublication:
    destination: object
    receipt_path: str
    _native_destination: object = field(init=False, repr=False)

    def __post_init__(self):
        destination = os.fspath(self.destination)
        if not isinstance(destination, (str, bytes)):
            raise TypeError("destination must resolve to native str or bytes")
        empty = (str.__len__(destination) == 0 if isinstance(destination, str)
                 else bytes.__len__(destination) == 0)
        nul = (str.__contains__(destination, "\0") if isinstance(destination, str)
               else bytes.__contains__(destination, b"\0"))
        if empty or nul:
            raise ValueError("destination must be nonempty and contain no NUL")
        object.__setattr__(self, "destination", destination)
        _text(self.receipt_path)
        if not self.receipt_path or "\0" in self.receipt_path:
            raise ValueError("receipt path must be nonempty and contain no NUL")
        native_destination = _surface_native()._surface_snapshot_destination(destination)
        object.__setattr__(self, "_native_destination", native_destination)


def _freeze_preset(value, template):
    if type(template) is dict:
        if type(value) is not dict:
            raise TypeError("preset branches require plain dictionaries")
        if len(value) != len(template) or any(type(k) is not str for k in value) or value.keys() != template.keys():
            raise ValueError("preset branch has missing or extra fields")
        return MappingProxyType({key: _freeze_preset(value[key], expected)
                                 for key, expected in template.items()})
    if type(template) is int:
        _integer(value)
        if template == 0 and value != 0:
            raise ValueError("inactive preset fields require canonical zero")
        return value
    if type(template) is str:
        _text(value)
        if value != template:
            raise ValueError("preset delegate selector differs from its encoding")
        return value
    raise RuntimeError("unsupported packaged surface preset field")


def _preset_template():
    document = json.loads(files(__package__).joinpath("_surface_file_presets.json").read_text(encoding="utf-8"))
    if document["schema"] != "meshvale.surface-python-presets/1" or document["contract"] != "SURFACE-PYTHON-001/0.1.3":
        raise RuntimeError("packaged surface preset contract mismatch")
    return document["presets"]["bounded"]


@dataclass(frozen=True, slots=True, init=False)
class SurfacePreset:
    """Complete frozen worksheet; preparation owns native policy admission."""
    _values: object

    def __new__(cls):
        raise TypeError("use SurfacePreset.from_mapping or SurfacePreset.bounded")

    @classmethod
    def from_mapping(cls, mapping):
        values = _freeze_preset(mapping, _preset_template())
        result = object.__new__(cls)
        object.__setattr__(result, "_values", values)
        return result

    @classmethod
    def bounded(cls):
        return cls.from_mapping(_preset_template())


_NATIVE_TYPES = (
    "SurfaceExecutionContext", "PreparedSurface", "SurfaceResult",
    "SurfaceDeliveryError", "SurfaceOutput", "SurfaceOutputInspection",
    "SurfacePublishedBundle", "SurfaceFacts",
)


def _surface_native():
    native = import_module("._interchange", __package__)
    if not hasattr(native, "_surface_prepare"):
        raise RuntimeError("the Surface Python interface is not enabled in this build")
    return native


def __getattr__(name):
    if name in _NATIVE_TYPES:
        return getattr(_surface_native(), name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def prepare_surface(options, *, preset, execution=None, cancellation=None,
                    publication=None):
    """Prepare one native operation with a reachable result before source capture.

    The preset is required explicitly. Native preparation owns closed validation,
    charged workspace admission and any finished preparation refusal. The returned
    operation's synchronous run method owns single-use admission before input
    argument inspection; this wrapper neither captures a mesh nor frames results.
    """
    return _surface_native()._surface_prepare(
        options, preset, execution, cancellation, publication)


__all__ = ["SurfaceChannel", "SurfaceUv", "SurfaceMaterial", "SurfaceCatalogUv",
           "SurfaceOptions", "SurfaceResource", "SurfaceCatalog",
           "SurfacePublication", "SurfacePreset", "prepare_surface",
           *_NATIVE_TYPES]
