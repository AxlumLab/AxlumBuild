# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Real DSL feature, dynamic generation, host tools and MSBuild bridge regression."""
import functools
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import zipfile

cli = Path(sys.argv[1]).resolve()

def run(*args, cwd=None, ok=True):
    result = subprocess.run(list(map(str, args)), cwd=cwd, capture_output=True, text=True, errors="replace")
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == ok, (args, result.returncode, output)
    return output

with tempfile.TemporaryDirectory(prefix="axlum features ") as temp:
    root = Path(temp)
    source, build = root / "source", root / "build"
    source.mkdir()
    (source / "inputs").mkdir()
    (source / "inputs/one.txt").write_text("42")
    (source / "assets/nested").mkdir(parents=True)
    (source / "assets/nested/data.txt").write_text("resource")
    (source / "Config.hpp.in").write_text('#define TEMPLATE_NUMBER @NUMBER@\n#define TEMPLATE_STRING @STRING:cstr@\n')
    (source / "Main.cpp").write_text('''#include <cstdlib>
#include <fstream>
#include <string>
int generated();
int main(){
 if(!ENABLED || TEMPLATE_NUMBER!=17 || generated()!=EXPECTED) return 1;
 if(std::string(TEXT)!="a\\\"b\\\\c") return 2;
 if(!std::getenv("TEST_VALUE")) return 3;
 std::ifstream file("data/nested/data.txt"); std::string s; file>>s; return s=="resource"?0:4;
}
''')
    (source / "Host.cpp").write_text(r'''#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
namespace fs=std::filesystem;
extern int hostGenerated();
void write(const fs::path& p, const std::string& s){std::ifstream in(p); std::ostringstream old;old<<in.rdbuf(); if(old.str()!=s)std::ofstream(p)<<s;}
int main(int argc,char**argv){
 if(argc!=3 || hostGenerated()!=42 || !std::getenv("ACTION_VALUE")) return 5;
 fs::path input=argv[1], output=argv[2];fs::create_directories(output);
 std::string manifest="[", sum="int generated(){return "; bool first=true;
 for(auto& entry:fs::directory_iterator(input)) {
  if(entry.path().extension()!=".txt") continue;
  std::ifstream in(entry.path());int n=0;in>>n;
  auto stem=entry.path().stem().string(); auto file=stem+".cpp";
  write(output/file,"int value_"+stem+"(){return "+std::to_string(n)+";}\n");
  sum="extern int value_"+stem+"();\n"+sum;
  sum+=(first?"":"+")+std::string("value_")+stem+"()";
  manifest+=(first?"":" ,")+std::string("\"")+file+"\""; first=false;
 }
 sum+=";}\n";write(output/"Sum.cpp",sum);
 manifest+=(first?"":" ,")+std::string("\"Sum.cpp\"]");write(output/"Outputs.json",manifest);
}
''')
    (source / "HostPartGen.py").write_text('from pathlib import Path\nimport json,sys\np=Path(sys.argv[1]);p.mkdir(parents=True,exist_ok=True)\nfor name,text in {"HostPart.cpp":"int hostGenerated(){return 42;}","Outputs.json":json.dumps(["HostPart.cpp"])}.items():\n f=p/name\n if not f.exists() or f.read_text()!=text:f.write_text(text)\n')
    (source / "Managed").mkdir()
    (source / "Managed/App.csproj").write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>')
    (source / "Managed/Program.cs").write_text('return Generated.Value == 42 ? 0 : 1;')
    (source / "Managed/Stale.cs").write_text('#error Must be excluded through the DSL\n')
    (source / "ManagedGen.py").write_text('from pathlib import Path\nimport sys\np=Path(sys.argv[1]);p.parent.mkdir(parents=True,exist_ok=True)\ns="static class Generated { public const int Value=42; }"\nif not p.exists() or p.read_text()!=s:p.write_text(s)\n')
    manifest = '''workspace("Features")
configuration("Debug","Release")
architecture("x64","arm64")
local enabled=option("enabled",{type="bool",default=true,description="Enable app"})
local backend=option("backend",{type="enum",default="a",values={"a","b"}})
local expected=option("expected",{type="string",default="42"})
local root=option("data",{type="path",default="assets"})
preset("alternate",{config="Debug",options={backend="b"}})
local python=host_program("python",{path=PYTHON,version_args={"--version"},version_pattern="Python 3"})
characterset("UTF-8")
configure_header("generated/Config.hpp",{ENABLED=enabled,TEXT='a"b\\\\c'})
configure_file("Config.hpp.in","generated/Template.hpp",{NUMBER=17,STRING='quoted"value'})
when("windows",function() rhodium.system("system",{links={"Ws2_32"}}) end)
target("HostParts") kind("Codegen") tool(python)
 args({"${workspace}/HostPartGen.py","${build}/hostparts"}) inputs("HostPartGen.py") outputmanifest("hostparts/Outputs.json")
target("HostGenerator") kind("ConsoleApp") hosttool(true) files("Host.cpp") generatedsources("HostParts")
target("Generate") kind("Codegen") tool("@target/HostGenerator")
 args({"${workspace}/inputs","${build}/generated/parts"})
 inputset("inputs",{"**.txt"}) outputmanifest("generated/parts/Outputs.json")
 workingdir("${workspace}") environment({ACTION_VALUE="yes"})
target("App") kind("ConsoleApp") files("Main.cpp")
 forceincludes({"@build/generated/Config.hpp","@build/generated/Template.hpp"})
 defines("EXPECTED="..expected) generatedsources("Generate")
 when("windows",function() packages("system") end)
 deployfiles(root,{destination="data",patterns={"**.txt"}})
test("Native",{target="App",timeout=10,labels={"smoke"},environment={TEST_VALUE="yes"}})
test("ArtifactReference",{command={PYTHON,"-c","import pathlib,sys;assert pathlib.Path(sys.argv[1]).is_file()"},args={targetfile("HostGenerator")},workingdir="${workspace}"})
target("ManagedGen") kind("Codegen") tool(python)
 args({"${workspace}/ManagedGen.py","${build}/generated/csharp/Generated.cs"})
 inputs("ManagedGen.py") outputs("generated/csharp/Generated.cs")
target("ManagedApp") kind("DotNetProject") project("Managed/App.csproj") private_deps("ManagedGen")
 managedsources({include={"@build/generated/csharp/*.cs"},remove={"Managed/Stale.cs"}})
test("Managed",{target="ManagedApp",labels={"managed"}})
'''.replace("PYTHON", json.dumps(sys.executable.replace("\\", "/")))
    (source / "axlum.build.lua").write_text(manifest)
    run(cli, "configure", "-S", source, "-B", build)
    run(cli, "test", "-B", build)
    product = build / "bin" / ("App.exe" if os.name == "nt" else "App")
    before = product.stat().st_mtime_ns
    output = run(cli, "build", "-B", build)
    assert product.stat().st_mtime_ns == before and "Compile " not in output and "Generate " not in output, output
    schema = json.loads(run(cli, "options", "-S", source, "-B", build, "--json"))
    assert schema["backend"]["value"] == "a"
    run(cli, "configure", "-S", source, "-B", build, "--preset", "alternate")
    assert json.loads(run(cli,"options","-S",source,"-B",build,"--json"))["backend"]["value"] == "b"
    saved = (build / "axlum.options.json").read_text()
    run(cli,"configure","-S",source,"-B",build,"--option","backend=bad",ok=False)
    assert (build / "axlum.options.json").read_text() == saved
    run(cli,"configure","-S",source,"-B",build,"--option","typo=true",ok=False)
    run(cli,"configure","-S",source,"-B",build,"--preset","alternate","--option","backend=a")
    run(cli,"build","-B",build)
    assert json.loads(run(cli,"options","-S",source,"-B",build,"--json"))["backend"]["value"] == "a"
    (source / "inputs/two.txt").write_text("1")
    run(cli,"configure","-S",source,"-B",build,"--option","expected=43")
    run(cli,"test","-B",build,"-R","^Native$")
    assert (build / "generated/parts/two.cpp").exists()
    (source / "inputs/two.txt").unlink()
    run(cli,"configure","-S",source,"-B",build,"--option","expected=42")
    run(cli,"test","-B",build,"-L","smoke")
    assert not (build / "generated/parts/two.cpp").exists()
    (source / "assets/nested/extra.txt").write_text("extra")
    run(cli,"build","-B",build)
    assert (build / "bin/data/nested/extra.txt").exists()
    (source / "assets/nested/extra.txt").unlink()
    run(cli,"build","-B",build)
    assert not (build / "bin/data/nested/extra.txt").exists()
    print("PASS: typed options, preset persistence/rollback, templates, native and managed tests, host tools, dynamic sources and resource membership")
    # Offline archive acquisition uses embedded Rhodium's real restore/cache path.
    (source / "Tool.py").write_text('from pathlib import Path\nimport sys\nPath(sys.argv[1]).write_text("tool works")\n')
    with zipfile.ZipFile(source/"tool.zip","w") as archive: archive.write(source/"Tool.py","Tool.py")
    digest=hashlib.sha256((source/"tool.zip").read_bytes()).hexdigest()
    server=http.server.ThreadingHTTPServer(("127.0.0.1",0),functools.partial(http.server.SimpleHTTPRequestHandler,directory=str(source)))
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        extra='''\nlocal packaged=rhodium.tool("ScriptTool",{source="URL",source_type="archive",sha256="DIGEST",build="header_only",export_defines=false,artifacts={include_dirs={"."}},executable="Tool.py"})
target("PackagedTool") kind("Codegen") tool(PYTHON) args({packaged,"${build}/tool-result.txt"}) outputs("tool-result.txt")
'''.replace("URL",f"http://127.0.0.1:{server.server_port}/tool.zip").replace("DIGEST",digest).replace("PYTHON",json.dumps(sys.executable.replace("\\","/")))
        (source/"axlum.build.lua").write_text(manifest+extra)
        run(cli,"configure","-S",source,"-B",build)
        run(cli,"build","-B",build,"-t","PackagedTool")
        assert (build/"tool-result.txt").read_text()=="tool works"
    finally: server.shutdown();server.server_close()
    print("PASS: Rhodium-backed host tool archive with checksum and actual execution")
