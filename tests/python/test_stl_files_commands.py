# SPDX-License-Identifier: Apache-2.0
import contextlib
import json
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import tempfile
import unittest

from meshvale_interchange import stl_files as sf

import stl_files_fixture as f


class StlFileCommandTests(unittest.TestCase):
    def command(self, argv, cwd, console=False):
        if console:
            entry = Path(sysconfig.get_path("scripts")) / ("meshvale-stl.exe" if os.name == "nt" else "meshvale-stl")
            self.assertTrue(entry.is_file(), "installed console entry point must exist in this interpreter's scripts directory")
            command = [str(entry)]
        else:
            command = [sys.executable, "-m", "meshvale_interchange.stl_files"]
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        completed = subprocess.run(command + list(argv), cwd=cwd, env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False)
        try:
            frame = f.strict_json(completed.stdout)
        except (ValueError, UnicodeError, AssertionError) as error:
            self.fail("command did not emit exactly one valid closed JSON frame: " + repr((completed.returncode, completed.stdout, completed.stderr, error)))
        return completed, frame

    def rewrite_args(self, source, destination, encoding="ascii", output=None, report=None):
        args = ["rewrite", str(source), str(destination), "--source-encoding", encoding,
                "--output-encoding", output or encoding, "--metadata", "retain", "--units", "unknown",
                "--acknowledge-corner-expansion", "--preset", "bounded"]
        if report is not None:
            args += ["--report-text", report]
        return args

    def manifest(self, path, rows):
        path.write_bytes(b"\n".join(json.dumps(row, ensure_ascii=False, separators=(",", ":")).encode("utf-8") for row in rows) + b"\n")

    def assert_published_physical(self, destination, source_name, facets=f.FACETS):
        self.assertEqual({p.name for p in destination.iterdir()}, {source_name, f.RECEIPT})
        physical = (destination / source_name).read_bytes()
        f.check_ascii(self, physical, facets)
        receipt = f.strict_json((destination / f.RECEIPT).read_bytes())
        self.assertEqual(receipt["payload"], {"encoding": "ascii", "entry": source_name,
                                             "bytes": len(physical), "sha256": f.sha(physical)})
        self.assertEqual(receipt["verification"], "staged-reread-and-fresh-codec-verified")
        self.assertFalse({"success", "commit", "publication", "cleanup", "delivery", "sha256"} & set(receipt))
        return physical

    def test_api_module_console_single_parity_uses_real_installed_entrypoints(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "\u90e8\u54c1.stl"
            source.write_bytes(f.ASCII)
            report = "quote \"\n\u96ea"
            prepared = sf.prepare_stl_rewrite(source, root / "api", source_encoding="ascii", output_encoding="ascii",
                                              metadata=sf.StlMetadata("retain", None, None), units=sf.StlUnits("unknown", None),
                                              acknowledge_corner_expansion=True, report=lambda verified: report)
            result = sf.run_stl(prepared)
            api = f.strict_json(result.export_json())
            f.check_frame(self, api, "rewrite", ["published"], 0)
            physical = self.assert_published_physical(root / "api", source.name)
            expected_receipt = (root / "api" / f.RECEIPT).read_bytes()
            for console, name in ((False, "mod"), (True, "console-output")):
                with self.subTest(console=console):
                    completed, frame = self.command(self.rewrite_args(source, root / name, report=report), root, console)
                    self.assertEqual(completed.returncode, 0, completed.stderr)
                    f.check_frame(self, frame, "rewrite", ["published"], 0)
                    self.assertEqual(self.assert_published_physical(root / name, source.name), physical)
                    self.assertEqual((root / name / f.RECEIPT).read_bytes(), expected_receipt)
                    # Live accounting/native scheduling may differ per process;
                    # the public behavior, declarations and reached facts agree.
                    for field in ("job_id", "mode", "declarations", "outcome", "phase", "diagnostic",
                                  "observations", "cleanup", "cleanup_error", "not_attempted_reason", "owners"):
                        self.assertEqual(frame["jobs"][0][field], api["jobs"][0][field], field)
                    def strings(value):
                        if isinstance(value, dict):
                            return [text for child in value.values() for text in strings(child)]
                        if isinstance(value, list):
                            return [text for child in value for text in strings(child)]
                        return [value] if type(value) is str else []
                    self.assertFalse(any(str(root).replace("\\", "/") in text.replace("\\", "/") for text in strings(frame)))
                    self.assertEqual(source.read_bytes(), f.ASCII)

    def test_read_and_cross_encoding_commands_preserve_explicit_metadata_and_bits(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "bounds.stl"
            source.write_bytes(f.binary(f.BOUNDARY))
            completed, frame = self.command(["read", source.name, "--source-encoding", "binary", "--units", "label", "--unit-label", "mm"], root)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            f.check_frame(self, frame, "read", ["read"], 0)
            self.assertEqual(frame["jobs"][0]["observations"]["read"]["facts"],
                             {"encoding": "binary", "facets": 1, "vertices": 3, "corners": 3,
                              "ascii_name": None, "binary_header_hex": f.HEADER.hex()})
            args = ["rewrite", source.name, "cross", "--source-encoding", "binary", "--output-encoding", "ascii",
                    "--metadata", "replace", "--ascii-name", "", "--units", "label", "--unit-label", "mm",
                    "--acknowledge-corner-expansion"]
            completed, frame = self.command(args, root, console=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            f.check_frame(self, frame, "rewrite", ["published"], 0)
            f.check_ascii(self, (root / "cross" / source.name).read_bytes(), f.BOUNDARY, "")
            receipt = f.strict_json((root / "cross" / f.RECEIPT).read_bytes())
            self.assertEqual(receipt["metadata"], {"requested": "replace", "actual": "replaced_source_metadata",
                                                   "ascii_name": "", "binary_header_hex": None})
            self.assertEqual(receipt["units"], {"kind": "label", "label": "mm", "rescaled": False})

    def test_batch_manifest_paths_are_relative_to_manifest_and_preserve_slots(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            folder = root / "manifest-home"
            folder.mkdir()
            (folder / "read.stl").write_bytes(f.ASCII)
            (folder / "write.stl").write_bytes(f.ASCII)
            rows = [f.read_row("read-id", "read.stl"), f.rewrite_row("write-id", "write.stl", "published")]
            manifest = folder / "jobs.jsonl"
            self.manifest(manifest, rows)
            completed, frame = self.command(["batch", str(manifest), "--failure-policy", "continue", "--preset", "bounded"], root)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            f.check_frame(self, frame, "batch", ["read", "published"], 0)
            self.assertEqual(frame["outcome"], "completed")
            self.assertEqual([job["job_id"] for job in frame["jobs"]], ["read-id", "write-id"])
            self.assert_published_physical(folder / "published", "write.stl")
            self.assertFalse((root / "published").exists())
            self.assertEqual((folder / "read.stl").read_bytes(), f.ASCII)
            self.assertEqual((folder / "write.stl").read_bytes(), f.ASCII)

    def test_invalid_last_manifest_row_keeps_prior_slot_and_never_attempts_capture(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "source.stl"
            source.write_bytes(f.ASCII)
            first = json.dumps(f.rewrite_row("known-first", source.name, "never-published"), separators=(",", ":")).encode()
            bad_rows = (b'{"job_id":"last","mode":"read","input":"missing","source_encoding":"ascii","units":{"kind":"unknown","label":null},"extra":7}',
                        b'{"job_id":"last","mode":"read","input":"other.stl","source_encoding":"ascii","units":{"kind":"unknown","label":null},"job_id":"duplicate"}',
                        b'{} trailing', b'{"bad":NaN}', b'')
            for index, bad in enumerate(bad_rows):
                with self.subTest(index=index):
                    manifest = root / ("bad" + str(index) + ".jsonl")
                    manifest.write_bytes(first + b"\n" + bad + b"\n")
                    completed, frame = self.command(["batch", manifest.name, "--failure-policy", "continue"], root)
                    self.assertEqual(completed.returncode, 2, completed.stderr)
                    self.assertEqual(frame["admission_diagnostic"]["code"], "manifest_json")
                    self.assertEqual(frame["admission_diagnostic"]["line_index"], 1)
                    self.assertGreaterEqual(len(frame["jobs"]), 1)
                    self.assertEqual(frame["jobs"][0]["job_id"], "known-first")
                    f.check_frame(self, frame, "batch", ["not_attempted"] * len(frame["jobs"]), 2, "manifest_json")
                    self.assertEqual(frame["outcome"], "admission_failed")
                    for job in frame["jobs"]:
                        self.assertEqual(job["not_attempted_reason"], "admission_failed")
                        self.assertIsNone(job["diagnostic"])
                        self.assertFalse(any(job["owners"].values()))
                        self.assertEqual(job["observations"]["capture"]["state"], "not_attempted")
                        self.assertEqual(job["execution"]["capture"], dict.fromkeys(f.STATS_KEYS, 0))
                    self.assertFalse((root / "never-published").exists())
                    self.assertFalse([p for p in root.iterdir() if p.is_dir()])
                    self.assertEqual(source.read_bytes(), f.ASCII)

    def test_manifest_encoding_limits_duplicate_ids_and_domains_are_admitted_as_a_whole(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for name in ("first.stl", "last.stl"):
                (root / name).write_bytes(f.ASCII)
            good = f.read_row("first", "first.stl")
            encoding_cases = (b"\xef\xbb\xbf" + json.dumps(good).encode() + b"\n", json.dumps(good).encode() + b"\n\xff\n")
            for index, raw in enumerate(encoding_cases):
                manifest = root / ("encoding" + str(index) + ".jsonl")
                manifest.write_bytes(raw)
                completed, frame = self.command(["batch", manifest.name, "--failure-policy", "continue"], root)
                self.assertEqual(completed.returncode, 2, completed.stderr)
                f.check_frame(self, frame, "batch", ["not_attempted"] * len(frame["jobs"]), 2, "manifest_utf8")
                self.assertFalse(any(any(job["owners"].values()) for job in frame["jobs"]))
            for code, rows in (("duplicate_job_id", [good, f.read_row("first", "last.stl")]),
                               (None, [good, f.read_row("last", "first.stl")]),
                               (None, [f.rewrite_row("a", "first.stl", "new"), f.rewrite_row("b", "last.stl", "new")])):
                manifest = root / "domain.jsonl"
                self.manifest(manifest, rows)
                completed, frame = self.command(["batch", manifest.name, "--failure-policy", "continue"], root)
                self.assertEqual(completed.returncode, 2, completed.stderr)
                actual = frame["admission_diagnostic"]["code"]
                self.assertEqual(actual, code) if code else self.assertIn(actual, ("path_admission", "domain_overlap"))
                f.check_frame(self, frame, "batch", ["not_attempted"] * len(frame["jobs"]), 2, actual)
                self.assertFalse((root / "new").exists())
            manifest = root / "line-limit.jsonl"
            manifest.write_bytes(b" " * 262145 + b"\n")
            completed, frame = self.command(["batch", manifest.name, "--failure-policy", "continue"], root)
            self.assertEqual(completed.returncode, 2, completed.stderr)
            f.check_frame(self, frame, "batch", [], 2, "manifest_extent")

    def test_batch_entrypoint_continue_failfast_parity_and_grammar_exits(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "bad.stl").write_bytes(f.binary()[:-1])
            (root / "good.stl").write_bytes(f.ASCII)
            manifest = root / "jobs.jsonl"
            self.manifest(manifest, [f.read_row("first", "bad.stl", "binary"), f.read_row("second", "good.stl")])
            for policy, outcomes in (("continue", ["failed", "read"]), ("fail-fast", ["failed", "not_attempted"])):
                jobs = (sf.StlReadJob("first", "read", root / "bad.stl", "binary", sf.StlUnits("unknown", None)),
                        sf.StlReadJob("second", "read", root / "good.stl", "ascii", sf.StlUnits("unknown", None)))
                api = f.strict_json(sf.run_stl_batch(sf.prepare_stl_batch(jobs, failure_policy=policy)).export_json())
                f.check_frame(self, api, "batch", outcomes, 1)
                for console in (False, True):
                    completed, frame = self.command(["batch", manifest.name, "--failure-policy", policy], root, console)
                    self.assertEqual(completed.returncode, 1, completed.stderr)
                    f.check_frame(self, frame, "batch", outcomes, 1)
                    self.assertEqual(frame["outcome"], "failed")
                    if policy == "fail-fast":
                        self.assertEqual(frame["jobs"][1]["not_attempted_reason"], "fail_fast")
                    for command_job, api_job in zip(frame["jobs"], api["jobs"]):
                        for field in ("job_id", "mode", "declarations", "outcome", "phase", "diagnostic", "observations",
                                      "cleanup", "cleanup_error", "not_attempted_reason", "owners"):
                            self.assertEqual(command_job[field], api_job[field], field)
            bad_invocations = ([], ["repair", "good.stl"], ["read", "good.stl"],
                               ["read", "good.stl", "--source-encoding", "ascii", "--units", "unknown", "--unit-label", "mm"],
                               self.rewrite_args("good.stl", "never") + ["--ascii-name", "inactive"],
                               ["rewrite", "good.stl", "never", "--source-encoding", "ascii", "--output-encoding", "binary",
                                "--metadata", "replace", "--binary-header-hex", "00", "--units", "unknown", "--acknowledge-corner-expansion"])
            for args in bad_invocations:
                completed, frame = self.command(args, root)
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(frame["admission_diagnostic"]["code"], "representation")
                f.check_frame(self, frame, frame["mode"], ["not_attempted"] * len(frame["jobs"]), 2, "representation")
                self.assertFalse((root / "never").exists())

    def test_real_main_postcommit_delivery_failures_and_interruption_preserve_files(self):
        class CallerStream:
            def __init__(self, mode, witness):
                self.mode, self.witness = mode, witness
                self.payload = bytearray()
                self.calls = 0
            def write(self, payload):
                self.calls += 1
                if self.calls == 1:
                    self.witness()
                if self.mode == "write":
                    raise BrokenPipeError("caller-owned failed output")
                if self.mode == "interrupt":
                    raise KeyboardInterrupt()
                if self.mode == "memory":
                    raise MemoryError("caller-owned allocation error")
                if self.mode == "partial" and self.calls > 1:
                    raise OSError("caller-owned partial output")
                count = min(7, len(payload)) if self.mode in ("partial", "short-success") else len(payload)
                self.payload.extend(payload[:count])
                return count
            def flush(self):
                if self.mode == "flush":
                    raise OSError("caller-owned failed flush")
                if self.mode == "flush-interrupt":
                    raise KeyboardInterrupt()
        class CallerOutput:
            def __init__(self, stream):
                self.buffer = stream
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "part.stl"
            source.write_bytes(f.ASCII)
            for mode, expected in (("write", 3), ("partial", 3), ("flush", 3), ("interrupt", 130),
                                   ("flush-interrupt", 130), ("short-success", 0), ("memory", None)):
                with self.subTest(mode=mode):
                    destination = root / mode
                    witnessed = []
                    def observe_committed_files():
                        witnessed.append(self.assert_published_physical(destination, source.name))
                    stream = CallerStream(mode, observe_committed_files)
                    with contextlib.redirect_stdout(CallerOutput(stream)):
                        if mode == "memory":
                            with self.assertRaises(MemoryError):
                                sf.main(self.rewrite_args(source, destination))
                        else:
                            self.assertEqual(sf.main(self.rewrite_args(source, destination)), expected)
                    self.assertTrue(witnessed, "real stream observed files only after native commit")
                    self.assert_published_physical(destination, source.name)
                    self.assertEqual(source.read_bytes(), f.ASCII)
                    if mode in ("flush", "flush-interrupt", "short-success"):
                        # The already-emitted frame cannot attest its later flush.
                        f.check_frame(self, f.strict_json(bytes(stream.payload)), "rewrite", ["published"], 0)
                    if mode == "partial":
                        self.assertEqual(len(stream.payload), 7)
                        self.assertEqual(stream.calls, 2)
            failed_stream = CallerStream("write", lambda: None)
            with contextlib.redirect_stdout(CallerOutput(failed_stream)):
                self.assertEqual(sf.main(["invalid-command"]), 2, "admission2 precedes delivery3")
            (root / "malformed.stl").write_bytes(f.binary()[:-1])
            failed_stream = CallerStream("write", lambda: None)
            with contextlib.redirect_stdout(CallerOutput(failed_stream)):
                self.assertEqual(sf.main(["read", str(root / "malformed.stl"), "--source-encoding", "binary", "--units", "unknown"]),
                                 3, "delivery3 precedes ordinary attempted-job1")


if __name__ == "__main__":
    unittest.main()
