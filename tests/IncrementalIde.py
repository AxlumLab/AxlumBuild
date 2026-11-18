"""Target-scoped incremental generation and conventional VS project selection."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

cli = Path(sys.argv[1]).resolve()

def run(*args, ok=True):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True, errors="replace")
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == ok, (args, result.returncode, output)
    return output

with tempfile.TemporaryDirectory(prefix="axlum incremental ide ") as temporary:
    root = Path(temporary)
    source, build = root / "source", root / "build"
    source.mkdir()
    (source / "Main.cpp").write_text('int main() { return 0; }\n')
    (source / "Host.cpp").write_text('int main() { return 0; }\n')
    (source / "GeneratorTool.cpp").write_text('int main() { return 0; }\n')
    (source / "GeneratorInput.hpp").write_text("#define EXIT_CODE 0\n")
    (source / "GeneratedApp.cpp").write_text('int generated(); int main() { return generated() == 42 ? 0 : 1; }\n')
    (source / "choice.txt").write_text("One")
    (source / "Generate.py").write_text(
        "import json,subprocess,sys\nfrom pathlib import Path\n"
        "subprocess.check_call([sys.argv[3]])\n"
        "root=Path(sys.argv[1]); output=Path(sys.argv[2]); output.mkdir(parents=True,exist_ok=True)\n"
        "name=(root/'choice.txt').read_text()+'.cpp'\n"
        "for file,text in {name:'int generated(){return 42;}', 'Outputs.json':json.dumps([name])}.items():\n"
        " p=output/file\n if not p.exists() or p.read_text()!=text:p.write_text(text)\n"
    )
    manifest = source / "axlum.build.lua"
    manifest.write_text(
        'workspace("Incremental")\nconfiguration("Debug")\n'
        'target("Plain") kind("ConsoleApp") files("Main.cpp")\n'
        'target("UnrelatedHost") kind("ConsoleApp") hosttool(true) files("Host.cpp")\n'
        'target("GeneratorTool") kind("ConsoleApp") hosttool(true) files("GeneratorTool.cpp")\n'
        'target("Generate") kind("Codegen") tool(' + json.dumps(sys.executable.replace("\\", "/")) + ')\n'
        'args({"${workspace}/Generate.py","${workspace}","${build}/generated",targetfile("GeneratorTool")})\n'
        'private_deps("GeneratorTool")\n'
        'inputs({"Generate.py","choice.txt"}) outputmanifest("generated/Outputs.json")\n'
        'target("GeneratedApp") kind("ConsoleApp") files("GeneratedApp.cpp") generatedsources("Generate")\n'
    )
    output = run(cli, "configure", "-f", manifest, "-B", build)
    assert "Compile " not in output and "Generate " not in output and "Link " not in output, output
    assert not (build / "generated/Outputs.json").exists()
    assert json.loads((build / "axlum.run-targets.json").read_text()) == ["Plain", "GeneratedApp"]
    assert not (build / "host/bin/UnrelatedHost.exe").exists()
    run(cli, "build", "-B", build, "-t", "Plain")
    assert not (build / "generated/Outputs.json").exists()
    snapshot = build / "axlum.configure-inputs.json"
    before = snapshot.stat().st_mtime_ns
    assert "no work to do" in run(cli, "build", "-B", build, "-t", "Plain")
    assert snapshot.stat().st_mtime_ns == before
    (source / "Host.cpp").write_text('#error unrelated host must not be compiled\n')
    (source / "choice.txt").write_text("Two")
    (source / "Main.cpp").write_text('int main() { return 0; } // edited\n')
    output = run(cli, "build", "-B", build, "-t", "Plain")
    assert "Compile Plain" in output and "UnrelatedHost" not in output, output
    assert not (build / "generated/Two.cpp").exists()
    assert snapshot.stat().st_mtime_ns == before
    run(cli, "run", "-B", build, "Plain")
    assert snapshot.stat().st_mtime_ns == before
    # Direct Ninja must also bootstrap a previously unbuilt dynamic source set.
    state = dict(line.split("=", 1) for line in (build / "axlum.state").read_text().splitlines() if "=" in line)
    env = dict(os.environ, AXLUM_BUILD_TARGETS=json.dumps(["GeneratedApp"]))
    result = subprocess.run([state["ninja"], "-C", str(build), "GeneratedApp"], env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    database = (build / "compile_commands.json").read_text()
    assert "Two.cpp" in database and "One.cpp" not in database
    run(cli, "run", "-B", build, "GeneratedApp")
    assert "no work to do" in run(cli, "build", "-B", build, "-t", "GeneratedApp")
    # Rebuilding a native generator updates Ninja's dependency log even when
    # the generator returns the same manifest and leaves its outputs alone.
    (source / "GeneratorTool.cpp").write_text('#include "GeneratorInput.hpp"\nint main() { return EXIT_CODE; }\n')
    output = run(cli, "build", "-B", build, "-t", "GeneratedApp")
    assert output.count("Compile GeneratorTool:GeneratorTool.cpp") == 1, output
    output = run(cli, "build", "-B", build, "-t", "GeneratedApp")
    assert "no work to do" in output and "premature end of file" not in output, output
    assert "Compile " not in output and "Link " not in output and "Generate " not in output, output
    (source / "choice.txt").write_text("Three")
    run(cli, "build", "-B", build, "-t", "GeneratedApp")
    assert "Three.cpp" in (build / "compile_commands.json").read_text()
    assert not (build / "generated/Two.cpp").exists()
    (source / "Host.cpp").write_text('int main() { return 0; }\n')
    ide = root / "ide"
    ide_builds = root / "ide-builds"
    run(cli, "configure", "-f", manifest, "-B", ide_builds / "Debug-x64", "--config", "Debug")
    run(cli, "ide", "-B", ide_builds, "-o", ide, "--format", "vs")
    solution = (ide / "vs/Incremental.sln").read_text()
    assert "VisualStudioVersion = 17." in solution
    assert "MinimumVisualStudioVersion = 10.0.40219.1" in solution
    section = solution.split("GlobalSection(SolutionConfigurationPlatforms)", 1)[1].split("EndGlobalSection", 1)[0]
    assert "Debug|x64" in section and "@" not in section and "Debug-x64" not in section
    project = ET.parse(ide / "vs/Plain.vcxproj")
    ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    assert [item.attrib["Include"] for item in project.findall(".//m:ProjectConfiguration", ns)] == ["Debug|x64"]
    assert '"Plain"' in project.find(".//m:NMakeBuildCommandLine", ns).text
    assert project.find(".//m:VCProjectVersion", ns).text == "17.0"
    assert project.find(".//m:WindowsTargetPlatformVersion", ns).text == "10.0"
    assert project.find(".//m:LocalDebuggerCommand", ns).text.endswith("Plain.exe" if os.name == "nt" else "Plain")
    msbuild = Path("C:/Apps/VisualStudio/MSBuild/Current/Bin/MSBuild.exe")
    if os.name == "nt" and msbuild.exists():
        ide_snapshot = root / "ide-builds/Debug-x64/axlum.configure-inputs.json"
        ide_before = ide_snapshot.stat().st_mtime_ns
        run(msbuild, ide / "vs/Plain.vcxproj", "/p:Configuration=Debug", "/p:Platform=x64", "/nologo", "/v:minimal")
        assert ide_snapshot.stat().st_mtime_ns == ide_before, "MSBuild environment triggered reconfigure"
        run(msbuild, ide / "vs/Incremental.sln", "/m:4", "/p:Configuration=Debug", "/p:Platform=x64", "/nologo", "/v:minimal")
print("PASS: no-op/source edits skip configure, unrelated dirty host/generator excluded, dynamic source set refresh, native run, VS project/solution builds")
