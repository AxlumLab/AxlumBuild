# .NET SDK builds and publication

AxlumBuild uses `dotnet restore`, `dotnet build` and `dotnet publish` from the
official SDK. SDK 8+ is required for isolated artifact output. Source-root
`global.json` SDK selection is honored for generated projects; existing projects
use their own directory. There is no Mono runtime selector, Mono AOT pipeline or
direct invocation of legacy private ILC tooling. NativeAOT's compiler/runtime
packages are selected and invoked by the .NET SDK itself.

## Managed DSL

`CSharpLibrary`/`CSharpConsole` generate SDK projects. `DotNetProject` plus `project`
delegates an existing `.csproj` to the SDK; it owns its compilation settings. The
managed property functions below apply to generated targets or workspace defaults
for CSharpLibrary/CSharpConsole. `filter` and target-platform `when` work normally.

| DSL | SDK contract |
| --- | --- |
| `csdefines({...})` | `DefineConstants`, preserving SDK configuration symbols |
| `nullable("Enable"/"Disable"/"Warnings"/"Annotations")` | `Nullable` |
| `cslangversion("latest")` | `LangVersion` |
| `implicitusings(bool)` | `ImplicitUsings` |
| `allowunsafe(bool)`, `checkoverflow(bool)` | Unsafe blocks / overflow checking |
| `deterministic(bool)`, `documentationfile(bool)` | Deterministic compiler / XML documentation |
| `enableanalyzers(bool)`, `analysislevel("latest")`, `enforcecodestyle(bool)` | Standard SDK analyzer policy |
| `warninglevel(5)` | C# warning level |
| `warningsaserrors(bool)` | `TreatWarningsAsErrors` (also used by native targets) |
| `nowarn({...})`, `warnings_as_errors({...})`, `warnings_not_as_errors({...})` | Diagnostic lists |
| `analyzers({...})`, `sourcegenerators({...})` | `Analyzer` DLL paths |
| `analyzer_projects({...})` | Analyzer ProjectReference with `OutputItemType=Analyzer`, `ReferenceOutputAssembly=false` |
| `additionalfiles({...})`, `editorconfigs({...})` | Analyzer inputs/configuration files |
| `emitgeneratedfiles(bool)` | Compiler-generated sources under the target's build directory |
| `assemblyname`, `rootnamespace`, `assemblyversion`, `fileversion`, `version`, `informationalversion` | Assembly identity/version properties |
| `assemblytitle`, `company`, `product`, `description`, `copyright` | SDK assembly metadata |
| `generateassemblyinfo(bool)` | SDK assembly-info generation |
| `assembly_attribute("Type", {"arg1", "arg2"})` | String-argument SDK AssemblyAttribute item |
| `nuget("ID", {version="...", private_assets="all", include_assets="analyzers", exclude_assets="..."})` | SDK PackageReference |
| `nativelibraries({...})`, `directpinvoke({...})` | Official NativeLibrary and DirectPInvoke items |

Native `defines` remain native options; use `csdefines` for C# symbols. Generated
C# projects also map explicit `optimize` to Optimize and `symbols` to DebugType/
DebugSymbols (portable PDBs when enabled). Common native warning-level names are
not C# warning levels: use `warninglevel` for C#.

Analyzer project dependencies participate in target ordering and workspace aliases.
Paths are rebased when composing projects. Analyzer DLLs, additional files,
editor configs and native-library files are tracked graph inputs. SDK-discovered
and explicit editor-config files are normalized/deduplicated before compilation.
An adjacent source-workspace NuGet.Config is supplied to generated projects, even
when the build directory is outside that workspace. NuGet handles package restore;
Rhodium remains the native package manager.

Managed actions always let MSBuild check incremental state. Unchanged binaries
preserve the target stamp, keeping downstream native actions incremental. `rebuild`
also requests the SDK's Rebuild target. Native dependencies and code generation
complete before the managed build. Complex project properties/imports belong in
an existing csproj rather than a second partial MSBuild implementation.

Generated projects include build-directory props selecting the same isolated
artifacts layout for CLI and IDE builds. Ordinary SDK/IDE builds invoke the common
graph before compilation; internal SDK invocations set AxlumInvocation to suppress
that hook, and design-time builds skip it. Declared native/runtime dependencies are
copied beside generated managed outputs after SDK compilation. Existing csproj files
retain their own project-level hooks/layout; publication still validates and gathers
their declared native dependency closure.

