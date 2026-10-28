# SPDX-License-Identifier: Apache-2.0
"""Explicit required Windows alias gate; intentionally outside test_* discovery."""
import ctypes
from ctypes import wintypes
import gc
import importlib.util
import json
import ntpath
import os
from pathlib import Path
import shutil
import subprocess
import sys
import sysconfig
import tempfile
import unittest

LITERAL_HELPER_MODULE = "_meshvale_stl_alias_literal_fixture"


def load_literal_helper():
    # Isolated Python omits the script directory from sys.path. Load only this
    # fixed adjacent test helper, leaving installed product import paths intact.
    path = Path(__file__).with_name("stl_files_fixture.py")
    specification = importlib.util.spec_from_file_location(LITERAL_HELPER_MODULE, path)
    if specification is None or specification.loader is None:
        raise ImportError("adjacent STL literal helper loader unavailable")
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


f = load_literal_helper()


PROTOCOL = "STL_ALIAS_1"
REQUIRED_ARGUMENT = "--require-windows-aliases"
CASE_IDS = (
    "A01.1", "A01.2", "A01.3", "A01.4",
    "A02.1", "A02.2", "A02.3", "A02.4",
    "A03.1", "A03.2", "A03.3", "A03.4", "A04.1", "A04.2",
    "C01.1", "C01.2", "C01.3", "C01.4", "C02.1", "C02.2",
    "C03.1", "C03.2", "C04.1", "C04.2",
)
SOURCE_LEAF = "Alias original \u90e8\u54c1 source.stl"
MANIFEST_LEAF = "Alias manifest self jobs.jsonl"
SENTINEL = b"replacement parent sentinel\n"
PATH_CAPACITY_UNITS = 32768
GENERIC_READ = 0x80000000
SHARE_READ_WRITE_DELETE = 7
OPEN_EXISTING = 3
FILE_ATTRIBUTE_DIRECTORY = 0x10
FILE_ATTRIBUTE_REPARSE_POINT = 0x400
FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
INVALID_FILE_ATTRIBUTES = 0xFFFFFFFF
ERROR_SHARING_VIOLATION = 32
SYMLINK_DIRECTORY_UNPRIVILEGED = 3
DWORD_BITS = 32
SINGLE_PAYLOAD_BYTES = 1073741824
BATCH_PAYLOAD_BYTES = 4294967296
COMMAND_TIMEOUT_SECONDS = 30
ZERO_HEADER = bytes(80)
RECORD_PASS = "PASS"
RECORD_PATH = "path"
FILE_NAME_NORMALIZED_DOS = 0


def record(tag, *fields):
    print("\t".join((PROTOCOL, tag, *(str(field) for field in fields))), flush=True)


def text_hex(value):
    return os.fspath(value).encode("utf-8", "strict").hex()


