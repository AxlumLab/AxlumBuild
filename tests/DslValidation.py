# @Author: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.
"""Boundary validation, byproducts and generated-only composed projects."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
cli=Path(sys.argv[1]).resolve()
def run(*args,ok=True):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True,errors="replace")
    output=p.stdout+p.stderr
    assert (p.returncode==0)==ok,(args,p.returncode,output)
    return output
with tempfile.TemporaryDirectory(prefix="axlum feature boundaries ") as directory:
    root=Path(directory); source=root/"source"; child=source/"child"; child.mkdir(parents=True); build=root/"build"
    (child/"schema").mkdir();(child/"schema/value.txt").write_text("42")
    (child/"Gen.py").write_text('''from pathlib import Path
import json,sys
p=Path(sys.argv[1]);p.mkdir(parents=True,exist_ok=True)
def write(name,text):
 f=p/name
 if not f.exists() or f.read_text()!=text:f.write_text(text)
write("Main.cpp",'#include "Settings.hpp"\\nint main(){return FLAG==1?0:1;}\\n')
write("Outputs.json",json.dumps(["Main.cpp"]))
write("Extra.txt","byproduct")
''')
    py=json.dumps(sys.executable.replace("\\","/"))
    (source/"axlum.build.lua").write_text('workspace("Root")\nconfiguration("Debug")\nworkspace_reference("Child",{file="child/axlum.build.lua"})\n')
    manifest='''workspace("Child")
configuration("Debug")
local header=configure_header("generated/Settings.hpp",{FLAG=true})
target("Headers") kind("HeaderOnly") private_deps(header)
target("Generate") kind("Codegen") tool(PYTHON)
 args({"${workspace}/Gen.py","${build}/generated"})
 inputs("Gen.py") inputset("schema",{"**.txt"})
 outputmanifest("generated/Outputs.json") byproducts("generated/Extra.txt")
target("App") kind("ConsoleApp") generatedsources("Generate")
 private_deps("Headers") includedirs("@build/generated")
test("Run",{target="App",workingdir="${build}/bin",labels={"child"}})
'''.replace("PYTHON",py)
    (child/"axlum.build.lua").write_text(manifest)
    run(cli,"configure","-S",source,"-B",build)
    run(cli,"test","-B",build,"-L","child")
    exe=build/"projects/Child/bin"/("App.exe" if os.name=="nt" else "App")
    before=exe.stat().st_mtime_ns
    output=run(cli,"build","-B",build)
    assert exe.stat().st_mtime_ns==before and "Compile " not in output,output
    extra=build/"projects/Child/generated/Extra.txt"; assert extra.is_file(); extra.unlink()
    run(cli,"build","-B",build,"-t","Child::App")
    assert extra.is_file()
    print("PASS: generated-only first build, template dependencies, byproduct recovery, child test and no-op")
    # Internal template executor is the same tracked operation used by configure_file.
    (source/"Bad.in").write_text("@MISSING@")
    spec=root/"Template.json"
    spec.write_text(json.dumps(dict(input="Bad.in",output="bad.hpp",workspace=str(source),build=str(build),config="Debug",values={})))
    assert "undefined template variable" in run(cli,"__feature",spec,ok=False)
    spec.write_text(json.dumps(dict(header=True,output="../outside.hpp",workspace=str(source),build=str(build),config="Debug",values={})))
    assert "escapes" in run(cli,"__feature",spec,ok=False)
    # A failed generator cannot retain a declared success marker.
    fail=source/"Failure.py";fail.write_text('from pathlib import Path\nimport sys\nPath(sys.argv[1]).write_text("partial")\nraise SystemExit(7)\n')
    (source/"axlum.build.lua").write_text('''workspace("Failure")
configuration("Debug")
target("Fail") kind("Codegen") tool(PYTHON) args({"${workspace}/Failure.py","${build}/partial.txt"}) inputs("Failure.py") outputs("partial.txt")
'''.replace("PYTHON",py))
    failure=root/"failure"
    run(cli,"configure","-S",source,"-B",failure)
    run(cli,"build","-B",failure,ok=False)
    assert not (failure/"partial.txt").exists()
    # Test runner must enforce timeouts, then clear obsolete registrations.
    (source/"axlum.build.lua").write_text('''workspace("Timeout")
configuration("Debug")
target("Empty") kind("HeaderOnly")
local enabled=option("test",{type="bool",default=true})
if enabled then
 test("DefaultDirectory",{command={PYTHON,"-c","import pathlib,os;assert pathlib.Path.cwd().name == 'bin';assert os.environ['VALUE']=='a;b'"},environment={VALUE="a;b"}})
 test("Timeout",{command={PYTHON,"-c","import time;time.sleep(10)"},timeout=1,workingdir="${workspace}"}) end
'''.replace("PYTHON",py))
    timeout=root/"timeout"
    run(cli,"configure","-S",source,"-B",timeout)
    run(cli,"test","-B",timeout,"--no-build","-R","DefaultDirectory")
    assert "Timeout" in run(cli,"test","-B",timeout,"--no-build","-R","^Timeout$",ok=False)
    run(cli,"configure","-S",source,"-B",timeout,"--option","test=false")
    assert json.loads((timeout/"axlum.tests.json").read_text())==[]
    print("PASS: strict templates, confined outputs, failed generator cleanup, test timeout and stale registration removal")
