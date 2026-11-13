# Lua build scripts

The default file is `axlum.build.lua`. `configure -f path.lua` selects another script. The Lua VM and the original `include`, `when` and package metadata bindings come from the pinned Rhodium submodule. AxlumBuild owns the build declarations below; Rhodium itself is unchanged.

```lua
workspace("Cobalt")
    configuration("Debug", "Release")
    architecture("x64")

target("CobaltRuntime")
    kind("SharedLibrary")
    language("C++")
    cppdialect("C++20")
    files({"src/**.cpp", "include/**.hpp"})
    public_includedirs({"include"})

target("GameScripts")
    kind("CSharpLibrary")
    framework("net10.0")
    files({"Scripts/**.cs"})
    references({"Cobalt.Managed"})
```

`Cobalt.Managed` in this example must be an assembly MSBuild can resolve. A local managed target of that name is also supported. NuGet dependencies belong in an existing `.csproj`.

## Workspace and files

- `workspace("Name")` declares the workspace once, before other declarations. `configuration` and `architecture` belong to workspace scope.
- `configuration("Debug", "Release")` or `configuration({"Debug", "Release"})` restricts allowed configurations. Debug, Development and custom configuration names are supported. IDE/matrix defaults are Debug/Release when no list is declared. Select one with `configure --config <name>` or a preset.
- `architecture("x64")` or `architecture({"x64", "arm64"})` declares allowed target architectures. Select with `--arch`, `--platform` and optional `--toolchain`.
- `include("relative.lua")` uses Rhodium's include handling: paths are relative to the including script and must stay in the workspace. Recursive/missing includes fail.
- `when("windows", function() ... end)` uses Rhodium's platform conditions (`all`, `*`, `unix`, `windows`, `linux`, `mac`, `macos`, `android`, `ios`). Conditions use the selected target platform; actual builds still require a supported toolchain.
- File and include paths are relative to the workspace root, including declarations in included scripts.
- `files({...})` accepts explicit paths and workspace-relative globs: `*` and `?` stay in one directory, `**` crosses directories, `**/` can match zero directories. Matches are sorted and deduplicated within each source group. Globs skip `.git`, `.axlum`, `.cache`, `.rhodium`, `.tpkg`, `build`, `obj` and `bin` directories. Generated paths use explicit `@build/...` values. Matches are refreshed automatically before each build.
- `removefiles("src/tests/**.cpp")` (or a string array) excludes files from the current target/module after expansion, including files added later in the script. Overlapping glob/explicit matches in a source group are deduplicated by normalized path.
- Headers in native `files` lists do not become compile actions. C# targets select `.cs` files. Native compilation supports `.c`, `.cc`, `.cpp`, `.cxx` and `.C`.

## Targets and compile settings

`target("Name")` starts a target. `kind(...)` accepts `ConsoleApp`/`Executable`, `StaticLibrary`, `SharedLibrary`, `HeaderOnly`, `DotNetProject`, `CSharpLibrary`, `CSharpConsole`, `Codegen`. Names are unique, begin with a letter/underscore and contain letters, digits, `_` or `-`.

| Function | Meaning |
| --- | --- |
| `language("C"/"C++"/"C#")` | Validate language spelling; native language is selected per source extension |
| `cppdialect("C++20")` | C++14, C++17, C++20, C++23, Latest/C++Latest (default C++20) |
| `cdialect("C17")` | C11, C17 (default C11) |
| `includedirs({...})`, `defines({...})` | Private compile settings |
| `public_includedirs({...})`, `public_defines({...})` | Compile settings also exposed to consumers |
| `cflags({...})`, `cxxflags({...})` | Compiler-specific flags |
| `links({...})`, `libdirs({...})`, `linkoptions({...})` | External libraries, search directories and linker flags |
| `public_deps({...})`, `private_deps({...})`, `interface_deps({...})` | Dependencies on other workspace targets |

