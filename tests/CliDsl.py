# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""CLI11 contracts and Lua/Rhodium embedding through the real executable."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

s_cli = Path(sys.argv[1]).resolve()


def run(*args, cwd=None, ok=True):
    result = subprocess.run(
        [str(x) for x in args],
        cwd=cwd,
        capture_output=True,
        text=True,
        errors="replace",
    )
    text = result.stdout + result.stderr
    assert (result.returncode == 0) == ok, (args, result.returncode, text)
    return text


assert "configure" in run(s_cli)
assert "doctor" in run(s_cli, "--help")
assert "AxlumBuild 0.1.0" in run(s_cli, "--version")
assert "--compiler" in run(s_cli, "configure", "--help")
assert "--target" in run(s_cli, "help", "build")
assert "--config" in run(s_cli, "--help-all")
assert "config" in run(s_cli, "configure", "--config", "bad/config", ok=False)
assert "compiler" in run(s_cli, "configure", "--compiler", "unknown", ok=False)
assert "file" in run(s_cli, "configure", "-f", "does-not-exist.lua", ok=False)
run(s_cli, "build", "--unknown", ok=False)
run(s_cli, "configure", "-f", ok=False)
run(s_cli, "build", "clean", ok=False)
run(s_cli, "bogus", ok=False)
print("PASS: CLI help, version, per-command options and invalid arguments")

