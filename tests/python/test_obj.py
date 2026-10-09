# SPDX-License-Identifier: Apache-2.0
from concurrent.futures import ThreadPoolExecutor
from dataclasses import FrozenInstanceError
import gc
import os
from pathlib import Path
import tempfile
import unittest
from meshvale_geometry import Mesh
from meshvale_interchange import (Cancellation, ObjDocument, ObjPart, ObjResource, ObjFileAsset,
    read_obj, write_obj, read_obj_file, publish_obj_bundle)

OBJ = (b"mtllib materials/a.mtl materials/b.mtl\no object\ng first second\ns 3\n"
       b"v 0 0 0\nv 2 0 0\nv 2 2 0\nv 1 1 0\nv 0 2 0\n"
       b"vt 0 0\nvt 1 0\nvt 1 1\nvt .5 .5\nvt 0 1\nvn 0 0 1\n"
       b"usemtl painted\nf 1/1/1 2/2/1 3/3/1 4/4/1 5/5/1\nf 1 3 5\n")
MTL = b"newmtl painted\nKd .1 .2 .3\nmap_Kd ../textures/paint.bin\n"
OPAQUE = b"\x00\xff\x80texture\r\n"


def fixture(root):
    (root / "materials").mkdir()
    (root / "textures").mkdir()
    (root / "materials/a.mtl").write_bytes(MTL)
    (root / "materials/b.mtl").write_bytes(b"# empty second library\r\n")
    (root / "textures/paint.bin").write_bytes(OPAQUE)
    (root / "model.obj").write_bytes(OBJ)
    return root / "model.obj"


def channel(document, semantic):
    return next(a for a in document.mesh.to_record()["attributes"] if a["semantic"] == semantic)


