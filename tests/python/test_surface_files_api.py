# SPDX-License-Identifier: Apache-2.0
"""Installed public API controls; require an explicitly enabled Surface build."""
from dataclasses import replace
import gc
import hashlib
from importlib.resources import files
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

from meshvale_interchange import Cancellation
from meshvale_interchange import surface_files as sf

import surface_files_fixture as f


class SurfaceFileApiTests(unittest.TestCase):
    def test_catalog_association_and_resource_bytes_survive_source_release(self):
        for encoding in ("gltf-json", "glb"):
            with self.subTest(encoding=encoding):
                json_source, image_source = bytearray(f.CATALOG), bytearray(f.PNG)
                catalog = sf.SurfaceCatalog(json_source, (sf.SurfaceResource("image", 0, image_source),))
                source = f.catalog_record()
                operation = sf.prepare_surface(f.catalog_options(encoding), preset="bounded")
                result = operation.run(source, catalog=catalog)
                self.assertTrue(result.complete, result.phase_states)
                self.assertEqual(result.catalog_read["status"], "kAccepted")
                self.assertEqual(result.phase_states["catalog-read"], "returned")
                # Resizing is also a direct check that no source buffer export
                # remains pinned after the joined public run.
                json_source.clear()
                image_source.clear()
                source["positions"].append(99)
                source["attributes"][0]["values"].append(99)
                members = {m.path: bytes(m.bytes) for m in result.output.members}
                document, binary = f.gltf_payload(encoding, members)
                primitive = document["meshes"][0]["primitives"][0]
                self.assertEqual(primitive["material"], 0)
                self.assertIn("TEXCOORD_0", primitive["attributes"])
                texture = document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]
                self.assertEqual(texture["index"], 0)
                self.assertEqual(texture.get("texCoord", 0), 0)
                self.assertEqual(document["textures"][0]["source"], 0)
                image = document["images"][0]
                self.assertEqual(image["mimeType"], "image/png")
                if binary is None:
                    self.assertEqual(members[image["uri"]], f.PNG)
                else:
                    view = document["bufferViews"][image["bufferView"]]
                    start = view.get("byteOffset", 0)
                    self.assertEqual(binary[start:start + view["byteLength"]], f.PNG)

    def test_eight_routes_decode_actual_bytes_and_maps(self):
        for encoding in f.ENCODINGS:
            for representation in f.REPRESENTATIONS:
                with self.subTest(encoding=encoding, representation=representation):
                    execution = sf.SurfaceExecutionContext(preset="bounded")
                    operation = sf.prepare_surface(f.options(encoding, representation),
                                                   preset="bounded", execution=execution)
                    holder = operation.result
                    self.assertEqual(operation.state, "ready")
                    result = operation.run(f.record(normals=encoding.startswith("stl-")), catalog=None)
                    self.assertIs(result, holder)
                    self.assertEqual(operation.state, "finished")
                    self.assertTrue(result.complete, result.phase_states)
                    self.assertEqual(result.terminal_phase, "complete")
                    self.assertEqual(result.encode["status"], "kSuccess")
                    self.assertEqual(result.inspection["status"], "kSuccess")
                    self.assertIsNone(result.catalog_read)
                    self.assertIsNone(result.publication)
                    self.assertEqual(result.phase_states["catalog-read"], "not-called")
                    self.assertEqual(result.phase_states["publication"], "not-called")
                    self.assertIsNone(result.triangulation["source_capture"])
                    output = result.output
                    self.assertEqual((output.encoding, output.representation), (encoding, representation))
                    members = {member.path: bytes(member.bytes) for member in output.members}
                    decoded = f.payload_positions(encoding, members)
                    self.assertEqual(decoded, f.POSITIONS)
                    self.assertEqual(struct.pack(">d", decoded[0]), bytes.fromhex("8000000000000000"))
                    self.assertEqual({m.path: bytes(m.bytes) for m in result.verified.members}, members)
                    for key in ("wire_corner_sources", "wire_vertex_sources"):
                        view = output.wire_maps[key]
                        self.assertEqual((view.format, view.itemsize, tuple(view)), ("Q", 8, (0, 1, 2)))
                        self.assertTrue(view.readonly)
                        with self.assertRaises(TypeError):
                            view[0] = 7
                    self.assertEqual(tuple(output.triangulation_maps["face_sources"]), (0,))
                    self.assertEqual(tuple(output.triangulation_maps["corner_sources"]), (0, 1, 2))
                    self.assertEqual(tuple(output.triangulation_maps["face_output_offsets"]), (0, 1))
                    with self.assertRaises(TypeError):
                        result.phase_states["encode"] = "not-called"
                    with self.assertRaises(TypeError):
                        result.encode["status"] = "invented"
                    with self.assertRaises(RuntimeError):
                        operation.run(object(), catalog=object())
                    self.assertTrue(result.complete)
                    self.assertEqual(execution.active_workers, 0)

    def test_readonly_views_retain_native_owner_after_result_deletion(self):
        execution = sf.SurfaceExecutionContext(preset="bounded")
        operation = sf.prepare_surface(f.options(), preset="bounded", execution=execution)
        result = operation.run(f.record(normals=True), catalog=None)
        payload = result.output.members[0].bytes
        source_map = result.output.wire_maps["wire_vertex_sources"]
        expected = bytes(payload)
        del result, operation
        gc.collect()
        self.assertGreater(execution.active_tracked_payload_bytes, 0)
        self.assertTrue(payload.readonly)
        self.assertEqual(bytes(payload), expected)
        self.assertEqual(tuple(source_map), (0, 1, 2))
        with self.assertRaises(TypeError):
            payload[0] = 0
        del payload, source_map
        gc.collect()
        self.assertEqual(execution.active_tracked_payload_bytes, 0)

    def test_publication_physical_receipt_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as scratch:
            destination = Path(scratch) / "published-\u5de5\u5355-\U0001f9ca"
            execution = sf.SurfaceExecutionContext(preset="bounded")
            publication = sf.SurfacePublication(destination, "proof/publication.json")
            operation = sf.prepare_surface(f.options("glb", "annotated-bundle-v1"),
                                           preset="bounded", execution=execution, publication=publication)
            holder = operation.result
            self.assertIs(operation.run(f.record(), catalog=None), holder)
            self.assertTrue(holder.complete, holder.phase_states)
            self.assertEqual(holder.publication["outcome"], "kPublished")
            receipt = holder.bundle.receipt
            receipt_view = receipt.bytes
            self.assertTrue(receipt_view.readonly)
            physical = {p.relative_to(destination).as_posix(): p.read_bytes()
                        for p in destination.rglob("*") if p.is_file()}
            raw = bytes(receipt.bytes)
            self.assertEqual(receipt.path, "proof/publication.json")
            self.assertEqual(physical[receipt.path], raw)
            self.assertEqual(receipt.sha256, hashlib.sha256(raw).digest())
            document = json.loads(raw)
            self.assertEqual(document["schema"], "meshvale.surface-publication/1")
            self.assertEqual(document["phase"], "staged-payload-verified")
            self.assertIsNone(document["caller_report"])
            self.assertEqual(set(physical), {receipt.path} | {r["path"] for r in document["payload_members"]})
            for row in document["payload_members"]:
                data = physical[row["path"]]
                self.assertEqual(int(row["byte_length"]), len(data))
                self.assertEqual(row["sha256"], hashlib.sha256(data).hexdigest())
            repeated = sf.prepare_surface(f.options("glb", "annotated-bundle-v1"),
                                          preset="bounded", execution=execution, publication=publication)
            refused = repeated.run(f.record(), catalog=None)
            self.assertFalse(refused.complete)
            self.assertIsNone(refused.bundle)
            self.assertEqual(refused.terminal_phase, "publication")
            self.assertEqual({p.relative_to(destination).as_posix(): p.read_bytes()
                              for p in destination.rglob("*") if p.is_file()}, physical)
            del receipt, holder, operation, refused, repeated
            gc.collect()
            self.assertEqual(bytes(receipt_view), raw)
            self.assertGreater(execution.active_tracked_payload_bytes, 0)
            del receipt_view
            gc.collect()
            self.assertEqual(execution.active_tracked_payload_bytes, 0)

    def test_pathlike_resolves_once_and_relative_path_uses_publication_cwd(self):
        class Destination:
            calls = 0

            def __fspath__(self):
                self.calls += 1
                if self.calls != 1:
                    raise AssertionError("destination protocol called more than once")
                return "relative-\u5de5\u5355-\U0001f9ca"

        original_cwd = os.getcwd()
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch).resolve()
            first, second = root / "prepare", root / "publish"
            first.mkdir()
            second.mkdir()
            destination = Destination()
            try:
                os.chdir(first)
                publication = sf.SurfacePublication(destination, "publication.json")
                operation = sf.prepare_surface(f.options(), preset="bounded", publication=publication)
                self.assertEqual(destination.calls, 1)
                os.chdir(second)
                result = operation.run(f.record(normals=True), catalog=None)
                self.assertTrue(result.complete, result.phase_states)
                self.assertEqual(destination.calls, 1)
                self.assertFalse((first / publication.destination).exists())
                receipt_path = second / publication.destination / result.bundle.receipt.path
                self.assertEqual(receipt_path.read_bytes(), bytes(result.bundle.receipt.bytes))
            finally:
                os.chdir(original_cwd)

    def test_malformed_record_precedes_prestop_and_attempt_is_consumed(self):
        cancellation = Cancellation()
        cancellation.request_stop()
        operation = sf.prepare_surface(f.options(), preset="bounded", cancellation=cancellation)
        source = f.record(normals=True)
        source["schema"] = "wrong-schema"
        with self.assertRaises(ValueError):
            operation.run(source, catalog=None)
        result = operation.result
        self.assertEqual(operation.state, "finished")
        self.assertIsNone(result.materialization)
        self.assertEqual(result.phase_states["materialization"], "not-called")
        self.assertEqual(result.ingress["copied_buffer_bytes"], 0)
        with self.assertRaises(RuntimeError):
            operation.run(object(), catalog=object())

    def test_zero_root_preparation_refusal_has_reachable_result(self):
        document = json.loads(files("meshvale_interchange").joinpath("_surface_file_presets.json").read_text())
        values = document["presets"]["bounded"]
        values["execution"]["tracked_payload_budget_bytes"] = 0
        preset = sf.SurfacePreset.from_mapping(values)
        operation = sf.prepare_surface(f.options(), preset=preset)
        result = operation.result
        self.assertEqual(operation.state, "finished")
        self.assertFalse(result.complete)
        self.assertEqual(result.ingress["status"], "budget-exceeded")
        self.assertEqual(result.ingress["copied_buffer_bytes"], 0)
        self.assertEqual(result.phase_states["ingress"], "returned")
        self.assertIsNone(result.materialization)
        with self.assertRaises(RuntimeError):
            operation.run(object(), catalog=object())

    def test_absent_authored_normal_is_actual_native_refusal(self):
        options = replace(f.options(), channels=())
        operation = sf.prepare_surface(options, preset="bounded")
        result = operation.run(f.record(), catalog=None)
        self.assertFalse(result.complete)
        self.assertEqual(result.terminal_phase, "encode")
        self.assertEqual(result.encode["diagnostic"]["code"], "kNormalAbsent")
        self.assertIsNone(result.output)
        self.assertIsNone(result.inspection)


if __name__ == "__main__":
    unittest.main()