with tempfile.TemporaryDirectory(prefix="axlum-dsl-") as s_temp:
    s_root = Path(s_temp)
    (s_root / "src" / "nested").mkdir(parents=True)
    (s_root / "include").mkdir()
    for s_cache in (".cache", ".rhodium", ".tpkg"):
        (s_root / s_cache).mkdir()
        (s_root / s_cache / "cache.cpp").write_text(
            '#error "Cache source must not be compiled"\n'
        )
    (s_root / "include" / "Value.hpp").write_text("#define RESULT 42\n")
    (s_root / "src" / "Main.cpp").write_text(
        '#include "Value.hpp"\nint main() { return RESULT == 42 ? 0 : 1; }\n'
    )
    (s_root / "src" / "nested" / "helper.cpp").write_text(
        "int helper() { return 1; }\n"
    )
    (s_root / "target.lua").write_text("""target("Demo")
kind("ConsoleApp")
language("C++")
cppdialect("C++20")
files({"**/*.cpp", "include/**.hpp"})
includedirs({"include"})
""")
    s_manifest = s_root / "axlum.build.lua"
    s_manifest.write_text("""workspace("DslTest")
configuration("Debug", "Release")
architecture("x64")
when("all", function() include("target.lua") end)
""")
    s_build = s_root / "output"
    run(s_cli, "configure", "-B", s_build, cwd=s_root)  # default Lua filename
    s_db = json.loads((s_build / "compile_commands.json").read_text())
    assert len(s_db) == 2, s_db
    run(s_cli, "build", "-B", s_build, "-t", "Demo")
    run(s_build / "bin" / ("Demo.exe" if os.name == "nt" else "Demo"))
    run(s_cli, "graph", "-B", s_build)
    run(s_cli, "configure", "--config", "RELEASE", "-B", s_root / "release", cwd=s_root)
    s_original = s_manifest.read_text()
    for s_script, s_message in [
        ('workspace("X")\nunknown_setting()\n', "unknown_setting"),
        ('workspace("X")\nfiles({"Main.cpp"})\n', "target"),
        ('workspace("X")\narchitecture("mips")\n', "architecture"),
        ('workspace("X")\ninclude("missing.lua")\n', "DSL errors"),
        (
            'workspace("X")\nrhodium.require("fmt", {version="10.2.1"})\n',
            "explicit source",
        ),
        (
            'workspace("X")\ntarget("A")\nkind("HeaderOnly")\nprivate_deps({"B"})\n'
            'target("B")\nkind("HeaderOnly")\nprivate_deps({"A"})\n',
            "cycle",
        ),
    ]:
        s_manifest.write_text(s_script)
        assert s_message in run(
            s_cli, "configure", "-B", s_root / "bad-output", cwd=s_root, ok=False
        )
    s_manifest.write_text(s_original.replace('"Debug", "Release"', '"Release"'))
    assert "not declared" in run(
        s_cli, "configure", "-B", s_root / "debug", cwd=s_root, ok=False
    )
    print(
        "PASS: Lua include/when, recursive globs, header filtering, build, graph and diagnostics"
    )

    (s_root / "src" / "excluded.cpp").write_text(
        '#error "removefiles did not exclude this file"\n'
    )
    (s_root / "conditional").mkdir()
    (s_root / "conditional" / "debugOnly.cpp").write_text(
        "int debugOnly() { return 1; }\n"
    )
    s_manifest.write_text("""workspace("Filtered")
configuration({"Debug", "Release"})
cppdialect("C++17")
includedirs("include")
defines("WORKSPACE_DEFAULT=1")
filter("configurations:Debug")
    defines("CONFIG_DEBUG=1")
filter({})
target("FilteredApp")
    kind("ConsoleApp")
    files({"src/**/*.cpp", "./src/Main.cpp"})
    removefiles("src/excluded.cpp")
    filter("configurations:Debug")
        files("conditional/debugOnly.cpp")
    filter({"configurations:Release", "architecture:x64"})
        defines("CONFIG_RELEASE=1")
    filter({})
    defines("AFTER_FILTER=1")
target("Unrelated")
    kind("HeaderOnly")
""")
    for s_config in ("debug", "release"):
        s_output = s_root / ("filtered-" + s_config)
        run(s_cli, "configure", "--config", s_config, "-B", s_output, cwd=s_root)
        s_entries = json.loads((s_output / "compile_commands.json").read_text())
        assert len(s_entries) == (3 if s_config == "debug" else 2), s_entries
        s_args = " ".join(s_entries[0]["arguments"])
        assert "WORKSPACE_DEFAULT=1" in s_args and "AFTER_FILTER=1" in s_args
        assert ("CONFIG_DEBUG=1" in s_args) == (s_config == "debug")
        assert ("CONFIG_RELEASE=1" in s_args) == (s_config == "release")
        assert "c++17" in s_args
        run(s_cli, "build", "-B", s_output)
        run(
            s_output / "bin" / ("FilteredApp.exe" if os.name == "nt" else "FilteredApp")
        )
        s_graph = run(s_cli, "graph", "-B", s_output)
        assert ("debugOnly.cpp" in s_graph) == (s_config == "debug")
    for s_script, s_message in [
        ('target("NoWorkspace")', "workspace"),
        ('workspace("X")\nworkspace("Y")', "once"),
        ('workspace("X")\nconfiguration({})', "requires"),
        ('workspace("X")\nfilter("unknown:Debug")', "unknown filter"),
        (
            'workspace("X")\nfilter("configurations:bad/config")',
            "invalid configuration",
        ),
        ('workspace("X")\ncppdialect("C++99")', "unsupported C++ dialect"),
        (
            'workspace("X")\ntarget("A")\nfiles({[1]="a.cpp", name="b.cpp"})',
            "contiguous",
        ),
        ('workspace("X")\ntarget("A")\nfiles({false})', "array of strings"),
        (
            'workspace("X")\ntarget("A")\nkind("HeaderOnly")\nframework("net8.0")',
            "managed settings",
        ),
        (
            'workspace("X")\ntarget("A")\nmodule("B")\nkind("StaticLibrary")',
            "target scope",
        ),
    ]:
        s_manifest.write_text(s_script)
        assert s_message in run(
            s_cli, "configure", "-B", s_root / "invalid", cwd=s_root, ok=False
        )
    print(
        "PASS: workspace defaults, configuration filters, reset, glob exclusions/deduplication and strict scopes/types"
    )

    (s_root / "Scripts").mkdir()
    (s_root / "Scripts" / "Library.cs").write_text(
        "public static class Api { public static int Value => 42; }\n"
    )
    (s_root / "Scripts" / "Main.cs").write_text(
        "System.Console.WriteLine(Api.Value);\n"
    )
    s_manifest.write_text("""workspace("ManagedDsl")
target("ManagedApi")
    kind("CSharpLibrary")
    framework("net8.0")
    files({"Scripts/Library.cs"})
target("ManagedApp")
    kind("CSharpConsole")
    framework("net8.0")
    files({"Scripts/Main.cs"})
    references({"ManagedApi"})
""")
    s_managed = s_root / "managed-output"
    run(s_cli, "configure", "-B", s_managed, cwd=s_root)
    s_project = s_managed / "dotnet" / "ManagedApp" / "ManagedApp.csproj"
    s_tree = ET.parse(s_project)
    assert s_tree.findtext("./PropertyGroup/TargetFramework") == "net8.0"
    s_ref = Path(s_tree.find("./ItemGroup/ProjectReference").attrib["Include"])
    assert s_ref.is_file() and s_ref.name == "ManagedApi.csproj"
    assert not list((s_root / "Scripts").glob("*.csproj"))
    if shutil.which("dotnet") and run("dotnet", "--list-sdks").strip():
        run(s_cli, "build", "-B", s_managed, "--target", "ManagedApp")
        s_dlls = list(
            (s_managed / "dotnet" / "ManagedApp" / "artifacts" / "bin").rglob(
                "ManagedApp.dll"
            )
        )
        assert len(s_dlls) == 1, s_dlls
        assert run("dotnet", s_dlls[0]).strip() == "42"
        run(s_cli, "clean", "-B", s_managed)
        assert s_project.is_file(), "clean removed a generated SDK project"
        run(s_cli, "build", "-B", s_managed, "--target", "ManagedApp")
        assert run("dotnet", s_dlls[0]).strip() == "42"
    else:
        print("SKIP: generated C# project execution (SDK absent)")
    print(
        "PASS: C# library/application DSL, generated SDK projects and ProjectReference"
    )
