# SPDX-License-Identifier: Apache-2.0
"""Explicit installed STL boundary regression; outside ordinary test discovery.

Build stl_boundary_fixture for the selected interpreter, then run this file with
--fixture ABSOLUTE_EXTENSION --output FRESH_DIRECTORY. The parent captures one
isolated child and rejects shutdown stderr, including nanobind leak diagnostics.
"""
import argparse
import gc
import hashlib
import importlib.util
import json
from pathlib import Path
import signal
import struct
import subprocess
import sys
import threading
import time
import traceback
import weakref


FACET_COUNT = 100000
TRACKED_BUDGET = 1073741824
WAIT_SECONDS = 10
FINALIZED = {}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def ledger(context):
    return {"workers": context.active_workers,
            "payload": context.active_tracked_payload_bytes}


def finalized(label):
    FINALIZED[label] = FINALIZED.get(label, 0) + 1


class LifetimePayload:
    pass


class BoundaryFault(RuntimeError):
    pass


def _distinct_traceback_origin():
    # This real traceback is created before any prepared operation exists. Its
    # frame contains no operation/result/context and cannot artificially root one.
    try:
        raise LookupError("independent traceback origin")
    except LookupError as error:
        return error.__traceback__


def traceback_ids(error):
    entries = []
    current = error.__traceback__
    while current is not None:
        entries.append((id(current), current.tb_frame.f_code.co_name))
        current = current.tb_next
    return entries


def check_exception(error, expected, previous_traceback):
    require(type(error) is BoundaryFault and id(error) == expected["identity"],
            "exception type/identity changed")
    require(error.args == (expected["marker"],) and error.marker == expected["marker"],
            "exception payload changed")
    require(id(error.__cause__) == expected["cause"]
            and id(error.__context__) == expected["context"]
            and error.__suppress_context__, "exception chaining changed")
    require(getattr(error, "__notes__", None) == expected["notes"], "exception notes changed")
    entries = traceback_ids(error)
    ids = [identity for identity, _ in entries]
    require(expected["original_traceback"] in ids, "separately supplied traceback was lost")
    require(any(name == "_distinct_traceback_origin" for _, name in entries),
            "original traceback origin disappeared")
    require(all(identity in ids for identity in previous_traceback),
            "replay truncated previously appended traceback")
    return ids


def normal_case(stl, fixture, context, source, expected_hash):
    prepared = holder = asset = None
    try:
        prepared = stl.prepare_stl_read(source, source_encoding="binary",
            units=stl.StlUnits("unknown", None), execution=context)
        holder = stl.run_stl(prepared)
        asset = holder.asset
        require(holder.outcome == "read" and holder.exit_code == 0, "normal control failed")
        require(asset.sha256 == expected_hash and asset.document.facet_count == FACET_COUNT
                and asset.document.vertex_count == 3 * FACET_COUNT
                and asset.export_bytes() == source.read_bytes(),
                "normal control did not reach the real asset")
        live = ledger(context)
        gc.collect()
        require(ledger(context) == live and holder.asset is asset,
                "GC erased a live normal result")
        return {"status": "PASS", "live": live, "source_sha256": asset.sha256}
    finally:
        fixture.clear_references()
        prepared = holder = asset = None


def provider_case(stl, fixture, context, source, expected_hash):
    prepared = holder = verified = None
    old_handler = None
    destination = source.parent / "provider-publication"
    try:
        prepared = stl.prepare_stl_rewrite(source, destination, source_encoding="binary",
            output_encoding="binary", metadata=stl.StlMetadata("retain", None, None),
            units=stl.StlUnits("unknown", None), acknowledge_corner_expansion=True,
            execution=context, report=fixture.provider)
        holder = prepared.result
        old_handler = signal.signal(signal.SIGINT, signal.default_int_handler)
        returned = stl.run_stl(prepared)
        signal.signal(signal.SIGINT, old_handler)
        old_handler = None
        require(returned is holder, "provider run changed stable holder")
        returned = None
        verified = fixture.retained_verified()
        facts = fixture.snapshot()
        require(type(verified) is stl.StlVerifiedRewrite and facts["provider_calls"] == 1
                and facts["provider_error_active"] == 1 and facts["provider_arm_result"] == 0,
                "same-C-call active ValueError/pending SIGINT was not attested")
        fixture.clear_references()
        require(fixture.snapshot()["owned_references"] == 0, "fixture retained provider owners")
        require((holder.outcome, holder.exit_code, holder.cleanup) == ("canceled", 130, "removed")
                and holder.observations["provider"]["state"] == "canceled",
                "pending KeyboardInterrupt lost precedence over ordinary provider error")
        require(all(getattr(holder, key) is not None for key in
                    ("asset", "output", "fresh_document", "verified")), "reached provider owners lost")
        require(holder.bundle is None and not destination.exists(), "interrupted provider committed")
        require(verified.source.sha256 == expected_hash and source.read_bytes() == verified.source.export_bytes(),
                "provider interruption changed source facts")
        require(holder.output.export_bytes() == verified.export_bytes()
                and holder.fresh_document.facet_count == FACET_COUNT,
                "provider interruption lost actual verified output facts")
        require(sorted(path.name for path in source.parent.iterdir()) == [source.name],
                "interrupted provider left staging entries")
        live = ledger(context)
        gc.collect()
        require(ledger(context) == live and verified.document.facet_count == FACET_COUNT,
                "GC erased live verified owners")
        return {"status": "PASS", "injection": facts, "live": live}
    finally:
        if old_handler is not None:
            signal.signal(signal.SIGINT, old_handler)
        fixture.clear_references()
        prepared = holder = verified = None


