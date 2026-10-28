# SPDX-License-Identifier: Apache-2.0
"""Independent synthetic STL wire, geometry and closed-schema test oracles."""
import hashlib
import json
import struct


NAME = "raw facets"
RECEIPT = "stl-publication.json"
EMPTY_ASCII = b"solid \nendsolid \n"
HEADER = bytes((i * 37) % 256 for i in range(80))
FACETS = (
    (0x80000000, 0, 0xC0000000, 0x80000000, 0, 0, 0x3F800000, 0, 0, 0, 0x3F800000, 0),
    (0, 0, 0, 0x80000000, 0, 0, 0x3F800000, 0, 0, 0, 0x3F800000, 0),
    (0x40000000, 0, 0, *(0x3F800000,) * 9),
)
BOUNDARY = (
    (0x80000000, 1, 0x7F7FFFFF, 0x80000000, 0x007FFFFF, 0x00800000,
     0x7F7FFFFF, 0xFF7FFFFF, 1, 0x3F800000, 0xBF800000, 0),
)
ASCII = (
    b"solid raw facets\n"
    b"facet normal -0 0 -2\nouter loop\nvertex -0 0 0\nvertex 1 0 0\n"
    b"vertex 0 1 0\nendloop\nendfacet\n"
    b"facet normal 0 0 0\nouter loop\nvertex -0 0 0\nvertex 1 0 0\n"
    b"vertex 0 1 0\nendloop\nendfacet\n"
    b"facet normal 2 0 0\nouter loop\nvertex 1 1 1\nvertex 1 1 1\n"
    b"vertex 1 1 1\nendloop\nendfacet\nendsolid raw facets\n"
)
STAGES = ("capture", "read", "write", "staged_reread", "fresh_read", "verification",
          "provider", "receipt_verification", "inventory", "publication")
FILE_EXECUTION = ("capture", "writing_files", "staged_reread", "byte_verification",
                  "value_verification", "receipt_verification", "inventory", "hashing", "provider_import")
OWNERS = ("captured", "asset", "output", "fresh_document", "verified", "bundle")
JOB_KEYS = {"job_id", "mode", "declarations", "outcome", "phase", "diagnostic", "observations",
            "execution", "requested_bytes", "cleanup", "cleanup_error", "not_attempted_reason", "owners"}
ENVELOPE_KEYS = {"schema", "mode", "outcome", "admission_diagnostic", "jobs", "delivery", "exit_code"}
DIAGNOSTIC_KEYS = {"status", "phase", "code", "file_index", "byte_offset", "facet",
                   "system_error", "stl_status", "stl_diagnostic"}
ADMISSION_KEYS = {"code", "line_index", "job_index", "field", "file_diagnostic"}
CODEC_REQUEST_KEYS = {"codec", "materialization", "inspection", "total"}
STATS_KEYS = {"planned_jobs", "launched_workers", "joined_workers", "caller_jobs"}
WRITE_FACT_KEYS = {"encoding", "face_normal_attribute", "source_vertex_count", "source_face_count",
                   "corner_expansion_acknowledged", "source_encoding", "metadata_policy", "bytes", "sha256"}