List functions accept a single string or a contiguous array of strings, and append values. Non-string entries and keyed tables are rejected. Public and private dependencies provide their public compile interface to the target; public/interface dependencies are re-exported. Static-library link dependencies propagate in dependency order. Scope duplication and cycles fail validation.

Compile settings (`cppdialect`, `cdialect`, include directories, defines, C/C++ flags) and `language` declared before the first target become defaults inherited by each target at declaration time. Target settings override dialect/language or append to inherited lists. Link settings, files, dependencies, package attachments, kind, framework and generator settings require target scope.

`filter("configurations:Debug")` or `filter({"configurations:Release", "system:windows"})` selects subsequent settings for the active build. Terms are combined with AND. Supported keys are `configuration`/`configurations`, `system`/`platform` (windows/linux/macos/android) and `architecture` (x64/x86_64/arm64/aarch64). Invalid keys/values fail immediately. `filter({})` restores unconditional settings. Starting a target or module resets its filter. Filtered workspace defaults are evaluated for the selected CLI configuration; `graph` uses the configuration stored in `axlum.state`. The selected toolchain provides actual native/cross compilation.

`dependson({...})` aliases `private_deps`. `packages` and `public_packages` are described below.

## Native compile/link options

These are typed native settings and participate in workspace defaults, filters and target/module overrides. Boolean settings accept `true`/`false` or `"On"`/`"Off"`. Raw `cflags`, `cxxflags`, `linkoptions` remain escape hatches; raw compiler flags can override a typed setting.

| Function | Values / effect |
| --- | --- |
| `optimize(...)` | Off, On, Speed, Size, Full; defaults to Off in Debug and Speed in Release |
| `symbols(...)` | On, Off, Full; defaults to On in Debug and Off in Release; MSVC uses embedded object debug info and declares the linker PDB as an output |
| `warnings(...)` | Off, Default, Extra, All; maps to MSVC warning levels or GCC/Clang warning groups |
| `warningsaserrors(...)` | Enable/disable warning-as-error enforcement |
| `exceptions(...)`, `rtti(...)` | C++ exception and RTTI policy, default On |
| `lto(...)` | Compile and link LTO, default Off; MSVC archives also receive LTCG policy |
| `runtime(...)` | Static/Dynamic; default Dynamic; MSVC selects MT/MD with configuration-appropriate debug CRT; GCC/Clang Static adds static standard-runtime link flags |
| `staticruntime(...)` | Boolean alias for Static/Dynamic runtime |
| `pic(...)` | Position-independent code on GCC/Clang; defaults On for shared-library targets |
| `sanitizers({...})`, `inlining(...)` | Sanitizer selection and independent inline policy |
| `functionlevellinking`, `runtimechecks`, `stringpooling`, `intrinsics`, `buffersecuritycheck`, `debugcrt` | Boolean compiler policies with backend/conflict validation |
| `frameworks({...})`, `frameworkdirs({...})` | Apple framework names/paths and search paths; target scope |

`incrementallink(bool)`, `subsystem("Console"/"Windows")`, `entrypoint("symbol")`, `moduledefinition("exports.def")`, `win32manifest("app.manifest")` and `delayloaddlls({...})` are target-level linker settings. Incremental linking defaults Off and cannot be combined with LTO. Windows-specific settings require the MSVC backend; executable/shared-library settings cannot be applied to static libraries. Module CRT policy must match its MSVC target; module LTO requires target LTO. The raw flags mechanism does not provide automatic conflict detection for arbitrary compiler switches.

## Precompiled headers

```lua
pchheader("include/Pch.hpp") -- may be a workspace default
target("Runtime")
    kind("SharedLibrary")
    files({"src/Runtime.cpp", "src/Pch.cpp"})
    pchsource("src/Pch.cpp") -- optional explicit C++ creator
target("Tool")
    kind("ConsoleApp")
    files("src/tool.cpp")   -- automatic creator
target("Plain")
    kind("ConsoleApp")
    files("src/plain.cpp")
    pch(false)              -- disable an inherited PCH
```

