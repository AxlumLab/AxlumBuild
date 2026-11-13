# CLI, installation and execution

```powershell
axlum-build -S examples/cli-lifecycle -B build/demo -G Ninja --config Release
axlum-build --build build/demo --target App --parallel 4
axlum-build build -B build/demo --target App --clean-first
axlum-build run -B build/demo App --env AXLUM_RUN_VALUE=demo -- "two words"
axlum-build publish -B build/demo --target App --prefix dist
axlum-build --install build/demo --prefix dist --component development
axlum-build uninstall --prefix dist --component development --dry-run
axlum-build targets -B build/demo --json
axlum-build info -B build/demo --json
axlum-build doctor --workspace . --json
```

`configure`, `build`, `install`, `-S/-B`, `--build <directory>` and
`--install <directory>` follow CMake's command shape. The generator is Ninja;
arbitrary CMake generators, scripts and `-D` variables are not interpreted.
Build directories retain a single configuration; `--config` on an existing build
validates that configuration. `--parallel` aliases `--jobs`; `--clean-first`
uses the existing rebuild flow.

## Publish/install

`publish` prepares selected targets and installs native outputs into `--prefix`
(default `<B>/publish`). `install` consumes existing native outputs; use
`--build-first` to prepare them. `--no-build` skips graph preparation.
Repeat `--target` to select multiple targets, including workspace aliases.
Omission selects all workspace targets; native dependencies follow their consumers.
Rhodium cache targets are not installed as independent development libraries;
declared package runtime files are included with consuming binaries.

Executables, DLLs and runtime files go to `bin`; native/static/import libraries
go to `lib`. `--component runtime|development|symbols|all` selects categories.
PDB/debug companions belong to `symbols`. Native `--strip-symbols` omits separate
symbol files; it does not rewrite the binary. .NET-only switches on a native-only
selection are rejected.

```lua
target("Library")
    kind("SharedLibrary")
    files("src/Library.cpp")
    public_includedirs("include")
    installheaders("include/**.hpp")
    installheaderbase("include")
    installfiles("resources/Notice.txt")
    installdatadir("share/MyLibrary")
```

Header globs preserve paths relative to `installheaderbase` (default `include`).
`installbindir`, `installlibdir`, `installincludedir`, `installdatadir` customize
relative destinations. `installable(false)` suppresses that target's installation.
Runtime dependencies remain required by installable consumers.

`axlum.install.json` records relative paths, components, target owners and SHA-256
hashes. Inputs and filename collisions are checked before copying. Reinstall keeps
unrelated files and removes obsolete owned files for selected targets/components.
Identical runtime files can have multiple owners. `uninstall` removes recorded
files only, retaining files still owned by another target and refusing to remove
modified files. `--dry-run` previews operations without building/copying/removing;
manifest/package/toolchain configuration checks still run.

Managed-only `publish` without `--prefix` preserves the official SDK output layout,
`-o` and NativeAOT/RID controls. `install` or `publish --prefix` uses official
`dotnet publish` in an isolated staging directory and installs it under
`bin/<target>`. The SDK may compile/restore even when graph preparation is skipped.
There is no Mono backend. See [DOTNET.md](DOTNET.md).

## Run

`run` builds and launches a native or managed executable; `--no-build` uses the
existing output. Omit the target only when one declared executable is available.
Arguments after `--` are forwarded verbatim. `--cwd` changes the working directory
(default: executable directory); repeat `--env NAME=VALUE` to override environment
values. Native runtime search paths are supplied from configured outputs.
Managed executables use the official dotnet host and isolated SDK artifacts.
Ambiguous managed outputs and incompatible native cross targets are rejected.
The CLI returns the application exit code.

## Clean

| Mode | Removes |
| --- | --- |
| `build` (default) | Ninja products/intermediates, native SDK clean outputs and owned managed build artifacts |
| `intermediates` | Objects, PCH/dependency and SDK intermediates; retains final binaries |
| `publish` | Recorded installations/publications inside this build directory |
| `cache` | Generated Ninja/configuration state; configure again afterward |
| `all` / `--all` | Build products, internal publications and configuration state |

`--target` scopes build/intermediate/publication cleaning. Dependency closures are
included unless `--no-dependencies` is supplied. `--dry-run` lists removals without
SDK clean or deletion. Cache/all cleaning rejects target selection before removal.
External installations require `uninstall --prefix`; clean never removes them.
Rhodium package/source caches and unrelated files are preserved.

## Doctor and API layout

Doctor displays Rhodium ToolchainDetector/ToolchainRegistry profiles directly:
compiler/SDK versions, roots/sysroots, Android SDK/NDK/JDK and missing requirements.
There is no separate AxlumBuild PATH inventory or SDK search implementation.
`--workspace` loads that workspace's Rhodium profile overrides; `--strict` fails
when no supported complete profile exists. JSON identifies Rhodium as provider.
This reports SDK kinds exposed by Rhodium's native API; it does not invent .NET
workload profiles.

`axlum::Application` owns CLI parsing, dispatch and internal commands; `Main.cpp`
only constructs it and calls `run`. Public AxlumBuild C++ APIs use `namespace axlum`.
Former header-level free functions are static class methods, including `BuildModel`,
`ManifestLoader`, `BuildPlanner`, `ToolchainDiscovery`, `PackageManager`, `FileSystem`,
`Process`, `BuildExecutor`, `BuildOperations`, managed builders and backend generators.
Embedders must update their source references; no free-function compatibility aliases
are retained. Source/header filenames use PascalCase.
