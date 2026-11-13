# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""CLI scheduling, scope, failure logging and machine-readable build reports."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

s_cli = Path(sys.argv[1]).resolve()


def run(*args, ok=True, cwd=None):
    proc = subprocess.run(
        list(map(str, args)), cwd=cwd, capture_output=True, text=True, errors="replace"
    )
    text = proc.stdout + proc.stderr
    assert (proc.returncode == 0) == ok, (args, proc.returncode, text)
    return text


with tempfile.TemporaryDirectory(prefix="axlum diagnostics ") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "sources"
    s_ws.mkdir()
    for s_name in ["a", "b"]:
        (s_ws / (s_name + ".cpp")).write_text("int main() { return 0; }\n")
    s_manifest = s_ws / "axlum.build.lua"
    s_manifest.write_text(
        'workspace("Diagnostics")\ntarget("A")\nkind("ConsoleApp")\nfiles("a.cpp")\n'
        'target("B")\nkind("ConsoleApp")\nfiles("b.cpp")\n'
    )
    s_out = s_root / "output"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_out)
    s_log, s_trace, s_report = (
        s_root / "logs" / "build output.log",
        s_root / "trace.json",
        s_root / "report.json",
    )
    s_text = run(
        s_cli,
        "build",
        "-B",
        s_out,
        "-t",
        "A",
        "-t",
        "B",
        "-j",
        "2",
        "--verbose",
        "--why-rebuild",
        "--timing",
        "--logfile",
        s_log,
        "--trace",
        s_trace,
        "--report",
        s_report,
    )
    assert "[timing]" in s_text and "ninja explain:" in s_text and "__run" in s_text
    assert "Compile" in s_log.read_text(
        errors="replace"
    ) or "cl.exe" in s_log.read_text(errors="replace")
    s_data = json.loads(s_report.read_text())
    assert (
        s_data["schemaVersion"] == 1 and s_data["exitCode"] == 0 and s_data["jobs"] == 2
    )
    assert s_data["targets"] == ["A", "B"] and s_data["durationMs"] > 0
    assert s_data["actions"] and all(a["durationMs"] >= 0 for a in s_data["actions"])
    s_events = json.loads(s_trace.read_text())["traceEvents"]
    assert any(e["cat"] == "ninja.action" for e in s_events) and all(
        e["ph"] == "X" for e in s_events
    )
    s_ext = "*.obj" if os.name == "nt" else "*.o"
    s_objects = {p: p.stat().st_mtime_ns for p in s_out.rglob(s_ext)}
    assert "no work to do" in run(
        s_cli, "build", "-B", s_out, "A", "B", "--report", s_report
    )
    assert s_objects == {p: p.stat().st_mtime_ns for p in s_objects}
    assert all(
        all(str(o).endswith("build.ninja") for o in a["outputs"])
        for a in json.loads(s_report.read_text())["actions"]
    )
    run(
        s_cli,
        "rebuild",
        "-B",
        s_out,
        "-t",
        "A",
        "-j",
        "1",
        "--report",
        s_report,
        "--trace",
        s_trace,
    )
    assert any(
        p.stat().st_mtime_ns != old
        for p, old in s_objects.items()
        if "/A/" in p.as_posix()
    )
    assert all(
        p.stat().st_mtime_ns == old
        for p, old in s_objects.items()
        if "/B/" in p.as_posix()
    )
    assert [p["name"] for p in json.loads(s_report.read_text())["phases"]] == [
        "clean",
        "build",
    ]
    s_before = {p: p.stat().st_mtime_ns for p in s_out.rglob(s_ext)}
    (s_ws / "a.cpp").write_text("int main() { return 0; } // changed\n")
    run(s_cli, "build", "-B", s_out, "-t", "A", "--dry-run")
    assert s_before == {p: p.stat().st_mtime_ns for p in s_before}
    (s_ws / "a.cpp").write_text(
        "#error intentional_diagnostic_failure\nint main() { return 0; }\n"
    )
    run(
        s_cli,
        "build",
        "-B",
        s_out,
        "-t",
        "A",
        "--logfile",
        s_log,
        "--report",
        s_report,
        "--trace",
        s_trace,
        ok=False,
    )
    s_data = json.loads(s_report.read_text())
    assert (
        s_data["exitCode"] != 0
        and s_data["error"]
        and s_data["phases"][-1]["exitCode"] != 0
    )
    assert "intentional_diagnostic_failure" in s_log.read_text(errors="replace")
    assert json.loads(s_trace.read_text())["traceEvents"]
    (s_ws / "a.cpp").write_text("int main() { return 0; }\n")
    # Bare report/trace filenames must work from the current directory.
    run(
        s_cli,
        "build",
        "-B",
        s_out,
        "-t",
        "A",
        "--report",
        "local-report.json",
        "--trace",
        "local-trace.json",
        cwd=s_root,
    )
    assert (s_root / "local-report.json").exists() and (
        s_root / "local-trace.json"
    ).exists()
    run(s_cli, "build", "-B", s_out, "-j", "0", ok=False)
    run(s_cli, "build", "-B", s_out, "-t", "Missing", "--report", s_report, ok=False)
    assert json.loads(s_report.read_text())["exitCode"] != 0
    print(
        "PASS: multi-target jobs, scoped rebuild, why/verbose, no-op/dry-run, streaming failure logs and JSON/trace reports"
    )