def key(path):
    # This independent comparison is only the contract's separator/ASCII rule.
    return ntpath.normpath(os.fspath(path)).replace("/", "\\").translate(
        str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"))


class WitnessFailure(RuntimeError):
    pass


def require_witness(value, message):
    if not value:
        raise WitnessFailure(message)


class FileInformation(ctypes.Structure):
    _fields_ = [("attributes", wintypes.DWORD), ("creation", wintypes.FILETIME),
                ("access", wintypes.FILETIME), ("write", wintypes.FILETIME),
                ("volume", wintypes.DWORD), ("size_high", wintypes.DWORD),
                ("size_low", wintypes.DWORD), ("links", wintypes.DWORD),
                ("index_high", wintypes.DWORD), ("index_low", wintypes.DWORD)]


class Handle:
    def __init__(self, win, value):
        self.win = win
        self.value = value

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        if self.value != self.win.invalid:
            closed = self.win.api.CloseHandle(self.value)
            error = ctypes.get_last_error() if not closed else 0
            self.value = self.win.invalid
            if not closed and kind is None:
                raise WitnessFailure("observer CloseHandle Win32=" + str(error))
            if not closed:
                record("OBSERVER_CLOSE_ERROR", error)


class Windows:
    def __init__(self):
        require_witness(os.name == "nt", "MISSED_WITNESS Windows local filesystem required")
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.cases_started = False
        self.invalid = ctypes.c_void_p(-1).value
        definitions = (
            ("CreateFileW", [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                             ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE], wintypes.HANDLE),
            ("CloseHandle", [wintypes.HANDLE], wintypes.BOOL),
            ("GetFileInformationByHandle", [wintypes.HANDLE, ctypes.POINTER(FileInformation)], wintypes.BOOL),
            ("GetFinalPathNameByHandleW", [wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD], wintypes.DWORD),
            ("GetLongPathNameW", [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD], wintypes.DWORD),
            ("GetShortPathNameW", [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD], wintypes.DWORD),
            ("GetFileAttributesW", [wintypes.LPCWSTR], wintypes.DWORD),
            ("CreateSymbolicLinkW", [wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD], wintypes.BOOLEAN),
        )
        for name, arguments, result in definitions:
            function = getattr(self.api, name)
            function.argtypes = arguments
            function.restype = result

    def failure(self, operation):
        raise WitnessFailure(operation + " Win32=" + str(ctypes.get_last_error()))

    def open(self, path, share=SHARE_READ_WRITE_DELETE):
        value = self.api.CreateFileW(str(path), GENERIC_READ, share, None,
                                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, None)
        if value == self.invalid:
            self.failure("observer CreateFileW")
        return Handle(self, value)

    def information(self, handle):
        info = FileInformation()
        if not self.api.GetFileInformationByHandle(handle.value, ctypes.byref(info)):
            self.failure("GetFileInformationByHandle")
        return (int(info.volume), (int(info.index_high) << DWORD_BITS) | int(info.index_low),
                (int(info.size_high) << DWORD_BITS) | int(info.size_low), int(info.attributes))

    def observe(self, path, role=RECORD_PATH):
        with self.open(path) as handle:
            info = self.information(handle)
            buffer = ctypes.create_unicode_buffer(PATH_CAPACITY_UNITS)
            length = self.api.GetFinalPathNameByHandleW(handle.value, buffer, PATH_CAPACITY_UNITS, FILE_NAME_NORMALIZED_DOS)
            if not length:
                self.failure("GetFinalPathNameByHandleW")
            require_witness(length < PATH_CAPACITY_UNITS, "MISSED_WITNESS final path capacity")
            final = buffer.value
        if self.cases_started:
            record("PATH", text_hex(path), text_hex(final), *info)
        else:
            record("WITNESS", role, RECORD_PASS, text_hex(path), text_hex(final), *info)
        return info

    def prefixes(self, path):
        path = Path(path)
        prefix = Path(path.anchor)
        for component in path.parts[1:]:
            prefix /= component
            attributes = self.api.GetFileAttributesW(str(prefix))
            if attributes == INVALID_FILE_ATTRIBUTES:
                self.failure("GetFileAttributesW")
            require_witness(not attributes & FILE_ATTRIBUTE_REPARSE_POINT,
                            "MISSED_WITNESS original prefix is reparse")

    def spelling(self, path, short=False):
        buffer = ctypes.create_unicode_buffer(PATH_CAPACITY_UNITS)
        function = self.api.GetShortPathNameW if short else self.api.GetLongPathNameW
        length = function(str(path), buffer, PATH_CAPACITY_UNITS)
        if not length:
            self.failure("GetShortPathNameW" if short else "GetLongPathNameW")
        require_witness(length < PATH_CAPACITY_UNITS, "MISSED_WITNESS spelling capacity")
        result = Path(buffer.value)
        self.prefixes(result)
        require_witness(self.observe(path) == self.observe(result), "MISSED_WITNESS spelling identity/extent")
        return result

    def closure(self, path, identity):
        with self.open(path) as control:
            require_witness(self.information(control) == identity, "closure positive control identity")
            value = self.api.CreateFileW(str(path), GENERIC_READ, 0, None,
                                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, None)
            error = ctypes.get_last_error() if value == self.invalid else 0
            if value != self.invalid:
                with Handle(self, value):
                    raise WitnessFailure("closure positive control unexpectedly opened")
            require_witness(error == ERROR_SHARING_VIOLATION,
                            "closure positive control Win32=" + str(error))
        with self.open(path, share=0) as observer:
            require_witness(self.information(observer) == identity and
                            not identity[3] & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT),
                            "closure same regular source identity with retained owner")
        record("CLOSURE", text_hex(path), "GENERIC_READ_ONLY", RECORD_PASS)


