# SPDX-License-Identifier: Apache-2.0
from dataclasses import FrozenInstanceError
import gc
from importlib.resources import files
import os
from pathlib import Path
import tempfile
import unittest

from meshvale_interchange import stl_files as sf

import stl_files_fixture as f


def expected_bounded_preset():
    """Independent full resource worksheet from closed baseline0.1.0."""
    def file_limits(publication=False):
        return dict(max_entry_bytes=67108864, max_receipt_bytes=1048576, max_total_file_bytes=68157440,
                    max_entry_name_bytes=240, max_native_path_code_units=32768, max_unit_label_bytes=128,
                    max_caller_report_bytes=65536, max_file_new_requested_bytes=134217728 if publication else 100663296)
    def codec_read():
        return dict(max_source_bytes=67108864, max_facets=100000, max_name_bytes=1024,
                    max_line_bytes=8192, max_token_bytes=128, max_codec_new_requested_bytes=100663296,
                    max_total_new_requested_bytes=176160768,
                    materialization=dict(max_vertices=300000, max_faces=100000, max_corners=300000,
                                         max_face_offsets=100001, max_attributes=1, max_attribute_scalars=300000,
                                         max_attribute_offsets=1, max_presence_bytes=1, max_metadata_entries=1,
                                         max_descriptor_bytes=4096, max_new_requested_bytes=67108864),
                    inspection=dict(max_issues=1024, max_new_requested_bytes=8388608))
    def file_work():
        return dict(io_block_bytes=65536, parallel_min_bytes=65536, partition_bytes=32768,
                    cancellation_block_bytes=4096, max_jobs=4)
    def codec_work():
        return dict(cancellation_block_bytes=4096, cancellation_block_tokens=128, cancellation_block_facets=128,
                    geometry=dict(parallel_min_bytes=65536, partition_bytes=32768,
                                  cancellation_block_scalars=1024, max_jobs=4))
    return dict(
        application_limits=dict(max_jobs=4, max_manifest_bytes=1048576, max_manifest_line_bytes=262144,
                                max_job_id_bytes=128, max_result_bytes=1048576, max_delivery_bytes=1048576,
                                max_application_new_requested_bytes=67108864, max_job_new_requested_bytes=696254464),
        single_execution=dict(worker_budget=4, minimum_parallel_vertices=65536, tracked_payload_budget_bytes=1073741824),
        batch_execution=dict(worker_budget=4, minimum_parallel_vertices=65536, tracked_payload_budget_bytes=4294967296),
        read_limits=dict(files=file_limits(), read=codec_read()),
        read_work=dict(files=file_work(), read=codec_work()),
        rewrite_limits=dict(files=file_limits(True),
                            write=dict(max_output_bytes=67108864, max_facets=100000, max_name_bytes=1024,
                                       max_line_bytes=8192, max_token_bytes=128, max_codec_new_requested_bytes=100663296,
                                       max_total_new_requested_bytes=109051904,
                                       inspection=dict(max_issues=1024, max_new_requested_bytes=8388608)),
                            fresh_read=codec_read(), max_total_new_requested_bytes=419430400),
        rewrite_work=dict(files=file_work(), write=codec_work(), fresh_read=codec_work()))