VERIFICATION_KEYS = {"position_bits", "raw_normal_bits", "facet_order", "corner_order", "soup_identity", "metadata", "exact_bytes"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def f32(word):
    return struct.unpack("<f", struct.pack("<I", word))[0]


def bits64(value):
    return struct.pack("<d", value)


def binary(facets=FACETS, header=HEADER):
    assert len(header) == 80
    return header + struct.pack("<I", len(facets)) + b"".join(
        struct.pack("<12IH", *facet, 0) for facet in facets)


def check_binary(test, payload, facets=FACETS, header=HEADER):
    test.assertEqual(len(payload), 84 + 50 * len(facets))
    test.assertEqual(payload[:80], header)
    test.assertEqual(struct.unpack_from("<I", payload, 80), (len(facets),))
    for index, facet in enumerate(facets):
        test.assertEqual(struct.unpack_from("<12IH", payload, 84 + 50 * index), (*facet, 0))


def check_ascii(test, payload, facets=FACETS, name=NAME):
    lines = payload.decode("ascii", "strict").splitlines()
    test.assertTrue(lines[0] == "solid " + name or not name and lines[0] == "solid")
    test.assertTrue(lines[-1] == "endsolid " + name or not name and lines[-1] == "endsolid")
    tokens = iter(" ".join(lines[1:-1]).split())
    for facet in facets:
        test.assertEqual((next(tokens), next(tokens)), ("facet", "normal"))
        for word in facet[:3]:
            test.assertEqual(bits64(float(next(tokens))), bits64(f32(word)))
        test.assertEqual((next(tokens), next(tokens)), ("outer", "loop"))
        for corner in range(3):
            test.assertEqual(next(tokens), "vertex")
            for word in facet[3 + corner * 3:6 + corner * 3]:
                test.assertEqual(bits64(float(next(tokens))), bits64(f32(word)))
        test.assertEqual((next(tokens), next(tokens)), ("endloop", "endfacet"))
    test.assertIsNone(next(tokens, None))


def check_document(test, document, encoding, facets=FACETS, name=NAME, header=HEADER):
    test.assertEqual(document.encoding, encoding)
    test.assertEqual(document.ascii_name, name if encoding == "ascii" else None)
    test.assertEqual(document.binary_header, header if encoding == "binary" else None)
    test.assertEqual((document.facet_count, document.vertex_count, document.corner_count),
                     (len(facets), 3 * len(facets), 3 * len(facets)))
    test.assertEqual(set(document.new_requested_bytes), CODEC_REQUEST_KEYS)
    mesh = document.export_mesh()
    test.assertFalse(mesh.inspect_storage())
    record = mesh.to_record()
    test.assertEqual(set(record), {"schema", "positions", "face_offsets", "corner_vertices", "attributes"})
    test.assertEqual(record["schema"], "meshvale.mesh/1")
    test.assertEqual(tuple(record["face_offsets"]), tuple(3 * i for i in range(len(facets) + 1)))
    test.assertEqual(tuple(record["corner_vertices"]), tuple(range(3 * len(facets))))
    test.assertEqual([bits64(v) for v in record["positions"]],
                     [bits64(f32(word)) for facet in facets for word in facet[3:]])
    test.assertEqual(len(record["attributes"]), 1)
    normal = record["attributes"][0]
    expected = {"domain": "face", "name": "facet_normal", "semantic": "normal", "set_index": None,
                "components": 3, "scalar_type": "float64", "offsets": None, "present": None, "metadata": {}}
    test.assertEqual({key: value for key, value in normal.items() if key != "values"}, expected)
    test.assertEqual([bits64(v) for v in normal["values"]],
                     [bits64(f32(word)) for facet in facets for word in facet[:3]])
    return mesh


def strict_json(payload):
    if payload.startswith(b"\xef\xbb\xbf"):
        raise AssertionError("JSON frame/receipt must not have a BOM")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise AssertionError("duplicate JSON key: " + key)
            result[key] = value
        return result
    def constant(value):
        raise AssertionError("nonfinite JSON constant: " + value)
    return json.loads(payload.decode("utf-8", "strict"), object_pairs_hook=pairs, parse_constant=constant)


def check_uint(test, value):
    test.assertIs(type(value), int)
    test.assertGreaterEqual(value, 0)
    test.assertLessEqual(value, (1 << 64) - 1)


def check_digest(test, value):
    test.assertIs(type(value), str)
    test.assertEqual(len(value), 64)
    test.assertTrue(all(c in "0123456789abcdef" for c in value))


def check_diagnostic(test, diagnostic):
    test.assertEqual(set(diagnostic), DIAGNOSTIC_KEYS)
    for key in ("status", "phase", "code"):
        test.assertIs(type(diagnostic[key]), str)
    for key in ("file_index", "byte_offset", "facet"):
        if diagnostic[key] is not None:
            check_uint(test, diagnostic[key])
    if diagnostic["system_error"] is not None:
        test.assertEqual(set(diagnostic["system_error"]), {"category", "value"})
        test.assertIs(type(diagnostic["system_error"]["category"]), str)
        test.assertIs(type(diagnostic["system_error"]["value"]), int)
    delegated = diagnostic["stl_diagnostic"]
    if delegated is not None:
        test.assertEqual(set(delegated), {"stage", "code", "byte_offset", "facet", "attribute",
                                          "geometry_status", "geometry_diagnostic", "storage_issue"})
        for key in ("byte_offset", "facet", "attribute"):
            if delegated[key] is not None:
                check_uint(test, delegated[key])
        geometry = delegated["geometry_diagnostic"]
        if geometry is not None:
            test.assertEqual(set(geometry), {"code", "attribute", "element", "storage_issue"})
        for issue in (delegated["storage_issue"], geometry["storage_issue"] if geometry else None):
            if issue is not None:
                test.assertEqual(set(issue), {"code", "attribute", "element"})


def check_capture_facts(test, facts):
    test.assertEqual(set(facts), {"entry", "bytes", "sha256"})
    test.assertIs(type(facts["entry"]), str)
    check_uint(test, facts["bytes"])
    check_digest(test, facts["sha256"])


def check_receipt(test, bundle, source_bytes, output_bytes, *, source_encoding, output_encoding,
                  entry, facets=FACETS, metadata_mode="retain", actual="retained_source_metadata",
                  name=NAME, header=HEADER, report=None, unit=None):
    payload = bundle.export_receipt_bytes()
    test.assertEqual(bundle.receipt_entry, RECEIPT)
    test.assertEqual(bundle.receipt_byte_count, len(payload))
    test.assertEqual(bundle.receipt_sha256, sha(payload))
    expected = {
        "schema": "meshvale.stl-publication/1", "profile": "codec-preserving",
        "source": {"encoding": source_encoding, "entry": entry, "bytes": len(source_bytes), "sha256": sha(source_bytes)},
        "payload": {"encoding": output_encoding, "entry": entry, "bytes": len(output_bytes), "sha256": sha(output_bytes)},
        "counts": {"facets": len(facets), "vertices": 3 * len(facets), "corners": 3 * len(facets)},
        "metadata": {"requested": metadata_mode, "actual": actual,
                     "ascii_name": name if output_encoding == "ascii" else None,
                     "binary_header_hex": header.hex() if output_encoding == "binary" else None},
        "units": {"kind": "label" if unit is not None else "unknown", "label": unit, "rescaled": False},
        "fidelity": dict.fromkeys(("position_bits", "raw_normal_bits", "facet_order", "corner_order", "soup_identity", "exact_bytes"), True),
        "verification": "staged-reread-and-fresh-codec-verified", "caller_report": report,
    }
    test.assertEqual(strict_json(payload), expected)


def check_observations(test, observations):
    test.assertEqual(set(observations), set(STAGES))
    for stage, observation in observations.items():
        test.assertEqual(set(observation), {"state", "diagnostic", "facts"}, stage)
        test.assertIn(observation["state"], ("not_attempted", "not_applicable", "failed", "canceled", "succeeded"))
        if observation["state"] in ("failed", "canceled"):
            test.assertIsNone(observation["facts"])
            check_diagnostic(test, observation["diagnostic"])
        elif observation["state"] in ("not_attempted", "not_applicable"):
            test.assertIsNone(observation["diagnostic"])
            test.assertIsNone(observation["facts"])
        else:
            test.assertIsNone(observation["diagnostic"])
            facts = observation["facts"]
            if stage in ("capture", "receipt_verification"):
                check_capture_facts(test, facts)
            elif stage in ("read", "fresh_read"):
                test.assertEqual(set(facts), {"encoding", "facets", "vertices", "corners", "ascii_name", "binary_header_hex"})
                for key in ("facets", "vertices", "corners"):
                    check_uint(test, facts[key])
                test.assertEqual((facts["vertices"], facts["corners"]), (3 * facts["facets"], 3 * facts["facets"]))
                test.assertIn(facts["encoding"], ("ascii", "binary"))
                if facts["encoding"] == "ascii":
                    test.assertIs(type(facts["ascii_name"]), str)
                    test.assertIsNone(facts["binary_header_hex"])
                else:
                    test.assertIsNone(facts["ascii_name"])
                    test.assertEqual(len(facts["binary_header_hex"]), 160)
                    test.assertTrue(all(c in "0123456789abcdef" for c in facts["binary_header_hex"]))
            elif stage == "write":
                test.assertEqual(set(facts), WRITE_FACT_KEYS)
                check_uint(test, facts["bytes"])
                check_digest(test, facts["sha256"])
                test.assertEqual(facts["face_normal_attribute"], 0)
                test.assertIs(facts["corner_expansion_acknowledged"], True)
                test.assertIn(facts["source_encoding"], ("ascii", "binary"))
            elif stage == "staged_reread":
                test.assertEqual(set(facts), {"bytes", "sha256"})
                check_uint(test, facts["bytes"])
                check_digest(test, facts["sha256"])
            elif stage == "verification":
                test.assertEqual(set(facts), VERIFICATION_KEYS)
                test.assertTrue(all(value is True for value in facts.values()))
            elif stage == "inventory":
                test.assertEqual(set(facts), {"entries"})
                test.assertEqual(len(facts["entries"]), 2)
                for entry in facts["entries"]:
                    check_capture_facts(test, entry)
                names = [entry["entry"] for entry in facts["entries"]]
                test.assertEqual(len(set(names)), 2)
                test.assertIn(RECEIPT, names)
                test.assertEqual(names, sorted(names, key=lambda name: name.encode("utf-8")))
            elif stage == "publication":
                test.assertEqual(facts, {"outcome": "published"})
            else:
                test.assertEqual(stage, "provider")
                test.assertIsNone(facts)


def check_execution(test, execution):
    test.assertEqual(set(execution), set(FILE_EXECUTION) | {"read", "write", "fresh_read"})
    groups = [execution[key] for key in FILE_EXECUTION]
    for key in ("read", "write", "fresh_read"):
        test.assertEqual(set(execution[key]), {"codec", "materialization", "inspection"})
        groups.extend(execution[key].values())
    for counters in groups:
        test.assertEqual(set(counters), STATS_KEYS)
        for value in counters.values():
            test.assertIs(type(value), int)
            test.assertGreaterEqual(value, 0)
        test.assertEqual(counters["launched_workers"], counters["joined_workers"])


def check_frame(test, frame, mode, outcomes, exit_code, admission=None):
    test.assertEqual(set(frame), ENVELOPE_KEYS)
    test.assertEqual(frame["schema"], "meshvale.stl-result/1")
    test.assertEqual(frame["mode"], mode)
    test.assertIn(frame["mode"], ("read", "rewrite", "batch"))
    test.assertEqual(frame["exit_code"], exit_code)
    test.assertEqual(frame["delivery"], {"state": "not_attempted", "code": None, "bytes_written": 0})
    test.assertIs(type(frame["jobs"]), list)
    test.assertEqual([job["outcome"] for job in frame["jobs"]], outcomes)
    if admission is None:
        test.assertIsNone(frame["admission_diagnostic"])
    else:
        test.assertEqual(set(frame["admission_diagnostic"]), ADMISSION_KEYS)
        test.assertEqual(frame["admission_diagnostic"]["code"], admission)
    for job in frame["jobs"]:
        test.assertEqual(set(job), JOB_KEYS)
        test.assertEqual(set(job["owners"]), set(OWNERS))
        test.assertTrue(all(type(value) is bool for value in job["owners"].values()))
        test.assertIn(job["mode"], ("read", "rewrite"))
        test.assertIn(job["outcome"], ("not_attempted", "read", "failed", "canceled", "published"))
        test.assertIn(job["cleanup"], ("not_required", "removed", "failed"))
        if mode != "batch":
            test.assertEqual(job["job_id"], "single")
            test.assertEqual(job["mode"], mode)
        test.assertEqual(set(job["declarations"]), {"source_encoding", "output_encoding", "metadata_mode", "units", "acknowledge_corner_expansion"})
        test.assertEqual(set(job["declarations"]["units"]), {"kind", "label", "rescaled"})
        test.assertIs(job["declarations"]["units"]["rescaled"], False)
        check_observations(test, job["observations"])
        check_execution(test, job["execution"])
        if job["diagnostic"] is not None:
            check_diagnostic(test, job["diagnostic"])
        test.assertEqual(set(job["requested_bytes"]), {"file_capture", "file_publication", "application", "read", "write", "fresh_read"})
        for key in ("file_capture", "file_publication", "application"):
            check_uint(test, job["requested_bytes"][key])
        for key in ("read", "write", "fresh_read"):
            counters = job["requested_bytes"][key]
            if counters is not None:
                test.assertEqual(set(counters), CODEC_REQUEST_KEYS)
                for value in counters.values():
                    check_uint(test, value)
        if job["not_attempted_reason"] is not None:
            test.assertEqual(job["outcome"], "not_attempted")
            test.assertIn(job["not_attempted_reason"], ("admission_failed", "fail_fast", "canceled"))
        if job["cleanup_error"] is not None:
            test.assertEqual(set(job["cleanup_error"]), {"category", "value"})


def read_row(job_id, name, encoding="ascii"):
    return {"job_id": job_id, "mode": "read", "input": name, "source_encoding": encoding,
            "units": {"kind": "unknown", "label": None}}


def rewrite_row(job_id, name, destination, encoding="ascii"):
    return dict(read_row(job_id, name, encoding), mode="rewrite", destination=destination,
                output_encoding=encoding, metadata={"mode": "retain", "ascii_name": None, "binary_header_hex": None},
                acknowledge_corner_expansion=True, report_text=None)
