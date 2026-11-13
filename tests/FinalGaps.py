# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""One small native build exercises external PCH, docs, standards and IDE customization."""

import json, os, shutil, subprocess, sys, tempfile, xml.etree.ElementTree as ET
from pathlib import Path

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
if os.name != "nt":
    print(
        "SKIP: MSVC XDC/external-PCH execution requires Windows; planner covers other compilers"
    )
    sys.exit(0)


def run(*args):
    p = subprocess.run(
        list(map(str, args)), capture_output=True, text=True, errors="replace"
    )
    assert p.returncode == 0, (args, p.stdout, p.stderr)
    return p.stdout + p.stderr


with tempfile.TemporaryDirectory(prefix="axlum final gaps ") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "source"
    s_out = s_root / "build"
    shutil.copytree(s_examples / "final-gaps", s_ws)
    s_f = s_ws / "axlum.build.lua"
    run(s_cli, "configure", "-f", s_f, "-B", s_out, "--config", "Release")
    run(
        s_cli,
        "build",
        "-B",
        s_out,
        "-t",
        "ExternalPchApp",
        "-t",
        "LatestApp",
        "-j",
        "2",
    )
    assert "external pch=42" in run(s_out / "bin/ExternalPchApp.exe")
    assert "latest=ok" in run(s_out / "bin/LatestApp.exe")
    s_pch = s_out / "obj/PchProducer/root/precompiled.pch"
    assert len(list((s_out / "obj").rglob("*.pch"))) == 1
    s_doc = s_out / "api-docs/ExternalPchApp/ExternalPchApp.xml"
    ET.parse(s_doc)
    assert "Gap documentation marker" in " ".join(ET.parse(s_doc).getroot().itertext())
    s_before = (
        s_pch.stat().st_mtime_ns,
        s_doc.stat().st_mtime_ns,
        (s_out / "bin/LatestApp.exe").stat().st_mtime_ns,
    )
    run(s_cli, "build", "-B", s_out, "-t", "ExternalPchApp", "-t", "LatestApp")
    assert s_before == (
        s_pch.stat().st_mtime_ns,
        s_doc.stat().st_mtime_ns,
        (s_out / "bin/LatestApp.exe").stat().st_mtime_ns,
    )
    s_source = s_ws / "Main.cpp"
    s_source.write_text(
        s_source.read_text().replace(
            "Gap documentation marker", "Updated documentation marker"
        )
    )
    run(s_cli, "build", "-B", s_out, "-t", "ExternalPchApp")
    assert (
        "Updated documentation marker" in " ".join(ET.parse(s_doc).getroot().itertext())
        and s_pch.stat().st_mtime_ns == s_before[0]
    )
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
    s_ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    s_project = ET.parse(s_ide / "vs/ExternalPchApp.vcxproj")
    assert s_project.find(".//m:ParityTag", s_ns).text == "target<&>"
    assert s_project.find(".//m:CustomTail", s_ns).text == "enabled"
    assert (
        ET.parse(s_ide / "vs/ExternalPchApp.vcxproj.user")
        .find(".//m:LocalDebuggerCommandArguments", s_ns)
        .text
        == "--example"
    )
    ET.parse(s_ide / "vs/ExternalPchApp.vcxproj.filters")
    s_launch = json.loads((s_ide / "vscode/.vscode/launch.json").read_text())
    assert any(
        x.get("args") == ["--example"] and x.get("stopAtEntry")
        for x in s_launch["configurations"]
    )
    s_workspace = json.loads((s_ide / "vscode/FinalGaps.code-workspace").read_text())
    assert s_workspace["settings"]["editor.tabSize"] == 4
    assert (
        "AXLUM_CUSTOM"
        in (s_ide / "xcode/FinalGaps.xcodeproj/project.pbxproj").read_text()
    )
    assert "customEntry" in (s_ide / "android-studio/build.gradle").read_text()
    run(s_cli, "clean", "-B", s_out, "-t", "ExternalPchApp")
    assert not s_doc.exists()
print(
    "PASS: external PCH C++14 app, independent Latest, real XDC/XML docs/no-op/update/clean, Lua customizers across four IDEs"
)
