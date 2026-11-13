# Workspaces, actions and matrix entries

## Project references

```lua
workspace("Application")
    configuration("Debug", "Development", "Release")
    architecture({"x64", "arm64"})
    workspace_reference("Common", {file="../common/axlum.build.lua"})

target("App")
    kind("ConsoleApp")
    files("src/Main.cpp")
    private_deps("Common::Utility")
```

The referenced manifest declares its own workspace/targets/modules and relative
paths. Composition rebases sources, include paths, PCH/resource files, native
library paths, tools, managed references, runtime inputs and package recipes.
`${workspace}` in a referenced action means that project's source root;
`${build}` and `@build/` mean its isolated subtree under `projects/<alias>/`.
Public/private native interfaces and binary artifacts keep their ordinary target
semantics. The root owns the selected toolchain/configuration and combined package
lock/cache; referenced manifests must accept that configuration. DSL package
overrides are namespaced; per-child mutable toolchain/cache state is not imported.

Public spelling is `Common::Utility`; internal target names use `Common__Utility`.
CLI `build -t Common::Utility` works. Nested aliases compose similarly. Canonical
manifest identity deduplicates diamond references; alias build entry points still
refer to the shared target. Duplicate aliases, target-name collisions, manifest
reference cycles, and final target dependency cycles fail explicitly. A source
file still cannot be compiled by two independently declared native targets.
This is explicit project composition, not a global engine-module registry.

## Incremental actions

```lua
target("Library")
    kind("StaticLibrary")
    files("src/Library.cpp")
    includedirs("@build/generated")
    prebuild("Generate", {
        tool="python",
        args={"${workspace}/Generate.py", "${build}/generated/config.hpp"},
        inputs={"Generate.py", "config.json"},
        outputs={"generated/config.hpp"}
    })
    postbuild("Publish", {
        tool="python",
        args={"${workspace}/publish.py", "${targetfile}", "${build}/publish/library.bin"},
        inputs={"publish.py"}, outputs={"publish/library.bin"}
    })
```

Actions take a name, executable `tool`, argument array, explicit inputs/outputs,
and optional GCC-style `depfile`. Empty outputs and duplicate action names fail.
Output/depfile paths stay inside the build directory. Strings are arguments, not
implicit shell programs; invoke an explicit shell only when the action needs one.

Declared pre-action outputs are normal dependencies of the owner's build actions,
including its modular binary groups. Multiple pre-actions execute in declaration
order. Post-actions depend on the owner's artifact/runtime outputs and previous
post-actions. Completion files participate in dependent action invalidation, including transitive HeaderOnly/interface dependencies. Host
`@target/Tool` action tools include that tool's produced runtime/completion files.
Cross builds must use an external host tool; building a second host graph inside
the target graph is not implemented.

Ninja executes actions only when inputs/commands change or outputs disappear.
`restat` is enabled; tools should preserve unchanged output timestamps. Failures
stop dependents and the wrapper removes partial declared outputs. Avoid modifying
the linker's own artifact in place from a post-action: output ownership is unique;
publish a separate output. `codegen` remains useful for reusable generated targets.

## Configurations and presets

```lua
configuration("Debug", "Development", "Release", "Profile")
architecture({"x64", "arm64"})
preset("dev", {config="Development", architecture="x64"})
preset("android-arm64", {
    config="Release", architecture="arm64", platform="android",
    toolchain="android-clang-arm64"
})
```

Configuration names accept letters, digits, `_` and `-`, starting with a letter.
Debug keeps debug defaults. Development uses optimized native defaults with
symbols; other custom names use release defaults. `filter("configurations:Profile")`
sets project-specific policies. Native CRT defaults and Rhodium's debug/release
artifact buckets follow the debug-versus-nondebug policy; recipe/toolchain hashes
still distinguish package inputs. Managed builds pass the selected name to SDK
builds. This does not invent arbitrary MSBuild configuration properties.

`architecture` accepts a string or array, with x64/x86_64 and arm64/aarch64 aliases.
Target `filter` and Rhodium `when` use the requested platform; architecture filters
use the requested architecture. Presets accept `config`, `architecture`, `platform`,
`toolchain`, and `compiler`. Preset selection supplies those values as a unit.

```powershell
build/axlum-build.exe configure -f examples/workspace-suite/axlum.build.lua -B build/dev --preset dev
build/axlum-build.exe matrix -f examples/workspace-suite/axlum.build.lua -B build/matrix --preset dev --preset release
build/axlum-build.exe matrix -f examples/cross-library/axlum.build.lua -B build/cross --preset android-arm64
```

`matrix` defaults to all named presets; without presets it expands declared
configurations × architectures. Each entry has its own directory/environment/
compiler database/objects/outputs. `--configure-only` skips execution; `-t` selects
a target in each entry. An unavailable entry fails with a toolchain diagnostic.

Toolchain families cover MSVC Windows, GCC/Clang Linux, Apple Clang macOS, and NDK
Clang Android, with x64/ARM64 selection. Windows and Android paths have real
execution coverage; Apple/Linux cross paths still need platform execution checks.
Android SDK/NDK/API discovery hints persist in the configured environment, so
later builds need not repeat the initial discovery shell variables. Changes
explicitly provided by the caller take precedence. This does not implement iOS,
UWP/GDK or general mobile application deployment.

See [workspace-suite](../examples/workspace-suite/axlum.build.lua) and
[IDE generation](IDE_PROJECTS.md).
