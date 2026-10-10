# SPDX-License-Identifier: Apache-2.0
"""Load, inspect and publish an owned quad after the source is removed."""
from pathlib import Path
import tempfile
from meshvale_interchange import read_obj_file, publish_obj_bundle

with tempfile.TemporaryDirectory() as scratch:
    root = Path(scratch)
    source = root / "quad.obj"
    source.write_text("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n", encoding="utf-8")
    loaded = read_obj_file(source)
    assert loaded.asset is not None, loaded.diagnostics
    source.unlink()
    assert not loaded.asset.document.mesh.inspect_storage()
    published = publish_obj_bundle(loaded.asset, root / "output")
    assert published.outcome == "published", published.diagnostics
    reloaded = read_obj_file(root / "output" / published.entry)
    assert reloaded.asset is not None, reloaded.diagnostics
    assert list(reloaded.asset.document.mesh.to_record()["face_offsets"]) == [0, 4]
    print("Installed OBJ bundle preserved the quad and survived source removal")
