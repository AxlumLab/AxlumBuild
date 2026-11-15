"""One tiny project verifies command/configure boundaries; no repository test suite."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="axlum command cache ") as temporary:
    root = Path(temporary)
    source, build = root / "source", root / "build"
    source.mkdir()
    (source / "Main.cpp").write_text("int generated(); int main(){return generated()==42?0:1;}")
    (source / "Generate.py").write_text("from pathlib import Path\nimport sys,json\np=Path(sys.argv[1]);p.mkdir(parents=True,exist_ok=True)\n(p/'Generated.cpp').write_text('int generated(){return 42;}')\n(p/'Outputs.json').write_text(json.dumps(['Generated.cpp']))\n")
    probe = source / "Probe.py"
    probe.write_text("import os,sys;sys.exit(1) if os.getenv('AXLUM_CACHE_TEST_FAIL') else print('OK')")
    (source / "axlum.build.lua").write_text('''local python=host_program("python",{path=PYTHON,version_args={PROBE},version_pattern="OK"})
workspace("Cached") configuration("Debug")
target("Generate") kind("Codegen") tool(python)
args({"${workspace}/Generate.py","${build}/generated"}) inputs("Generate.py") outputmanifest("generated/Outputs.json")
target("App") kind("ConsoleApp") files("Main.cpp") generatedsources("Generate")
test("Smoke",{target="App"})
'''.replace("PYTHON", json.dumps(sys.executable.replace("\\", "/"))).replace("PROBE", json.dumps(probe.as_posix())))
    blocked = dict(os.environ, AXLUM_CACHE_TEST_FAIL="1")
    def run(*args, ok=True, env=None):
        result = subprocess.run([str(cli), *map(str,args)], cwd=source, env=env,
                                capture_output=True, text=True, errors="replace")
        output = result.stdout + result.stderr
        assert (result.returncode == 0) == ok, output
        return output
    assert "configure" in run("ide", "-B", build, ok=False)
    run("configure", "-B", build)
    assert not list(build.rglob("*.obj")) and not (build / "generated/Outputs.json").exists()
    before = (build / "build.ninja").stat().st_mtime_ns
    run("ide", "-B", build, "-o", root / "ide", "--format", "desktop", env=blocked)
    assert (root / "ide/vs/Cached.sln").exists()
    assert (root / "ide/vscode/axlum.ide.json").exists()
    assert (build / "build.ninja").stat().st_mtime_ns == before
    assert not list(build.rglob("*.obj")) and not (build / "generated/Outputs.json").exists()
    for command in ("graph", "info", "targets", "options"):
        run(command, "-B", build, env=blocked)
    assert "build first" in run("install", "-B", build, "--prefix", root / "install", ok=False, env=blocked)
    run("build", "-B", build, env=blocked)
    assert any("Generated.cpp" in entry["file"] for entry in json.loads((build / "compile_commands.json").read_text()))
    stamp = (build / "bin" / ("App.exe" if os.name == "nt" else "App")).stat().st_mtime_ns
    for command in ("run", "test", "build"):
        output = run(command, "-B", build, env=blocked)
        assert "Compile " not in output and "Generate " not in output and "[configure" not in output, output
    run("install", "-B", build, "--prefix", root / "install", env=blocked)
    run("publish", "-B", build, "--prefix", root / "publish", "--no-build", env=blocked)
    run("clean", "-B", build, "--dry-run", env=blocked)
    assert (build / "bin" / ("App.exe" if os.name == "nt" else "App")).stat().st_mtime_ns == stamp
    assert "host program version check failed" in run("configure", "-B", build, ok=False, env=blocked)
    # A changed script asks for explicit configure; it is never evaluated by build.
    manifest = source / "axlum.build.lua"
    manifest.write_text(manifest.read_text() + "\nthis is intentionally invalid Lua\n")
    output = run("build", "-B", build, ok=False, env=blocked)
    assert "configuration inputs changed; run configure" in output and "[configure" not in output, output
    run("ide", "-B", build, "-o", root / "ide", "--format", "desktop", env=blocked)
    print("PASS: cached IDE/query/install/run/test, one tiny build, dynamic graph discovery, explicit configure only")