Each target/module compilation group gets a PCH with the same C++ standard, flags, definitions and include interface as its consumers. `sharedpch(true)` reuses compatible automatic/manual creators when full argv, inputs and prerequisite contracts match; differing automatic contracts stay isolated, and incompatible explicit shared creators fail. Native C, Objective-C/Objective-C++ and RC resources never consume C++ PCH. MSVC uses Yc/Yu/FI/Fp and links the creator object once per binary; GCC uses a generated adjacent `.gch`, with invalid-PCH errors enabled; Clang uses `-include-pch`. Compiler dependency files track header changes, and Ninja command hashes handle option changes.

`pchsource` is target/module scope only. A target's explicit creator belongs to its primary source group and is compiled once; other modules get automatic creators unless they supply their own. An explicit creator need not also appear in `files`. The module name `root` is reserved when the target already has primary sources. `pch(true)` requires a header; `pchsource` must name a C++ translation unit.

`pchfile("prebuilt/pch.pch")` consumes an external PCH instead of generating one;
`pchobject("prebuilt/pch.obj")` optionally links its MSVC creator object. Both are
tracked inputs, including `@build/` producers. `pchfile` and `pchsource` cannot be
combined. GCC requires the adjacent `<pchheader>.gch` path. Producers and consumers
must use a compatible compiler/dialect/options/ABI; compiler diagnostics validate
compatibility. See [FINAL_GAPS.md](FINAL_GAPS.md) for exact contracts.

`cppdocumentation(bool)` enables tracked MSVC XDC files and an xdcmake merge into
XML; `documentationdir("api-docs/${target}")` selects the output directory under
the build tree. The default is `docs/<target>`. Documentation participates in
target completion, increments and clean. C# uses `documentationfile` separately.

## Native outputs and runtime dependencies

- `targetname("filename_base")` changes the output basename without changing its graph target name. Platform prefixes/extensions are still applied.
- `targetdir("products/${config}/${target}")` changes the primary output directory, relative to the selected build directory.
- `objdir("objects/${config}/${target}")` changes the object/PCH base directory; module subdirectories are appended.
- `implibdir("imports/${config}")` changes the MSVC shared-library import-library/EXP directory.
- Output paths also accept `${build}` and `${workspace}` substitutions and `@build/`, but must remain under the selected build directory. Paths escaping it, duplicate outputs and runtime basename collisions are errors.
- `runtimefiles({...})` declares required file paths; `optionalruntimefiles({...})` skips absent files at configure time. These take explicit paths, not directory/glob deployment rules. Paths are workspace-relative or `@build/...`, with the same substitutions. Optional files are rediscovered before each build; removed files lose their copied outputs.
- Executable/shared targets gather runtime files transitively from native dependencies and imported packages. Shared-library dependencies are copied to the consumer directory when needed. Packages export `bin_files`, runtime binaries in `bin_dirs`, and shared `lib_files` through the same mechanism. Copies preserve timestamps on identical content and participate in incrementality, target builds and clean. Host tools used by codegen wait for their copied dependencies too.
- GCC/Clang shared outputs use a SONAME and the loader searches `$ORIGIN`, so a copied library can run from the consumer directory. This is a build-tree runtime layout, not a general install/package/signing system.
- `resources({...})` or `win32resourcefile(...)` adds Windows `.rc` sources (`files` also accepts `.rc`). Literal quoted includes and ICON/BITMAP/CURSOR/RCDATA/HTML/MANIFEST/font/type-library file resources are tracked recursively. Use `resourceinputs({...})` for macro-generated or conditional filenames the scanner cannot resolve. Resource compilation uses detected `rc.exe` and the SDK environment.
- `win32icon("app.ico")` generates a tracked RC input with icon resource ID 1. Avoid declaring that same icon ID in another RC source.
- `exports({"entry", "other=internal"})` adds explicit MSVC DLL exports. `moduledefinition` supports an export definition file; source annotations remain supported. Automatic scanning/exporting of all object symbols is not provided.
- `importlibrary(false)` supports an MSVC plugin DLL without an import-library contract, including a DLL with no exports. Such a DLL cannot be a native link dependency; load it at runtime. Shared libraries default to an import library and must export symbols. PDB/EXP/ILK files for enabled policies are declared outputs.