class Source:
    def __init__(self, tree, identifier, payload=f.ASCII):
        self.tree = tree
        parent = tree.grand_long / ("Alias source parent " + identifier)
        parent.mkdir()
        (parent / SOURCE_LEAF).write_bytes(payload)
        self.parent_long = tree.win.spelling(parent)
        self.parent_short = tree.win.spelling(parent, short=True)
        self.long = tree.win.spelling(parent / SOURCE_LEAF)
        self.short = tree.win.spelling(self.long, short=True)
        self.identity = tree.win.observe(self.long)
        require_witness(key(self.parent_long) != key(self.parent_short), "MISSED_WITNESS parent alias")
        require_witness(key(self.long.name) != key(self.short.name), "MISSED_WITNESS source leaf alias")
        for index in range(4):
            path = self.path(index)
            tree.win.prefixes(path)
            require_witness(tree.win.observe(path) == self.identity and path.read_bytes() == payload,
                            "MISSED_WITNESS real four-way file aliases/bytes")

    def path(self, index):
        return (self.parent_long if index < 2 else self.parent_short) / (
            self.long.name if index % 2 == 0 else self.short.name)


class Tree:
    def __init__(self):
        self.win = Windows()
        self.root = Path(tempfile.mkdtemp(prefix="Meshvale alias gate long root "))
        self.link = None
        try:
            grand = self.root / "Alias distinguishing grandparent directory"
            grand.mkdir()
            self.grand_long = self.win.spelling(grand)
            self.grand_short = self.win.spelling(grand, short=True)
            require_witness(key(self.grand_long) != key(self.grand_short),
                            "MISSED_DISTINGUISHING_WITNESS grandparent alias")
            self.first = Source(self, "witness")
            mixed = self.grand_long / self.first.parent_short.name
            require_witness(self.win.observe(mixed) == self.win.observe(self.first.parent_long),
                            "MISSED_DISTINGUISHING_WITNESS mixed identity")
            for whole in (self.first.long.parent, self.win.spelling(self.first.long).parent,
                          self.first.short.parent):
                distinct = key(mixed) != key(whole)
                record("DISTINCT", text_hex(mixed), text_hex(whole), int(distinct))
                require_witness(distinct, "MISSED_DISTINGUISHING_WITNESS mixed equals whole")
            self.link = self.grand_long / "observed directory link"
            if not self.win.api.CreateSymbolicLinkW(str(self.link), str(self.first.parent_long),
                                                    SYMLINK_DIRECTORY_UNPRIVILEGED):
                self.win.failure("required real directory symlink")
            attributes = self.win.api.GetFileAttributesW(str(self.link))
            require_witness(attributes != INVALID_FILE_ATTRIBUTES and attributes & FILE_ATTRIBUTE_REPARSE_POINT,
                            "MISSED_WITNESS genuine reparse attributes")
            require_witness(self.win.observe(self.link / self.first.long.name) == self.first.identity,
                            "MISSED_WITNESS genuine reparse target identity")
            manifest = self.grand_long / MANIFEST_LEAF
            manifest.write_bytes(b"witness only\n")
            short_manifest = self.win.spelling(manifest, short=True)
            require_witness(key(manifest.name) != key(short_manifest.name), "MISSED_WITNESS manifest leaf alias")
            self.win.observe(self.grand_short, "parent-alias")
            self.win.observe(self.first.short, "source-leaf-alias")
            self.win.observe(mixed, "mixed-component")
            self.win.observe(self.link / self.first.long.name, "reparse-traversal")
            self.win.observe(short_manifest, "manifest-leaf-alias")
            self.cwd = self.grand_long / "separate process cwd"
            self.cwd.mkdir()
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.link is not None and os.path.lexists(self.link):
            self.link.unlink()
        self.link = None
        if self.root is not None:
            shutil.rmtree(self.root)
            self.root = None


