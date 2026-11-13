# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Offline tests through embedded Rhodium APIs (no Rhodium CLI or source build)."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()


def run(*args, cwd=None, ok=True):
    proc = subprocess.run(
        [str(a) for a in args],
        cwd=cwd,
        capture_output=True,
        text=True,
        errors="replace",
    )
    text = proc.stdout + proc.stderr
    assert (proc.returncode == 0) == ok, (args, proc.returncode, text)
    return text


with tempfile.TemporaryDirectory(prefix="axlum-packages-") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "workspace with spaces"
    shutil.copytree(s_examples / "rhodium-local", s_ws)
    s_manifest = s_ws / "axlum.build.lua"
    s_private = s_ws / "packages" / "private" / "include"
    s_private.mkdir(parents=True)
    (s_private / "private.hpp").write_text("#define PRIVATE_VALUE 1\n")
    s_manifest.write_text(
        s_manifest.read_text()
        .replace(
            'target("MathWrapper")',
            'rhodium.require_local("private_headers", {path="packages/private", build="header_only", '
            'artifacts={include_dirs={"include"}, defines={"PRIVATE_SECRET=1"}}})\n'
            'target("MathWrapper")',
        )
        .replace(
            'public_packages("sample_math")',
            'public_packages("sample_math")\n    packages("private_headers")',
        )
    )
    s_wrapper = s_ws / "src" / "Wrapper.cpp"
    s_wrapper.write_text(
        '#include <private.hpp>\n#ifndef PRIVATE_SECRET\n#error "Missing private package"\n#endif\n'
        + s_wrapper.read_text()
    )
    s_main = s_ws / "src" / "Main.cpp"
    s_main.write_text(
        '#ifdef PRIVATE_SECRET\n#error "Private package leaked"\n#endif\n'
        + s_main.read_text()
    )
    s_build = s_root / "debug output"
    s_command = [s_cli, "configure", "-f", s_manifest, "-B", s_build]
    assert "restore failed" in run(*s_command, "--locked", ok=False)
    s_log = run(*s_command)
    assert "[rhodium]" in s_log, s_log
    assert (s_ws / "tpkg.lock.toml").exists()
    assert (s_ws / ".rhodium" / "artifacts").is_dir()
    s_db = json.loads((s_build / "compile_commands.json").read_text())
    assert len(s_db) == 2
    assert all(
        any("SAMPLE_PACKAGE=7" in arg for arg in item["arguments"]) for item in s_db
    )
    assert (
        sum(
            any("PRIVATE_SECRET=1" in arg for arg in item["arguments"]) for item in s_db
        )
        == 1
    )
    run(s_cli, "build", "-B", s_build, "-t", "PackageHello")
    s_exe = (
        s_build / "bin" / ("PackageHello.exe" if os.name == "nt" else "PackageHello")
    )
    assert run(s_exe).strip() == "package result=42"
    assert "no work to do" in run(s_cli, "build", "-B", s_build).lower()
    run(*s_command, "--locked")
    run(s_cli, "graph", "-B", s_build)
    assert not list((s_ws / "packages" / "math").glob("CMakeCache.txt"))
    s_original = s_manifest.read_text()
    s_manifest.write_text(s_original.replace("SAMPLE_PACKAGE=7", "SAMPLE_PACKAGE=8"))
    assert "restore failed" in run(*s_command, "--locked", ok=False)
    s_manifest.write_text(s_original)
    s_source = s_ws / "packages" / "math" / "src" / "Add.cpp"
    s_source.write_text(s_source.read_text().replace("lhs + rhs", "lhs + rhs + 1"))
    run(*s_command)
    run(s_cli, "build", "-B", s_build)
    assert run(s_exe).strip() == "package result=43"
    s_release = s_root / "release output"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_release, "--config", "release")
    run(s_cli, "build", "-B", s_release)
    assert run(s_release / "bin" / s_exe.name).strip() == "package result=43"
    s_lock = (s_ws / "tpkg.lock.toml").read_text()
    assert "debug" in s_lock and "release" in s_lock
    run(
        s_cli, "graph", "-B", s_build
    )  # the Release restore must preserve Debug artifacts
    print(
        "PASS: local header/static packages, public/transitive interfaces, locked mode, package edits, Debug/Release"
    )

    # A pinned local Git repository exercises fetch/cache/lock without network.
    s_repository = s_root / "git source"
    (s_repository / "include").mkdir(parents=True)
    (s_repository / "include" / "git_value.hpp").write_text("#define GIT_VALUE 91\n")
    run("git", "init", s_repository)
    run("git", "-C", s_repository, "add", ".")
    run(
        "git",
        "-C",
        s_repository,
        "-c",
        "user.name=Test",
        "-c",
        "user.email=test@example.invalid",
        "commit",
        "-m",
        "fixture",
    )
    s_commit = run("git", "-C", s_repository, "rev-parse", "HEAD").strip()
    s_gitws = s_root / "git workspace"
    s_gitws.mkdir()
    (s_gitws / "Main.cpp").write_text(
        "#include <git_value.hpp>\nint main() { return GIT_VALUE == 91 ? 0 : 1; }\n"
    )
    s_gitmanifest = s_gitws / "axlum.build.lua"
    s_gitmanifest.write_text(
        'workspace("GitPackage")\n'
        + 'rhodium.require("local_git", {source='
        + json.dumps(s_repository.as_uri())
        + ", version="
        + json.dumps(s_commit)
        + ', build="header_only"})\n'
        + 'target("GitApp")\nkind("ConsoleApp")\nfiles("Main.cpp")\npackages("local_git")\n'
    )
    s_gitbuild = s_root / "git output"
    s_gitcommand = [s_cli, "configure", "-f", s_gitmanifest, "-B", s_gitbuild]
    run(*s_gitcommand)
    run(s_cli, "build", "-B", s_gitbuild)
    run(s_gitbuild / "bin" / ("GitApp.exe" if os.name == "nt" else "GitApp"))
    assert s_commit in (s_gitws / "tpkg.lock.toml").read_text()
    run(*s_gitcommand, "--locked")
    s_gitmanifest.write_text(
        s_gitmanifest.read_text().replace(
            'packages("local_git")', 'packages("missing")'
        )
    )
    assert "undeclared package" in run(*s_gitcommand, ok=False)
    print(
        "PASS: embedded Git fetch, version/ref mapping, lock/cache reuse and unknown-package diagnostics"
    )

    # Shared-package bin/lib metadata must reach the consumer runtime directory.
    s_sharedws = s_root / "shared package workspace"
    shutil.copytree(s_examples / "rhodium-local", s_sharedws)
    s_cmake = s_sharedws / "packages" / "math" / "CMakeLists.txt"
    s_cmake.write_text(
        s_cmake.read_text()
        .replace(
            "add_library(sample_math STATIC",
            "set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)\nadd_library(sample_math SHARED",
        )
        .replace(
            "ARCHIVE DESTINATION lib",
            "ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin",
        )
    )
    s_sharedManifest = s_sharedws / "axlum.build.lua"
    s_sharedManifest.write_text(
        s_sharedManifest.read_text()
        .replace('linkage = "static"', 'linkage = "shared"')
        .replace('lib_dirs = {"lib"}', 'lib_dirs = {"lib"}, bin_dirs = {"bin"}')
    )
    s_sharedOutput = s_root / "shared output"
    run(s_cli, "configure", "-f", s_sharedManifest, "-B", s_sharedOutput)
    run(s_cli, "build", "-B", s_sharedOutput, "-t", "PackageHello")
    s_sharedExe = (
        s_sharedOutput
        / "bin"
        / ("PackageHello.exe" if os.name == "nt" else "PackageHello")
    )
    s_runtimeName = "sample_math.dll" if os.name == "nt" else "libsample_math.so"
    assert (s_sharedExe.parent / s_runtimeName).is_file()
    s_originalRuntime = next(
        (s_sharedws / ".rhodium" / "artifacts").rglob(s_runtimeName)
    )
    s_hidden = s_originalRuntime.with_name(s_originalRuntime.name + ".saved")
    s_originalRuntime.rename(s_hidden)
    try:
        assert run(s_sharedExe, cwd=s_sharedExe.parent).strip() == "package result=42"
    finally:
        s_hidden.rename(s_originalRuntime)
    print(
        "PASS: shared-package runtime metadata, automatic copy and execution without the cached DLL/SO"
    )

    if os.name == "nt":
        s_staticws = s_root / "static CRT workspace"
        shutil.copytree(s_examples / "rhodium-local", s_staticws)
        s_staticManifest = s_staticws / "axlum.build.lua"
        s_staticManifest.write_text(
            s_staticManifest.read_text().replace(
                'cppdialect("C++20")', 'cppdialect("C++20")\n    runtime("Static")'
            )
        )
        s_staticOutput = s_root / "static CRT output"
        run(s_cli, "configure", "-f", s_staticManifest, "-B", s_staticOutput)
        run(s_cli, "build", "-B", s_staticOutput)
        assert (
            run(s_staticOutput / "bin" / "PackageHello.exe").strip()
            == "package result=42"
        )
        s_db = json.loads((s_staticOutput / "compile_commands.json").read_text())
        assert all("/MTd" in item["arguments"] for item in s_db)
        s_staticManifest.write_text(
            s_staticManifest.read_text().replace(
                'linkage = "static",', 'linkage = "static", runtime = "dynamic",'
            )
        )
        assert "CRT policy conflicts" in run(
            s_cli, "configure", "-f", s_staticManifest, "-B", s_staticOutput, ok=False
        )
        print(
            "PASS: native and CMake-package static CRT agreement and incompatible-policy rejection"
        )
