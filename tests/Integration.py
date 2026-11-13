# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Real end-to-end builds using the produced AxlumBuild executable and Ninja."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

s_cli, s_samples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
s_suffix = ".exe" if os.name == "nt" else ""


def run(*args, cwd=None, ok=True, env=None):
    proc = subprocess.run(
        [str(a) for a in args],
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    if ok and proc.returncode:
        raise AssertionError(
            f"command failed ({proc.returncode}): {args}\n{proc.stdout}"
        )
    if not ok and proc.returncode == 0:
        raise AssertionError(f"command should have failed: {args}\n{proc.stdout}")
    return proc.stdout


def fixture(name, root, label=None):
    label = label or name
    src = root / ("workspace with spaces " + label)
    shutil.copytree(s_samples / name, src)
    build = root / ("output with spaces " + label)
    return src, build


with tempfile.TemporaryDirectory(prefix="axlum-build-test-") as s_tmp:
    s_root = Path(s_tmp)
    # Native exe and public/private compile interface tests.
    s_ws, s_build = fixture("static-and-exe", s_root)
    run(
        s_cli,
        "configure",
        "-f",
        s_ws / "axlum.build.lua",
        "-B",
        s_build,
        "--config",
        "debug",
    )
    s_db = json.loads((s_build / "compile_commands.json").read_text(encoding="utf-8"))
    assert len(s_db) == 2 and all("arguments" in cmd for cmd in s_db)
    assert "MATH_PUBLIC=7" in " ".join(s_db[1]["arguments"] + s_db[0]["arguments"])
    run(s_cli, "build", "-B", s_build, "--target", "Hello")
    s_exe = s_build / "bin" / ("Hello" + s_suffix)
    assert run(s_exe).strip() == "result=5"
    s_objs = list((s_build / "obj").rglob("*.obj" if os.name == "nt" else "*.o"))
    assert len(s_objs) == 2, s_objs
    s_before = {str(f): f.stat().st_mtime_ns for f in s_objs}
    s_log = run(s_cli, "build", "-B", s_build, "--target", "Hello")
    assert "no work to do" in s_log.lower(), s_log
    assert s_before == {str(f): f.stat().st_mtime_ns for f in s_objs}
    time.sleep(1.1)
    (s_ws / "src" / "Main.cpp").write_text(
        (s_ws / "src" / "Main.cpp").read_text() + "\n// edited\n"
    )
    run(s_cli, "build", "-B", s_build, "--target", "Hello")
    s_after = {str(f): f.stat().st_mtime_ns for f in s_objs}
    assert sum(s_after[k] != s_before[k] for k in s_before) == 1, (s_before, s_after)
    time.sleep(1.1)
    (s_ws / "include" / "Math.hpp").write_text(
        (s_ws / "include" / "Math.hpp").read_text() + "\n// header edited\n"
    )
    run(s_cli, "build", "-B", s_build, "--target", "Hello")
    s_afterHeaders = {str(f): f.stat().st_mtime_ns for f in s_objs}
    assert sum(s_afterHeaders[k] != s_after[k] for k in s_after) == 2, (
        s_after,
        s_afterHeaders,
    )
    # Failed compile must be surfaced and not cache a successful object.
    s_main = s_ws / "src" / "Main.cpp"
    s_main.write_text(s_main.read_text() + '\n#error "intentional failure"\n')
    run(s_cli, "build", "-B", s_build, "--target", "Hello", ok=False)
    s_main.write_text(
        s_main.read_text().replace('#error "intentional failure"', "// fixed")
    )
    run(s_cli, "build", "-B", s_build, "--target", "Hello")
    assert run(s_exe).strip() == "result=5"
    run(s_cli, "clean", "-B", s_build, "--target", "Hello")
    assert not s_exe.exists()
    print(
        "PASS: native graph, interfaces, spaces, no-op, source/header increments, errors, targeted clean"
    )

    s_ws, s_build = fixture("hello-cpp", s_root)
    run(
        s_cli,
        "configure",
        "-f",
        s_ws / "axlum.build.lua",
        "-B",
        s_build,
        "--config",
        "release",
    )
    run(s_cli, "build", "-B", s_build)
    assert "hello from AxlumBuild" in run(s_build / "bin" / ("HelloCpp" + s_suffix))
    print("PASS: standalone hello C++ release build")

    s_ws, s_build = fixture("mixed-c-cpp", s_root)
    run(s_cli, "configure", "-f", s_ws / "axlum.build.lua", "-B", s_build)
    run(s_cli, "build", "-B", s_build)
    assert run(s_build / "bin" / ("MixedApp" + s_suffix)).strip() == "mixed=9"
    s_objects = list((s_build / "obj").rglob("*.obj" if os.name == "nt" else "*.o"))
    assert len(s_objects) == 3, s_objects
    print(
        "PASS: C + C++ compilation, transitive static linkage, private dependency isolation"
    )

    s_ws, s_build = fixture("shared-and-exe", s_root)
    run(s_cli, "configure", "-f", s_ws / "axlum.build.lua", "-B", s_build)
    run(s_cli, "build", "-B", s_build)
    assert run(s_build / "bin" / ("SharedApp" + s_suffix)).strip() == "shared=12"
    print("PASS: shared library build/link/runtime loader search")

    # Native project with an explicit Clang selection (when both clang and clang++ exist).
    if os.name != "nt" and shutil.which("clang") and shutil.which("clang++"):
        s_ws, s_build = fixture("hello-cpp", s_root, "hello-cpp-clang")
        run(
            s_cli,
            "configure",
            "-f",
            s_ws / "axlum.build.lua",
            "-B",
            s_build,
            "--compiler",
            "clang",
        )
        run(s_cli, "build", "-B", s_build)
        assert "hello from AxlumBuild" in run(s_build / "bin" / ("HelloCpp" + s_suffix))
        assert "incompatible configuration/toolchain" in run(
            s_cli,
            "configure",
            "-f",
            s_ws / "axlum.build.lua",
            "-B",
            s_build,
            "--compiler",
            "clang",
            "--config",
            "release",
            ok=False,
        )
        print(
            "PASS: explicit Clang native compiler and incompatible configuration guard"
        )
    else:
        print(
            "SKIP: explicit Clang project build (unsupported host or compiler absent)"
        )

    # Generic external code generator, generated C++ source + header, dep ordering and incremental input change.
    s_ws, s_build = fixture("codegen-to-cpp", s_root)
    s_manifest = s_ws / "axlum.build.lua"
    s_manifest.write_text(
        s_manifest.read_text().replace(
            'tool("python3")', "tool(" + json.dumps(sys.executable) + ")"
        )
    )
    run(s_cli, "configure", "-f", s_manifest, "-B", s_build)
    run(s_cli, "build", "-B", s_build)
    s_exe = s_build / "bin" / ("CodegenHello" + s_suffix)
    assert run(s_exe).strip() == "generated=43"
    assert "no work to do" in run(s_cli, "build", "-B", s_build).lower()
    (s_ws / "source" / "value.txt").write_text("67\n")
    run(s_cli, "build", "-B", s_build)
    assert run(s_exe).strip() == "generated=67"
    print("PASS: external codegen and generated source rebuild")

    # Explicit diagnostics for malformed and circular manifests.
    s_bad = s_ws / "bad.toml"
    s_bad.write_text(
        '[workspace]\nname = "X"\n[[target]]\nname = "A"\nkind = "invalid"\n'
    )
    assert "bad.toml:5:" in run(
        s_cli, "configure", "-f", s_bad, "-B", s_build, ok=False
    )
    s_cycle = s_ws / "cycle.toml"
    s_cycle.write_text(
        '[workspace]\nname = "X"\n[[target]]\nname = "A"\nkind = "header_only"\n'
        'public_dependencies = ["B"]\n[[target]]\nname = "B"\nkind = "header_only"\n'
        'public_dependencies = ["A"]\n'
    )
    assert "cycle" in run(s_cli, "configure", "-f", s_cycle, "-B", s_build, ok=False)
    print("PASS: malformed and cyclic manifest diagnostics")

    # Protocol-level fake .NET SDK: validate command arguments, build-tree outputs and
    # consecutive managed action runs without claiming this is a real SDK build.
    if os.name != "nt":
        s_ws, s_build = fixture("hello-dotnet", s_root)
        s_fakebin = s_root / "fake-dotnet-bin"
        s_fakebin.mkdir()
        s_fake = s_fakebin / "dotnet"
        s_fake.write_text(
            """#!/usr/bin/env python3
import os
from pathlib import Path
import sys
args=sys.argv[1:]
with open(os.environ['FAKE_DOTNET_LOG'],'a',encoding='utf-8') as f:
    f.write(args[0]+'\\n')
if args[0] == 'build':
    prefix=next(a.split('=',1)[1] for a in args if a.startswith('-p:ArtifactsPath='))
    assert '-p:UseArtifactsOutput=true' in args
    out=Path(prefix)/'bin'/'ManagedHello'/'debug_net8.0'/'ManagedHello.dll'
    out.parent.mkdir(parents=True,exist_ok=True)
    if not out.exists():out.write_bytes(b'shim only')
""",
            encoding="utf-8",
        )
        s_fake.chmod(0o755)
        s_logfile = s_root / "fake-dotnet.log"
        s_shimEnv = os.environ.copy()
        s_shimEnv["PATH"] = str(s_fakebin) + os.pathsep + s_shimEnv.get("PATH", "")
        s_shimEnv["FAKE_DOTNET_LOG"] = str(s_logfile)
        run(
            s_cli,
            "configure",
            "-f",
            s_ws / "axlum.build.lua",
            "-B",
            s_build,
            env=s_shimEnv,
        )
        run(s_cli, "build", "-B", s_build, env=s_shimEnv)
        s_stamp = s_build / "dotnet" / "ManagedHello" / "built.stamp"
        assert s_stamp.exists() and not (s_ws / "Managed" / "obj").exists()
        assert (
            len(
                list(
                    (s_build / "dotnet" / "ManagedHello" / "artifacts" / "bin").rglob(
                        "*.dll"
                    )
                )
            )
            == 1
        )
        s_oldtime = s_stamp.stat().st_mtime_ns
        run(s_cli, "build", "-B", s_build, env=s_shimEnv)
        assert s_oldtime == s_stamp.stat().st_mtime_ns
        assert s_logfile.read_text().splitlines() == [
            "restore",
            "build",
            "restore",
            "build",
        ]
        print(
            "PASS: .NET orchestration protocol with fake SDK, separate from real SDK acceptance"
        )

    if shutil.which("dotnet"):
        s_ws, s_build = fixture("hello-dotnet", s_root, "hello-dotnet-real")
        run(s_cli, "configure", "-f", s_ws / "axlum.build.lua", "-B", s_build)
        run(s_cli, "build", "-B", s_build)
        s_output = list(
            (s_build / "dotnet" / "ManagedHello" / "artifacts" / "bin").rglob(
                "ManagedHello.dll"
            )
        )
        assert len(s_output) == 1, s_output
        assert not (s_ws / "Managed" / "obj").exists()
        assert not (s_ws / "Managed" / "bin").exists()
        assert "Hello from C#" in run("dotnet", s_output[0])
        print("PASS: official dotnet SDK build from build directory")
    else:
        print("SKIP: .NET integration (dotnet SDK absent)")
print("ALL integration checks passed")
