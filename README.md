# AxlumBuild

[Build and release](https://github.com/AxlumLab/AxlumBuild/actions/workflows/build-release.yml)
builds Windows x64, Linux x64 and macOS ARM64 on pushes and pull requests.
Version tags such as `v0.1.0` publish release archives and SHA-256 checksums after
all builds and minimal checks pass. Release tags must match the CMake and vcpkg
versions; each archive records the exact application and Rhodium commits.

Release builds enable `AXLUM_STATIC_RELEASE`: Windows uses the static CRT and
dependencies, Linux requires a static ELF without an interpreter or DT_NEEDED,
and macOS links dependencies statically while retaining OS-provided libraries.
CI checks the installed binary before packaging and includes `linkage.json`.

See [CLI commands](docs/CLI.md) for native/.NET publish and install, `run`, detailed
clean modes, CMake-style invocation, target inspection and Rhodium SDK diagnostics.

A standalone C++20 build orchestrator. It builds **C and C++ directly through Ninja** and coordinates **C# `.csproj` projects and DSL-defined C# targets through the official .NET SDK/MSBuild**. AxlumBuild itself bootstraps with CMake; projects built by AxlumBuild do **not** require CMake.

AxlumBuild is maintained by AxlumLab. Its native build graph, Lua DSL, embedded
package management and official .NET SDK integration share one target model.

## Requirements

- Bootstrap: CMake 3.25+, C++20 compiler, STL, Rhodium submodule and its native dependencies (vcpkg manifest included), CLI11. Optional Python 3 for integration tests.
- Native projects: Ninja 1.10+ and a complete Rhodium-discovered/registered toolchain:
  - Windows x64: Visual Studio C++ tools and Windows SDK; AxlumBuild discovers the installation and supplies its environment, including from ordinary PowerShell.
  - Linux x86_64: GCC or Clang and GNU `ar`.
- Bootstrapping AxlumBuild with CMake + MSVC/Ninja still requires an initialized compiler environment.
- Managed projects **only**: official .NET SDK 8+ on PATH (`dotnet`). C# support does not make AxlumBuild itself depend on .NET.
- Rhodium is linked as a library from `third_party/rhodium` (Git submodule). Its embedded library restores user-project packages; no Rhodium CLI installation is required. CLI11 handles the command-line interface.

> Toolchain selection and environment handling are documented in [docs/TOOLCHAINS.md](docs/TOOLCHAINS.md). Native option/PCH/runtime test coverage is recorded in [docs/NATIVE_VALIDATION.md](docs/NATIVE_VALIDATION.md).

## Build AxlumBuild

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build --output-on-failure
./build/axlum-build doctor           # Windows: .\\build\\axlum-build.exe doctor
```

Use a vcpkg triplet matching your compiler and CRT, for example `-DVCPKG_TARGET_TRIPLET=x64-windows-static` on Windows. CMake uses the pinned Rhodium submodule by default and disables its CLI, tests and installation. `-DAXLUM_USE_INSTALLED_RHODIUM=ON -DCMAKE_PREFIX_PATH=<rhodium-install>` is an alternative for an existing compatible library installation. Rhodium is not used to acquire AxlumBuild dependencies.

The root `CMakeLists.txt` keeps all dependency, target, test and installation
declarations together, with consistent four-space formatting and shared target
settings. The CLI and C++ test executables remain directly under `build/`;
`compile_commands.json` is enabled.
Install the CLI with `cmake --install build --prefix <destination> --component Runtime`.

CLI11 supplies `--help`, `--help-all`, `--version`, per-command help (`axlum-build configure --help` or `axlum-build help configure`), validated configuration/compiler values and command-specific options. `build`, `clean` and `rebuild` accept repeated `-t` / `--target` or positional targets. Build controls include `-j`, `--verbose`, `--why-rebuild`, `--logfile`, `--timing`, JSON reports and Chrome traces; see [diagnostics](docs/DIAGNOSTICS.md). `configure --toolchain <id>` selects a Rhodium profile; `doctor` lists detected profiles and missing requirements.

## Build a native project (no user-project CMake)

```sh
cd examples/static-and-exe
../../build/axlum-build configure -f axlum.build.lua -B .axlum/build --config debug
../../build/axlum-build build -B .axlum/build --target Hello
./.axlum/build/bin/Hello            # Windows: .axlum\\build\\bin\\Hello.exe
../../build/axlum-build graph -B .axlum/build
../../build/axlum-build clean -B .axlum/build --target Hello
```

`configure` validates the manifest, restores declared Rhodium packages, and writes `build.ninja`, `compile_commands.json`, `axlum.state`. `build` invokes Ninja, which owns C/C++ dependency tracking and incremental scheduling. `clean` delegates to `ninja -t clean`. For an independent Release build, use **a separate build directory**, for example `-B .axlum/release --config release`. Reusing a build directory with a different configuration or compiler is rejected.

### Minimal `axlum.build.lua`

```lua
workspace("Demo")
    configuration("Debug", "Release")
    architecture("x64")

target("Math")
    kind("StaticLibrary")
    language("C++")
    cppdialect("C++20")
    files({"src/Math.cpp", "include/**.hpp"})
    public_includedirs({"include"})

target("Hello")
    kind("ConsoleApp")
    files({"src/Main.cpp"})
    private_deps({"Math"})
```

Implementation headers and sources live together under `src/` by module (`core`, `model`, `manifest`, `graph`, `toolchain`, `backend/ninja`, `packages`, `cli`); CMake exposes `src/` as the include root.

Lua is the default manifest format. Rhodium supplies its Lua runtime, metadata, `include` and platform `when` helpers; AxlumBuild adds the build declarations. Every build, including direct Ninja execution, reloads Lua/includes, refreshes file globs and checks packages/toolchains. `module("Name")` supports scoped dependencies and `linktype("Monolithic"/"Modular")` binary planning with import/export API macros. See [the module example](examples/modules/README.md) and [configuration behavior](docs/MODULES_AND_RECONFIGURATION.md). Public/private dependency scopes and static link propagation are preserved. Compile settings declared before the first target become workspace defaults. `filter("configurations:Debug")` selects settings for the active configuration, and `filter({})` resets the filter. `removefiles` excludes matches from a source group.

Typed native settings now cover optimization, symbols, warnings/Werror, exceptions, RTTI, CRT, LTO, PIC and Windows linker policies. PCH is planned per target/module. Custom output/object/import-library paths and tracked transitive runtime copies are supported. See the runnable [native-features example](examples/native-features/README.md) and [DSL.md](docs/DSL.md).

Additional supported-platform policies include sanitizers, inlining/security/runtime
checks, compatible shared PCHs, Apple frameworks and Objective-C sources, iOS device/
simulator profiles, qualified cross-workspace module interfaces, mixed C#/native IDE
projects and Android NativeActivity application entries. See [parity-suite](examples/parity-suite/README.md)
and [the precise contracts and validation boundaries](docs/SUPPORTED_PLATFORM_PARITY.md).
Native architectures are x64/ARM64; UWP, GDK, other consoles and 32-bit targets are
explicitly unsupported.

The final parity additions include framework compiler interfaces, external PCH
inputs, C++14/Latest, MSVC native XML documentation and Lua/C++ IDE customizers.
See [the contracts](docs/FINAL_GAPS.md) and [the small runnable example](examples/final-gaps/README.md).

C/C++ header dependencies use GCC/Clang depfiles or MSVC `/sourceDependencies` JSON converted into Ninja depfiles, independent of the compiler's output language. `compile_commands.json` contains expanded compiler arguments for IDEs.

See [docs/DSL.md](docs/DSL.md) for the Lua API. Legacy TOML remains available through explicit `-f old.build.toml`; its subset is documented in [docs/MANIFEST.md](docs/MANIFEST.md).

## External code generation

See `examples/codegen-to-cpp` for real generated `.cpp` and `.hpp` files:

```lua
target("GenerateValue")
    kind("Codegen")
    tool("python3")
    args({"${workspace}/scripts/Generate.py", "${workspace}/source/value.txt",
          "${build}/generated/value.cpp", "${build}/generated/Value.hpp"})
    inputs({"scripts/Generate.py", "source/value.txt"})
    outputs({"generated/value.cpp", "generated/Value.hpp"})

target("CodegenHello")
    kind("ConsoleApp")
    files({"src/Main.cpp", "@build/generated/value.cpp"})
    includedirs({"@build/generated"})
    private_deps({"GenerateValue"})
```

Codegen executable may be an externally installed host tool or `@target/Generator` for an executable **built for the host**. In the latter case, declare the generator target as a dependency. Files produced by codegen are confined to the build directory; generator tool changes and declared inputs are tracked. `iridiumc` can be attached this way without adding reflection concepts to Core.

## C# via official SDK

See `examples/hello-dotnet`:

```lua
target("GameScripts")
    kind("CSharpLibrary")
    framework("net10.0")
    files({"Scripts/**.cs"})
    references({"Cobalt.Managed"})
    csdefines({"GAME_SCRIPTS"})
    nullable("Enable")
    warningsaserrors(true)
    assemblyversion("1.0.0.0")
```

`CSharpLibrary` / `CSharpConsole` generate SDK projects inside the build directory. References may name other managed targets, `.csproj` files, `.dll` files or assembly names. The requested framework must be installed; assembly names must resolve through MSBuild. For an existing project, use `kind("DotNetProject")` and `project("Managed/GameScripts.csproj")` instead of `framework` and `files`.

This runs `dotnet restore` and `dotnet build -c Debug|Release --no-restore` in a separate Ninja target. The .NET 8+ SDK `UseArtifactsOutput=true` and `ArtifactsPath` properties place artifacts under `<buildDir>/dotnet/<target>/artifacts/`, with a per-project name directory that avoids collisions between `ProjectReference` outputs. `.csproj` may opt in to generated C# files with MSBuild property `$(AxlumGeneratedDir)`, which points at `<buildDir>/generated/`.

MSBuild resolves NuGet and project-internal references. AxlumBuild does not parse/compile C# itself. Because MSBuild owns the managed project graph, the managed action runs when requested and lets MSBuild decide whether recompilation is necessary. It preserves a stamp across no-op managed builds and uses Ninja `restat` for downstream actions. The DSL also exposes analyzers/source generators, diagnostic policies, compiler options, assembly attributes and NuGet references. See [managed-dsl](examples/managed-dsl/README.md) and [the .NET API](docs/DOTNET.md).

`publish` uses the official `dotnet publish` pipeline, including framework-dependent,
self-contained and NativeAOT publication. NativeAOT console and shared-library
outputs support RID selection, native export/dependency validation and SHA-256
publication manifests. Publication stages outputs before updating the final directory;
failed validation preserves the previous publication. Only the official .NET SDK
pipeline is supported; there is no Mono backend.

```powershell
build/axlum-build.exe configure -f examples/dotnet-publish/axlum.build.lua -B build/publish --config Release
build/axlum-build.exe publish -B build/publish -t AotHello --rid win-x64 --aot
build/axlum-build.exe publish -B build/publish -t AotLibrary --rid win-x64 --aot --native-lib Shared
```

See [dotnet-publish](examples/dotnet-publish/README.md) for native interop and toolchain requirements.

## Where Rhodium fits

Rhodium is pinned by Git submodule at `third_party/rhodium`. AxlumBuild calls its C++ restore and artifact APIs directly during configure; no Rhodium subprocess is used. `rhodium.require` declares Git/archive dependencies; `rhodium.require_local` declares local packages. Attach them with `packages` (private) or `public_packages` (re-exported). `version` aliases Git `ref`; `source` is explicit because Rhodium has no registry.

- [Offline local packages](examples/rhodium-local/README.md): header-only + CMake static package, transitive compile/link interfaces.
- [Git fmt package](examples/rhodium-fmt/README.md): fetch fmt 10.2.1 and build/run a real consumer.

`configure --locked` validates dependency recipes against `tpkg.lock.toml`; `.rhodium/` holds local state. Local package source changes are checked automatically before builds. Packages may use CMake internally. External libraries can also use `includedirs`, `libdirs` and `links` directly. See [docs/EXTENSIONS.md](docs/EXTENSIONS.md).

## Tests and handoff

- `tests/ManagedSdk.py`: real SDK compilation, source generators, nullable/diagnostic policy, metadata, no-op/rebuild and offline NuGet analyzer restore.
- `tests/DotNetPublish.py`: real NativeAOT executable and C-export DLL, native interop/import validation, hashes, publication failure recovery and standard managed publication.
- `tests/Diagnostics.py`: jobs/multiple targets, scoped rebuild, failure logs, why/verbose, no-op/dry-run and JSON/trace reports.
- `tests/ModulesReconfigure.py`: module graphs/binary grouping, import/export interfaces, automatic Lua/glob/package/toolchain refresh, stale outputs, RC/icon/plugin policy.
- `tests/NativeFeatures.py`: real MSVC discovery without Developer environment, options, PCH/increments, resources/manifest, custom outputs, runtime copies and profile invalidation.
- `tests/NativePlan.cpp`: MSVC/GCC/Clang native planning, per-module PCH isolation, output confinement and runtime collisions.
- `tests/Packages.py`: offline local/Git restore, compiled package linking, private/public/transitive interfaces, lock validation and configuration isolation.
- `tests/CliDsl.py`: CLI11 help/errors, Lua includes, globbing, diagnostics, generated SDK projects and managed references.
- `tests/Unit.cpp`: graph validation, cycles, unknown target, repeated dependencies, path and Ninja escaping.
- `tests/Integration.py`: real build/link/run, modules, public/private interface, GCC depfiles, incremental rebuild, deliberate failure and clean, C/C++ mixed toolchain, codegen, manifest diagnostics. Tests create temporary workspaces so **no `bin`/`obj` files are written to examples**.
- `docs/CODE_STYLE.md`: naming, formatting and AxlumLab file headers.
- `docs/NATIVE_VALIDATION.md`: native feature validation and platform limits.

## IDEs, workspaces and matrix builds

```powershell
build/axlum-build.exe ide -f examples/workspace-suite/axlum.build.lua -B build/suite-projects --format all
build/axlum-build.exe matrix -f examples/workspace-suite/axlum.build.lua -B build/suite-matrix
```

Generated projects cover VSCode workspace/tasks/launch/IntelliSense, Visual Studio
Makefile sln/vcxproj, Xcode external-build projects/schemes, and Android Studio
Gradle tasks plus an optional NDK/CMake library bridge. All use the shared target
model and AxlumBuild/Ninja execution. See [IDE documentation](docs/IDE_PROJECTS.md).

`workspace_reference` composes manifests through alias-qualified native targets.
`prebuild` and `postbuild` use declared inputs/outputs for incremental execution.
Named configurations, presets, x64/ARM64 selection and isolated matrix directories
are documented in [workspaces/actions/matrix](docs/WORKSPACES_AND_ACTIONS.md).
[workspace-suite](examples/workspace-suite/README.md) is a real native example;
[cross-library](examples/cross-library/README.md) includes Android ARM64 compilation.

[native-project](examples/native-project/README.md) imports an existing vcxproj
with configuration mappings and target platform/architecture/configuration limits.
See [the contract](docs/TARGETS_AND_NATIVE_PROJECTS.md).
