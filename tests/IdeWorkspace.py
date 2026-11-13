# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Real composed builds/hooks/matrix and generated IDE entry points."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()


def run(*args, cwd=None, ok=True, env=None):
    proc = subprocess.run(
        list(map(str, args)),
        cwd=cwd,
        env=env,
        capture_output=True,
        text=True,
        errors="replace",
    )
    text = proc.stdout + proc.stderr
    assert (proc.returncode == 0) == ok, (args, proc.returncode, text)
    return text


with tempfile.TemporaryDirectory(prefix="axlum ide workspace ") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "source tree"
    shutil.copytree(s_examples / "workspace-suite", s_ws)
    s_manifest = s_ws / "axlum.build.lua"
    s_out = s_root / "build tree"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_out, "--preset", "dev")
    s_log = run(s_cli, "build", "-B", s_out)
    assert (
        s_log.index("Pre-build")
        < s_log.index("Compile Common")
        < s_log.index("Post-build")
        < s_log.index("Link Suite")
    )
    s_exe = s_out / "bin" / ("SuiteApp.exe" if os.name == "nt" else "SuiteApp")
    assert run(s_exe).strip() == "suite=42"
    s_published = s_out / "projects" / "Common" / "published" / "utility.bin"
    assert s_published.exists()
    assert "no work to do" in run(s_cli, "build", "-B", s_out)
    (s_ws / "common" / "value.txt").write_text("43\n")
    run(s_cli, "build", "-B", s_out)
    assert run(s_exe).strip() == "suite=43"
    assert "SUITE_DEVELOPMENT=1" in (s_out / "compile_commands.json").read_text()
    s_originalScript = (s_ws / "common" / "scripts" / "Actions.py").read_text()
    (s_ws / "common" / "scripts" / "Actions.py").write_text(
        'raise RuntimeError("intentional prebuild failure")\n'
    )
    assert "intentional prebuild failure" in run(s_cli, "build", "-B", s_out, ok=False)
    (s_ws / "common" / "scripts" / "Actions.py").write_text(s_originalScript)
    run(s_cli, "build", "-B", s_out)
    assert run(s_exe).strip() == "suite=43"
    print(
        "PASS: cross-workspace native interface, ordered hooks, no-op, failure propagation/recovery"
    )

    s_headers = s_root / "transitive headers"
    s_headers.mkdir()
    (s_headers / "Generate.py").write_text(s_originalScript)
    (s_headers / "value.txt").write_text("42\n")
    (s_headers / "Main.cpp").write_text(
        "#include <config.hpp>\n#include <iostream>\nint main() { std::cout << GENERATED_VALUE; }\n"
    )
    (s_headers / "axlum.build.lua").write_text(
        'workspace("Headers")\ntarget("Generated")\nkind("HeaderOnly")\n'
        'public_includedirs("@build/generated")\nprebuild("Generate", {tool="python", '
        'args={"${workspace}/Generate.py", "pre", "${workspace}/value.txt", "${build}/generated/config.hpp"}, '
        'inputs={"Generate.py", "value.txt"}, outputs={"generated/config.hpp"}})\n'
        'target("Facade")\nkind("HeaderOnly")\npublic_deps("Generated")\n'
        'target("Consumer")\nkind("ConsoleApp")\nfiles("Main.cpp")\nprivate_deps("Facade")\n'
    )
    s_hout = s_root / "header output"
    run(s_cli, "configure", "-f", s_headers / "axlum.build.lua", "-B", s_hout)
    run(s_cli, "build", "-B", s_hout)
    s_hexe = s_hout / "bin" / ("Consumer.exe" if os.name == "nt" else "Consumer")
    assert run(s_hexe) == "42"
    (s_headers / "value.txt").write_text("43\n")
    run(s_cli, "build", "-B", s_hout)
    assert run(s_hexe) == "43"
    assert "no work to do" in run(s_cli, "build", "-B", s_hout)

    s_baseManifest = s_manifest.read_text()
    s_commonManifest = s_ws / "common" / "axlum.build.lua"
    s_baseCommon = s_commonManifest.read_text()
    for s_file in (s_manifest, s_commonManifest):
        s_file.write_text(
            s_file.read_text().replace(
                'configuration("Debug", "Development", "Release")',
                'configuration("Debug", "Development", "Release", "Profile")',
            )
        )
    with s_manifest.open("a") as s_script:
        s_script.write(
            '\nfilter("configurations:Profile")\ndefines("SUITE_PROFILE=1")\n'
        )
    s_profile = s_root / "custom configuration"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_profile, "--config", "Profile")
    run(s_cli, "build", "-B", s_profile)
    assert "SUITE_PROFILE=1" in (s_profile / "compile_commands.json").read_text()
    s_manifest.write_text(s_baseManifest)
    s_commonManifest.write_text(s_baseCommon)

    s_matrix = s_root / "matrix"
    run(
        s_cli,
        "matrix",
        "-f",
        s_manifest,
        "-B",
        s_matrix,
        "--preset",
        "dev",
        "--preset",
        "release",
        "-t",
        "SuiteApp",
    )
    for s_entry, s_config in [("dev", "Development"), ("release", "Release")]:
        assert ("config=" + s_config) in (
            s_matrix / s_entry / "axlum.state"
        ).read_text()
        assert run(s_matrix / s_entry / "bin" / s_exe.name).strip() == "suite=43"
    assert "unknown preset" in run(
        s_cli,
        "matrix",
        "-f",
        s_manifest,
        "-B",
        s_matrix,
        "--preset",
        "missing",
        ok=False,
    )

    # Diamond references share a canonical project, rather than compiling sources twice.
    s_lib = s_ws / "common" / "axlum.build.lua"
    s_middle = s_ws / "middle"
    s_middle.mkdir()
    (s_middle / "axlum.build.lua").write_text(
        'workspace("Middle")\nworkspace_reference("Common", {file="../common/axlum.build.lua"})\n'
        'target("Facade")\nkind("HeaderOnly")\npublic_deps("Common::Utility")\n'
    )
    s_originalManifest = s_manifest.read_text()
    s_manifest.write_text(
        s_originalManifest.replace(
            'target("SuiteApp")',
            'workspace_reference("Middle", {file="middle/axlum.build.lua"})\ntarget("SuiteApp")',
        ).replace(
            'private_deps("Common::Utility")',
            'private_deps({"Common::Utility", "Middle::Facade"})',
        )
    )
    run(s_cli, "build", "-B", s_out)
    assert len(json.loads((s_out / "compile_commands.json").read_text())) == 2
    s_reordered = s_manifest.read_text().replace(
        '    workspace_reference("Common", {file="common/axlum.build.lua"})\n', ""
    )
    s_reordered = s_reordered.replace(
        'workspace_reference("Middle", {file="middle/axlum.build.lua"})',
        'workspace_reference("Middle", {file="middle/axlum.build.lua"})\nworkspace_reference("Common", {file="common/axlum.build.lua"})',
    )
    s_manifest.write_text(s_reordered)
    run(s_cli, "build", "-B", s_out, "-t", "Common::Utility")
    assert len(json.loads((s_out / "compile_commands.json").read_text())) == 2
    s_lib.write_text(
        s_lib.read_text().replace(
            'target("Utility")',
            'workspace_reference("Root", {file="../axlum.build.lua"})\ntarget("Utility")',
        )
    )
    assert "workspace reference cycle" in run(s_cli, "build", "-B", s_out, ok=False)
    s_lib.write_text(
        s_lib.read_text().replace(
            'workspace_reference("Root", {file="../axlum.build.lua"})\n', ""
        )
    )
    s_manifest.write_text(s_originalManifest)
    print(
        "PASS: preset matrix isolation, diamond identity and workspace cycle diagnostics"
    )

    s_idir = s_root / "IDE files"
    s_ibuild = s_root / "IDE builds"
    run(s_cli, "ide", "-f", s_manifest, "-B", s_ibuild, "-o", s_idir, "--format", "all")
    s_tasks = json.loads((s_idir / "vscode" / ".vscode" / "tasks.json").read_text())
    s_launch = json.loads((s_idir / "vscode" / ".vscode" / "launch.json").read_text())
    s_properties = json.loads(
        (s_idir / "vscode" / ".vscode" / "c_cpp_properties.json").read_text()
    )
    assert len(s_properties["configurations"]) == 3
    s_task = next(
        t for t in s_tasks["tasks"] if t["label"] == "build:Development-x64:SuiteApp"
    )
    run(s_task["command"], *s_task["args"], cwd=s_task["options"]["cwd"])
    s_debug = next(
        c for c in s_launch["configurations"] if c["name"] == "Development-x64:SuiteApp"
    )
    assert (
        s_debug["preLaunchTask"] == s_task["label"]
        and Path(s_debug["program"]).exists()
    )
    for s_file in (s_idir / "vs").glob("*.vcxproj"):
        ET.parse(s_file)
    ET.parse(
        s_idir / "xcode" / "WorkspaceSuite.xcworkspace" / "contents.xcworkspacedata"
    )
    s_schemes = list((s_idir / "xcode").rglob("*.xcscheme"))
    assert len(s_schemes) >= 6
    for s_file in s_schemes:
        ET.parse(s_file)
    run(
        sys.executable,
        s_idir / "xcode" / "axlum_driver.py",
        "--config",
        "Development-x64",
        "--target",
        "SuiteApp",
    )
    assert "unknown IDE configuration" in run(
        sys.executable,
        s_idir / "xcode" / "axlum_driver.py",
        "--config",
        "bad",
        ok=False,
    )
    for s_file in (s_idir / "android-studio" / ".idea").rglob("*.xml"):
        ET.parse(s_file)
    assert (
        "axlum_android"
        in (s_idir / "android-studio" / "native" / "CMakeLists.txt").read_text()
    )
    s_msbuild = Path("C:/Apps/VisualStudio/MSBuild/Current/Bin/MSBuild.exe")
    if os.name == "nt" and s_msbuild.exists():
        run(
            s_msbuild,
            s_idir / "vs" / "WorkspaceSuite.sln",
            "/t:Build",
            "/p:Configuration=Debug-x64",
            "/p:Platform=x64",
            "/nologo",
            "/verbosity:minimal",
        )
    s_keep = s_idir / "vs" / "user-notes.txt"
    s_keep.write_text("preserve user files")
    s_manifest.write_text(
        s_originalManifest.replace('target("SuiteApp")', 'target("RenamedApp")')
    )
    run(s_cli, "ide", "-f", s_manifest, "-B", s_ibuild, "-o", s_idir, "--format", "vs")
    assert not (s_idir / "vs" / "SuiteApp.vcxproj").exists()
    assert (s_idir / "vs" / "RenamedApp.vcxproj").exists()
    assert s_keep.read_text() == "preserve user files"
    assert (s_idir / "vscode" / ".vscode" / "tasks.json").exists()
    run(s_cli, "ide", "-f", s_manifest, "-B", s_ibuild, "-o", s_idir, "--format", "all")
    assert not list((s_idir / "xcode").rglob("SuiteApp-*.xcscheme"))
    print(
        "PASS: VSCode tasks/launch/database, MSBuild solution, Xcode formats/driver and Android Studio files"
    )
