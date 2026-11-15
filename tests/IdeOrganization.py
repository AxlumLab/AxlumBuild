"""Generated IDE views, executable tasks and personal-setting preservation."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

cli = Path(sys.argv[1]).resolve()
ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}

def run(*args, ok=True):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True, errors="replace")
    text = result.stdout + result.stderr
    assert (result.returncode == 0) == ok, (args, result.returncode, text)
    return text

with tempfile.TemporaryDirectory(prefix="axlum ide organization ") as temporary:
    root = Path(temporary)
    source, builds, ide = root / "source", root / "builds", root / "ide"
    (source / "src/lib").mkdir(parents=True)
    (source / "src/Main.cpp").write_text("extern int value(); int main(){return VALUE == value() ? 0 : 1;}\n")
    (source / "src/lib/Value.cpp").write_text("int value(){return 42;}\n")
    (source / "Tool.cpp").write_text("int main(){return 0;}\n")
    (source / "Check.cpp").write_text("int main(){return 0;}\n")
    manifest = source / "axlum.build.lua"
    script = '''workspace("Organized")
configuration("Debug", "Release")
configure_header("generated/Settings.hpp", {VALUE=42})
target("Lib") kind("StaticLibrary") files("src/lib/Value.cpp")
target("Hidden") kind("HeaderOnly") ide_target({visible=false})
target("Tool") kind("ConsoleApp") hosttool(true) files("Tool.cpp")
target("Check") kind("ConsoleApp") files("Check.cpp")
test("smoke", {target="Check"})
target("App") kind("ConsoleApp") files({"src/Main.cpp", "@build/generated/Settings.hpp"})
forceincludes("@build/generated/Settings.hpp") private_deps({"Lib","Hidden"})
ide_target({group="Applications/Demos", display_name="Sample App", startup=true})
ide_customize("vs", {debugger={LocalDebuggerCommandArguments="--default"}})
'''
    manifest.write_text(script)
    def generate(ok=True):
        return run(cli, "ide", "-f", manifest, "-B", builds, "-o", ide, "--format", "desktop", ok=ok)
    generate()
    sln = (ide / "vs/Organized.sln").read_text()
    assert ' = "Sample App", "App.vcxproj"' in sln
    assert sln.index(' = "Sample App"') < sln.index(' = "all"')
    assert "GlobalSection(NestedProjects)" in sln
    for group in ("Applications", "Demos", "Libraries", "Tools", "Tests", "Build"):
        assert f' = "{group}", "{group}"' in sln
    assert '"Hidden.vcxproj"' not in sln and '"_template_' not in sln
    assert not (ide / "vs/Hidden.vcxproj").exists()
    tree = ET.parse(ide / "vs/App.vcxproj.filters")
    filters = {x.attrib["Include"] for x in tree.findall(".//m:Filter[@Include]", ns)}
    assert "src" in filters and any(x.startswith("Generated") for x in filters)
    includes = {x.attrib["Include"] for x in tree.findall(".//m:ClInclude", ns)}
    assert any(x.endswith("Settings.hpp") for x in includes)
    lib = ET.parse(ide / "vs/Lib.vcxproj.filters")
    assert "src\\lib" in {x.attrib["Include"] for x in lib.findall(".//m:Filter[@Include]", ns)}
    workspace = ide / "vscode/Organized.code-workspace"
    launch_file = ide / "vscode/.vscode/launch.json"
    tasks_file = ide / "vscode/.vscode/tasks.json"
    data = json.loads(workspace.read_text())
    tasks = data["tasks"]["tasks"]
    assert len([t for t in tasks if not t.get("hide")]) == 4
    launches = data["launch"]["configurations"]
    assert len(launches) == 4, launches  # App and Check, two configurations; host tool hidden from launch.
    assert launches[0]["name"].endswith(":Sample App")
    assert all("run existing" not in x["name"] for x in launches)
    assert len(data["folders"]) == 1
    assert data["settings"]["files.exclude"]["**/.cache"]
    choices = next(x for x in data["tasks"]["inputs"] if x["id"] == "axlum.buildTarget")["options"]
    assert not any(isinstance(x, dict) and (x["value"] == "Hidden" or x["value"].startswith("_template_")) for x in choices)
    generic = next(x for x in tasks if x["label"] == "Axlum: Build selected target")
    args = [x.replace("${input:axlum.configuration}", str(builds / "Release-x64")).replace("${input:axlum.buildTarget}", "App") for x in generic["args"]]
    run(generic["command"], *args)
    product = builds / "Release-x64/bin" / ("App.exe" if os.name == "nt" else "App")
    run(product)
    hidden = next(t for t in tasks if t["label"] == "build:Debug-x64:App")
    assert hidden["hide"] and "group" not in hidden
    run(hidden["command"], *hidden["args"])
    # A hidden target remains usable by Ninja and by visible target dependencies.
    run(cli, "build", "-B", builds / "Debug-x64", "-t", "Hidden")
    personal = {"name": "Attach manually", "type": "cppvsdbg", "request": "attach", "processId": "${command:pickProcess}"}
    def personalize(value):
        conf = next(x for x in value["configurations"] if x["preLaunchTask"] == "build:Debug-x64:App")
        conf["args"] = ['--literal=,}', '--quoted="value"']
        conf["cwd"] = str(root)
        conf["environment"] = [{"name": "USER_MODE", "value": "yes"}]
        value["configurations"].append(personal)
    personalize(data["launch"])
    data["settings"]["editor.fontSize"] = 19
    custom_task = {"label": "User task", "type": "process", "command": sys.executable, "args": ["-c", "print(42)"]}
    data["tasks"]["tasks"].append(custom_task)
    # VS Code JSONC comments and trailing commas remain accepted on regeneration.
    workspace.write_text("// personal settings\n" + json.dumps(data, indent=2)[:-1] + ",\n}\n")
    standalone = json.loads(launch_file.read_text()); personalize(standalone)
    launch_file.write_text(json.dumps(standalone))
    user_file = ide / "vs/App.vcxproj.user"
    personal_xml = user_file.read_text().replace("--default", "--personal")
    user_file.write_text(personal_xml)
    # Display-name change must not lose settings tied to the same target.
    manifest.write_text(script.replace("Sample App", "Renamed App"))
    generate()
    for config in [json.loads(workspace.read_text())["launch"], json.loads(launch_file.read_text())]:
        app = next(x for x in config["configurations"] if x.get("preLaunchTask") == "build:Debug-x64:App")
        assert app["name"] == "Debug-x64:Renamed App"
        assert app["args"] == ['--literal=,}', '--quoted="value"']
        assert app["cwd"] == str(root) and app["environment"][0]["value"] == "yes"
        assert personal in config["configurations"]
    data = json.loads(workspace.read_text())
    assert data["settings"]["editor.fontSize"] == 19 and custom_task in data["tasks"]["tasks"]
    assert user_file.read_text() == personal_xml
    assert Path(str(workspace) + ".user-backup").exists()
    before = workspace.stat().st_mtime_ns
    generate()
    assert workspace.stat().st_mtime_ns == before
    commented = "// keep personal comments\n" + workspace.read_text()
    workspace.write_text(commented)
    generate()
    assert workspace.read_text() == commented
    msbuild = Path("C:/Apps/VisualStudio/MSBuild/Current/Bin/MSBuild.exe")
    if os.name == "nt" and msbuild.exists():
        run(msbuild, ide / "vs/Organized.sln", "/m:4", "/p:Configuration=Debug", "/p:Platform=x64", "/v:minimal", "/nologo")
    # Hiding a project removes generated views, while retaining personal VS data.
    tool_user = ide / "vs/Tool.vcxproj.user"
    tool_user.write_text("personal")
    manifest.write_text(script.replace('files("Tool.cpp")\ntarget("Check")', 'files("Tool.cpp") ide_target({visible=false})\ntarget("Check")'))
    generate()
    assert not (ide / "vs/Tool.vcxproj").exists()
    assert not (ide / "vs/Tool.vcxproj.filters").exists()
    assert tool_user.read_text() == "personal"
    assert personal in json.loads(workspace.read_text().removeprefix("// keep personal comments\n"))["launch"]["configurations"]
    # Invalid user JSON is reported, never replaced.
    workspace.write_text("{broken")
    generate(ok=False)
    assert workspace.read_text() == "{broken"
print("PASS: folders/filters, hidden dependencies, startup/display metadata, config-specific tasks, JSONC/VS personal settings, repeat generation, MSBuild")