class Gate:
    def __init__(self, tree, sf):
        self.tree = tree
        self.sf = sf
        self.test = unittest.TestCase()
        self.completed = 0
        self.child = 0
        self.contexts = []

    def run_case(self, identifier, body):
        self.test.assertEqual(identifier, CASE_IDS[self.completed])
        self.tree.win.cases_started = True
        record("REACHED", identifier)
        first_context = len(self.contexts)
        body(identifier)
        gc.collect()
        for context in self.contexts[first_context:]:
            self.test.assertEqual(context.active_workers, 0)
            self.test.assertEqual(context.active_tracked_payload_bytes, 0)
        del self.contexts[first_context:]
        self.completed += 1
        record("CASE", identifier, RECORD_PASS)

    def context(self, batch=False):
        context = self.sf.StlExecutionContext(worker_budget=4, minimum_parallel_vertices=65536,
                                               tracked_payload_budget_bytes=BATCH_PAYLOAD_BYTES if batch else SINGLE_PAYLOAD_BYTES)
        self.contexts.append(context)
        return context

    def read_job(self, identifier, path):
        return self.sf.StlReadJob(identifier, "read", path, "ascii", self.sf.StlUnits("unknown", None))

    def rewrite_job(self, identifier, path, destination):
        return self.sf.StlRewriteJob(identifier, "rewrite", path, "ascii", self.sf.StlUnits("unknown", None),
                                     destination, "ascii", self.sf.StlMetadata("retain", None, None), True, None)

    def options(self, cross=False):
        return dict(source_encoding="ascii", output_encoding="binary" if cross else "ascii",
                    metadata=self.sf.StlMetadata("replace", None, ZERO_HEADER) if cross else self.sf.StlMetadata("retain", None, None),
                    units=self.sf.StlUnits("unknown", None), acknowledge_corner_expansion=True, preset="bounded")

    def frame(self, result, mode, outcomes, exit_code, code=None):
        frame = f.strict_json(result.export_json())
        f.check_frame(self.test, frame, mode, outcomes, exit_code, code)
        return frame

    def admission(self, frame, identifiers, code):
        f.check_frame(self.test, frame, "batch", ["not_attempted"] * len(identifiers), 2, code)
        self.test.assertEqual([slot["job_id"] for slot in frame["jobs"]], identifiers)
        for slot in frame["jobs"]:
            self.test.assertEqual(slot["not_attempted_reason"], "admission_failed")
            self.test.assertFalse(any(slot["owners"].values()))
            self.test.assertIsNone(slot["diagnostic"])
            for name in ("capture", "read"):
                self.test.assertEqual(slot["observations"][name]["state"], "not_attempted")
            self.test.assertTrue(all(value == 0 for value in slot["execution"]["capture"].values()))

    def physical(self, destination, entry, cross=False, bundle=None):
        self.test.assertEqual({path.name for path in destination.iterdir()}, {entry, f.RECEIPT})
        payload = (destination / entry).read_bytes()
        receipt_bytes = (destination / f.RECEIPT).read_bytes()
        if cross:
            f.check_binary(self.test, payload, header=ZERO_HEADER)
        else:
            f.check_ascii(self.test, payload)
        expected = {
            "schema": "meshvale.stl-publication/1", "profile": "codec-preserving",
            "source": {"encoding": "ascii", "entry": entry, "bytes": 299, "sha256": f.sha(f.ASCII)},
            "payload": {"encoding": "binary" if cross else "ascii", "entry": entry, "bytes": len(payload), "sha256": f.sha(payload)},
            "counts": {"facets": 3, "vertices": 9, "corners": 9},
            "metadata": {"requested": "replace" if cross else "retain", "actual": "replaced_source_metadata" if cross else "retained_source_metadata",
                         "ascii_name": None if cross else "raw facets", "binary_header_hex": ZERO_HEADER.hex() if cross else None},
            "units": {"kind": "unknown", "label": None, "rescaled": False},
            "fidelity": dict.fromkeys(("position_bits", "raw_normal_bits", "facet_order", "corner_order", "soup_identity", "exact_bytes"), True),
            "verification": "staged-reread-and-fresh-codec-verified", "caller_report": None,
        }
        self.test.assertEqual(f.strict_json(receipt_bytes), expected)
        if bundle is not None:
            self.test.assertEqual(bundle.export_receipt_bytes(), receipt_bytes)
            self.test.assertEqual(bundle.verified.export_bytes(), payload)
            self.test.assertEqual(bundle.verified.entry, entry)
            self.test.assertEqual(bundle.verified.source.entry, entry)
            self.test.assertEqual(bundle.verified.source.export_bytes(), f.ASCII)
            self.test.assertEqual(bundle.verified.output.export_corner_source_vertices(), tuple(range(9)))
            f.check_document(self.test, bundle.verified.document, "binary" if cross else "ascii", header=ZERO_HEADER)
        record("BYTES", text_hex(entry), len(payload), f.sha(payload))
        record("BYTES", text_hex(f.RECEIPT), len(receipt_bytes), f.sha(receipt_bytes))

    def command(self, identifier, arguments, console=False):
        if console:
            entry = Path(sysconfig.get_path("scripts")) / "meshvale-stl.exe"
            self.test.assertTrue(entry.is_file(), "actual installed console entry required")
            command = [str(entry)]
        else:
            command = [sys.executable, "-m", "meshvale_interchange.stl_files"]
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        completed = subprocess.run(command + list(arguments), cwd=self.tree.cwd, env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   timeout=COMMAND_TIMEOUT_SECONDS, check=False)
        self.child += 1
        record("CHILD", identifier, self.child, completed.returncode,
               completed.stdout.hex(), completed.stderr.hex())
        return completed, f.strict_json(completed.stdout)

    def rewrite_args(self, source, destination, cross=False):
        arguments = ["rewrite", str(source), str(destination), "--source-encoding", "ascii",
                     "--output-encoding", "binary" if cross else "ascii", "--metadata", "replace" if cross else "retain",
                     "--units", "unknown", "--acknowledge-corner-expansion", "--preset", "bounded"]
        if cross:
            arguments += ["--binary-header-hex", ZERO_HEADER.hex()]
        return arguments

    def manifest(self, path, rows):
        payload = b"\n".join(json.dumps(row, ensure_ascii=False, separators=(",", ":")).encode("utf-8") for row in rows) + b"\n"
        path.write_bytes(payload)
        return payload

    def api_success(self, identifier, index):
        sf = self.sf
        source = Source(self.tree, identifier)
        input_path = source.path(index)
        destination = (source.parent_long if index < 2 else source.parent_short) / "published"
        context = self.context()
        prepared = sf.prepare_stl_read(input_path, source_encoding="ascii", units=sf.StlUnits("unknown", None),
                                       preset="bounded", execution=context)
        result = sf.run_stl(prepared)
        self.test.assertIs(result, prepared.result)
        self.frame(result, "read", ["read"], 0)
        asset = result.asset
        self.test.assertEqual(asset.entry, input_path.name)
        self.test.assertEqual(asset.export_bytes(), f.ASCII)
        self.test.assertEqual(asset.sha256, f.sha(f.ASCII))
        f.check_document(self.test, asset.document, "ascii")
        prepared_write = sf.prepare_stl_rewrite(input_path, destination, execution=context, **self.options(index == 3))
        written = sf.run_stl(prepared_write)
        self.test.assertIs(written, prepared_write.result)
        self.frame(written, "rewrite", ["published"], 0)
        bundle = written.bundle
        self.physical(destination, input_path.name, cross=index == 3, bundle=bundle)
        self.test.assertEqual(source.long.read_bytes(), f.ASCII)
        self.test.assertEqual(self.tree.win.observe(input_path), source.identity)
        self.tree.win.closure(input_path, source.identity)
        source.long.unlink()
        self.test.assertFalse(source.long.exists())
        self.test.assertFalse(source.short.exists())
        del result, written, prepared, prepared_write
        gc.collect()
        self.test.assertGreater(context.active_tracked_payload_bytes, 0)
        self.test.assertEqual(context.active_workers, 0)
        self.test.assertEqual(asset.export_bytes(), f.ASCII)
        self.physical(destination, input_path.name, cross=index == 3, bundle=bundle)
        del asset, bundle
        gc.collect()
        self.test.assertEqual(context.active_tracked_payload_bytes, 0)
        self.test.assertEqual(context.active_workers, 0)

    def api_duplicates(self, identifier, index):
        source = Source(self.tree, identifier)
        destination = source.parent_long / "first would publish"
        first, last = source.path(index), source.path((index + 1) % 4)
        prepared = self.sf.prepare_stl_batch([self.rewrite_job("first", first, destination), self.read_job("last", last)],
                                               preset="bounded", execution=self.context(batch=True))
        slots = prepared.result.jobs
        result = self.sf.run_stl_batch(prepared)
        self.test.assertIs(result, prepared.result)
        self.test.assertTrue(all(left is right for left, right in zip(slots, result.jobs)))
        self.admission(f.strict_json(result.export_json()), ["first", "last"], "domain_overlap")
        self.test.assertFalse(destination.exists())
        self.test.assertEqual(self.tree.win.observe(source.long), source.identity)
        self.test.assertEqual(source.long.read_bytes(), f.ASCII)

    def api_overlap(self, identifier, index):
        source = Source(self.tree, identifier)
        if index < 2:
            destination = source.path(3) if index == 0 else source.parent_short
            prepared = self.sf.prepare_stl_rewrite(source.long, destination, execution=self.context(), **self.options())
            result = self.sf.run_stl(prepared)
            frame = self.frame(result, "rewrite", ["not_attempted"], 2, "path_admission")
            self.test.assertEqual(frame["jobs"][0]["not_attempted_reason"], "admission_failed")
            self.test.assertFalse(any(frame["jobs"][0]["owners"].values()))
        elif index == 2:
            destination = source.parent_short / "disjoint sibling"
            prepared = self.sf.prepare_stl_rewrite(source.path(3), destination, execution=self.context(), **self.options())
            result = self.sf.run_stl(prepared)
            self.frame(result, "rewrite", ["published"], 0)
            self.physical(destination, source.short.name, bundle=result.bundle)
        else:
            other = Source(self.tree, identifier + " other")
            destination = self.tree.grand_long / "equal new destinations"
            alias_destination = self.tree.grand_short / destination.name
            self.test.assertFalse(destination.exists())
            prepared = self.sf.prepare_stl_batch([
                self.rewrite_job("first", source.long, destination),
                self.rewrite_job("last", other.short, alias_destination)], preset="bounded", execution=self.context(batch=True))
            result = self.sf.run_stl_batch(prepared)
            self.admission(f.strict_json(result.export_json()), ["first", "last"], "domain_overlap")
            self.test.assertFalse(destination.exists())
            self.test.assertEqual(other.long.read_bytes(), f.ASCII)
        self.test.assertEqual(self.tree.win.observe(source.long), source.identity)
        self.test.assertEqual(source.long.read_bytes(), f.ASCII)

    def api_reparse(self, identifier, index):
        source = Source(self.tree, identifier)
        original = self.tree.first.long
        input_path = self.tree.link / original.name if index == 0 else self.tree.link / ".." / original.parent.name / original.name
        destination = source.parent_short / "first would publish"
        prepared = self.sf.prepare_stl_batch([self.rewrite_job("first", source.long, destination),
                                               self.read_job("last", input_path)], preset="bounded", execution=self.context(batch=True))
        result = self.sf.run_stl_batch(prepared)
        self.admission(f.strict_json(result.export_json()), ["first", "last"], "path_admission")
        self.test.assertFalse(destination.exists())
        self.test.assertEqual(original.read_bytes(), f.ASCII)
        self.test.assertEqual(source.long.read_bytes(), f.ASCII)
        self.test.assertEqual(self.tree.win.observe(source.long), source.identity)

    def command_success(self, identifier, index):
        source = Source(self.tree, identifier)
        input_path = source.path(2 + index % 2)
        console = index >= 2
        completed, frame = self.command(identifier, ["read", str(input_path), "--source-encoding", "ascii",
                                                     "--units", "unknown", "--preset", "bounded"], console)
        self.test.assertEqual(completed.returncode, 0, completed.stderr)
        f.check_frame(self.test, frame, "read", ["read"], 0)
        self.test.assertEqual(frame["jobs"][0]["observations"]["capture"]["facts"],
                              {"entry": input_path.name, "bytes": 299, "sha256": f.sha(f.ASCII)})
        destination = source.parent_short / "command published"
        completed, frame = self.command(identifier, self.rewrite_args(input_path, destination, index == 3), console)
        self.test.assertEqual(completed.returncode, 0, completed.stderr)
        f.check_frame(self.test, frame, "rewrite", ["published"], 0)
        self.physical(destination, input_path.name, cross=index == 3)
        self.test.assertEqual(self.tree.win.observe(source.long), source.identity)
        self.test.assertEqual(source.long.read_bytes(), f.ASCII)

    def command_duplicates(self, identifier, index):
        source = Source(self.tree, identifier)
        manifest = source.parent_long / "Alias batch jobs.jsonl"
        payload = self.manifest(manifest, [f.rewrite_row("first", source.long.name, "first would publish"),
                                            f.read_row("last", source.short.name)])
        input_path = manifest if index == 0 else source.parent_short / manifest.name
        completed, frame = self.command(identifier, ["batch", str(input_path), "--failure-policy", "continue", "--preset", "bounded"], bool(index))
        self.test.assertEqual(completed.returncode, 2, completed.stderr)
        self.admission(frame, ["first", "last"], "domain_overlap")
        self.test.assertFalse((source.parent_long / "first would publish").exists())
        self.test.assertEqual(manifest.read_bytes(), payload)
        self.test.assertEqual(source.long.read_bytes(), f.ASCII)

    def command_self_manifest(self, identifier, index):
        directory = self.tree.grand_long / ("Self manifest directory " + identifier)
        directory.mkdir()
        manifest = directory / MANIFEST_LEAF
        manifest.write_bytes(b"")
        short = self.tree.win.spelling(manifest, short=True)
        require_witness(key(manifest.name) != key(short.name), "MISSED_WITNESS live manifest short leaf")
        payload = self.manifest(manifest, [f.read_row("self-long", manifest.name), f.read_row("self-short", short.name)])
        identity = self.tree.win.observe(manifest)
        require_witness(self.tree.win.observe(short) == identity and short.read_bytes() == payload,
                        "MISSED_WITNESS final strict manifest aliases/bytes")
        completed, frame = self.command(identifier, ["batch", str(manifest if index == 0 else short),
                                                     "--failure-policy", "continue", "--preset", "bounded"], bool(index))
        self.test.assertEqual(completed.returncode, 2, completed.stderr)
        self.admission(frame, ["self-long", "self-short"], "domain_overlap")
        self.test.assertEqual(manifest.read_bytes(), payload)
        self.test.assertEqual(short.read_bytes(), payload)
        self.test.assertEqual(self.tree.win.observe(manifest), identity)
        record("BYTES", text_hex(manifest), len(payload), f.sha(payload))
        # Live equal-file identity only. No retained manifest-component claim.

    def command_destination_collision(self, identifier, index):
        first, last = Source(self.tree, identifier + " first"), Source(self.tree, identifier + " last")
        self.test.assertNotEqual(first.identity[:2], last.identity[:2])
        leaf = "New alias destination " + identifier
        destination = self.tree.grand_long / leaf
        other_destination = self.tree.grand_short / (leaf.upper() if index else leaf)
        self.test.assertFalse(destination.exists())
        self.test.assertFalse(other_destination.exists())
        manifest = self.tree.grand_long / (identifier + " manifest.jsonl")
        payload = self.manifest(manifest, [f.rewrite_row("first", str(first.long), str(destination)),
                                            f.rewrite_row("last", str(last.short), str(other_destination))])
        completed, frame = self.command(identifier, ["batch", str(manifest), "--failure-policy", "continue", "--preset", "bounded"], bool(index))
        self.test.assertEqual(completed.returncode, 2, completed.stderr)
        self.admission(frame, ["first", "last"], "domain_overlap")
        self.test.assertFalse(destination.exists())
        self.test.assertFalse(other_destination.exists())
        self.test.assertEqual(first.long.read_bytes(), f.ASCII)
        self.test.assertEqual(last.long.read_bytes(), f.ASCII)
        self.test.assertEqual(manifest.read_bytes(), payload)

    def run(self):
        families = (("A01", 4, self.api_success), ("A02", 4, self.api_duplicates),
                    ("A03", 4, self.api_overlap), ("A04", 2, self.api_reparse),
                    ("C01", 4, self.command_success), ("C02", 2, self.command_duplicates),
                    ("C03", 2, self.command_self_manifest), ("C04", 2, self.command_destination_collision))
        for family, count, function in families:
            for index in range(count):
                self.run_case(family + "." + str(index + 1), lambda identifier, n=index, call=function: call(identifier, n))
        self.test.assertEqual(self.completed, len(CASE_IDS))


def main():
    tree = None
    try:
        if sys.argv[1:] != [REQUIRED_ARGUMENT]:
            raise AssertionError("exact sole required alias gate flag is mandatory")
        for identifier in CASE_IDS:
            record("PLANNED", identifier)
        record("MODE", "python", "public-api-module-console")
        tree = Tree()
        from meshvale_interchange import stl_files
        gate = Gate(tree, stl_files)
        gate.run()
        tree.close()
        tree = None
        record("SUMMARY", RECORD_PASS, gate.completed, len(CASE_IDS))
        return 0
    except WitnessFailure as error:
        print("\t".join((PROTOCOL, "FAIL", "MISSED_WITNESS", text_hex(str(error)))), file=sys.stderr)
        return 2
    except Exception as error:
        print("\t".join((PROTOCOL, "FAIL", "ASSERTION_OR_EXCEPTION", text_hex(repr(error)))), file=sys.stderr)
        return 1
    finally:
        if tree is not None:
            tree.close()


if __name__ == "__main__":
    raise SystemExit(main())
