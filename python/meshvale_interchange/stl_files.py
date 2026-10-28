# SPDX-License-Identifier: Apache-2.0
"""Retained STL capture and verified codec-preserving file rewrites.

Bare STL coordinates have no inferred units. This module does not repair,
index, weld, rescale or generate normals. Result owners survive delivery errors.
"""
from dataclasses import dataclass
from importlib.resources import files
import argparse
import json
import os
import sys

from . import _interchange as _native

Cancellation = _native.Cancellation
StlExecutionContext = _native.StlExecutionContext
StlCapturedFile = _native.StlCapturedFile
StlDocument = _native.StlDocument
StlFileAsset = _native.StlFileAsset
StlOutput = _native.StlOutput
StlVerifiedRewrite = _native.StlVerifiedRewrite
StlPublishedBundle = _native.StlPublishedBundle
StlResult = _native.StlResult
StlBatchResult = _native.StlBatchResult
PreparedStl = _native.PreparedStl
PreparedStlBatch = _native.PreparedStlBatch

_ENCODINGS = ("ascii", "binary")
_HEADER_BYTES = 80
_SINGLE_ID = "single"
_SCHEMA = "meshvale.stl-result/1"
_JSON = json.JSONEncoder(ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _text(value, *, empty=True, controls=True):
    if type(value) is not str:
        raise TypeError("expected str")
    value.encode("utf-8", "strict")
    if not empty and not value:
        raise ValueError("text must be nonempty")
    if not controls and any(ord(c) < 0x20 or 0x7f <= ord(c) < 0xa0 for c in value):
        raise ValueError("text contains a control character")
    return value


def _choice(value, choices):
    _text(value)
    if value not in choices:
        raise ValueError("unsupported explicit selector")
    return value


def _path(value):
    value = os.fspath(value)
    if type(value) not in (str, bytes):
        raise TypeError("path must resolve to str or bytes")
    if not value or ("\0" in value if type(value) is str else b"\0" in value):
        raise ValueError("path is empty or contains NUL")
    return value


@dataclass(frozen=True, slots=True)
class StlMetadata:
    mode: str
    ascii_name: str | None
    binary_header: bytes | None

    def __post_init__(self):
        _choice(self.mode, ("retain", "replace"))
        if self.mode == "retain":
            if self.ascii_name is not None or self.binary_header is not None:
                raise ValueError("retain requires inactive replacement fields")
        elif (self.ascii_name is None) == (self.binary_header is None):
            raise ValueError("replace requires exactly one metadata field")
        if self.ascii_name is not None:
            _text(self.ascii_name)
            if any(not 0x20 <= ord(c) < 0x7f for c in self.ascii_name):
                raise ValueError("ASCII name must contain printable ASCII")
        if self.binary_header is not None:
            if type(self.binary_header) is not bytes:
                raise TypeError("binary_header must be bytes")
            if len(self.binary_header) != _HEADER_BYTES:
                raise ValueError("binary_header must contain exactly 80 bytes")


@dataclass(frozen=True, slots=True)
class StlUnits:
    kind: str
    label: str | None

    def __post_init__(self):
        _choice(self.kind, ("unknown", "label"))
        if self.kind == "unknown":
            if self.label is not None:
                raise ValueError("unknown requires label=None")
        else:
            _text(self.label, empty=False, controls=False)


@dataclass(frozen=True, slots=True)
class StlReadJob:
    job_id: str
    mode: str
    input: object
    source_encoding: str
    units: StlUnits

    def __post_init__(self):
        _text(self.job_id, empty=False, controls=False)
        _choice(self.mode, ("read",))
        _choice(self.source_encoding, _ENCODINGS)
        if type(self.units) is not StlUnits:
            raise TypeError("units must be StlUnits")
        object.__setattr__(self, "input", _path(self.input))


@dataclass(frozen=True, slots=True)
class StlRewriteJob:
    job_id: str
    mode: str
    input: object
    source_encoding: str
    units: StlUnits
    destination: object
    output_encoding: str
    metadata: StlMetadata
    acknowledge_corner_expansion: bool
    report_text: str | None = None

    def __post_init__(self):
        _text(self.job_id, empty=False, controls=False)
        _choice(self.mode, ("rewrite",))
        _choice(self.source_encoding, _ENCODINGS)
        _choice(self.output_encoding, _ENCODINGS)
        if type(self.units) is not StlUnits or type(self.metadata) is not StlMetadata:
            raise TypeError("explicit StlUnits and StlMetadata are required")
        if type(self.acknowledge_corner_expansion) is not bool or not self.acknowledge_corner_expansion:
            raise ValueError("acknowledge_corner_expansion must be built-in True")
        if self.metadata.mode == "retain" and self.source_encoding != self.output_encoding:
            raise ValueError("cross-encoding requires replacement metadata")
        if self.metadata.mode == "replace":
            if (self.output_encoding == "ascii") != (self.metadata.ascii_name is not None):
                raise ValueError("replacement metadata must match output encoding")
        if self.report_text is not None:
            _text(self.report_text)
        object.__setattr__(self, "input", _path(self.input))
        object.__setattr__(self, "destination", _path(self.destination))


def _preset(name):
    _choice(name, ("bounded",))
    source = files(__package__).joinpath("_stl_file_presets.json").read_bytes()
    data = json.loads(source.decode("utf-8", "strict"))
    if data["schema"] != "meshvale.stl-file-presets/1" or data["contract"] != "STL-FILES-001/0.1.0":
        raise ValueError("unsupported resource baseline")
    return data["presets"][name]


def _create(mode, preset, execution, failure_policy="continue"):
    config = _preset(preset)
    _choice(failure_policy, ("continue", "fail-fast"))
    if execution is None:
        execution = StlExecutionContext(**config["batch_execution" if mode == "batch" else "single_execution"])
    if type(execution) is not StlExecutionContext:
        raise TypeError("execution must be StlExecutionContext or None")
    # Native slots and delivery storage are reserved before a user iterable or
    # any manifest row is materialized. cwd is captured once for all paths.
    prepared = _native._stl_create(config, execution, mode, failure_policy)
    return prepared, config, os.getcwd()


def _record(job, report=None, config=None):
    if config is not None and type(job) is StlRewriteJob and job.report_text is not None:
        if len(job.report_text.encode("utf-8")) > config["rewrite_limits"]["files"]["max_caller_report_bytes"]:
            raise ValueError("known report_text exceeds its import cap")
    result = {"job_id": job.job_id, "mode": job.mode, "input": job.input,
              "source_encoding": job.source_encoding,
              "units": {"kind": job.units.kind, "label": job.units.label}}
    if type(job) is StlRewriteJob:
        if report is None and job.report_text is not None:
            text = job.report_text
            report = lambda verified: text
        result.update(destination=job.destination, output_encoding=job.output_encoding,
                      metadata={"mode": job.metadata.mode, "ascii_name": job.metadata.ascii_name,
                                "binary_header": job.metadata.binary_header},
                      acknowledge_corner_expansion=job.acknowledge_corner_expansion, report=report)
    return result


def prepare_stl_read(input, *, source_encoding, units, preset="bounded", execution=None):
    job = StlReadJob(_SINGLE_ID, "read", input, source_encoding, units)
    prepared, config, cwd = _create("read", preset, execution)
    _native._stl_add(prepared, _record(job, config=config))
    _native._stl_finish(prepared, cwd)
    return prepared


def prepare_stl_rewrite(input, destination, *, source_encoding, output_encoding,
                        metadata, units, acknowledge_corner_expansion,
                        preset="bounded", execution=None, report=None):
    if report is not None and not callable(report):
        raise TypeError("report must be callable or None")
    job = StlRewriteJob(_SINGLE_ID, "rewrite", input, source_encoding, units,
                        destination, output_encoding, metadata, acknowledge_corner_expansion)
    prepared, config, cwd = _create("rewrite", preset, execution)
    _native._stl_add(prepared, _record(job, report, config))
    _native._stl_finish(prepared, cwd)
    return prepared


def prepare_stl_batch(jobs, *, preset="bounded", execution=None, failure_policy="continue"):
    prepared, config, cwd = _create("batch", preset, execution, failure_policy)
    if isinstance(jobs, (str, bytes)):
        raise TypeError("jobs must be an iterable of frozen STL jobs")
    for index, job in enumerate(jobs):
        if index >= config["application_limits"]["max_jobs"]:
            _native._stl_reject(prepared, "job_extent", None, index, None)
            break
        if type(job) not in (StlReadJob, StlRewriteJob):
            raise TypeError("jobs must contain StlReadJob or StlRewriteJob")
        _native._stl_add(prepared, _record(job, config=config))
    _native._stl_finish(prepared, cwd)
    return prepared


def run_stl(prepared, *, cancellation=None):
    if type(prepared) is not PreparedStl:
        raise TypeError("prepared must be PreparedStl")
    try:
        return _native._stl_run(prepared, cancellation)
    except KeyboardInterrupt:
        return _native._stl_interrupt(prepared)


def run_stl_batch(prepared, *, cancellation=None):
    if type(prepared) is not PreparedStlBatch:
        raise TypeError("prepared must be PreparedStlBatch")
    try:
        return _native._stl_run(prepared, cancellation)
    except KeyboardInterrupt:
        return _native._stl_interrupt(prepared)


def read_stl_file(input, *, source_encoding, units, preset="bounded", execution=None, cancellation=None):
    return run_stl(prepare_stl_read(input, source_encoding=source_encoding,
                                   units=units, preset=preset, execution=execution), cancellation=cancellation)


def _export_json(result):
    _native._stl_frame_begin(result)
    try:
        frame = result._wire()
        for text in _JSON.iterencode(frame):
            if not _native._stl_frame_chunk(result, text.encode("utf-8", "strict")):
                return None
        return _native._stl_frame_finish(result)
    except KeyboardInterrupt:
        _native._stl_delivery(result, "canceled", "interrupted", 0)
        return None
    except (ValueError, OverflowError, UnicodeError):
        _native._stl_delivery(result, "failed", "framing", 0)
        return None


class _Invocation(ValueError):
    pass


class _Parser(argparse.ArgumentParser):
    def error(self, message):
        raise _Invocation("invalid STL invocation")


def _parser():
    parser = _Parser(prog="meshvale-stl")
    modes = parser.add_subparsers(dest="mode", required=True)
    for mode in ("read", "rewrite"):
        sub = modes.add_parser(mode)
        sub.add_argument("input")
        if mode == "rewrite":
            sub.add_argument("destination")
            sub.add_argument("--output-encoding", choices=_ENCODINGS, required=True)
            sub.add_argument("--metadata", choices=("retain", "replace"), required=True)
            sub.add_argument("--ascii-name")
            sub.add_argument("--binary-header-hex")
            sub.add_argument("--acknowledge-corner-expansion", action="store_true", required=True)
            sub.add_argument("--report-text")
        sub.add_argument("--source-encoding", choices=_ENCODINGS, required=True)
        sub.add_argument("--units", choices=("unknown", "label"), required=True)
        sub.add_argument("--unit-label")
        sub.add_argument("--preset", choices=("bounded",), default="bounded")
    batch = modes.add_parser("batch")
    batch.add_argument("manifest")
    batch.add_argument("--failure-policy", choices=("continue", "fail-fast"), required=True)
    batch.add_argument("--preset", choices=("bounded",), default="bounded")
    return parser


def _exact(value, keys):
    if type(value) is not dict or set(value) != set(keys):
        raise _Invocation("object fields do not match the STL schema")
    return value


def _hex_header(value):
    if value is None:
        return None
    _text(value)
    if len(value) != _HEADER_BYTES * 2 or any(c not in "0123456789abcdefABCDEF" for c in value):
        raise _Invocation("binary header requires 160 hexadecimal digits")
    return bytes.fromhex(value)


def _manifest_job(value):
    common = ("job_id", "mode", "input", "source_encoding", "units")
    rewrite = ("destination", "output_encoding", "metadata", "acknowledge_corner_expansion", "report_text")
    if type(value) is not dict:
        raise _Invocation("manifest row must be an object")
    mode = _choice(value.get("mode"), ("read", "rewrite"))
    _exact(value, common if mode == "read" else common + rewrite)
    units = _exact(value["units"], ("kind", "label"))
    args = {key: value[key] for key in common}
    args["units"] = StlUnits(**units)
    if mode == "read":
        return StlReadJob(**args)
    metadata = _exact(value["metadata"], ("mode", "ascii_name", "binary_header_hex"))
    args.update({key: value[key] for key in rewrite})
    args["metadata"] = StlMetadata(metadata["mode"], metadata["ascii_name"], _hex_header(metadata["binary_header_hex"]))
    return StlRewriteJob(**args)


def _pairs(values):
    result = {}
    for key, value in values:
        if key in result:
            raise _Invocation("duplicate JSON key")
        result[key] = value
    return result


def _constant(value):
    raise _Invocation("nonfinite JSON constant")


def _prepare_manifest(args):
    prepared, config, cwd = _create("batch", args.preset, None, args.failure_policy)
    raw = _native._stl_manifest(prepared, _path(args.manifest), cwd)
    if raw is None:
        _native._stl_finish(prepared, cwd)
        return prepared
    try:
        raw.decode("utf-8", "strict")
        if raw.startswith(b"\xef\xbb\xbf"):
            raise UnicodeError("BOM is unsupported")
    except UnicodeError:
        _native._stl_reject(prepared, "manifest_utf8", None, None, None)
        _native._stl_finish(prepared, cwd)
        return prepared
    lines = raw.split(b"\n")
    if lines and lines[-1] == b"":
        lines.pop()
    for index, line in enumerate(lines):
        if index >= config["application_limits"]["max_jobs"]:
            _native._stl_reject(prepared, "job_extent", index, index, None)
            break
        if len(line) > config["application_limits"]["max_manifest_line_bytes"]:
            _native._stl_reject(prepared, "manifest_extent", index, None, None)
            break
        try:
            value = json.loads(line.decode("utf-8"), object_pairs_hook=_pairs, parse_constant=_constant)
            job = _manifest_job(value)
            _native._stl_add(prepared, _record(job, config=config))
        except (TypeError, ValueError, OverflowError):
            _native._stl_reject(prepared, "manifest_json", index, None, None)
            break
    # The native manifest reader retains its admitted containing directory as
    # this batch's base and its one entry as an additional protected domain.
    _native._stl_finish(prepared, cwd)
    return prepared


def _command(argv):
    try:
        args = _parser().parse_args(argv)
        if args.mode == "batch":
            return _prepare_manifest(args)
        units = StlUnits(args.units, args.unit_label)
        if args.mode == "read":
            return prepare_stl_read(args.input, source_encoding=args.source_encoding,
                                     units=units, preset=args.preset)
        metadata = StlMetadata(args.metadata, args.ascii_name, _hex_header(args.binary_header_hex))
        if args.report_text is not None:
            _text(args.report_text)
            if len(args.report_text.encode("utf-8")) > _preset(args.preset)["rewrite_limits"]["files"]["max_caller_report_bytes"]:
                raise _Invocation("report text exceeds its cap")
        report = None if args.report_text is None else lambda verified: args.report_text
        return prepare_stl_rewrite(args.input, args.destination, source_encoding=args.source_encoding,
                                    output_encoding=args.output_encoding, metadata=metadata, units=units,
                                    acknowledge_corner_expansion=args.acknowledge_corner_expansion,
                                    preset=args.preset, report=report)
    except (TypeError, ValueError, OverflowError):
        mode = argv[0] if argv and argv[0] in ("read", "rewrite", "batch") else "read"
        prepared, _, cwd = _create(mode, "bounded", None)
        _native._stl_reject(prepared, "representation", None, None, None)
        _native._stl_finish(prepared, cwd)
        return prepared


def _deliver(result, stream):
    frame = result.export_json()
    if frame is None:
        return result.exit_code
    _native._stl_delivery(result, "not_attempted", None, 0)
    written = 0
    try:
        maximum = _native._stl_delivery_limit(result)
        if len(frame) > maximum:
            _native._stl_delivery(result, "failed", "framing", 0)
            return result.exit_code
        while written < len(frame):
            count = stream.write(frame[written:])
            if type(count) is not int or not 0 < count <= len(frame) - written:
                raise OSError("incomplete result write")
            written += count
            _native._stl_delivery(result, "not_attempted", None, written)
        try:
            stream.flush()
        except OSError:
            _native._stl_delivery(result, "failed", "flush", written)
            return result.exit_code
        _native._stl_delivery(result, "succeeded", None, written)
    except KeyboardInterrupt:
        _native._stl_delivery(result, "canceled", "interrupted", written)
    except OSError:
        _native._stl_delivery(result, "failed", "write", written)
    return result.exit_code


def main(argv=None):
    prepared = _command(list(sys.argv[1:] if argv is None else argv))
    result = run_stl_batch(prepared) if type(prepared) is PreparedStlBatch else run_stl(prepared)
    return _deliver(result, sys.stdout.buffer)


__all__ = ["Cancellation", "StlExecutionContext", "StlMetadata", "StlUnits", "StlReadJob", "StlRewriteJob",
           "StlCapturedFile", "StlDocument", "StlFileAsset", "StlOutput", "StlVerifiedRewrite", "StlPublishedBundle",
           "PreparedStl", "PreparedStlBatch", "StlResult", "StlBatchResult", "prepare_stl_read", "prepare_stl_rewrite",
           "prepare_stl_batch", "run_stl", "run_stl_batch", "read_stl_file", "main"]

if __name__ == "__main__":
    raise SystemExit(main())
