# Read and rewrite an STL file

This walkthrough uses the development `meshvale_interchange.stl_files` API and
`meshvale-stl` command. The evaluated development combination is ordinary
GIL-enabled CPython 3.13 with MSVC; wider interpreter/platform qualification and
stable release distribution remain open. No package-index artifact is implied.
The [STL file contract](stl-files.md) owns the complete behavior and limits.

Start with an installed source-built candidate from this checkout. Follow
[ENVIRONMENT.md](../ENVIRONMENT.md) for compiler, runtime, dependency prefixes and
source-build setup, and the [Python build instructions](python.md#build-installation-and-example)
for wheel tooling. Select these two Geometry inputs separately:

- Native SDK: revision `5dd210b19d05bf651b853b8fdac398c7feceeba9`, with matching
  execution/materialization interfaces and an admitted compiled allocation profile.
  Match its compiler, configuration and C++ runtime when building Interchange.
- Python record provider: revision `48298b24be7c132816fb5580d22d58bde61558d0`,
  yielding the pinned `meshvale-geometry==0.0.1.dev134+g48298b24b` wheel. This
  dependency does not select or replace the native SDK above.

The current wheel configuration enables `MESHVALE_INTERCHANGE_STL_FILES`; a
native-only build must enable it explicitly. Install the matching locally built
Geometry and Interchange wheels into the same environment and activate it. The
older OBJ native producer selection in the general Python guide is insufficient
for this STL workflow. Check the installed environment before continuing:

```sh
python -m pip check
python -c "from meshvale_interchange import stl_files; print(stl_files.__name__)"
```

Create a fresh working directory and run the following examples there. Output
directory names must be new portable names; avoid reserved device names such as
`CON` on Windows.

```sh
mkdir stl-example
cd stl-example
```

Save this as `make_example.py`. It creates an original one-facet ASCII file and
three strict JSON Lines manifests. The two successful batch jobs use distinct
source entries. Every manifest line is one complete JSON object with explicit
inactive fields set to null.

```python
# make_example.py
import json
from pathlib import Path

names = ("part.stl", "batch-source.stl", "bad.stl", "jobs.jsonl",
         "failure-jobs.jsonl", "invalid-jobs.jsonl")
assert not any(Path(name).exists() for name in names), "Use a fresh directory"
source = b"""solid sample
facet normal 0 0 -2
outer loop
vertex -0 0 0
vertex 1 0 0
vertex 0 1 0
endloop
endfacet
endsolid sample
"""
Path("part.stl").write_bytes(source)
Path("batch-source.stl").write_bytes(source)
Path("bad.stl").write_bytes(b"solid incomplete\n")
units = {"kind": "unknown", "label": None}
read = {"job_id": "inspect", "mode": "read", "input": "part.stl",
        "source_encoding": "ascii", "units": units}
rewrite = {"job_id": "rewrite", "mode": "rewrite", "input": "batch-source.stl",
           "source_encoding": "ascii", "units": units, "destination": "batch-output",
           "output_encoding": "ascii",
           "metadata": {"mode": "retain", "ascii_name": None, "binary_header_hex": None},
           "acknowledge_corner_expansion": True, "report_text": "ordered batch"}
def manifest(name, rows):
    Path(name).write_bytes(b"".join((json.dumps(row, separators=(",", ":")) + "\n").encode("utf-8")
                                  for row in rows))
manifest("jobs.jsonl", [read, rewrite])
manifest("failure-jobs.jsonl", [dict(read, job_id="bad-first", input="bad.stl"),
                                 dict(read, job_id="good-next")])
manifest("invalid-jobs.jsonl", [dict(rewrite, job_id="known-first", destination="admission-output"),
                                 dict(read, job_id="invalid-last", unexpected=True)])
```

```sh
python make_example.py
python -m meshvale_interchange.stl_files read part.stl --source-encoding ascii --units unknown --preset bounded
```

The read reports one facet and three distinct vertices/corners. Its raw normal
is `(0, 0, -2)`, and the first position retains negative zero. The `bounded`
[preset](stl-file-presets.json) is an explicit finite resource policy, not a
universal maximum.

Rewrite to little-endian binary in a new directory. Cross-encoding requires
replacement metadata: the 160 hexadecimal zeros below explicitly supply an
opaque 80-byte binary header. `mm` is a caller provenance label; coordinates are
not rescaled and STL itself gains no unit field.

```sh
python -m meshvale_interchange.stl_files rewrite part.stl binary-output --source-encoding ascii --output-encoding binary --metadata replace --binary-header-hex 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 --units label --unit-label mm --acknowledge-corner-expansion --preset bounded --report-text "example rewrite"
meshvale-stl read binary-output/part.stl --source-encoding binary --units unknown --preset bounded
```

A successful rewrite has outcome `published` and exit 0. It preserves the ordered
facet, positions, raw normal and soup identities. The publisher closes and
reopens staged bytes, freshly decodes and compares them, then commits a new
directory without replacement. `binary-output` contains exactly `part.stl` and
`stl-publication.json`. The receipt records staged verification; it does not
claim its own later commit, inventory, delivery or self-hash.

Save and run this independent physical inspection:

```python
# inspect_output.py
import hashlib
import json
from pathlib import Path
import struct

root = Path("binary-output")
assert {p.name for p in root.iterdir()} == {"part.stl", "stl-publication.json"}
payload = (root / "part.stl").read_bytes()
assert len(payload) == 134  # 84-byte prefix plus one 50-byte facet
assert payload[:80] == bytes(80)
assert struct.unpack_from("<I", payload, 80) == (1,)
assert struct.unpack_from("<12IH", payload, 84) == (
    0, 0, 0xC0000000, 0x80000000, 0, 0, 0x3F800000, 0, 0, 0, 0x3F800000, 0, 0)
receipt = json.loads((root / "stl-publication.json").read_text(encoding="utf-8"))
assert receipt["schema"] == "meshvale.stl-publication/1"
assert receipt["payload"] == {"encoding": "binary", "entry": "part.stl", "bytes": 134,
                              "sha256": hashlib.sha256(payload).hexdigest()}
assert receipt["source"]["sha256"] == hashlib.sha256(Path("part.stl").read_bytes()).hexdigest()
assert receipt["counts"] == {"facets": 1, "vertices": 3, "corners": 3}
assert receipt["metadata"] == {"requested": "replace", "actual": "replaced_source_metadata",
                               "ascii_name": None, "binary_header_hex": "00" * 80}
assert receipt["units"] == {"kind": "label", "label": "mm", "rescaled": False}
assert receipt["caller_report"] == "example rewrite"
assert receipt["verification"] == "staged-reread-and-fresh-codec-verified"
assert not {"commit", "success", "inventory", "cleanup", "delivery", "sha256"} & receipt.keys()
```

```sh
python inspect_output.py
```

The reusable API exposes stable prepared results and readonly native owners.
Save this as `prepared_api.py`. Metadata retain is explicit and requires the same
encoding. Exported bytes, dictionaries and meshes are detached copies; an export
can allocate independently of the completed operation.

```python
# prepared_api.py
from pathlib import Path
from meshvale_interchange import stl_files as stl

units = stl.StlUnits("unknown", None)
prepared = stl.prepare_stl_read("part.stl", source_encoding="ascii", units=units,
                                preset="bounded", execution=None)
holder = prepared.result
assert stl.run_stl(prepared, cancellation=None) is holder
assert holder.outcome == "read", holder.diagnostic
asset = holder.asset
del prepared, holder
assert asset.document.facet_count == 1
assert asset.document.vertex_count == 3
assert asset.export_bytes() == Path("part.stl").read_bytes()
assert tuple(asset.document.export_mesh().to_record()["corner_vertices"]) == (0, 1, 2)

def prepare(destination):
    return stl.prepare_stl_rewrite(
        "part.stl", destination, source_encoding="ascii", output_encoding="ascii",
        metadata=stl.StlMetadata("retain", None, None), units=units,
        acknowledge_corner_expansion=True, preset="bounded", execution=None, report=None)

prepared = prepare("api-output")
result = stl.run_stl(prepared, cancellation=None)
assert result is prepared.result
assert result.outcome == "published", result.diagnostic
assert result.output.export_corner_source_vertices() == (0, 1, 2)
before = (Path("api-output") / "part.stl").read_bytes()
denied = stl.run_stl(prepare("api-output"), cancellation=None)
assert denied.exit_code == 2
assert denied.not_attempted_reason == "admission_failed"
assert (Path("api-output") / "part.stl").read_bytes() == before
```

```sh
python prepared_api.py
```

A prepared operation runs once. Create a new prepared operation for another
request. Retained owners outlive the original result/prepared wrapper. The API
keeps reached facts available after expected later failures; interruption and
unexpected allocation exceptions follow the separate [failure contract](stl-files.md#sfile-python-001-prepared-reusable-operation-and-exact-getters).

Run the ordered batch. Manifest paths resolve against the manifest's containing
directory, even when the command runs elsewhere. Jobs run in order under one
shared root; a batch does not execute jobs simultaneously.

```sh
meshvale-stl batch jobs.jsonl --failure-policy fail-fast --preset bounded
python -m meshvale_interchange.stl_files batch failure-jobs.jsonl --failure-policy fail-fast --preset bounded
python -m meshvale_interchange.stl_files batch invalid-jobs.jsonl --failure-policy continue --preset bounded
```

The first command exits 0 with `read`, then `published`. The second intentionally
exits 1: the malformed source fails, and `good-next` remains `not_attempted` with
reason `fail_fast`. With `continue`, an ordinary failed job would allow the next
admitted job to run. The third intentionally exits 2: an unknown key in the last
row rejects the entire manifest before any source capture or staging. Its known
first slot remains `not_attempted` with reason `admission_failed`; it does not
create `admission-output`.

```sh
python -c "from pathlib import Path; assert not Path('admission-output').exists(); assert {p.name for p in Path('batch-output').iterdir()} == {'batch-source.stl', 'stl-publication.json'}"
```

Manifests accept strict UTF-8 JSON Lines without comments, BOM, blank lines,
duplicate keys or unknown fields. All rows and source/destination domains are
admitted before capture. Repeated sources and overlapping output directories
are refused. Keep the original manifests and choose fresh output directory
names when repeating the successful examples.

Command JSON always has the closed result envelope. Its emitted delivery field
is `not_attempted`: a frame cannot attest its own later successful write/flush.
The process exit observes delivery afterward. Exit precedence is admission 2,
observed cancellation/interruption 130, delivery failure 3, attempted job failure
1, then success 0. Later output failure does not undo an already committed bundle.
These codec-preserving rewrites do not select repair, indexing, normal generation
or a coordinate/unit transformation; see the [contract](stl-files.md) for the
full retained-owner, numeric, path and cancellation boundaries.
