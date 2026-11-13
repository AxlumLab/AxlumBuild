# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""One combined build/publication and IDE generation; no AOT stress or full regression."""

import json, os, shutil, subprocess, sys, tempfile
from pathlib import Path

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()


def run(*args):
    p = subprocess.run(
        list(map(str, args)), capture_output=True, text=True, errors="replace"
    )
    assert p.returncode == 0, (args, p.stdout, p.stderr)
    return p.stdout + p.stderr


with tempfile.TemporaryDirectory(prefix="axlum parity ") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "source"
    s_out = s_root / "build"
    shutil.copytree(s_examples / "parity-suite", s_ws)
    s_major = int(run("dotnet", "--version").strip().split(".")[0])
    s_f = s_ws / "axlum.build.lua"
    s_f.write_text(s_f.read_text().replace("net10.0", f"net{s_major}.0"))
    run(s_cli, "configure", "-f", s_f, "-B", s_out, "--config", "Release")
    run(s_cli, "build", "-B", s_out, "-t", "ManagedApp", "-t", "PchConsumer", "-j", "2")
    s_pchs = list((s_out / "obj/PchConsumer").rglob("*.pch")) + list(
        (s_out / "obj/PchConsumer").rglob("*.gch")
    )
    assert len(s_pchs) == 1, s_pchs
    assert "modular interop=42" in run(
        "dotnet",
        s_out / "dotnet/ManagedApp/artifacts/bin/ManagedApp/release/ManagedApp.dll",
    )
    run(s_cli, "publish", "-B", s_out, "-t", "ManagedApp", "--framework-dependent")
    s_pub = s_out / "publish/ManagedApp/portable/managed"
    s_m = json.loads((s_pub / "axlum.publish.json").read_text())
    s_files = [x["path"] for x in s_m["files"]]
    assert any("NativeCore" in x for x in s_files) and any(
        "NativeFacade" in x for x in s_files
    )
    assert "modular interop=42" in run("dotnet", s_pub / "ManagedApp.dll")
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
        "all",
    )
    s_sln = (s_ide / "vs/ParitySuite.sln").read_text()
    assert "9A19103F" in s_sln and "ManagedApp.csproj" in s_sln
    # A direct SDK/IDE build must prepare native dependencies through the same graph.
    s_project = s_root / "ide-builds/Debug-x64/dotnet/ManagedApp/ManagedApp.csproj"
    run("dotnet", "build", s_project, "--nologo")
    assert "modular interop=42" in run(
        "dotnet", s_project.parent / "artifacts/bin/ManagedApp/debug/ManagedApp.dll"
    )
    s_launch = json.loads((s_ide / "vscode/.vscode/launch.json").read_text())
    assert any(
        x["type"] == "coreclr" and x["program"].endswith("ManagedApp.dll")
        for x in s_launch["configurations"]
    )
    assert (
        "com.android.application"
        in (s_ide / "android-studio/app_AndroidApp/build.gradle").read_text()
    )
    assert (
        "android.app.NativeActivity"
        in (
            s_ide / "android-studio/app_AndroidApp/src/main/AndroidManifest.xml"
        ).read_text()
    )
print(
    "PASS: cross-workspace module binary/interface, real shared PCH, Modular managed publication/interop, mixed IDEs and Android app entry"
)
