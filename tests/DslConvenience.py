"""DSL table forms, file scans and incremental GoogleTest protocol discovery."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

cli = Path(sys.argv[1]).resolve()
def run(*args, ok=True):
    result = subprocess.run(list(map(str,args)), capture_output=True, text=True, errors='replace')
    assert (result.returncode == 0) == ok, result.stdout + result.stderr
    return result.stdout + result.stderr

with tempfile.TemporaryDirectory(prefix='axlum discovery ') as tmp:
    root=Path(tmp); source=root/'source'; build=root/'build'
    (source/'src').mkdir(parents=True)
    (source/'src/ignored.cpp').write_text('#error excluded file')
    (source/'src/build/nested').mkdir(parents=True)
    (source/'src/build/nested/hidden.cpp').write_text('#error build directory must be excluded')
    program=r'''#include <iostream>
#include <string>
#include <cstdlib>
#ifndef TABLE_SETTING
#error missing table setting
#endif
int main(int argc,char**argv) {
 if(!std::getenv("TEST_CONTEXT"))return 4;
 for(int i=1;i<argc;++i) {
  std::string arg=argv[i];
  if(arg=="--gtest_list_tests") {std::cout<<"Core.\n  Pass\n  Fail\n  DISABLED_Off\nParams/Value. # TypeParam = int\n  Works/0 # GetParam() = 42\n";return 0;}
  if(arg=="--gtest_filter=Core.Fail")return 1;
 }
 return 0;
}
'''
    (source/'src/main.cpp').write_text(program)
    manifest='''workspace("Convenience")
assert(#matchfiles("src/build/nested/*.cpp")==0)
assert(#matchfiles("absent/nested/*.cpp")==0)
local sources=matchfiles("src/**/*.cpp", "**/ignored.cpp")
assert(#sources==1 and sources[1]=="src/main.cpp")
assert(#list.unique(list.concat({"A"},{"A","B"}))==2)
assert(list.prefix("prefix/", {"a"})[1]=="prefix/a")
target("Unit", {kind="ConsoleApp", files=sources, defines={"TABLE_SETTING"}})
when("all", {defines={"TABLE_CONDITION"}})
when("all", function() defines("CALLBACK_COMPATIBILITY") end)
gtest_discover_tests("Unit", {environment={TEST_CONTEXT="ok"},labels={"unit"}})
'''
    (source/'axlum.build.lua').write_text(manifest)
    run(cli,'configure','-S',source,'-B',build)
    before=json.loads(run('ctest','--test-dir',build,'--show-only=json-v1'))
    assert before['tests'][0]['name']=='Unit/NOT_BUILT'
    run(cli,'build','-B',build,'-t','Unit')
    registration=json.loads((build/'axlum.tests.json').read_text())[0]
    cases_file=Path(registration['discoveryFile'])
    cases=json.loads(cases_file.read_text())
    assert len(cases)==4 and cases[2]['disabled']
    assert cases[3]['name']=='Unit/Params/Value.Works/0'
    stamp=cases_file.stat().st_mtime_ns
    run(cli,'build','-B',build,'-t','Unit')
    assert cases_file.stat().st_mtime_ns==stamp
    run(cli,'test','-B',build,'--no-build','-R','Pass|Works')
    run(cli,'test','-B',build,'--no-build','-R','Fail',ok=False)
    (source/'src/main.cpp').write_text(program.replace('  Fail\\n',''))
    run(cli,'build','-B',build,'-t','Unit')
    assert len(json.loads(cases_file.read_text()))==3
    # New matches are picked up on build, without an explicit configure command.
    (source/'axlum.build.lua').write_text(manifest.replace('assert(#sources==1 and sources[1]=="src/main.cpp")','assert(#sources==2)'))
    (source/'src/new.cpp').write_text('int extra(){return 42;}')
    run(cli,'build','-B',build,'-t','Unit')
    assert any(c['file'].endswith('new.cpp') for c in json.loads((build/'compile_commands.json').read_text()))
    current = (source/'axlum.build.lua').read_text()
    (source/'axlum.build.lua').write_text(current + '\nlocal generated=configure_header("generated/api/Config.hpp", {SDK_VALUE=42})\ntarget("Headers") kind("HeaderOnly") private_deps(generated) installheaders("@build/generated/api/Config.hpp") installheaderbase("@build/generated")\n')
    run(cli,'build','-B',build,'-t','Headers')
    run(cli,'install','-B',build,'--prefix',root/'install','-t','Headers','--no-build')
    assert (root/'install/include/api/Config.hpp').is_file()
    (source/'axlum.build.lua').write_text('workspace("Bad")\ntarget("Bad", {kind="ConsoleApp", typo=true})')
    assert 'unknown declarative setting' in run(cli,'configure','-S',source,'-B',build,ok=False)
print('PASS: DSL tables, scans, list helpers, discovery, CTest filtering/failure and incremental refresh')
