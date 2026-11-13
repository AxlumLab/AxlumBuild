# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Real binary grouping, native deployment and automatic Ninja reconfiguration."""

import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
s_windows = os.name == "nt"


def run(*args, cwd=None, ok=True, env=None):
    result = subprocess.run(
        list(map(str, args)),
        cwd=cwd,
        env=env,
        capture_output=True,
        text=True,
        errors="replace",
    )
    text = result.stdout + result.stderr
    assert (result.returncode == 0) == ok, (args, result.returncode, text)
    return text


with tempfile.TemporaryDirectory(prefix="axlum-modules-") as s_temporary:
    s_root = Path(s_temporary)
    s_ws = s_root / "workspace"
    shutil.copytree(s_examples / "modules", s_ws)
    s_manifest = s_ws / "axlum.build.lua"
    s_script = s_manifest.read_text()
    s_out = s_root / "build output"
    run(s_cli, "configure", "-f", s_manifest, "-B", s_out)
    run(s_cli, "build", "-B", s_out, "-t", "ModuleApp")
    s_exe = s_out / "bin" / ("ModuleApp.exe" if s_windows else "ModuleApp")
    assert run(s_exe).strip() == "module result=42"
    s_suffix = "*.dll" if s_windows else "*.so"
    assert len(list((s_out / ("bin" if s_windows else "lib")).glob(s_suffix))) == 2
    assert len(list((s_out / "bin").glob(s_suffix))) == 2
    s_objects = {
        p: p.stat().st_mtime_ns for p in s_out.rglob("*.obj" if s_windows else "*.o")
    }
    s_planTime = (s_out / "build.ninja").stat().st_mtime_ns
    assert "no work to do" in run(s_cli, "build", "-B", s_out)
    assert s_planTime == (s_out / "build.ninja").stat().st_mtime_ns
    assert s_objects == {p: p.stat().st_mtime_ns for p in s_objects}

    # Included Lua is re-executed by direct Ninja as well as the build CLI.
    s_settings = s_ws / "settings.lua"
    s_settings.write_text('defines("FROM_INCLUDE=1")\n')
    s_manifest.write_text(
        s_script.replace(
            'files("src/Main.cpp")', 'files("src/Main.cpp")\ninclude("settings.lua")'
        )
    )
    run(s_cli, "build", "-B", s_out)
    s_db = json.loads((s_out / "compile_commands.json").read_text())
    assert any("FROM_INCLUDE=1" in a for a in s_db[0]["arguments"])
    s_state = dict(
        line.split("=", 1) for line in (s_out / "axlum.state").read_text().splitlines()
    )
    s_settings.write_text('defines("FROM_INCLUDE=2")\n')
    run(s_state["ninja"], "-C", s_out)
    assert "FROM_INCLUDE=2" in (s_out / "compile_commands.json").read_text()

    # Switching binary layout removes declared DLLs and runtime copies.
    s_manifest.write_text(
        s_script.replace('linktype("Modular")', 'linktype("Monolithic")')
    )
    run(s_cli, "build", "-B", s_out)
    assert run(s_exe).strip() == "module result=42"
    assert not list((s_out / "lib").glob(s_suffix))
    assert not list((s_out / "bin").glob(s_suffix))

    # Unknown deps, module cycles, and cycles created only by binary merging.
    s_manifest.write_text(
        s_script.replace('private_deps("Extra")', 'private_deps("Missing")')
    )
    assert "unknown module dependency" in run(s_cli, "build", "-B", s_out, ok=False)
    s_manifest.write_text(
        s_script.replace(
            'defines("BASE_PRIVATE=1")',
            'defines("BASE_PRIVATE=1")\nprivate_deps("Logic")',
        )
    )
    assert "module dependency cycle" in run(s_cli, "build", "-B", s_out, ok=False)
    s_manifest.write_text(
        s_script.replace(
            'binarymodulename("Core") -- Both', 'binarymodulename("Logic") -- Both'
        )
    )
    # Base(Core) <- Extra(Logic) <- Logic(Logic) remains legal.
    run(s_cli, "build", "-B", s_out)
    s_manifest.write_text(
        s_script.replace(
            'binarymodulename("Logic")', 'binarymodulename("Core")'
        ).replace(
            'binarymodulename("Core") -- Both', 'binarymodulename("Middle") -- Both'
        )
    )
    assert "dependency cycle" in run(s_cli, "build", "-B", s_out, ok=False)
    print(
        "PASS: module scopes, imports/exports, binary merging, monolithic switching and cycle diagnostics"
    )

    # A monolithic shared owner must still export module APIs to external targets.
    s_single = s_root / "single dll"
    s_single.mkdir()
    (s_single / "api.hpp").write_text("#pragma once\nSINGLE_API int single_value();\n")
    (s_single / "Library.cpp").write_text(
        '#include "api.hpp"\nint single_value() { return 42; }\n'
    )
    (s_single / "Main.cpp").write_text(
        '#include "api.hpp"\nint main() { return single_value() == 42 ? 0 : 1; }\n'
    )
    (s_single / "axlum.build.lua").write_text(
        'workspace("Single")\ntarget("Single")\nkind("SharedLibrary")\n'
        'module("API")\napidefine("SINGLE_API")\npublic_includedirs(".")\nfiles("Library.cpp")\n'
        'target("App")\nkind("ConsoleApp")\nfiles("Main.cpp")\ndependson("Single")\n'
    )
    s_sout = s_root / "single output"
    run(s_cli, "configure", "-f", s_single / "axlum.build.lua", "-B", s_sout)
    run(s_cli, "build", "-B", s_sout, "-t", "App")
    run(s_sout / "bin" / ("App.exe" if s_windows else "App"))
    print("PASS: monolithic shared-library module API consumed by an external target")

    # Globs add/remove sources, output names and optional deployment files without configure.
    s_globws = s_root / "glob"
    s_globws.mkdir()
    (s_globws / "Main.cpp").write_text("int main() { return 0; }\n")
    (s_globws / "axlum.build.lua").write_text(
        'workspace("Auto")\ntarget("Auto")\nkind("ConsoleApp")\nfiles("*.cpp")\noptionalruntimefiles("extra.dat")\n'
    )
    s_gout = s_root / "glob output"
    run(s_cli, "configure", "-f", s_globws / "axlum.build.lua", "-B", s_gout)
    run(s_cli, "build", "-B", s_gout)
    (s_globws / "new.cpp").write_text("int newly_discovered() { return 5; }\n")
    (s_globws / "extra.dat").write_text("first")
    run(s_cli, "build", "-B", s_gout)
    assert len(json.loads((s_gout / "compile_commands.json").read_text())) == 2
    assert (s_gout / "bin" / "extra.dat").read_text() == "first"
    (s_globws / "new.cpp").unlink()
    (s_globws / "extra.dat").unlink()
    run(s_cli, "build", "-B", s_gout)
    assert len(json.loads((s_gout / "compile_commands.json").read_text())) == 1
    assert not (s_gout / "bin" / "extra.dat").exists()
    with (s_globws / "axlum.build.lua").open("a") as s_f:
        s_f.write('targetname("Renamed")\n')
    run(s_cli, "build", "-B", s_gout)
    assert not (s_gout / "bin" / ("Auto.exe" if s_windows else "Auto")).exists()
    assert (s_gout / "bin" / ("Renamed.exe" if s_windows else "Renamed")).exists()
    # Existing pre-generator build directories upgrade through the CLI build entry.
    s_legacyState = s_gout / "axlum.state"
    s_legacyState.write_text(s_legacyState.read_text().replace("schema=2\n", ""))
    s_legacyNinja = (s_gout / "build.ninja").read_text()
    s_start = s_legacyNinja.index("rule axlum_reconfigure")
    s_end = s_legacyNinja.index("rule axlum_rule_", s_start)
    (s_gout / "build.ninja").write_text(s_legacyNinja[:s_start] + s_legacyNinja[s_end:])
    run(s_cli, "build", "-B", s_gout)
    assert "rule axlum_reconfigure" in (s_gout / "build.ninja").read_text()
    if s_windows:
        s_stateBefore = (s_gout / "axlum.state").read_text()
        s_modifiedEnv = os.environ.copy()
        s_modifiedEnv["CL"] = "/DAUTO_TOOL_ENV=1"
        (s_globws / "Main.cpp").write_text(
            '#ifndef AUTO_TOOL_ENV\n#error "Environment was not refreshed"\n#endif\nint main() { return 0; }\n'
        )
        run(s_cli, "build", "-B", s_gout, env=s_modifiedEnv)
        assert (s_gout / "axlum.state").read_text() != s_stateBefore
        (s_globws / "Main.cpp").write_text("int main() { return 0; }\n")
        run(s_cli, "build", "-B", s_gout)
        assert (s_gout / "axlum.state").read_text() == s_stateBefore
    print("PASS: automatic glob refresh and stale output/runtime copy removal")

    # Local package content changes cause restore/rebuild automatically.
    s_packagews = s_root / "package"
    shutil.copytree(s_examples / "rhodium-local", s_packagews)
    s_pout = s_root / "package output"
    run(s_cli, "configure", "-f", s_packagews / "axlum.build.lua", "-B", s_pout)
    run(s_cli, "build", "-B", s_pout)
    s_pexe = s_pout / "bin" / ("PackageHello.exe" if s_windows else "PackageHello")
    assert run(s_pexe).strip() == "package result=42"
    s_source = next((s_packagews / "packages" / "math").rglob("*.cpp"))
    s_source.write_text(
        s_source.read_text().replace("return lhs + rhs;", "return lhs + rhs + 1;")
    )
    run(s_cli, "build", "-B", s_pout)
    assert run(s_pexe).strip() == "package result=43"
    print("PASS: local package sources are restored automatically")

    if s_windows:
        s_rws = s_root / "resources"
        s_rws.mkdir()
        (s_rws / "Main.cpp").write_text("int main() { return 0; }\n")
        (s_rws / "header.h").write_text("#define PAYLOAD 101\n")
        (s_rws / "payload.bin").write_bytes(b"payload-v1")
        (s_rws / "test.rc").write_text(
            '#include "header.h"\nPAYLOAD RCDATA "payload.bin"\n'
        )
        # A valid one-pixel 32-bit ICO (DIB plus AND mask).
        s_dib = (
            struct.pack("<IIIHHIIIIII", 40, 1, 2, 1, 32, 0, 4, 0, 0, 0, 0)
            + b"\0\0\xff\xff"
            + b"\0" * 4
        )
        (s_rws / "app.ico").write_bytes(
            struct.pack("<HHHBBBBHHII", 0, 1, 1, 1, 1, 0, 0, 1, 32, len(s_dib), 22)
            + s_dib
        )
        (s_rws / "plugin.cpp").write_text('extern "C" int entry() { return 42; }\n')
        (s_rws / "axlum.build.lua").write_text(
            'workspace("RC")\ntarget("RC")\nkind("ConsoleApp")\nfiles("Main.cpp")\n'
            'win32resourcefile("test.rc")\nwin32icon("app.ico")\nruntimefiles("@build/bin/Plugin.dll")\n'
            'target("Plugin")\nkind("SharedLibrary")\nfiles("plugin.cpp")\nimportlibrary(false)\n'
            'target("Explicit")\nkind("SharedLibrary")\nfiles("explicit.cpp")\nexports("entry")\n'
        )
        shutil.copyfile(s_rws / "plugin.cpp", s_rws / "explicit.cpp")
        s_rout = s_root / "resource output"
        run(s_cli, "configure", "-f", s_rws / "axlum.build.lua", "-B", s_rout)
        run(s_cli, "build", "-B", s_rout, "-t", "RC")
        assert (s_rout / "bin" / "Plugin.dll").exists()
        run(s_cli, "build", "-B", s_rout)
        assert (s_rout / "bin" / "Plugin.dll").exists()
        assert not (s_rout / "lib" / "Plugin.lib").exists()
        assert (s_rout / "lib" / "Explicit.lib").exists()
        s_resources = {p: p.stat().st_mtime_ns for p in s_rout.rglob("*.res")}
        assert len(s_resources) == 2
        (s_rws / "payload.bin").write_bytes(b"payload-v2")
        run(s_cli, "build", "-B", s_rout)
        assert sum(p.stat().st_mtime_ns != old for p, old in s_resources.items()) == 1
        (s_rws / "header.h").write_text("#define PAYLOAD 102\n")
        run(s_cli, "build", "-B", s_rout)
        print(
            "PASS: RC literal dependency scanning, icon generation and DLL export/import-library policy"
        )
