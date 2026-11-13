# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Real toolchain discovery, structured options, PCH and runtime artifacts."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
s_msvc = os.name == "nt"
s_env = os.environ.copy()
if s_msvc:
    # Discovery and builds must work without a VS Developer shell.
    s_env["PATH"] = os.pathsep.join(
        p
        for p in s_env.get("PATH", "").split(os.pathsep)
        if "visualstudio" not in p.lower() and "windows kits" not in p.lower()
    )
    for s_name in list(s_env):
        if s_name.lower() in ("include", "lib", "libpath") or s_name.lower().startswith(
            ("vctools", "vcinstall", "vsinstall", "windowssdk", "vscmd")
        ):
            s_env.pop(s_name)


def run(*args, cwd=None, ok=True):
    result = subprocess.run(
        [str(x) for x in args],
        cwd=cwd,
        env=s_env,
        capture_output=True,
        text=True,
        errors="replace",
    )
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == ok, (args, result.returncode, output)
    return output


def stamp(paths):
    return {str(p): p.stat().st_mtime_ns for p in paths}


assert "Toolchains discovered by Rhodium" in run(s_cli, "doctor")
with tempfile.TemporaryDirectory(prefix="axlum-native-features-") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "workspace with spaces"
    shutil.copytree(s_examples / "native-features", s_ws)
    s_manifest = s_ws / "axlum.build.lua"
    s_output = s_root / "debug output"
    s_configure = [s_cli, "configure", "-f", s_manifest, "-B", s_output]
    run(*s_configure)
    s_env = json.loads((s_output / "axlum.environment.json").read_text())
    if s_msvc:
        assert s_env.get("INCLUDE") and s_env.get("LIB")
    run(s_cli, "build", "-B", s_output, "--target", "FeatureApp")
    s_exe = (
        s_output
        / "products"
        / "Debug"
        / "app"
        / ("feature_app.exe" if s_msvc else "feature_app")
    )
    assert run(s_exe, cwd=s_exe.parent).strip() == "value=32 data=demo"
    s_runtime = s_exe.parent / (
        "feature_runtime.dll" if s_msvc else "libfeature_runtime.so"
    )
    assert s_runtime.is_file() and (s_exe.parent / "config.txt").is_file()
    assert not (s_exe.parent / "optional.txt").exists()
    assert not list((s_output / "obj").glob("*"))
    s_pchs = list((s_output / "objects").rglob("*.pch")) + list(
        (s_output / "objects").rglob("*.gch")
    )
    assert len(s_pchs) == 2, s_pchs
    if s_msvc:
        assert (s_exe.with_suffix(".pdb")).is_file()
        assert (
            s_output / "products" / "Debug" / "import" / "feature_runtime.lib"
        ).is_file()
    s_db = json.loads((s_output / "compile_commands.json").read_text())
    s_c = next(item for item in s_db if item["file"].endswith("Helper.c"))
    s_cpp = next(item for item in s_db if item["file"].endswith("Main.cpp"))
    assert not any("/Yu" in arg or "-include" in arg for arg in s_c["arguments"])
    assert any("/Yu" in arg or "-include" in arg for arg in s_cpp["arguments"])
    assert ("/W4" if s_msvc else "-Wextra") in s_cpp["arguments"]
    assert ("/WX" if s_msvc else "-Werror") in s_cpp["arguments"]
    assert (
        "no work to do"
        in run(s_cli, "build", "-B", s_output, "-t", "FeatureApp").lower()
    )
    s_beforePch, s_beforeC = stamp(s_pchs), Path(s_c["output"]).stat().st_mtime_ns
    time.sleep(1.1)
    s_value = s_ws / "include" / "Value.hpp"
    s_value.write_text(s_value.read_text().replace("10", "20"))
    run(s_cli, "build", "-B", s_output, "-t", "FeatureApp")
    assert run(s_exe, cwd=s_exe.parent).strip() == "value=52 data=demo"
    assert all(stamp(s_pchs)[key] > old for key, old in s_beforePch.items())
    assert Path(s_c["output"]).stat().st_mtime_ns == s_beforeC
    print(
        "PASS: Rhodium discovery outside Developer shell, PCH creation/use/header invalidation and C isolation"
    )

    s_beforeObjects = stamp([Path(item["output"]) for item in s_db])
    time.sleep(1.1)
    (s_ws / "data" / "config.txt").write_text("updated\n")
    run(s_cli, "build", "-B", s_output, "-t", "FeatureApp")
    assert run(s_exe, cwd=s_exe.parent).strip() == "value=52 data=updated"
    assert stamp([Path(item["output"]) for item in s_db]) == s_beforeObjects
    s_original = s_manifest.read_text()
    s_manifest.write_text(
        s_original.replace(
            'warnings("Extra")', 'warnings("Extra")\ndefines("PCH_OPTIONS_REVISION=1")'
        )
    )
    s_beforePch = stamp(s_pchs)
    run(*s_configure)
    run(s_cli, "build", "-B", s_output, "-t", "FeatureApp")
    assert all(stamp(s_pchs)[key] > old for key, old in s_beforePch.items())
    (s_ws / "data" / "optional.txt").write_text("optional\n")
    run(*s_configure)
    run(s_cli, "build", "-B", s_output, "-t", "FeatureApp")
    assert (s_exe.parent / "optional.txt").read_text().strip() == "optional"
    if s_msvc:
        s_res = next((s_output / "objects" / "Debug" / "FeatureApp").rglob("*.res"))
        s_oldRes = s_res.stat().st_mtime_ns
        s_beforeObjects = stamp([Path(item["output"]) for item in s_db])
        time.sleep(1.1)
        (s_ws / "data" / "payload.bin").write_text("changed resource payload\n")
        run(s_cli, "build", "-B", s_output, "-t", "FeatureApp")
        assert s_res.stat().st_mtime_ns > s_oldRes
        assert stamp([Path(item["output"]) for item in s_db]) == s_beforeObjects
    run(s_cli, "clean", "-B", s_output)
    run(s_cli, "build", "-B", s_output, "-t", "FeatureApp")
    assert run(s_exe, cwd=s_exe.parent).strip() == "value=52 data=updated"
    print(
        "PASS: output/import-library/PDB layout, runtime copies, optional files, resource dependencies, PCH option invalidation and clean"
    )

    s_release = s_root / "release output"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_release, "--config", "release")
    run(s_cli, "build", "-B", s_release, "-t", "FeatureApp")
    s_releaseExe = s_release / "products" / "Release" / "app" / s_exe.name
    assert run(s_releaseExe, cwd=s_releaseExe.parent).strip() == "value=52 data=updated"
    print("PASS: Release LTO with PCH and runtime DLL copying")

    # A missing/ambiguous runtime file must never silently overwrite an artifact.
    (s_ws / "bad.cpp").write_text("int main() { return 0; }\n")
    for s_suffix, s_message in [
        (
            'target("Bad")\nkind("ConsoleApp")\nfiles("bad.cpp")\ntargetdir("../escape")',
            "stay under",
        ),
        ('target("Bad")\nkind("HeaderOnly")\noptimize("typo")', "invalid optimize"),
        (
            'target("Bad")\nkind("ConsoleApp")\nfiles("bad.cpp")\nlto(true)\nincrementallink(true)',
            "LTO",
        ),
        (
            'target("Bad")\nkind("ConsoleApp")\nfiles("bad.cpp")\nruntimefiles("missing.dll")',
            "missing input",
        ),
    ]:
        s_manifest.write_text(s_original + "\n" + s_suffix + "\n")
        s_diagnostic = run(
            s_cli, "configure", "-f", s_manifest, "-B", s_root / "bad output", ok=False
        )
        assert s_message in s_diagnostic, s_diagnostic
    s_manifest.write_text(s_original)
    assert "matches platform" in run(
        *s_configure, "--toolchain", "does-not-exist", ok=False
    )
    print(
        "PASS: invalid structured options, PCH/link conflicts, missing runtime files and unsafe output diagnostics"
    )

    (s_ws / "settings.cpp").write_text("""#if defined(_MSC_VER)
#if defined(_CPPUNWIND) || defined(_CPPRTTI)
#error "Exceptions or RTTI were not disabled"
#endif
#else
#if defined(__EXCEPTIONS) || defined(__GXX_RTTI)
#error "Exceptions or RTTI were not disabled"
#endif
#endif
#ifdef PCH_VALUE
#error "Inherited PCH was not disabled"
#endif
int main() { return 0; }
""")
    s_manifest.write_text("""workspace("Settings")
pchheader("include/Pch.hpp")
includedirs("include")
target("Settings")
kind("ConsoleApp")
files("settings.cpp")
exceptions(false)
rtti("Off")
symbols("Off")
optimize("Size")
runtime("Static")
pch(false)
""")
    s_settings = s_root / "settings output"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_settings)
    run(s_cli, "build", "-B", s_settings)
    run(s_settings / "bin" / ("Settings.exe" if s_msvc else "Settings"))
    assert not list(s_settings.rglob("*.pch"))
    if s_msvc:
        assert not (s_settings / "bin" / "Settings.pdb").exists()
        s_args = json.loads((s_settings / "compile_commands.json").read_text())[0][
            "arguments"
        ]
        assert "/MTd" in s_args and "/O1" in s_args and "/GR-" in s_args
    (s_ws / "warning.cpp").write_text(
        "int main() { int unused_value = 1; return 0; }\n"
    )
    s_warningScript = 'workspace("Warnings")\ntarget("Warning")\nkind("ConsoleApp")\nfiles("warning.cpp")\nwarnings("Extra")\nwarningsaserrors(true)\n'
    s_manifest.write_text(s_warningScript)
    s_warningsOutput = s_root / "warnings output"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_warningsOutput)
    run(s_cli, "build", "-B", s_warningsOutput, ok=False)
    s_manifest.write_text(
        s_warningScript.replace("warningsaserrors(true)", "warningsaserrors(false)")
    )
    run(s_cli, "configure", "-f", s_manifest, "-B", s_warningsOutput)
    run(s_cli, "build", "-B", s_warningsOutput)
    print(
        "PASS: actual exception/RTTI policy, static CRT, symbols off, PCH opt-out and warning-as-error enforcement"
    )

    # User profiles and project preference are parsed/validated by Rhodium.
    s_state = dict(
        line.split("=", 1)
        for line in (s_output / "axlum.state").read_text().splitlines()
    )
    s_binaryPath = next(
        (v for k, v in s_env.items() if k.lower() == "path"), s_env.get("PATH", "")
    )
    s_compiler = s_cpp["arguments"][0]
    s_profile = {
        "id": "axlum-test-profile",
        "platform": "windows" if s_msvc else "linux",
        "host_arch": "x64",
        "target_arch": "x64",
        "compiler_kind": "msvc" if s_msvc else "gcc",
        "compiler": s_compiler if s_msvc else shutil.which("gcc", path=s_binaryPath),
        "cxx_compiler": s_compiler,
        "linker": shutil.which("link.exe", path=s_binaryPath) if s_msvc else s_compiler,
        "archiver": shutil.which("lib.exe" if s_msvc else "ar", path=s_binaryPath),
        "ninja": s_state["ninja"],
        "sdk_version": "test-sdk-1",
        "compiler_version": "test-compiler-1",
        "system_include_dirs": s_env.get("INCLUDE", "").split(";") if s_msvc else [],
        "system_lib_dirs": s_env.get("LIB", "").split(";") if s_msvc else [],
    }
    s_profilePath = s_ws / ".rhodium" / "toolchains" / "user.toml"
    s_profilePath.parent.mkdir(parents=True, exist_ok=True)
    s_text = "[[toolchain]]\n" + "".join(
        key + " = " + json.dumps(value) + "\n" for key, value in s_profile.items()
    )
    s_text += "\n[toolchain.environment]\n" + "".join(
        key + " = " + json.dumps(value) + "\n" for key, value in s_env.items()
    )
    s_profilePath.write_text(s_text)
    (s_ws / ".rhodium" / "local.toml").write_text(
        'preferred_toolchain = "axlum-test-profile"\n'
    )
    s_preferred = s_root / "preferred output"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_preferred)
    assert "toolchain=axlum-test-profile" in (s_preferred / "axlum.state").read_text()
    run(s_cli, "build", "-B", s_preferred)
    s_profilePath.write_text(s_text.replace("test-sdk-1", "test-sdk-2"))
    assert "incompatible configuration/toolchain" in run(
        s_cli, "configure", "-f", s_manifest, "-B", s_preferred, ok=False
    )
    s_previousFingerprint = (s_preferred / "axlum.state").read_text()
    s_previousObjects = {
        p: p.stat().st_mtime_ns for p in s_preferred.rglob("*.obj" if s_msvc else "*.o")
    }
    run(s_cli, "build", "-B", s_preferred)
    assert (s_preferred / "axlum.state").read_text() != s_previousFingerprint
    assert s_previousObjects and all(
        p.stat().st_mtime_ns != old for p, old in s_previousObjects.items()
    )
    print(
        "PASS: Rhodium user profile selection, preferred-toolchain config and SDK fingerprint invalidation"
    )

    s_globws = s_root / "glob workspace"
    s_globws.mkdir()
    (s_globws / "Pch.hpp").write_text("#pragma once\n#define GLOB_PCH 1\n")
    (s_globws / "Main.cpp").write_text("int main() { return GLOB_PCH == 1 ? 0 : 1; }\n")
    (s_globws / "axlum.build.lua").write_text(
        'workspace("GlobBuild")\ntarget("App")\nkind("ConsoleApp")\n'
        'pchheader("Pch.hpp")\nfiles("**/*.cpp")\nobjdir("snapshots")\n'
    )
    s_globout = s_globws / "custom-output"
    s_command = [s_cli, "configure", "-B", s_globout]
    run(*s_command, cwd=s_globws)
    run(s_cli, "build", "-B", s_globout)
    (s_globout / "poison.cpp").write_text(
        '#error "Build output leaked into source glob"\n'
    )
    run(*s_command, cwd=s_globws)
    assert len(json.loads((s_globout / "compile_commands.json").read_text())) == 1
    run(s_cli, "build", "-B", s_globout)
    print(
        "PASS: recursive source globs exclude arbitrary build directories and generated PCH sources"
    )
