"""Exercise the installed VS Code extension host with real Axlum builds."""
import json
from contextlib import nullcontext
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

cli = Path(sys.argv[1]).resolve()
repo = Path(__file__).resolve().parents[1]
code = shutil.which("code")
if not code or os.name != "nt":
    print("SKIP: local Windows VS Code extension host required")
    raise SystemExit(0)

def run(*args, **kwargs):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True, errors="replace", **kwargs)
    if result.returncode:
        raise RuntimeError(f"{args}\n{result.stdout}\n{result.stderr}")
    return result.stdout

# Keep host logs/fixtures under the ignored build tree: Windows terminal helpers
# can retain their working directory briefly after the editor process exits.
(repo / "build").mkdir(exist_ok=True)
with nullcontext(tempfile.mkdtemp(prefix="editor-host-", dir=repo / "build")) as tmp:
    root = Path(tmp)
    source = root / "source"
    source.mkdir()
    (source / "Main.cpp").write_text('''#include <fstream>
#include <cstdlib>
int main(int argc,char** argv){if(argc>1)std::ofstream(argv[1]) << (std::getenv("AXLUM_EDITOR_TEST") ? std::getenv("AXLUM_EDITOR_TEST") : "missing");return 0;}
''')
    (source / "Sleep.cpp").write_text('#include <thread>\n#include <chrono>\nint main(){std::this_thread::sleep_for(std::chrono::seconds(30));}\n')
    (source / "Library.cpp").write_text('int library(){return 1;}\n')
    (source / "Managed.csproj").write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="Managed.cs" /></ItemGroup></Project>')
    (source / "Managed.cs").write_text('public class Managed {}\n')
    (source / "ManagedApp").mkdir()
    (source / "ManagedApp/ManagedApp.csproj").write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType></PropertyGroup></Project>')
    (source / "ManagedApp/Program.cs").write_text('System.IO.File.WriteAllText(args[0], System.Environment.GetEnvironmentVariable("AXLUM_EDITOR_TEST"));')
    manifest = source / "axlum.build.lua"
    manifest.write_text('''workspace("EditorIntegration")
configuration("Debug", "Release")
option("editor_value", {type="string", default="default"})
preset("Debug-x64", {config="Debug", architecture="x64", options={editor_value="preset"}})
preset("Release-x64", {config="Release", architecture="x64", options={editor_value="preset"}})
target("App") kind("ConsoleApp") files("Main.cpp") ide_target({startup=true})
target("Sleep") kind("ConsoleApp") files("Sleep.cpp")
target("Library") kind("StaticLibrary") files("Library.cpp")
target("Managed") kind("DotNetProject") project("Managed.csproj")
target("ManagedApp") kind("DotNetProject") project("ManagedApp/ManagedApp.csproj")
test("App smoke", {target="App", args={"${workspace}/../tested.txt"}, environment={AXLUM_EDITOR_TEST="test-settings"}})
test("Sleep test", {target="Sleep", timeout=60})

''')
    builds, ide = root / "builds", root / "ide"
    generation = [cli, "ide", "-f", manifest, "-B", builds, "-o", ide, "--format", "desktop",
                  "--preset", "Debug-x64", "--preset", "Release-x64", "--option", "editor_value=cli value"]
    run(*generation)
    project = json.loads((ide / "vscode/axlum.ide.json").read_text())
    assert [e["id"] for e in project["entries"]] == ["Debug-x64", "Release-x64"]
    assert "editor_value=cli value" in project["reconfigure"]["args"]
    assert project["reconfigure"]["args"].count("--preset") == 2
    assert json.loads((builds / "Debug-x64/axlum.options.json").read_text())["editor_value"] == "cli value"
    solution = (ide / "vs/EditorIntegration.sln").read_text()
    managed = re.findall(r'Project\("\{9A19103F[^\n]+Managed.csproj", "([^"\n]+)"', solution)
    assert len(managed) == 1, solution
    assert managed[0] + '.Release|x64.ActiveCfg = Release|Any CPU' in solution
    assert '"..\\..\\source\\Managed.csproj"' in solution
    profiles = ide / "vs/EditorIntegration.slnLaunch"
    data = json.loads(profiles.read_text())
    assert {p["Name"] for p in data} == {"App", "Sleep", "ManagedApp"}
    assert data[0]["Projects"][0]["Path"] == "App.vcxproj"
    data[0]["Projects"][0]["Action"] = "StartWithoutDebugging"
    data.append({"Name": "Personal", "Projects": []})
    profiles.write_text(json.dumps(data))
    run(*generation)
    assert json.loads(profiles.read_text()) == data
    # Exercise normal solution open, not /Upgrade (which always writes a report).
    probe_script = root / "probe-vs.ps1"
    probe_template = '''$probeVs=$null
try { $probeVs=New-Object -ComObject VisualStudio.DTE.17.0 }
catch { Write-Output 'SKIP: Visual Studio automation unavailable'; exit 0 }
try {
    $probeVs.MainWindow.Visible=$false
    $probeVs.UserControl=$false
    $probeVs.SuppressUI=$true
    $probeVs.Solution.Open('@SOLUTION@')
    if(-not $probeVs.Solution.IsOpen){throw 'Solution did not load'}
    Start-Sleep -Seconds 2
    $probeVs.Solution.Close($false)
} finally { $probeVs.Quit() }
'''
    def open_vs(file):
        probe_script.write_text(probe_template.replace('@SOLUTION@', str(file).replace("'", "''")))
        return run("powershell", "-NoProfile", "-File", probe_script)
    print(open_vs(ide / "vs/EditorIntegration.sln"), end="")
    if (ide / "vs/UpgradeLog.htm").exists():
        # Some VS installations migrate even an official SDK-generated solution.
        # Require a matching independent baseline, rather than blaming our header
        # or silently skipping a genuine project-format regression.
        baseline = root / "official"
        baseline.mkdir()
        shutil.copyfile(source / "Managed.csproj", baseline / "Managed.csproj")
        options = ["--format", "sln"] if "--format" in run("dotnet", "new", "sln", "--help") else []
        run("dotnet", "new", "sln", "-n", "Official", "-o", baseline, *options)
        run("dotnet", "sln", baseline / "Official.sln", "add", baseline / "Managed.csproj")
        open_vs(baseline / "Official.sln")
        assert (baseline / "UpgradeLog.htm").exists(), "Only Axlum's solution triggers migration"
        for report in [ide / "vs/UpgradeLog.htm", baseline / "UpgradeLog.htm"]:
            html = report.read_text(encoding="utf-8-sig")
            assert not re.search(r'<td[^>]*class="IconErrorEncoded"', html), report
            assert set(re.findall(r'name="WarningRowClass([^"]+)"', html)) == {"Solution"}, report
        print("NOTE: this VS installation also migrates the official dotnet SDK solution; mixed-project migration remains an environment limitation.")
    native_dir = root / "native-vs"
    native_dir.mkdir()
    for project_file in (ide / "vs").glob("*.vcxproj"):
        shutil.copyfile(project_file, native_dir / project_file.name)
    native_solution = re.sub(r'(?m)^Project\("\{9A19103F[^\n]+\nEndProject\n', '', solution)
    managed_guids = re.findall(r'Project\("\{9A19103F[^\n]+", "([^"\n]+)"', solution)
    native_solution = ''.join(line for line in native_solution.splitlines(keepends=True) if not any(guid in line for guid in managed_guids))
    (native_dir / "Native.sln").write_text(native_solution)
    open_vs(native_dir / "Native.sln")
    assert not (native_dir / "UpgradeLog.htm").exists(), "Native solution must load without migration"
    # A native sleeper allows cancellation of a real VS Code ProcessExecution.
    run(cli, "build", "-B", builds / "Debug-x64", "-t", "Sleep")
    workspace = ide / "vscode/EditorIntegration.code-workspace"
    doc = json.loads(workspace.read_text())
    for launch in doc["launch"]["configurations"]:
        if launch["preLaunchTask"].endswith((":App", ":ManagedApp")):
            launch["args"] = [str(root / "ran.txt")]
            launch["environment"] = [{"name": "AXLUM_EDITOR_TEST", "value": "personal-settings"}]
    workspace.write_text(json.dumps(doc))
    env = dict(os.environ, AXLUM_EDITOR_TEST_ROOT=str(root))
    # The current debugger install is used, with an isolated VS Code user profile.
    extensions = run(code, "--list-extensions").splitlines()
    if "ms-vscode.cpptools" not in extensions:
        print("SKIP: Microsoft C/C++ extension required for debug host test")
        raise SystemExit(0)
    disabled = [arg for extension in extensions if extension not in {"ms-vscode.cpptools", "axlum.axlum-build"} for arg in ["--disable-extension", extension]]
    editor = Path(code).resolve().parent.parent / "Code.exe"
    args = [str(editor), "--new-window", "--skip-welcome", "--skip-release-notes", "--disable-workspace-trust",
            "--user-data-dir", str(root / "user"), "--extensionDevelopmentPath", str(repo / "editors/vscode"),
            "--extensionTestsPath", str(repo / "editors/vscode/test/host.js"), *disabled, str(workspace)]
    # Avoid showing a test window while a user is working in their own editor.
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    result = subprocess.run(args, env=env, capture_output=True, text=True, errors="replace", timeout=240, startupinfo=startup)
    report = root / "result.json"
    if result.returncode or not report.exists():
        raise RuntimeError(f"VS Code host exit={result.returncode}\n{result.stdout}\n{result.stderr}")
    status = json.loads(report.read_text())
    assert status["ok"], status
    print(status["message"])