See [managed-dsl](../examples/managed-dsl/README.md), including a real Roslyn source
generator, diagnostic policy, AdditionalFiles and reflection-visible metadata.

## Official publish and NativeAOT

The general `publish/install` commands also handle native targets. Managed-only
`publish` retains the SDK layout described here; `--prefix` selects installation
under `bin/<target>`. See [CLI.md](CLI.md) for native artifacts and components.

```powershell
build/axlum-build.exe configure -f examples/dotnet-publish/axlum.build.lua -B build/publish --config Release
build/axlum-build.exe publish -B build/publish -t AotHello --rid win-x64 --aot
build/axlum-build.exe publish -B build/publish -t AotLibrary --rid win-x64 --aot --native-lib Shared
build/axlum-build.exe publish -B build/publish -t AotHello --rid win-x64 --no-aot --framework-dependent
```

Publication uses the configured configuration/filters, prepares its target closure
through Ninja, then invokes `dotnet publish`. AOT must match the configured native
platform and architecture. Desktop RIDs accepted are win-x64/win-arm64, linux-x64/
linux-arm64, linux-musl-x64/linux-musl-arm64 and osx-x64/osx-arm64. The installed
SDK/native toolchain must support the requested combination; cross-OS AOT is not
implemented by wrapping the SDK. Mono-based Android/browser/iOS runtime RIDs are
not accepted by this desktop .NET publisher.

DSL target defaults:

```lua
publishaot(true)
selfcontained(true)
runtimeidentifier("win-x64")
publishtrimmed(true)
stripsymbols(false)
native_library("Shared") -- library targets only; static NativeAOT libraries are not exposed
native_exports({"axlum_add"})
native_dependencies({"Vendor.dll"})
system_dependencies({"AnExplicitPlatformDependency.dll"})
publishdir("publish/${target}/${rid}/${mode}")
```

CLI overrides: `--aot/--no-aot`, `--self-contained/--framework-dependent`,
`--trimmed/--no-trim`, `--single-file/--no-single-file`,
`--strip-symbols/--keep-symbols`, `--rid`, `--output`, `--native-lib Shared`.
AOT requires self-contained output and cannot be combined with PublishSingleFile;
its output is already native. Ordinary single-file publication remains an SDK
option. Class-library AOT output uses NativeLib=Shared. Exported methods use the
SDK-supported `UnmanagedCallersOnly(EntryPoint=...)` annotation. Generated projects
enable IsAotCompatible diagnostics when their DSL selects AOT.

Publication staging and SDK intermediates are isolated by target/RID/mode. Final
outputs default to `<B>/publish/<target>/<RID-or-portable>/<managed|aot|aot-library>`.
Output paths must remain under the build directory and cannot overwrite internal
SDK intermediates or declared graph artifacts. Failed SDK/validation phases leave
the previous publication in place; successful publication updates files by content
and prunes obsolete previously-owned files. Unknown user files are preserved.

Required/optional runtime files, plus transitive native shared-library/package
runtime dependencies, are copied beside the publication. Publish-only runtime
paths can use `${rid}` and `${mode}`. Filename collisions fail. NativeLibrary items
support static native linker inputs; use DirectPInvoke when the official SDK must
bind P/Invokes directly to those inputs.

## Publication manifests and native validation

`axlum.publish.json` records SDK version, configuration/RID/mode, entry point,
SHA-256 and size of each owned file, parsed native images, exports/imports and
dependency statuses. PE, little-endian ELF64 and Mach-O64 inspection are supported.
NativeAOT output must be native and match the RID architecture. Requested exports
must exist. Declared native dependencies must be present native images with the
matching architecture. Shipped imports are followed recursively; unshipped imports
must be known platform dependencies or explicitly declared system dependencies.

This validates artifact structure and declared dependency closure, not ABI
compatibility or availability/version of every platform library on another machine.
It does not discover every lazy/dynamic P/Invoke: declare those native dependencies
or express them as native target/runtime dependencies. Signing, app installers and
unloading NativeAOT libraries are not implemented here.

Execution coverage in this session is Windows x64 NativeAOT console and shared
library publication, including calling a C export that calls a C++ DLL. Linux/macOS
AOT and Mach-O execution still require validation on their native hosts.

Official references: [SDK MSBuild properties](https://learn.microsoft.com/en-us/dotnet/core/project-sdk/msbuild-props),
[NativeAOT overview](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/),
[native libraries](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/libraries),
[native interop](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/interop).
