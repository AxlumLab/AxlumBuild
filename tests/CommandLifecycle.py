# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""One small native workspace validates CLI install/run/clean and the Rhodium SDK bridge."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
s_scratch = s_cli.parent / "test-work"
s_scratch.mkdir(exist_ok=True)


def run(*args, code=0):
    result = subprocess.run(
        list(map(str, args)), capture_output=True, text=True, errors="replace"
    )
    assert result.returncode == code, (
        args,
        result.returncode,
        result.stdout,
        result.stderr,
    )
    return result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix="cli lifecycle ", dir=s_scratch) as s_tmp:
    s_root = Path(s_tmp)
    s_source, s_build, s_prefix = (
        s_root / "source",
        s_root / "build",
        s_root / "install prefix",
    )
    shutil.copytree(s_examples / "cli-lifecycle", s_source)
    run(s_cli, "-S", s_source, "-B", s_build, "-G", "Ninja", "--config", "Release")
    run(
        s_cli,
        "--build",
        s_build,
        "--parallel",
        "2",
        "--target",
        "App",
        "--config",
        "Release",
    )
    s_result = run(
        s_cli,
        "run",
        "App",
        "-B",
        s_build,
        "--no-build",
        "--env",
        "AXLUM_RUN_VALUE=works",
        "--",
        "two words",
        "-x",
    )
    assert (
        "value=42 data=runtime-data" in s_result
        and "env=works" in s_result
        and "arg=two words" in s_result
        and "arg=-x" in s_result
    )
    run(s_cli, "run", "-B", s_build, "-t", "App", "--no-build", "--", "--fail", code=7)
    s_targets = json.loads(run(s_cli, "targets", "-B", s_build, "--json"))
    assert {"App", "Library"} <= s_targets["targets"].keys()
    run(s_cli, "publish", "-B", s_build, "-t", "App", "--prefix", s_prefix, "--dry-run")
    assert not s_prefix.exists()
    run(
        s_cli, "publish", "-B", s_build, "-t", "App", "--prefix", s_prefix, "--no-build"
    )
    s_exe = s_prefix / "bin" / ("App.exe" if os.name == "nt" else "App")
    assert s_exe.is_file() and (s_prefix / "include/Library.hpp").is_file()
    assert (s_prefix / "bin/Data.txt").read_text().strip() == "runtime-data"
    assert (s_prefix / "share/CliLifecycle/Notice.txt").is_file()
    s_manifest = json.loads((s_prefix / "axlum.install.json").read_text())
    assert {"runtime", "development"} <= {f["component"] for f in s_manifest["files"]}
    s_stamp = s_exe.stat().st_mtime_ns
    run(s_cli, "--install", s_build, "--prefix", s_prefix, "--component", "runtime")
    assert s_exe.stat().st_mtime_ns == s_stamp
    (s_prefix / "keep.txt").write_text("user file")
    run(
        s_cli,
        "uninstall",
        "--prefix",
        s_prefix,
        "--component",
        "development",
        "--dry-run",
    )
    assert (s_prefix / "include/Library.hpp").is_file()
    run(s_cli, "uninstall", "--prefix", s_prefix, "--component", "development")
    assert not (s_prefix / "include/Library.hpp").exists() and s_exe.exists()
    s_objectExtension = "*.obj" if os.name == "nt" else "*.o"
    s_objects = list((s_build / "obj/App").rglob(s_objectExtension))
    assert s_objects
    run(
        s_cli,
        "clean",
        "-B",
        s_build,
        "-t",
        "App",
        "--mode",
        "intermediates",
        "--no-dependencies",
        "--dry-run",
    )
    assert all(f.exists() for f in s_objects)
    run(
        s_cli,
        "clean",
        "-B",
        s_build,
        "-t",
        "App",
        "--mode",
        "intermediates",
        "--no-dependencies",
    )
    assert (
        not any(f.exists() for f in s_objects)
        and (s_build / "bin" / s_exe.name).exists()
    )
    run(
        s_cli,
        "build",
        "-B",
        s_build,
        "--target",
        "App",
        "--clean-first",
        "--parallel",
        "2",
    )
    run(s_cli, "publish", "-B", s_build, "-t", "App", "--no-build")
    assert (s_build / "publish/axlum.install.json").exists()
    run(s_cli, "clean", "-B", s_build, "--mode", "publish")
    assert not (s_build / "publish/axlum.install.json").exists() and s_exe.exists()
    run(s_cli, "uninstall", "--prefix", s_prefix)
    assert not s_exe.exists() and (s_prefix / "keep.txt").is_file()
    run(s_cli, "clean", "-B", s_build, "--mode", "cache", "--dry-run")
    assert (s_build / "axlum.state").exists()
    run(s_cli, "clean", "-B", s_build, "--mode", "cache")
    assert not (s_build / "axlum.state").exists()
    s_sdk = json.loads(run(s_cli, "doctor", "--workspace", s_source, "--json"))
    assert s_sdk["provider"] == "Rhodium" and isinstance(s_sdk["profiles"], list)
    assert all("sdkRoot" in p and "missing" in p for p in s_sdk["profiles"])

    if shutil.which("dotnet"):
        s_major = int(run("dotnet", "--version").strip().split(".")[0])
        s_managedSource, s_managedBuild = s_root / "managed", s_root / "managed-build"
        s_managedSource.mkdir()
        (s_managedSource / "axlum.build.lua").write_text(
            'workspace("ManagedCommands")\nconfiguration("Release")\n'
            'target("Managed")\nkind("CSharpConsole")\n'
            f'framework("net{s_major}.0")\nfiles("Program.cs")\n'
        )
        (s_managedSource / "Program.cs").write_text(
            'System.Console.WriteLine("managed-cli:" + string.Join("|", args));\n'
        )
        run(
            s_cli,
            "configure",
            "-S",
            s_managedSource,
            "-B",
            s_managedBuild,
            "--config",
            "Release",
        )
        assert "managed-cli:hello world" in run(
            s_cli, "run", "-B", s_managedBuild, "-t", "Managed", "--", "hello world"
        )
        run(
            s_cli,
            "publish",
            "-B",
            s_managedBuild,
            "-t",
            "Managed",
            "--no-build",
            "--no-aot",
            "--framework-dependent",
            "-o",
            "sdk-publish",
        )
        assert (s_managedBuild / "sdk-publish/axlum.publish.json").is_file()
        run(
            s_cli,
            "install",
            "-B",
            s_managedBuild,
            "-t",
            "Managed",
            "--prefix",
            s_root / "managed-install",
            "--no-aot",
            "--framework-dependent",
        )
        assert (s_root / "managed-install/bin/Managed/Managed.dll").is_file()
        run(s_cli, "clean", "-B", s_managedBuild, "--mode", "publish")
        assert not (s_managedBuild / "sdk-publish/axlum.publish.json").exists()

print(
    "PASS: native publish/install/components/uninstall, run argv/environment/exit, clean modes, CMake CLI forms, Rhodium SDK report"
)
