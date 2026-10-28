# SPDX-License-Identifier: Apache-2.0
import _thread
from concurrent.futures import ThreadPoolExecutor
import gc
from pathlib import Path
import tempfile
import threading
import unittest

from meshvale_interchange import stl_files as sf

import stl_files_fixture as f


class StlFileApiTests(unittest.TestCase):
    def context(self):
        return sf.StlExecutionContext(worker_budget=4, minimum_parallel_vertices=65536,
                                      tracked_payload_budget_bytes=1073741824)

    def rewrite(self, source, destination, encoding="ascii", output=None, metadata=None,
                execution=None, report=None, units=None):
        return sf.prepare_stl_rewrite(
            source, destination, source_encoding=encoding, output_encoding=output or encoding,
            metadata=metadata or sf.StlMetadata("retain", None, None),
            units=units or sf.StlUnits("unknown", None), acknowledge_corner_expansion=True,
            execution=execution, report=report)

    def test_literal_physical_roundtrips_metadata_values_normals_and_maps(self):
        cases = ((f.ASCII, "ascii", f.FACETS, f.NAME), (f.binary(), "binary", f.FACETS, None),
                 (f.binary(f.BOUNDARY), "binary", f.BOUNDARY, None),
                 (f.EMPTY_ASCII, "ascii", (), ""), (f.binary(()), "binary", (), None))
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            context = self.context()
            for case, (original, encoding, facets, original_name) in enumerate(cases):
                source = root / (str(case) + "-\u90e8\u54c1.stl")
                source.write_bytes(original)
                for variant in ("retain", "same", "different", "cross"):
                    with self.subTest(case=case, variant=variant):
                        output = ("binary" if encoding == "ascii" else "ascii") if variant == "cross" else encoding
                        name = original_name if variant == "same" else "replacement"
                        header = f.HEADER if variant in ("same", "cross") else bytes([0x5A]) + f.HEADER[1:]
                        metadata = sf.StlMetadata("retain", None, None) if variant == "retain" else sf.StlMetadata(
                            "replace", name if output == "ascii" else None, header if output == "binary" else None)
                        if variant == "retain":
                            name, header = original_name, f.HEADER
                        destination = root / (str(case) + "-" + variant + "-\u516c\u958b")
                        prepared = self.rewrite(source, destination, encoding, output, metadata, context)
                        holder = prepared.result
                        self.assertIs(holder, prepared.result)
                        self.assertEqual(holder.outcome, "not_attempted")
                        result = sf.run_stl(prepared)
                        self.assertIs(result, holder)
                        self.assertEqual((result.outcome, result.exit_code), ("published", 0), result.diagnostic)
                        self.assertIs(result.bundle, result.bundle)
                        self.assertEqual(result.asset.sha256, result.verified.source.sha256)
                        self.assertEqual(result.output.sha256, result.verified.output.sha256)
                        self.assertEqual(result.fresh_document.export_bytes(), result.verified.document.export_bytes())
                        self.assertEqual(result.verified.sha256, result.bundle.verified.sha256)
                        self.assertIs(result.verified.document, result.verified.document)
                        self.assertEqual(result.declarations,
                                         {"source_encoding": encoding, "output_encoding": output,
                                          "metadata_mode": metadata.mode,
                                          "units": {"kind": "unknown", "label": None, "rescaled": False},
                                          "acknowledge_corner_expansion": True})
                        self.assertIsNone(result.captured)
                        self.assertEqual(source.read_bytes(), original)
                        self.assertEqual({p.name for p in destination.iterdir()}, {source.name, f.RECEIPT})
                        physical = (destination / source.name).read_bytes()
                        self.assertEqual(physical, result.output.export_bytes())
                        self.assertEqual(physical, result.verified.export_bytes())
                        self.assertEqual(result.verified.sha256, f.sha(physical))
                        self.assertEqual(result.output.byte_count, len(physical))
                        self.assertEqual(result.output.sha256, f.sha(physical))
                        self.assertEqual(result.output.export_corner_source_vertices(), tuple(range(3 * len(facets))))
                        self.assertIs(type(result.output.export_corner_source_vertices()), tuple)
                        self.assertEqual(result.asset.export_bytes(), original)
                        self.assertEqual(result.asset.entry, source.name)
                        self.assertEqual(result.verified.entry, source.name)
                        self.assertEqual(result.asset.sha256, f.sha(original))
                        f.check_document(self, result.asset.document, encoding, facets, original_name)
                        f.check_document(self, result.fresh_document, output, facets, name, header)
                        if output == "binary":
                            f.check_binary(self, physical, facets, header)
                        else:
                            f.check_ascii(self, physical, facets, name)
                        actual = "retained_source_metadata" if variant in ("retain", "same") else "replaced_source_metadata"
                        facts = result.output.facts
                        self.assertEqual(facts, {"encoding": output, "face_normal_attribute": 0,
                                                "source_vertex_count": 3 * len(facets), "source_face_count": len(facets),
                                                "corner_expansion_acknowledged": True, "source_encoding": encoding,
                                                "metadata_policy": actual, "bytes": len(physical), "sha256": f.sha(physical)})
                        f.check_receipt(self, result.bundle, original, physical, source_encoding=encoding,
                                        output_encoding=output, entry=source.name, facets=facets,
                                        metadata_mode=metadata.mode, actual=actual, name=name, header=header)
                        self.assertEqual((destination / f.RECEIPT).read_bytes(), result.bundle.export_receipt_bytes())
                        reread = sf.read_stl_file(destination / source.name, source_encoding=output,
                                                  units=sf.StlUnits("unknown", None), execution=context)
                        self.assertEqual(reread.outcome, "read", reread.diagnostic)
                        f.check_document(self, reread.asset.document, output, facets, name, header)
                        f.check_observations(self, result.observations)
                        f.check_execution(self, result.execution)
                        self.assertEqual(result.delivery, {"state": "not_attempted", "code": None, "bytes_written": 0})
                        frame_bytes = result.export_json()
                        f.check_frame(self, f.strict_json(frame_bytes), "rewrite", ["published"], 0)
                        self.assertEqual(result.delivery, {"state": "succeeded", "code": None, "bytes_written": len(frame_bytes)})
                        facts["metadata_policy"] = "tampered detached facts"
                        self.assertEqual(result.output.facts["metadata_policy"], actual)
                        detached = result.observations
                        detached["publication"]["facts"]["outcome"] = "tampered"
                        self.assertEqual(result.observations["publication"]["facts"], {"outcome": "published"})
                        self.assertEqual(context.active_workers, 0)

    def test_read_admission_and_real_codec_refusals_keep_reached_facts(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            missing = sf.read_stl_file(root / "missing.stl", source_encoding="ascii", units=sf.StlUnits("unknown", None))
            self.assertEqual((missing.outcome, missing.exit_code), ("not_attempted", 2))
            self.assertEqual(missing.not_attempted_reason, "admission_failed")
            self.assertIsNone(missing.diagnostic)
            self.assertIsNone(missing.captured)
            self.assertIsNone(missing.asset)
            self.assertEqual(missing.observations["capture"]["state"], "not_attempted")
            self.assertEqual(missing.observations["read"]["state"], "not_attempted")
            self.assertEqual(missing.execution["capture"], dict.fromkeys(f.STATS_KEYS, 0))
            f.check_frame(self, f.strict_json(missing.export_json()), "read", ["not_attempted"], 2, "path_admission")
            variants = [f.binary()[:-1], f.binary() + b"\0",
                        f.binary()[:132] + b"\1" + f.binary()[133:]]
            for index, raw in enumerate(variants):
                path = root / (str(index) + ".stl")
                path.write_bytes(raw)
                result = sf.read_stl_file(path, source_encoding="binary", units=sf.StlUnits("unknown", None))
                self.assertEqual((result.outcome, result.exit_code), ("failed", 1))
                self.assertIsNone(result.asset)
                self.assertEqual(result.captured.export_bytes(), raw)
                self.assertEqual(result.captured.sha256, f.sha(raw))
                self.assertEqual(result.observations["capture"]["facts"], {"entry": path.name, "bytes": len(raw), "sha256": f.sha(raw)})
                self.assertEqual(result.observations["read"]["state"], "failed")
                self.assertEqual(result.diagnostic["code"], "delegated_stl")
                f.check_frame(self, f.strict_json(result.export_json()), "read", ["failed"], 1)

    def test_real_inexact_binary32_refusal_preserves_source_asset(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "double.stl"
            raw = f.ASCII.replace(b"vertex 1 0 0", b"vertex 0.1 0 0", 1)
            source.write_bytes(raw)
            prepared = self.rewrite(source, root / "refused", output="binary",
                                    metadata=sf.StlMetadata("replace", None, f.HEADER))
            result = sf.run_stl(prepared)
            self.assertEqual(result.outcome, "failed")
            self.assertEqual(result.diagnostic["stl_status"], "unsupported_numeric_conversion")
            self.assertEqual(result.asset.export_bytes(), raw)
            self.assertIsNone(result.output)
            self.assertFalse((root / "refused").exists())
            self.assertEqual(result.observations["write"]["state"], "failed")

    def test_provider_str_bytes_empty_and_escaped_receipt_with_source_removal(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for index, report in enumerate(("", b"", "quote \"\n\u96ea", b"strict bytes")):
                source = root / (str(index) + ".stl")
                source.write_bytes(f.ASCII)
                held, calls = [], []
                def provider(verified):
                    calls.append(True)
                    self.assertIsInstance(verified, sf.StlVerifiedRewrite)
                    f.check_document(self, verified.document, "ascii")
                    held.append(verified)
                    source.unlink()
                    return report
                prepared = self.rewrite(source, root / ("bundle" + str(index)), report=provider,
                                        units=sf.StlUnits("label", "mm"))
                result = sf.run_stl(prepared)
                self.assertEqual(result.outcome, "published", result.diagnostic)
                self.assertEqual(calls, [True])
                physical = result.verified.export_bytes()
                f.check_receipt(self, result.bundle, f.ASCII, physical, source_encoding="ascii",
                                output_encoding="ascii", entry=source.name, report=report.decode() if type(report) is bytes else report,
                                unit="mm")
                del result, prepared
                gc.collect()
                self.assertEqual(held[0].source.export_bytes(), f.ASCII)
                f.check_document(self, held[0].document, "ascii")
                with ThreadPoolExecutor(max_workers=2) as executor:
                    self.assertEqual(list(executor.map(lambda _: held[0].export_bytes(), range(4))), [physical] * 4)

    def test_provider_failures_interruptions_and_memoryerror_are_distinct(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for kind in ("ordinary", "wrong_type", "invalid_utf8", "too_large", "keyboard", "pending", "memory"):
                with self.subTest(kind=kind):
                    source = root / (kind + ".stl")
                    source.write_bytes(f.ASCII)
                    expected_members = {path.name for path in root.iterdir()}
                    destination = root / (kind + "-bundle")
                    context = self.context()
                    retained, calls = [], []
                    def provider(verified):
                        retained.append(verified)
                        calls.append(kind)
                        if kind == "ordinary":
                            raise RuntimeError("ordinary provider error")
                        if kind == "wrong_type":
                            return 7
                        if kind == "invalid_utf8":
                            return b"\xff"
                        if kind == "too_large":
                            return b"x" * 65537
                        if kind == "keyboard":
                            raise KeyboardInterrupt()
                        if kind == "pending":
                            _thread.interrupt_main()
                            return "pending interruption"
                        raise MemoryError("distinct provider allocation error")
                    prepared = self.rewrite(source, destination, execution=context, report=provider)
                    holder = prepared.result
                    if kind == "memory":
                        with self.assertRaises(MemoryError):
                            sf.run_stl(prepared)
                        self.assertIs(prepared.result, holder)
                        self.assertIsNone(holder.bundle)
                        # Unexpected unwind does not invent a terminal scalar
                        # result. Repeated public terminal observation remains
                        # safe and must propagate the allocation exception.
                        for observe in (lambda: holder.exit_code, holder.export_json):
                            for _ in range(3):
                                with self.assertRaises(MemoryError):
                                    observe()
                    else:
                        result = sf.run_stl(prepared)
                        self.assertIs(result, holder)
                        interrupted = kind in ("keyboard", "pending")
                        self.assertEqual(result.outcome, "canceled" if interrupted else "failed")
                        self.assertEqual(result.exit_code, 130 if interrupted else 1)
                        self.assertEqual(result.observations["provider"]["state"], "canceled" if interrupted else "failed")
                        self.assertIsNotNone(result.verified)
                        self.assertIsNotNone(result.fresh_document)
                        self.assertIsNotNone(result.output)
                        self.assertEqual(result.observations["verification"]["state"], "succeeded")
                        self.assertIsNone(result.bundle)
                        self.assertEqual(result.cleanup, "removed")
                    self.assertEqual(calls, [kind])
                    self.assertFalse(destination.exists())
                    self.assertEqual({path.name for path in root.iterdir()}, expected_members)
                    self.assertEqual(context.active_workers, 0)
                    self.assertEqual(retained[0].source.export_bytes(), f.ASCII)

    def test_real_cross_thread_stop_at_verified_provider_boundary(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "source.stl"
            source.write_bytes(f.ASCII)
            token = sf.Cancellation()
            ready, release = threading.Event(), threading.Event()
            def provider(verified):
                ready.set()
                if not release.wait(5):
                    raise AssertionError("caller did not release verified provider")
                return "bounded report"
            prepared = self.rewrite(source, root / "canceled", report=provider)
            with ThreadPoolExecutor(max_workers=1) as executor:
                future = executor.submit(sf.run_stl, prepared, cancellation=token)
                try:
                    self.assertTrue(ready.wait(5), "actual verified provider boundary reached")
                    self.assertTrue(token.request_stop())
                    self.assertTrue(token.stop_requested)
                    self.assertFalse(token.request_stop())
                finally:
                    release.set()
                result = future.result(timeout=10)
            self.assertIs(result, prepared.result)
            self.assertEqual((result.outcome, result.exit_code), ("canceled", 130))
            self.assertIsNotNone(result.verified)
            self.assertIsNone(result.bundle)
            self.assertFalse((root / "canceled").exists())

    def test_existing_destination_is_admitted_before_source_capture(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "part.stl"
            source.write_bytes(f.ASCII)
            destination = root / "exists"
            destination.mkdir()
            (destination / "winner").write_bytes(b"existing bytes")
            prepared = self.rewrite(source, destination)
            result = sf.run_stl(prepared)
            self.assertEqual(result.outcome, "not_attempted")
            self.assertEqual(result.not_attempted_reason, "admission_failed")
            self.assertEqual(result.exit_code, 2)
            self.assertEqual((destination / "winner").read_bytes(), b"existing bytes")
            self.assertEqual(source.read_bytes(), f.ASCII)
            self.assertIsNone(result.asset)

    def test_prepared_run_is_one_shot_without_erasing_real_publication(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "part.stl"
            source.write_bytes(f.ASCII)
            calls = []
            def report(verified):
                calls.append(verified.sha256)
                return "once"
            prepared = self.rewrite(source, root / "bundle", report=report)
            result = sf.run_stl(prepared)
            self.assertEqual(result.outcome, "published")
            bundle = result.bundle
            before = {p.name: p.read_bytes() for p in (root / "bundle").iterdir()}
            again = sf.run_stl(prepared)
            self.assertIs(again, result)
            self.assertIs(again.bundle, bundle)
            self.assertEqual(again.outcome, "published")
            self.assertEqual(again.observations["publication"]["state"], "succeeded")
            self.assertEqual(len(calls), 1)
            self.assertEqual({p.name: p.read_bytes() for p in (root / "bundle").iterdir()}, before)
            f.check_frame(self, f.strict_json(again.export_json()), "rewrite", ["published"], 2, "previously_run")

    def test_read_detached_exports_and_observations_cannot_mutate_retained_owner(self):
        with tempfile.TemporaryDirectory() as scratch:
            source = Path(scratch) / "part.stl"
            source.write_bytes(f.ASCII)
            prepared = sf.prepare_stl_read(source, source_encoding="ascii", units=sf.StlUnits("unknown", None))
            result = sf.run_stl(prepared)
            self.assertEqual(result.declarations,
                             {"source_encoding": "ascii", "output_encoding": None, "metadata_mode": None,
                              "units": {"kind": "unknown", "label": None, "rescaled": False},
                              "acknowledge_corner_expansion": None})
            self.assertIsNone(result.captured)
            self.assertEqual(result.observations["capture"]["state"], "succeeded")
            self.assertEqual(result.observations["read"]["state"], "succeeded")
            self.assertTrue(all(result.observations[key]["state"] == "not_applicable" for key in f.STAGES[2:]))
            document = result.asset.document
            raw = document.export_bytes()
            self.assertIs(type(raw), bytes)
            detached = bytearray(raw)
            detached[0] ^= 1
            self.assertEqual(document.export_bytes(), f.ASCII)
            record = document.export_mesh().to_record()
            record["attributes"][0]["name"] = "detached replacement"
            f.check_document(self, document, "ascii")
            observations = result.observations
            observations["read"]["facts"]["facets"] = 999
            self.assertEqual(result.observations["read"]["facts"]["facets"], 3)
            for owner, field in ((document, "encoding"), (result.asset, "entry"), (result, "outcome")):
                with self.assertRaises(AttributeError):
                    setattr(owner, field, "forged")
            source.unlink()
            del prepared, result
            gc.collect()
            self.assertEqual(document.export_bytes(), f.ASCII)


if __name__ == "__main__":
    unittest.main()