The output and typed linker APIs apply to native targets. Managed SDK artifact layout remains controlled by the .NET path below. For a complete runnable example, see [native-features](../examples/native-features/README.md). Recursive source globs exclude the selected build directory and existing Axlum/CMake build directories, preventing generated PCH sources from being reimported as user sources.

`module("Name")` declares a module owned by the current native target. Files, compile settings, `public_deps` and `private_deps` then affect that module. Dependencies accept local names and qualified `Target/Module` or `alias::Target/Module` names. Targets import module interfaces with `public_modules`/`private_modules`. `linktype("Monolithic")` merges module objects into the owner. `linktype("Modular")` groups modules into shared libraries using `binarymodulename("Core")`. `apidefine("CORE_API")` supplies an import/export annotation for public declarations. See [module planning](MODULES_AND_RECONFIGURATION.md) and [supported-platform additions](SUPPORTED_PLATFORM_PARITY.md). Target-level kind/link/output/framework settings must precede module declarations.

## C#

`CSharpLibrary` and `CSharpConsole` take `framework("net8.0")` (or another installed TFM), `files` and `references`. Configure writes SDK-style `.csproj` files under `<build>/dotnet/<target>/`. `references` accepts managed target names, `.csproj` paths, `.dll` paths and MSBuild assembly names. References to managed targets also create build dependencies and are cycle-checked. A source file can belong to only one target/module.

Use `kind("DotNetProject")` and `project("Managed/Existing.csproj")` for a pre-existing project. Its own project file controls sources, references and framework; do not combine `project` with `framework`, `files`, `references` or generated-project compiler settings. Managed-only settings on native targets and incompatible language/kind combinations fail during configure. Generated SDK projects survive `clean`, allowing a subsequent build without reconfiguration.

Generated C# targets support `csdefines`, `nullable`, `cslangversion`, unsafe/overflow policy, optimization/debug settings, analyzers/source generators and their inputs, diagnostic policies, NuGet references and assembly metadata/attributes. Managed workspace defaults and filters apply to these settings. `publish` exposes official SDK publication and NativeAOT with RID/output options, native export/dependency checks and publication manifests. There is no Mono backend. Full signatures and examples are in [DOTNET.md](DOTNET.md); build controls and reports are in [DIAGNOSTICS.md](DIAGNOSTICS.md).

## Code generation

`kind("Codegen")` accepts `tool("python3")`, `args({...})`, `inputs({...})`, `outputs({...})`, `depfile("relative.d")`. Arguments support `${workspace}`, `${build}`, `${config}`. Outputs and depfiles stay under the build directory. `tool("@target/Generator")` selects a host executable target which must also be a declared dependency. Generated native/C# source paths use `@build/generated/File.cpp`/`.cs`.

## Embedded Rhodium packages

Rhodium remains a Git submodule library dependency of AxlumBuild. Its package manager is embedded: configure calls `DependencyResolver` and reads structured `PackageArtifactResolver` data. No Rhodium CLI or generated CMake import script is used as an intermediary.

```lua
rhodium.require("fmt", {
    source = "https://github.com/fmtlib/fmt.git",
    version = "10.2.1", -- alias for ref, not a version range/registry lookup
    build = "header_only",
    artifacts = {include_dirs = {"include"}, defines = {"FMT_HEADER_ONLY=1"}}
})

rhodium.require_local("sample_math", {
    path = "packages/math",
    build = "cmake",
    linkage = "static",
    artifacts = {include_dirs = {"include"}, lib_dirs = {"lib"}, libs = {"sample_math"}}
})

target("App")
    kind("ConsoleApp")
    files("src/Main.cpp")
    packages({"fmt", "sample_math"})
```