class StlFileAdmissionTests(unittest.TestCase):
    def test_packaged_finite_resource_baseline_has_exact_111_positive_leaves(self):
        # Package artifact inspection only; no hidden native override/API is used.
        raw = files("meshvale_interchange").joinpath("_stl_file_presets.json").read_bytes()
        data = f.strict_json(raw)
        expected = expected_bounded_preset()
        self.assertEqual(data, {"schema": "meshvale.stl-file-presets/1", "contract": "STL-FILES-001/0.1.0",
                                "presets": {"bounded": expected}})
        def leaves(value):
            if isinstance(value, dict):
                return [leaf for child in value.values() for leaf in leaves(child)]
            self.assertIs(type(value), int)
            self.assertGreater(value, 0)
            return [value]
        self.assertEqual(len(leaves(expected)), 111)
        self.assertEqual(len(leaves(expected["read_limits"])) + len(leaves(expected["read_work"])), 40)
        self.assertEqual(len(leaves(expected["rewrite_limits"])) + len(leaves(expected["rewrite_work"])), 57)
        self.assertEqual(sum(len(leaves(expected[k])) for k in ("application_limits", "single_execution", "batch_execution")), 14)
        # These are111 baseline leaves, not111 mutable public/native tests.

    def test_all_explicit_execution_fields_require_positive_builtin_integers(self):
        valid = dict(worker_budget=4, minimum_parallel_vertices=65536, tracked_payload_budget_bytes=1073741824)
        for field in valid:
            for bad in (0, -1, True, 1.5, "4", None, 1 << 128):
                with self.subTest(field=field, bad=bad):
                    args = dict(valid, **{field: bad})
                    with self.assertRaises((TypeError, ValueError, OverflowError)):
                        sf.StlExecutionContext(**args)
            args = dict(valid)
            del args[field]
            with self.assertRaises(TypeError):
                sf.StlExecutionContext(**args)
        class IntegerSubclass(int):
            pass
        with self.assertRaises(TypeError):
            sf.StlExecutionContext(**dict(valid, worker_budget=IntegerSubclass(4)))
        context = sf.StlExecutionContext(**valid)
        for name in ("worker_budget", "active_workers", "peak_workers", "tracked_payload_budget_bytes",
                     "active_tracked_payload_bytes", "peak_tracked_payload_bytes"):
            self.assertIs(type(getattr(context, name)), int)
            with self.assertRaises(AttributeError):
                setattr(context, name, 7)

    def test_metadata_units_jobs_and_opaque_owners_cannot_be_forged(self):
        bad_metadata = (("infer", None, None), ("retain", "", None), ("retain", None, f.HEADER),
                        ("replace", None, None), ("replace", "name", f.HEADER),
                        ("replace", "\n", None), ("replace", "\u96ea", None),
                        ("replace", None, b""), ("replace", None, f.HEADER[:-1]),
                        ("replace", None, f.HEADER + b"x"), ("replace", None, bytearray(f.HEADER)))
        for args in bad_metadata:
            with self.subTest(metadata=args):
                with self.assertRaises((TypeError, ValueError, OverflowError)):
                    sf.StlMetadata(*args)
        for args in (("infer", None), ("unknown", "mm"), ("label", None), ("label", ""),
                     ("label", "\0"), ("label", "\n"), ("label", "\x7f"), ("label", "\x81"), ("label", "\ud800")):
            with self.subTest(units=args):
                with self.assertRaises((TypeError, ValueError, OverflowError)):
                    sf.StlUnits(*args)
        for value in (sf.StlUnits("label", "mm"), sf.StlMetadata("replace", "", None),
                      sf.StlReadJob("id", "read", "part.stl", "ascii", sf.StlUnits("unknown", None))):
            with self.assertRaises((AttributeError, FrozenInstanceError, TypeError)):
                value.mode = "tampered"
        for identifier in ("", "a\0b", "a\nb", "\x81", "\ud800", 7, b"bytes-id"):
            with self.assertRaises((TypeError, ValueError, OverflowError)):
                sf.StlReadJob(identifier, "read", "part.stl", "ascii", sf.StlUnits("unknown", None))
        with self.assertRaises(TypeError):
            sf.StlReadJob("id", "read", "part.stl", "ascii", sf.StlUnits("unknown", None), destination="forbidden")
        with self.assertRaises((TypeError, ValueError)):
            sf.StlReadJob("id", "rewrite", "part.stl", "ascii", sf.StlUnits("unknown", None))
        with self.assertRaises(TypeError):
            sf.StlRewriteJob("id", "rewrite", "part.stl", "ascii", sf.StlUnits("unknown", None), "output", "ascii",
                             sf.StlMetadata("retain", None, None), True, b"report_text must be str")
        for owner in (sf.StlCapturedFile, sf.StlDocument, sf.StlFileAsset, sf.StlOutput,
                      sf.StlVerifiedRewrite, sf.StlPublishedBundle):
            with self.subTest(owner=owner.__name__):
                with self.assertRaises(TypeError):
                    owner()

    def test_representation_errors_and_pre_stopped_options_have_zero_file_side_effects(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "part.stl"
            source.write_bytes(f.ASCII)
            token = sf.Cancellation()
            token.request_stop()
            calls = []
            def provider(verified):
                calls.append(verified)
                return "unreachable"
            good = dict(source_encoding="ascii", output_encoding="ascii", metadata=sf.StlMetadata("retain", None, None),
                        units=sf.StlUnits("unknown", None), acknowledge_corner_expansion=True, report=provider)
            changes = [dict(source_encoding="infer"), dict(output_encoding="auto"), dict(metadata={"mode": "retain"}),
                       dict(units={"kind": "unknown"}), dict(acknowledge_corner_expansion=1),
                       dict(acknowledge_corner_expansion=False), dict(report=7), dict(preset="missing"),
                       dict(execution=object()), dict(output_encoding="binary"),
                       dict(metadata=sf.StlMetadata("replace", None, f.HEADER))]
            for index, change in enumerate(changes):
                with self.subTest(change=change):
                    with self.assertRaises((TypeError, ValueError, OverflowError)):
                        prepared = sf.prepare_stl_rewrite(source, root / ("bad" + str(index)), **dict(good, **change))
                        sf.run_stl(prepared, cancellation=token)
                    self.assertEqual(source.read_bytes(), f.ASCII)
                    self.assertEqual({p.name for p in root.iterdir()}, {source.name})
                    self.assertEqual(calls, [])
            for path in (None, 7, "", b"", "a\0b", b"a\0b"):
                with self.assertRaises((TypeError, ValueError, OverflowError)):
                    sf.prepare_stl_read(path, source_encoding="ascii", units=sf.StlUnits("unknown", None))
            prepared = sf.prepare_stl_rewrite(source, root / "stopped", **good)
            result = sf.run_stl(prepared, cancellation=token)
            self.assertIs(result, prepared.result)
            self.assertEqual((result.outcome, result.exit_code), ("canceled", 130))
            self.assertIsNone(result.asset)
            self.assertEqual(calls, [])
            self.assertEqual({p.name for p in root.iterdir()}, {source.name})

    def test_native_pathlike_and_preparation_working_directory_are_copied(self):
        class MutablePath:
            def __init__(self, value):
                self.value = value
            def __fspath__(self):
                return self.value
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "\u90e8\u54c1.stl"
            source.write_bytes(f.ASCII)
            before = Path.cwd()
            try:
                os.chdir(root)
                path = MutablePath(source.name)
                prepared = sf.prepare_stl_read(path, source_encoding="ascii", units=sf.StlUnits("unknown", None))
                path.value = "now-missing.stl"
                os.chdir(root.parent)
                result = sf.run_stl(prepared)
                self.assertEqual(result.outcome, "read", result.diagnostic)
                self.assertEqual(result.asset.export_bytes(), f.ASCII)
            finally:
                os.chdir(before)
            for value in (str(source), os.fsencode(source), source):
                result = sf.read_stl_file(value, source_encoding="ascii", units=sf.StlUnits("unknown", None))
                self.assertEqual(result.outcome, "read", result.diagnostic)

    def test_real_root_budget_admission_and_last_owner_release(self):
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / "part.stl"
            source.write_bytes(f.ASCII)
            tiny = sf.StlExecutionContext(worker_budget=4, minimum_parallel_vertices=65536,
                                          tracked_payload_budget_bytes=67108864)
            prepared = sf.prepare_stl_read(source, source_encoding="ascii", units=sf.StlUnits("unknown", None), execution=tiny)
            result = sf.run_stl(prepared)
            frame = f.strict_json(result.export_json())
            f.check_frame(self, frame, "read", ["not_attempted"], 2, "root_budget")
            self.assertEqual(result.not_attempted_reason, "admission_failed")
            self.assertIsNone(result.captured)
            self.assertIsNone(result.asset)
            context = sf.StlExecutionContext(worker_budget=4, minimum_parallel_vertices=65536,
                                             tracked_payload_budget_bytes=1073741824)
            prepared = sf.prepare_stl_read(source, source_encoding="ascii", units=sf.StlUnits("unknown", None), execution=context)
            result = sf.run_stl(prepared)
            asset = result.asset
            self.assertGreater(context.active_tracked_payload_bytes, 0)
            del prepared, result
            gc.collect()
            self.assertGreater(context.active_tracked_payload_bytes, 0)
            self.assertEqual(asset.export_bytes(), f.ASCII)
            del asset
            gc.collect()
            self.assertEqual(context.active_tracked_payload_bytes, 0)
            self.assertEqual(context.active_workers, 0)

    def test_ordered_batch_continue_failfast_prestop_and_complete_admission(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "bad.stl").write_bytes(f.binary()[:-1])
            (root / "good.stl").write_bytes(f.ASCII)
            def jobs():
                return (sf.StlReadJob("first", "read", root / "bad.stl", "binary", sf.StlUnits("unknown", None)),
                        sf.StlReadJob("second", "read", root / "good.stl", "ascii", sf.StlUnits("unknown", None)))
            for policy, outcomes in (("continue", ["failed", "read"]), ("fail-fast", ["failed", "not_attempted"])):
                prepared = sf.prepare_stl_batch(jobs(), failure_policy=policy)
                slots = prepared.result.jobs
                result = sf.run_stl_batch(prepared)
                self.assertIs(result, prepared.result)
                self.assertEqual([slot.outcome for slot in slots], outcomes)
                self.assertTrue(all(a is b for a, b in zip(slots, result.jobs)))
                self.assertEqual([slot.job_id for slot in result.jobs], ["first", "second"])
                self.assertEqual(result.exit_code, 1)
                self.assertEqual(result.outcome, "failed")
                if policy == "fail-fast":
                    self.assertEqual(result.jobs[1].not_attempted_reason, "fail_fast")
                    self.assertIsNone(result.jobs[1].diagnostic)
                f.check_frame(self, f.strict_json(result.export_json()), "batch", outcomes, 1)
                self.assertTrue(all(slot.delivery == {"state": "not_attempted", "code": None, "bytes_written": 0} for slot in slots))
            token = sf.Cancellation()
            token.request_stop()
            prepared = sf.prepare_stl_batch(jobs())
            result = sf.run_stl_batch(prepared, cancellation=token)
            self.assertEqual((result.outcome, result.exit_code), ("canceled", 130))
            self.assertTrue(all(job.outcome == "not_attempted" and job.not_attempted_reason == "canceled" for job in result.jobs))
            duplicate = list(jobs())
            duplicate[1] = sf.StlReadJob("first", "read", root / "good.stl", "ascii", sf.StlUnits("unknown", None))
            prepared = sf.prepare_stl_batch(duplicate)
            result = sf.run_stl_batch(prepared)
            f.check_frame(self, f.strict_json(result.export_json()), "batch", ["not_attempted"] * 2, 2, "duplicate_job_id")
            self.assertTrue(all(job.not_attempted_reason == "admission_failed" and job.captured is None and job.asset is None for job in result.jobs))

    def test_batch_iterable_is_fully_materialized_and_job_cap_is_not_unbounded(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            entered = []
            def invalid_last():
                for index in range(2):
                    entered.append(index)
                    source = root / (str(index) + ".stl")
                    source.write_bytes(f.ASCII)
                    yield sf.StlRewriteJob(str(index), "rewrite", source, "ascii", sf.StlUnits("unknown", None),
                                           root / ("out" + str(index)), "ascii", sf.StlMetadata("retain", None, None), True, None)
                entered.append(2)
                yield object()
            with self.assertRaises((TypeError, ValueError)):
                sf.prepare_stl_batch(invalid_last())
            self.assertEqual(entered, [0, 1, 2])
            self.assertEqual({p.name for p in root.iterdir()}, {"0.stl", "1.stl"})
            rows = []
            for index in range(5):
                source = root / ("cap" + str(index) + ".stl")
                source.write_bytes(f.ASCII)
                rows.append(sf.StlReadJob(str(index), "read", source, "ascii", sf.StlUnits("unknown", None)))
            prepared = sf.prepare_stl_batch(rows)
            result = sf.run_stl_batch(prepared)
            self.assertEqual(len(result.jobs), 4)
            f.check_frame(self, f.strict_json(result.export_json()), "batch", ["not_attempted"] * 4, 2, "job_extent")
            with self.assertRaises((TypeError, ValueError)):
                sf.prepare_stl_batch(rows[:1], failure_policy="skip")

    def test_observed_file_extent_refuses_before_payload_or_codec_owner(self):
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / "extent.stl"
            # A real file extent just above the published 64MiB capture limit;
            # do not allocate or write a 64MiB Python payload to manufacture it.
            with source.open("wb") as stream:
                stream.truncate(67108865)
            result = sf.read_stl_file(source, source_encoding="binary", units=sf.StlUnits("unknown", None))
            self.assertEqual((result.outcome, result.exit_code), ("failed", 1))
            self.assertEqual(result.phase, "capture")
            self.assertEqual(result.diagnostic["status"], "limit_exceeded")
            self.assertIsNone(result.captured)
            self.assertIsNone(result.asset)
            self.assertEqual(result.observations["capture"]["state"], "failed")
            self.assertEqual(result.observations["read"]["state"], "not_attempted")
            self.assertEqual(source.stat().st_size, 67108865)

    def test_exact_public_job_id_and_caller_report_byte_boundaries(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "part.stl"
            source.write_bytes(f.ASCII)
            identifier = "i" * 128
            job = sf.StlReadJob(identifier, "read", source, "ascii", sf.StlUnits("unknown", None))
            result = sf.run_stl_batch(sf.prepare_stl_batch((job,)))
            self.assertEqual(result.jobs[0].job_id, identifier)
            self.assertEqual(result.jobs[0].outcome, "read")
            report = "\u96ea" * 21845 + "x"
            self.assertEqual(len(report.encode("utf-8")), 65536)
            prepared = sf.prepare_stl_rewrite(source, root / "report-limit", source_encoding="ascii", output_encoding="ascii",
                                              metadata=sf.StlMetadata("retain", None, None), units=sf.StlUnits("unknown", None),
                                              acknowledge_corner_expansion=True, report=lambda verified: report)
            result = sf.run_stl(prepared)
            self.assertEqual(result.outcome, "published", result.diagnostic)
            self.assertEqual(f.strict_json(result.bundle.export_receipt_bytes())["caller_report"], report)


if __name__ == "__main__":
    unittest.main()