def unexpected_case(stl, fixture, context, source, expected_hash, mode):
    prepared = holder = asset = error_value = original_traceback = payload = None
    thread = watcher = paths = terminal_callable = None
    previous_handler = None
    stop, ready = threading.Event(), threading.Event()
    details = {"mode": mode, "replays": [], "watcher_errors": []}
    label = "unexpected-" + mode
    try:
        # Preconstruct the exact exception and a distinct real traceback before
        # app setup. A C signal callable supplies fresh owned raw triple refs.
        original_traceback = _distinct_traceback_origin()
        error_value = BoundaryFault(label)
        error_value.marker = label
        error_value.__cause__ = ValueError("explicit boundary cause")
        error_value.__context__ = LookupError("explicit boundary context")
        if hasattr(error_value, "add_note"):
            error_value.add_note("retained boundary note")
        payload = LifetimePayload()
        weakref.finalize(payload, finalized, label)
        error_value.payload = payload
        expected = {"identity": id(error_value), "marker": label,
                    "cause": id(error_value.__cause__), "context": id(error_value.__context__),
                    "notes": getattr(error_value, "__notes__", None),
                    "original_traceback": id(original_traceback)}
        require(error_value.__traceback__ is None, "signal value already owns a traceback")
        fixture.configure_signal(error_value, original_traceback)
        if mode == "single":
            prepared = stl.prepare_stl_read(source, source_encoding="binary",
                units=stl.StlUnits("unknown", None), execution=context)
            run = stl.run_stl
        else:
            job = stl.StlReadJob("boundary", "read", source, "binary", stl.StlUnits("unknown", None))
            prepared = stl.prepare_stl_batch((job,), execution=context)
            job = None
            run = stl.run_stl_batch
        holder = prepared.result
        baseline = context.active_tracked_payload_bytes
        fixture.reset_witness()
        def watcher():
            try:
                ready.set()
                deadline = time.monotonic() + WAIT_SECONDS
                while not stop.is_set() and time.monotonic() < deadline:
                    if fixture.try_arm(context, baseline):
                        return
            except BaseException:
                details["watcher_errors"].append(traceback.format_exc())
        previous_handler = signal.signal(signal.SIGINT, fixture.raw_signal_handler)
        thread = threading.Thread(target=watcher, name="stl-boundary-witness")
        thread.start()
        require(ready.wait(WAIT_SECONDS), "native witness thread did not start")
        initial = None
        try:
            run(prepared)
        except BoundaryFault as error:
            initial = check_exception(error, expected, [])
        stop.set()
        thread.join(WAIT_SECONDS)
        signal.signal(signal.SIGINT, previous_handler)
        previous_handler = None
        require(not thread.is_alive() and not details["watcher_errors"], "witness thread failure")
        facts = fixture.snapshot()
        details["injection"] = facts
        require(facts["armed"] and facts["witness_workers"] > 0
                and facts["witness_payload"] > baseline
                and facts["witness_thread"] != threading.get_ident(),
                "UNQUALIFIED: actual native worker/payload witness missed")
        require(facts["signal_calls"] == 1 and facts["signal_value_initially_without_traceback"]
                and facts["signal_thread"] == threading.get_ident()
                and facts["signal_traceback_distinct"] and initial,
                "raw C signal exception/distinct traceback boundary not reached")
        if sys.version_info < (3, 12):
            require(facts["legacy_raw_triple_attested"] == 1,
                    "actual legacy raw type/value/distinct traceback was not attested")
        # Remove fixture and direct caller exception roots while keeping the
        # result live. Its master must preserve payload, chaining and traceback.
        fixture.clear_references()
        require(fixture.snapshot()["owned_references"] == 0, "fixture kept exception/traceback roots")
        error_value = original_traceback = payload = None
        asset = holder.asset if mode == "single" else holder.jobs[0].asset
        require(asset.sha256 == expected_hash and asset.document.facet_count == FACET_COUNT
                and asset.document.vertex_count == 3 * FACET_COUNT
                and asset.export_bytes() == source.read_bytes(),
                "unexpected path lacks genuine completed asset")
        live = ledger(context)
        gc.collect()
        require(ledger(context) == live and FINALIZED.get(label, 0) == 0,
                "GC cleared reachable exception or native owner")
        paths = [("exit_code", lambda: holder.exit_code), ("export_json", holder.export_json)]
        if mode == "batch":
            paths.append(("outcome", lambda: holder.outcome))
        previous_traceback = initial
        for name, terminal_callable in paths:
            for index in range(3):
                observed = False
                try:
                    terminal_callable()
                except BoundaryFault as error:
                    previous_traceback = check_exception(error, expected, previous_traceback)
                    details["replays"].append({"path": name, "index": index,
                                              "traceback_depth": len(previous_traceback)})
                    observed = True
                require(observed, "terminal path did not replay unexpected exception")
        details.update(status="PASS", live=live, source_sha256=asset.sha256,
                       post_prepare_baseline=baseline, finalizer_label=label)
        return details
    finally:
        stop.set()
        joined = True
        try:
            if thread is not None and thread.ident is not None:
                thread.join(WAIT_SECONDS)
                joined = not thread.is_alive()
        finally:
            try:
                if previous_handler is not None:
                    signal.signal(signal.SIGINT, previous_handler)
            finally:
                fixture.clear_references()
                prepared = holder = asset = error_value = original_traceback = payload = None
                thread = watcher = paths = terminal_callable = None
        require(joined, "witness thread remained live")


