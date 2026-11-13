# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Single small MSBuild import + target-selection smoke, no broad regression."""

import os, shutil, subprocess, sys, tempfile
from pathlib import Path

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
if os.name != "nt":
    print("SKIP: native vcxproj execution requires Windows")
    sys.exit(0)


def run(*args, ok=True):
    p = subprocess.run(
        list(map(str, args)), capture_output=True, text=True, errors="replace"
    )
    assert (p.returncode == 0) == ok, (args, p.stdout, p.stderr)
    return p.stdout + p.stderr


# MSBuild's file tracker excludes the system TEMP tree. Use the normal build
# tree so SDK incremental tracking can record both inputs and outputs.
s_scratch = s_cli.parent / "test-work"
s_scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(
    prefix="axlum native project ", dir=s_scratch
) as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "source"
    s_out = s_root / "build"
    shutil.copytree(s_examples / "native-project", s_ws)
    s_f = s_ws / "axlum.build.lua"
    s_original = (s_ws / "external/math.vcxproj").read_bytes()
    run(s_cli, "configure", "-f", s_f, "-B", s_out, "--config", "Release")
    run(s_cli, "build", "-B", s_out, "-t", "App", "-j", "2")
    assert "external=42" in run(s_out / "bin/App.exe")
    assert (
        not (s_ws / "external/original-bin").exists()
        and not (s_ws / "external/original-obj").exists()
    )
    assert "unsupported" in run(
        s_cli, "build", "-B", s_out, "-t", "DebugOnly", ok=False
    )
    s_before = (s_out / "lib/ExternalMath.lib").stat().st_mtime_ns
    s_noop = run(s_cli, "build", "-B", s_out, "-t", "App")
    assert (s_out / "lib/ExternalMath.lib").stat().st_mtime_ns == s_before, s_noop
    run(s_cli, "rebuild", "-B", s_out, "-t", "ExternalMath")
    assert (s_out / "lib/ExternalMath.lib").stat().st_mtime_ns != s_before
    s_ide = s_root / "ide"
    run(
        s_cli,
        "ide",
        "-f",
        s_f,
        "-B",
        s_root / "ide-builds",
        "-o",
        s_ide,
        "--format",
        "vs",
    )
    s_sln = (s_ide / "vs/NativeProjectDemo.sln").read_text()
    assert (
        "math.vcxproj" in s_sln
        and "Development|x64" in s_sln
        and "Shipping|x64" in s_sln
    )
    assert (
        "A84363D7-2861-42F5-A1A1-C9EF0656BA16" in s_sln
        and not (s_ide / "vs/ExternalMath.vcxproj").exists()
    )
    s_matrix = run(
        s_cli, "matrix", "-f", s_f, "-B", s_root / "matrix", "--configure-only"
    )
    assert "Skipped Debug-arm64" in s_matrix and "Skipped Release-arm64" in s_matrix
    assert "Release-x64" not in (s_ide / "vs/DebugOnly.vcxproj").read_text()
    s_f.write_text(
        s_f.read_text()
        + '\ntarget("Broken")\nkind("HeaderOnly")\nprivate_deps("DebugOnly")\n'
    )
    assert "depends on unavailable" in run(
        s_cli,
        "configure",
        "-f",
        s_f,
        "-B",
        s_root / "bad",
        "--config",
        "Release",
        ok=False,
    )
    run(s_cli, "clean", "-B", s_out, "-t", "ExternalMath")
    assert not (s_out / "lib/ExternalMath.lib").exists()
    assert not list((s_out / "native-projects/ExternalMath/obj").rglob("*.obj"))
    assert (s_ws / "external/math.vcxproj").read_bytes() == s_original
print(
    "PASS: target limits/diagnostics, matrix skips, native vcxproj/configuration mapping, MSBuild build/no-op/rebuild/clean and direct solution import"
)