- Global `require` / `require_local` are equivalent to the namespaced forms. Namespaced/global `dependency_overrides({name="local/path"})` reuse Rhodium overrides. Package requirement declarations and overrides respect the active filter.
- Package recipe fields (`source`, `source_type`, `ref`, `path`, `build`, `cmake`, `artifacts`, `dependencies`, archive hashes/patches, etc.) are parsed by Rhodium. `version` is an AxlumBuild convenience alias for `ref`; conflicting values fail. Git/source declarations need explicit sources. Rhodium has no registry or semantic-version solver.
- `packages` gives the current target a private package interface. `public_packages` also exposes headers/defines to consumers. Static-library link dependencies propagate even when compile settings are private. `dependencies` names other declared packages and supplies their transitive interfaces; it does not invent missing recipes or CMake targets inside a package build.
- Missing packages, duplicate names/scope attachments, invalid recipes and dependency cycles fail. Native packages cannot be attached to managed targets; use MSBuild/NuGet for managed dependencies.
- `configure` restores source/artifacts using Rhodium. Each build (including direct Ninja execution) checks the configuration and restores changed packages before executing actions; `graph` also refreshes the configuration. Lua/includes, globs and local package source changes do not require a separate configure invocation.
- `configure --locked` requires dependency inputs to match `tpkg.lock.toml`. Local sources remain mutable: locked mode validates recipe/toolchain/configuration identity, not local file contents. Commit lockfiles, not `.rhodium/` caches. Existing `.tpkg/` state is honored by Rhodium.
- Package builds use the same discovered Rhodium profile and configuration as native actions. Windows packages match their consumers' Static/Dynamic CRT policy, and conflicting consumers or recipes are rejected. Debug/Release artifacts have distinct identities. This is not a complete compiler/SDK ABI compatibility validator.
- Structured include dirs, defines, library dirs, concrete library files, system libraries and Apple frameworks enter the native graph. Exported package DLL/SO runtime files are copied beside consuming native binaries. Framework metadata is supported on macOS/iOS toolchains.

See [the offline local example](../examples/rhodium-local/README.md) and [the Git fmt example](../examples/rhodium-fmt/README.md). CLI11 and Rhodium's own bootstrap dependencies still come from CMake/vcpkg, independently of user-project packages.

## IDEs, project references and action graphs

Native installation uses `installheaders`, `installheaderbase`, `installfiles`,
`installbindir`, `installlibdir`, `installincludedir`, `installdatadir` and
`installable`; see [CLI.md](CLI.md) for components and target ownership.

Targets can restrict eligibility with `platforms("windows", "linux")`,
`architectures("x64", "arm64")` and `configurations("Debug", "Development")`.
Dependencies on unavailable targets fail with the selection reason; matrix entries
with no eligible targets are skipped before toolchain discovery.

Existing native SDK projects use a native `kind` plus `project("path.vcxproj")`.
`external_configurations`, `external_architectures` and `external_properties`
configure MSBuild delegation. `debuglink("Default"/"Full"/"FastLink")` selects
the MSVC debug-link policy. See [target selection and native projects](TARGETS_AND_NATIVE_PROJECTS.md)
for mappings, isolated outputs, clean/rebuild and direct solution inclusion.

See [IDE_PROJECTS.md](IDE_PROJECTS.md) for `ide --format vscode|vs|xcode|android-studio|all`. [WORKSPACES_AND_ACTIONS.md](WORKSPACES_AND_ACTIONS.md) specifies `workspace_reference`, alias-qualified dependencies, `prebuild`/`postbuild`, presets, custom configurations and `matrix`.

`ide_customize(format, {...})` adds workspace/target IDE properties, debugger
settings, format-specific fragments and VSCode JSON patches. Filters apply.
Embedders can pass `IDEProjectCustomizer` callbacks to `IDEGenerator::generateIDE`. Signatures
and examples are documented in [FINAL_GAPS.md](FINAL_GAPS.md).