def child(fixture_path, output):
    from meshvale_interchange import stl_files as stl
    spec = importlib.util.spec_from_file_location("stl_boundary_fixture", fixture_path)
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    inputs = output / "inputs"
    inputs.mkdir()
    source = inputs / "facets.stl"
    facet = struct.pack("<12fH", 0, 0, -2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0)
    raw = bytes(range(80)) + struct.pack("<I", FACET_COUNT) + facet * FACET_COUNT
    source.write_bytes(raw)
    expected_hash = sha256(raw)
    result = {"status": "RUNNING", "cases": {}, "python": sys.version,
              "legacy_raw_triple_required": sys.version_info < (3, 12)}
    try:
        for name in ("normal", "provider", "single", "batch"):
            context = stl.StlExecutionContext(worker_budget=4, minimum_parallel_vertices=65536,
                                             tracked_payload_budget_bytes=TRACKED_BUDGET)
            baseline = ledger(context)
            if name == "normal":
                details = normal_case(stl, fixture, context, source, expected_hash)
            elif name == "provider":
                details = provider_case(stl, fixture, context, source, expected_hash)
            else:
                details = unexpected_case(stl, fixture, context, source, expected_hash, name)
            require(fixture.snapshot()["owned_references"] == 0, "helper reference cleanup failed")
            details["gc_collected"] = gc.collect()
            details["post_scope"] = ledger(context)
            result["cases"][name] = details
            require(ledger(context) == baseline, "operation retained payload/workers after scope and GC")
            if name in ("single", "batch"):
                require(FINALIZED.get(details["finalizer_label"], 0) == 1,
                        "unreachable custom exception payload was not finalized exactly once")
            context = details = None
        require(sum(len(result["cases"][name]["replays"]) for name in ("single", "batch")) == 15,
                "expected fifteen terminal replays")
        result["status"] = "PASS"
    except BaseException:
        result["status"] = "FAIL"
        result["exception"] = traceback.format_exc()
        raise
    finally:
        fixture.clear_references()
        (output / "child-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    fixture = arguments.fixture.resolve(strict=True)
    output = arguments.output.resolve()
    if arguments.child:
        child(fixture, output)
        return 0
    output.mkdir(parents=True, exist_ok=False)
    argv = [sys.executable, "-I", "-B", "-W", "error", str(Path(__file__).resolve()),
            "--fixture", str(fixture), "--output", str(output), "--child"]
    completed = subprocess.run(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, check=False)
    (output / "child.stdout.bin").write_bytes(completed.stdout)
    (output / "child.stderr.bin").write_bytes(completed.stderr)
    result = {"status": "FAIL", "argv": argv, "exit_code": completed.returncode,
              "fixture_sha256": sha256(fixture.read_bytes()),
              "stdout_sha256": sha256(completed.stdout), "stderr_sha256": sha256(completed.stderr)}
    child_result = output / "child-result.json"
    if child_result.exists():
        result["child"] = json.loads(child_result.read_text(encoding="utf-8"))
    if completed.returncode == 0 and not completed.stderr and result.get("child", {}).get("status") == "PASS":
        result["status"] = "PASS"
    (output / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({"status": result["status"], "exit_code": completed.returncode,
                      "shutdown_stderr_bytes": len(completed.stderr)}))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
