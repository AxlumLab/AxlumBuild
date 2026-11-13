# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Official framework-dependent/self-contained/NativeAOT publications and C exports."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()


def run(*args, ok=True, env=None):
    proc = subprocess.run(
        list(map(str, args)), env=env, capture_output=True, text=True, errors="replace"
    )
    text = proc.stdout + proc.stderr
    assert (proc.returncode == 0) == ok, (args, proc.returncode, text)
    return text


s_major = int(run("dotnet", "--version").strip().split(".")[0])
s_rid = (
    "win" if os.name == "nt" else "osx" if sys.platform == "darwin" else "linux"
) + "-x64"


def verify(directory):
    manifest = json.loads((directory / "axlum.publish.json").read_text())
    assert manifest["schemaVersion"] == 1 and manifest["rid"] == s_rid
    for file in manifest["files"]:
        path = directory / file["path"]
        assert path.stat().st_size == file["size"]
        assert hashlib.sha256(path.read_bytes()).hexdigest() == file["sha256"]
    assert all(d["status"] != "missing" for d in manifest["nativeDependencies"])
    return manifest


with tempfile.TemporaryDirectory(prefix="axlum dotnet publish ") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "sources"
    shutil.copytree(s_examples / "dotnet-publish", s_ws)
    s_manifest = s_ws / "axlum.build.lua"
    s_script = s_manifest.read_text().replace("net10.0", f"net{s_major}.0")
    s_manifest.write_text(s_script)
    s_out = s_root / "build"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_out, "--config", "Release")
    s_log, s_trace = s_root / "publish log.txt", s_root / "publish trace.json"
    run(
        s_cli,
        "publish",
        "-B",
        s_out,
        "-t",
        "AotHello",
        "--rid",
        s_rid,
        "--timing",
        "--logfile",
        s_log,
        "--trace",
        s_trace,
    )
    s_published = s_out / "publish" / "AotHello" / s_rid / "aot"
    s_data = verify(s_published)
    assert s_data["mode"] == "aot" and not s_data["native"]["managedHeader"]
    s_env = os.environ.copy()
    s_env["DOTNET_ROOT"] = str(s_root / "no-dotnet-runtime")
    assert "standard dotnet aot=42" in run(
        s_published / s_data["entrypoint"], env=s_env
    )
    assert not list(s_published.glob("*.runtimeconfig.json"))
    assert any(
        p["name"] == "dotnet publish"
        and p["arguments"][1] == "publish"
        and "-p:PublishAot=true" in p["arguments"]
        for p in json.loads((s_out / "axlum.last-publish.json").read_text())["phases"]
    )
    assert json.loads(s_trace.read_text())["traceEvents"]
    # Optional/stale data are synchronized; unknown user files are retained.
    s_manifest.write_text(
        s_script.replace(
            'csdefines("STANDARD_DOTNET_AOT")',
            'csdefines("STANDARD_DOTNET_AOT")\noptionalruntimefiles("optional.dat")',
        )
    )
    (s_ws / "optional.dat").write_text("optional")
    s_before = (s_published / s_data["entrypoint"]).stat().st_mtime_ns
    run(s_cli, "publish", "-B", s_out, "-t", "AotHello", "--rid", s_rid)
    assert (s_published / s_data["entrypoint"]).stat().st_mtime_ns == s_before
    assert (s_published / "optional.dat").exists()
    (s_published / "user-note.txt").write_text("preserve")
    (s_ws / "optional.dat").unlink()
    run(s_cli, "publish", "-B", s_out, "-t", "AotHello", "--rid", s_rid)
    assert (
        not (s_published / "optional.dat").exists()
        and (s_published / "user-note.txt").read_text() == "preserve"
    )
    print(
        "PASS: real standard NativeAOT executable, no runtime host, hashes, no-op output preservation and publication cleanup"
    )

    s_manifest.write_text(s_script)
    run(s_cli, "publish", "-B", s_out, "-t", "AotLibrary", "--rid", s_rid)
    s_library = s_out / "publish" / "AotLibrary" / s_rid / "aot-library"
    s_data = verify(s_library)
    assert "axlum_add" in s_data["native"]["exports"]
    assert any("Vendor" in i["file"] for i in s_data["nativeImages"])
    s_vendor = s_library / (
        "Vendor.dll"
        if os.name == "nt"
        else "libVendor.dylib" if sys.platform == "darwin" else "libVendor.so"
    )
    s_caller = (
        'import ctypes,sys; v=ctypes.CDLL(sys.argv[1], mode=getattr(ctypes,"RTLD_GLOBAL",0)); '
        "a=ctypes.CDLL(sys.argv[2]); f=a.axlum_add; f.argtypes=[ctypes.c_int,ctypes.c_int]; "
        "f.restype=ctypes.c_int; print(f(20,22))"
    )
    assert (
        run(
            sys.executable,
            "-c",
            s_caller,
            s_vendor,
            s_library / s_data["entrypoint"],
            env=s_env,
        ).strip()
        == "42"
    )
    s_previous = (s_library / "axlum.publish.json").read_bytes()
    s_manifest.write_text(s_script + '\nnative_exports("missing_export")\n')
    assert "missing native export" in run(
        s_cli,
        "publish",
        "-B",
        s_out,
        "-t",
        "AotLibrary",
        "--rid",
        s_rid,
        "--logfile",
        s_log,
        ok=False,
    )
    assert (s_library / "axlum.publish.json").read_bytes() == s_previous
    assert (
        "missing native export"
        in json.loads((s_out / "axlum.last-publish.json").read_text())["error"]
    )
    s_manifest.write_text(s_script + '\nnative_dependencies("MissingNative.dll")\n')
    assert "missing declared native dependency" in run(
        s_cli, "publish", "-B", s_out, "-t", "AotLibrary", "--rid", s_rid, ok=False
    )
    if os.name == "nt":
        # A valid PE with a deliberately unresolved transitive native import must fail validation.
        s_imports = next(
            i for i in s_data["nativeImages"] if i["file"] == "Vendor.dll"
        )["imports"]
        s_name = next(n for n in s_imports if n.lower().endswith(".dll"))
        s_bogus = "X" * (len(s_name) - 4) + ".dll"
        s_original = s_vendor.read_bytes()
        assert (s_name.encode() + b"\0") in s_original
        (s_ws / "Fake.dll").write_bytes(
            s_original.replace(s_name.encode() + b"\0", s_bogus.encode() + b"\0")
        )
        s_manifest.write_text(
            s_script + '\nruntimefiles("Fake.dll")\nnative_dependencies("Fake.dll")\n'
        )
        assert "unresolved native import" in run(
            s_cli, "publish", "-B", s_out, "-t", "AotLibrary", "--rid", s_rid, ok=False
        )
    s_manifest.write_text(s_script)
    print(
        "PASS: NativeLib=Shared C export call, native dependency closure, missing export/import failures and previous-output preservation"
    )

    run(
        s_cli,
        "publish",
        "-B",
        s_out,
        "-t",
        "AotHello",
        "--rid",
        s_rid,
        "--no-aot",
        "--framework-dependent",
    )
    s_managed = s_out / "publish" / "AotHello" / s_rid / "managed"
    assert verify(s_managed)["mode"] == "managed"
    assert "standard dotnet aot=42" in run("dotnet", s_managed / "AotHello.dll")
    run(
        s_cli,
        "publish",
        "-B",
        s_out,
        "-t",
        "AotHello",
        "--rid",
        s_rid,
        "--no-aot",
        "--self-contained",
    )
    assert (
        s_managed
        / (
            "coreclr.dll"
            if os.name == "nt"
            else "libcoreclr.dylib" if sys.platform == "darwin" else "libcoreclr.so"
        )
    ).exists()
    run(
        s_cli,
        "publish",
        "-B",
        s_out,
        "-t",
        "AotHello",
        "--rid",
        s_rid,
        "--no-aot",
        "--framework-dependent",
    )
    assert not (s_managed / "coreclr.dll").exists()
    assert "requires self-contained" in run(
        s_cli,
        "publish",
        "-B",
        s_out,
        "-t",
        "AotHello",
        "--framework-dependent",
        ok=False,
    )
    assert "PublishSingleFile" in run(
        s_cli, "publish", "-B", s_out, "-t", "AotHello", "--single-file", ok=False
    )
    assert "unsupported desktop" in run(
        s_cli,
        "publish",
        "-B",
        s_out,
        "-t",
        "AotHello",
        "--rid",
        "android-arm64",
        ok=False,
    )
    assert "own directory" in run(
        s_cli,
        "publish",
        "-B",
        s_out,
        "-t",
        "AotHello",
        "--output",
        "../escape",
        ok=False,
    )
    print(
        "PASS: framework-dependent/self-contained official publish, mode isolation, runtime cleanup and invalid option diagnostics"
    )
