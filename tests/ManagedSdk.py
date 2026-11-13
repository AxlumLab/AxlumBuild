# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Real SDK properties, source generators, warnings, NuGet analyzer and rebuild behavior."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()


def run(*args, ok=True, env=None):
    proc = subprocess.run(
        list(map(str, args)), env=env, capture_output=True, text=True, errors="replace"
    )
    text = proc.stdout + proc.stderr
    assert (proc.returncode == 0) == ok, (args, proc.returncode, text)
    return text


s_major = int(run("dotnet", "--version").strip().split(".")[0])
with tempfile.TemporaryDirectory(prefix="axlum managed sdk ") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "source"
    shutil.copytree(s_examples / "managed-dsl", s_ws)
    for s_file in (s_ws / "axlum.build.lua", s_ws / "generator/ValueGenerator.csproj"):
        s_file.write_text(s_file.read_text().replace("net10.0", f"net{s_major}.0"))
    s_manifest = s_ws / "axlum.build.lua"
    s_script = s_manifest.read_text()
    s_out = s_root / "build"
    s_env = os.environ.copy()
    s_env["NUGET_PACKAGES"] = str(s_root / "package cache")
    run(
        s_cli,
        "configure",
        "-f",
        s_manifest,
        "-B",
        s_out,
        "--config",
        "Release",
        env=s_env,
    )
    run(s_cli, "build", "-B", s_out, "-t", "ManagedApp", "-j", "2", env=s_env)
    s_dll = (
        s_out / "dotnet/ManagedApp/artifacts/bin/ManagedApp/release/Managed.Sample.dll"
    )
    assert "value=42 metadata=sdk<&> nullable=True" in run("dotnet", s_dll, env=s_env)
    s_xml = ET.parse(s_out / "dotnet/ManagedApp/ManagedApp.csproj").getroot()
    s_props = {
        element.tag: element.text
        for group in s_xml.findall("PropertyGroup")
        for element in group
    }
    assert (
        s_props["Nullable"] == "enable"
        and s_props["Optimize"] == "true"
        and s_props["DebugType"] == "portable"
    )
    assert s_props["AssemblyVersion"] == "1.2.3.0" and s_props["Company"] == "Axlum"
    assert (
        s_props["RootNamespace"] == "ManagedSample"
        and s_props["Deterministic"] == "true"
    )
    assert list((s_out / "dotnet/ManagedApp/generated").rglob("GeneratedValue.g.cs"))
    s_before = s_dll.stat().st_mtime_ns
    run(s_cli, "build", "-B", s_out, "-t", "ManagedApp", env=s_env)
    assert s_dll.stat().st_mtime_ns == s_before
    (s_ws / "value.txt").write_text("43")
    run(s_cli, "build", "-B", s_out, "-t", "ManagedApp", env=s_env)
    assert "value=43" in run("dotnet", s_dll, env=s_env)
    (s_ws / "value.txt").write_text("warn")
    assert "error AXL001" in run(
        s_cli, "build", "-B", s_out, "-t", "ManagedApp", env=s_env, ok=False
    )
    s_manifest.write_text(
        s_script.replace(
            'warnings_as_errors("AXL001")', 'warnings_not_as_errors("AXL001")'
        )
    )
    assert "warning AXL001" in run(
        s_cli, "build", "-B", s_out, "-t", "ManagedApp", env=s_env
    )
    s_manifest.write_text(s_script + '\nnowarn("AXL001")\n')
    run(s_cli, "build", "-B", s_out, "-t", "ManagedApp", env=s_env)
    (s_ws / "value.txt").write_text("44")
    s_source = s_ws / "src/Program.cs"
    s_original = s_source.read_text()
    s_source.write_text(
        s_original
        + "\nstring invalid = null; System.Console.WriteLine(invalid.Length);\n"
    )
    assert "CS860" in run(
        s_cli, "build", "-B", s_out, "-t", "ManagedApp", env=s_env, ok=False
    )
    s_source.write_text(s_original)
    s_manifest.write_text(s_script)
    run(s_cli, "build", "-B", s_out, "-t", "ManagedApp", env=s_env)
    s_before = s_dll.stat().st_mtime_ns
    run(s_cli, "rebuild", "-B", s_out, "-t", "ManagedApp", env=s_env)
    assert s_dll.stat().st_mtime_ns != s_before
    print(
        "PASS: SDK XML, reflection metadata, constants/nullable, generator files, warning errors/exceptions/suppression and forced managed rebuild"
    )

    # Create a local analyzer package from the real compiled generator; restore through SDK/NuGet.
    s_feed = s_root / "local feed"
    s_feed.mkdir()
    s_generator = (
        s_out
        / "dotnet/ValueGenerator/artifacts/bin/ValueGenerator/release/ValueGenerator.dll"
    )
    with zipfile.ZipFile(
        s_feed / "Axlum.FixtureGenerator.1.0.0.nupkg", "w"
    ) as s_package:
        s_package.writestr(
            "Axlum.FixtureGenerator.nuspec",
            "<package><metadata><id>Axlum.FixtureGenerator</id><version>1.0.0</version><authors>Axlum</authors><description>Local analyzer test</description></metadata></package>",
        )
        s_package.write(s_generator, "analyzers/dotnet/cs/ValueGenerator.dll")
    s_configuration = ET.Element("configuration")
    s_sources = ET.SubElement(s_configuration, "packageSources")
    ET.SubElement(s_sources, "clear")
    ET.SubElement(s_sources, "add", key="local", value=str(s_feed))
    ET.ElementTree(s_configuration).write(
        s_ws / "NuGet.Config", encoding="utf-8", xml_declaration=True
    )
    s_manifest.write_text(
        s_script.replace(
            'analyzer_projects("ValueGenerator")',
            'nuget("Axlum.FixtureGenerator", {version="1.0.0", private_assets="all", include_assets="analyzers"})',
        )
    )
    run(s_cli, "build", "-B", s_out, "-t", "ManagedApp", env=s_env)
    assert "value=44" in run("dotnet", s_dll, env=s_env)
    assert (
        "PackageReference"
        in (s_out / "dotnet/ManagedApp/ManagedApp.csproj").read_text()
    )
    print(
        "PASS: local NuGet analyzer restore with project-level NuGet.Config outside generated project directories"
    )

    for s_bad, s_message in [
        ('nullable("nonsense")', "nullable"),
        ('csdefines("BAD=1")', "identifiers"),
        ('nuget("X", {version="1"})\nnuget("X", {version="2"})', "duplicate NuGet"),
    ]:
        s_manifest.write_text(s_script + "\n" + s_bad)
        assert s_message in run(
            s_cli,
            "configure",
            "-f",
            s_manifest,
            "-B",
            s_root / "invalid",
            env=s_env,
            ok=False,
        )