class ObjTests(unittest.TestCase):
    def test_text_polygon_seams_missingness_and_lifetime(self):
        result = read_obj(OBJ, MTL)
        self.assertIsNotNone(result.document, result.diagnostics)
        doc = result.document
        self.assertEqual(list(doc.mesh.to_record()["face_offsets"]), [0, 5, 8])
        self.assertEqual(doc.parts, (ObjPart("object", ("first", "second")),))
        self.assertEqual(doc.material_library, MTL)
        self.assertEqual(list(channel(doc, "texcoord")["present"]), [1]*5 + [0]*3)
        exported = write_obj(doc)
        self.assertIsNotNone(exported.text, exported.diagnostics)
        del result, doc
        gc.collect()
        reloaded = read_obj(exported.text.obj, exported.text.mtl)
        self.assertEqual(list(reloaded.document.mesh.to_record()["face_offsets"]), [0, 5, 8])
        self.assertEqual(exported.text.mtl, MTL)
        self.assertEqual(read_obj(b"o name\xff\n" + OBJ.split(b"o object\n", 1)[1], MTL).document.parts[0].object,
                         "name\udcff")

    def test_owned_resources_and_verified_publication(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); source = fixture(root)
            asset = read_obj_file(os.fsencode(source)).asset
            self.assertIsNotNone(asset)
            self.assertEqual(asset.material_libraries, (Path("materials/a.mtl"), Path("materials/b.mtl")))
            self.assertIn(ObjResource("textures/paint.bin", OPAQUE), asset.resources)
            for resource in asset.resources: (root / resource.path).unlink()
            source.unlink()
            phases = []
            dest = root / "输出"
            result = publish_obj_bundle(asset, dest, on_phase=phases.append,
                                        supplemental_files=[ObjResource("notes/info.bin", b"\x00evidence")])
            self.assertEqual(result.outcome, "published", result.diagnostics)
            self.assertEqual(phases, ["preflight", "staging", "verification", "publication"])
            self.assertEqual((dest / "textures/paint.bin").read_bytes(), OPAQUE)
            self.assertEqual((dest / "notes/info.bin").read_bytes(), b"\x00evidence")
            loaded = read_obj_file(dest / result.entry)
            self.assertEqual(loaded.asset.document.mesh.face_count, 2)
            self.assertEqual(loaded.asset.resources, asset.resources)
            again = publish_obj_bundle(asset, dest)
            self.assertEqual(again.outcome, "failed")
            self.assertEqual((dest / "textures/paint.bin").read_bytes(), OPAQUE)
            with self.assertRaises(FrozenInstanceError): asset.obj_path = Path("changed.obj")
            self.assertIs(asset.with_mesh(asset.document.mesh).document.mesh, asset.document.mesh)

    def test_cancellation_callback_failure_and_race_cleanup(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); asset = read_obj_file(fixture(root)).asset
            for phase in ["preflight", "staging", "verification", "publication"]:
                dest = root / phase; token = Cancellation()
                def cancel(value):
                    if value == phase: token.request_stop()
                result = publish_obj_bundle(asset, dest, cancellation=token, on_phase=cancel)
                self.assertEqual(result.outcome, "cancelled", (phase, result))
                self.assertFalse(dest.exists())
                self.assertTrue(token.stop_requested)
                self.assertFalse(token.request_stop())
            token = Cancellation(); token.request_stop()
            self.assertIsNone(read_obj_file(root / "model.obj", cancellation=token).asset)
            def bad(phase):
                if phase == "verification": raise RuntimeError("consumer callback error")
            result = publish_obj_bundle(asset, root / "bad", on_phase=bad)
            self.assertEqual(result.outcome, "failed")
            self.assertIn("obj.progress_callback_failed", [d["code"] for d in result.diagnostics])
            self.assertFalse((root / "bad").exists())
            dest = root / "race"
            def race(phase):
                if phase == "publication":
                    dest.mkdir(); (dest / "winner").write_bytes(b"winner")
            result = publish_obj_bundle(asset, dest, on_phase=race)
            self.assertEqual(result.outcome, "failed")
            self.assertEqual(list(dest.iterdir()), [dest / "winner"])
            self.assertEqual({p.name for p in root.iterdir()}, {"model.obj", "materials", "textures", "race"})

    def test_subset_failures_and_input_protection(self):
        doc = read_obj(OBJ, MTL).document
        record = doc.mesh.to_record(); uv = channel(doc, "texcoord")
        extra = dict(uv, name="uv_secondary", set_index=1)
        record["attributes"].append(extra)
        changed = doc.with_mesh(Mesh.from_record(record))
        result = write_obj(changed)
        self.assertIsNone(result.text)
        self.assertEqual(len(doc.mesh.to_record()["attributes"]), 5)
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch); asset = read_obj_file(fixture(root)).asset
            for resources in [(ObjResource("../escape", b"x"),), (ObjResource("model.obj", b"x"),)]:
                result = publish_obj_bundle(asset, root / "failed", supplemental_files=resources)
                self.assertEqual(result.outcome, "failed", result)
                self.assertFalse((root / "failed").exists())
            (root / "materials/a.mtl").write_bytes(b"newmtl painted\nmap_Kd ../../escape.bin\n")
            self.assertIsNone(read_obj_file(root / "model.obj").asset)

    def test_representation_errors_and_owned_constructor_fields(self):
        for value in [3, [], None]:
            with self.assertRaises(TypeError): read_obj(value)
            with self.assertRaises(TypeError): ObjResource("x", value)
        with self.assertRaises(TypeError): ObjDocument(object())
        with self.assertRaises(TypeError): ObjPart("x", "groups")
        for value in ["x\0tail", b"x\0tail"]:
            with self.assertRaises(ValueError): read_obj_file(value)
            with self.assertRaises(ValueError): ObjResource(value, b"x")
        data = bytearray(OPAQUE); value = ObjResource("x", data); data.clear()
        self.assertEqual(value.bytes, OPAQUE)
        doc = read_obj(OBJ, MTL).document
        with self.assertRaises(TypeError): read_obj_file("x", cancellation=object())
        asset = ObjFileAsset(doc, "model.obj")
        with self.assertRaises(TypeError): publish_obj_bundle(asset, "x", on_phase=5)

    def test_concurrent_owned_parses(self):
        with ThreadPoolExecutor(max_workers=3) as executor:
            values = list(executor.map(lambda _: read_obj(OBJ, MTL), range(12)))
        self.assertTrue(all(v.document.mesh.face_count == 2 for v in values))


if __name__ == "__main__": unittest.main()
