# Target support limits and existing native projects

## Target selection

```lua
target("DesktopTool")
    kind("ConsoleApp")
    platforms({"windows", "linux", "macos"})
    architectures({"x64", "arm64"})
    configurations({"Debug", "Release"})
    files("src/tool.cpp")
```

These target-scoped lists constrain the workspace/preset selection; omitted lists
inherit its range. `configuration`/`architecture` remain workspace declarations.
Only the supported 64-bit architectures and Windows/Linux/macOS/Android/iOS native
platforms are accepted. Lists reject empty values, duplicates and unsupported axes.

Unsupported targets and their modules are excluded before source validation and
planning. A Windows-only target may therefore refer to source files absent on a
Linux host. Active targets cannot depend on an excluded target/module/host tool;
configuration fails with the dependency and unsupported combination. Filters can
select a different dependency explicitly.

Explicit CLI or direct Ninja requests for an excluded target/alias produce a
target-specific diagnostic. Excluded targets do not enter default `all`. Matrix
entries with no eligible targets are skipped before compiler/SDK discovery; IDE
entries omit unavailable target/configuration pairs. Explicit configure with no
eligible targets reports the unsupported reasons. Existing states still require
separate build directories per configuration/architecture/toolchain.

## Existing vcxproj

```lua
target("ExternalMath")
    kind("StaticLibrary") -- SharedLibrary / ConsoleApp also supported
    platforms("windows")
    architectures("x64")
    project("external/math.vcxproj")
    external_configurations({Debug="Development", Release="Shipping"})
    external_architectures({x64="x64"})
    external_properties({MyProjectProperty="value"})
    public_includedirs("external/include")
```

The native target kind plus `project` selects delegation to MSBuild. An existing
csproj continues to use DotNetProject. The source project remains unmodified and
owns its source/PCH/documentation graph and compiler settings. Use `inputs` for
extra tracked files and ordinary public compile/link interfaces for consumers;
do not attach DSL sources/modules/PCH/documentation generation to that target.

Configuration names map from the root build configuration. The default platform
mapping is x64/ARM64; known 32-bit or ABI-changing mappings are rejected. Declared
ProjectConfiguration pairs are checked before generating the plan. MSBuild remains
responsible for evaluating imports and advanced project behavior.

A generated ForceImportBeforeCppTargets bridge confines output/intermediate paths
to the build tree, chooses the declared target name/extension/import-library/PDB,
and enforces the target's CRT contract. Model dependencies supply compile interfaces
and native link inputs. SDK project references retain their own names and use the
isolated output/intermediate root. Projects must honor the standard C++ targets
and output metadata; declared products are checked after successful execution.
The bridge validates ConfigurationType against the selected AxlumBuild kind.

Reserved output/configuration/bridge properties cannot be overridden with
external_properties. A project-specific preliminary import may be supplied via
the absolute `AxlumNativeBeforeTargets` property. Project code/custom SDK tasks
remain trusted project behavior. Additional SDK runtime files should be declared
through runtimefiles when they form part of a consumer/publication interface.

Native actions let MSBuild perform its own incremental checks and use Ninja restat
to avoid relinking unchanged consumers. Rebuild invokes SDK Rebuild. Clean invokes
SDK Clean for selected external-project dependency closures before Ninja clean,
including SDK-owned intermediates. Saved toolchain environments are supplied.
Windows paths passed to the SDK are normalized to long paths. Use a normal build
directory outside system TEMP: in the local SDK, the file tracker omitted outputs
inside TEMP and rebuilt the library on every invocation.

Visual Studio solutions reference the original vcxproj and its ProjectGuid, with
root configurations mapped to its actual configurations/platforms. No generated
wrapper replaces that project. The aggregate solution build uses the shared graph;
building the original project individually uses its own SDK settings. A project is
declared once, with mappings, rather than included multiple times under different
names. Other IDEs retain common graph task entry points; opaque MSBuild compiler
commands are not translated into compile_commands.json.

## Debug linking

`debuglink("Default"/"Full"/"FastLink")` exposes the remaining MSVC debug-link
policy. Enable `symbols` when selecting a debug-information mode. Other backends
reject this MSVC setting; actual switch availability follows the selected toolset.
Raw linkoptions remain available.

## Validation and comparison

The small [native-project example](../examples/native-project/README.md) exercises
mapped SDK builds, support selection, direct solution inclusion and isolated output.
`tests/TargetSelection.py` adds no-op/rebuild/clean, unsupported dependency/request
diagnostics and matrix skips without requiring an ARM64 SDK. No full regression or
NativeAOT run is part of this change. Platform validation limits are recorded
in [NATIVE_VALIDATION.md](NATIVE_VALIDATION.md).
