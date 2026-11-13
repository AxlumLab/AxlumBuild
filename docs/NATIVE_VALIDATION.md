# Native build validation — 2026-11-13

## CMake layout and author-header update

The normal `build/` directory was configured and built in Debug. All build
declarations remain in the root `CMakeLists.txt`. The Lua binding translation unit uses
MSVC `/bigobj` to support its Debug template instantiations. Both C++ tests passed
(2/2, 1.63 seconds). `cmake --install` with the Runtime component installed the CLI
under a build-local prefix, and the installed executable returned its version.
CLI publish help, 115 exact C/C++/C#/RC author headers, formatter checks and Python
syntax were checked. No full regression or NativeAOT publication was rerun.

## CLI and namespace update

The normal build rebuilt the CLI/core and both C++ test executables. The unit,
native-plan and command-lifecycle tests passed (3/3, 70.42 seconds). The focused
lifecycle test covers real native shared-library execution, argument/environment/
exit-code forwarding, install components and ownership/uninstall, clean modes,
CMake command forms and the Rhodium SDK JSON bridge. A tiny real managed target
also passed official SDK run, legacy publish and prefix installation. No AOT
publication or full regression suite was rerun. Public header inspection confirmed
all former free-function APIs are class members under `axlum`.

The current executable is `build/axlum-build.exe`. AxlumBuild was built in the normal `build/` directory, using Windows x64 MSVC 19.44.35228 (toolset 14.44.35207), Windows SDK 10.0.26100.0, CMake 4.4.0-rc3 and Ninja 1.13.2.

Rhodium remains an unchanged Git submodule. Earlier checks reused an installed
static library; after that external installation disappeared, the user clarified
that the dependency should build from the submodule. The final bootstrap uses
`AXLUM_USE_INSTALLED_RHODIUM=OFF`, building only Rhodium Core as an AxlumBuild
dependency, with its CLI/tests/install disabled. No Rhodium source was changed
and none of its own tests were run.

## Commands and results

The final CMake configure uses `AXLUM_USE_INSTALLED_RHODIUM=OFF`, the VS vcpkg
toolchain, the existing vcpkg package cache at
`E:/Projects/Rhodium/build/vcpkg_installed`, `VCPKG_TARGET_TRIPLET=x64-windows-static`,
`VCPKG_MANIFEST_INSTALL=OFF`, and Release. This reuses third-party package binaries;
the Rhodium Core library itself builds from `third_party/rhodium` into this
project's `build/` directory. A fresh checkout can use the root vcpkg manifest and
its own installed directory. Bootstrap builds run in a VS x64 Developer environment:

```powershell
cmake --build build
```

User-project tests then run from ordinary PowerShell, without loading the VS development environment:

```powershell
ctest --test-dir build --output-on-failure
```

**Previous full suite: 9/9 passed, 271.47 seconds.** This includes real NDK ARM64/x64 and generated Android Studio bridge/Gradle execution, in addition to the existing native/.NET/package suites. After transitive-interface action dependencies and VSCode clean-task schema refinements, affected unit/native-plan/ide-workspace suites passed **3/3, 130.78 seconds**, including custom Profile configurations and generated-header increments. Final IDE metadata smoke checks passed for module header visibility, executable-only debugger entries, valid clean-task metadata and OpenStep parsing. Generated VS DLL and app projects were rebuilt through real MSBuild, and the app returned suite=42.

After the .NET/diagnostics additions, the three new suites passed 3/3. A subsequent
12-suite regression was interrupted at the user's request to avoid excessive
testing; observed passes included native-features, modules-reconfigure,
managed-sdk and dotnet-publish. It is not recorded as a completed 12/12 run.


| Suite | Coverage / result |
| --- | --- |
| managed-sdk | Actual Roslyn source generator, constants/nullable/optimization/assembly metadata, AdditionalFiles increments, analyzer warning/error policy, no-op/forced rebuild, offline NuGet analyzer restore |
| dotnet-publish | Official framework-dependent/self-contained publish and real NativeAOT exe/DLL, C export invoking a native DLL, SHA-256 manifests, recursive import validation, missing export/dependency failures, stale-file cleanup and previous-publication preservation |
| diagnostics | Jobs/multiple targets/scoped rebuild, streaming failure logs, verbose/why, no-op/dry-run, JSON reports and Chrome traces |
| cross-matrix | Optional installed-NDK test: host/Android preset entries, AArch64/x86_64 ELF headers, persisted SDK hints/no-op, generated Android Studio CMake/JNI and Gradle Exec |
| ide-workspace | Cross-workspace static interfaces, pre/post hooks/no-op/failure recovery, diamond aliases/cycles, matrix isolation, IDE JSON/XML, real MSBuild and Xcode driver, generated-file ownership |
| unit | Model/graph validation, dependency cycles, path and Ninja escaping |
| native-plan | MSVC/GCC/Clang command planning; options, LTO, isolated module PCHs, custom paths, runtime collisions; no foreign compiler execution |
| integration | Real native static/shared builds, mixed C/C++, header increments/no-op/failure recovery, codegen and official .NET SDK builds |
| cli-dsl | CLI11 help/validation, Lua scopes/types/defaults/filters/globs, generated SDK projects and managed references |
| packages | Local/Git restore, public/private/transitive interfaces, locked mode, Debug/Release, compiled static/shared packages, copied runtime DLLs and CRT consistency |
| modules-reconfigure | Real module DLL grouping/imports/exports, Monolithic/Modular switching, cycles, direct-Ninja includes, glob membership, local package edits, optional/stale outputs, ambient CL invalidation, RC/icon/plugin policy |
| native-features | Actual discovery without Developer environment, PCH, typed flags, outputs, Windows resources/manifest/exports/delay-load, increments, clean and user profiles |

Automatic package restoration initially broke package no-op builds by refreshing local artifact timestamps. Package input fingerprints now avoid unnecessary restores and validate cached artifacts; the full package suite passed with this fix. All implementation changes are confined to AxlumBuild, not Rhodium.

## Behaviors exercised

- Rhodium finds the installed VS C++ toolset and Windows SDK when `cl.exe`, `link.exe` and `lib.exe` are absent from the caller's PATH.
- Tests explicitly strip Developer-shell PATH/include/library variables. Native actions and CMake package builds use the detected profile's saved environment.
- Project user profiles and `.rhodium/local.toml` preferences select a custom profile. Changing its SDK version is rejected by explicit configure in the same directory; automatic build refresh updates the saved fingerprint and recompiles native objects.
- A separate manual check configured `examples/hello-cpp` into `build/profile-switch` from a Developer shell, then ran `graph`, `build` and `HelloCpp.exe` from ordinary PowerShell successfully.
- C++ PCH supports an explicit creator with real code and an automatic creator. Mixed C sources do not consume C++ PCH. Nested-header changes rebuild PCH and C++ consumers while preserving unchanged C objects; definition/option changes invalidate PCH; `pch(false)` disables an inherited header.
- Target/module PCH plans are isolated, including different module definitions/options. A target's explicit creator is not duplicated into unrelated module groups.
- Release LTO with PCH executes successfully. Tests check actual exception/RTTI compiler macros, static CRT flags, symbol disabling, size optimization and warning-as-error failure/recovery.
- Custom target names, object/PCH directories, DLL/import-library/EXP/PDB locations and incremental-link outputs are tracked. Clean followed by build succeeds.
- RC input changes rebuild the `.res` and link output while preserving unrelated C/C++ objects. A `.def` export file, embedded manifest and delay-load DLL are used by the runnable example.
- Required and optional runtime files copy to the consumer directory. Content-only runtime changes do not recompile native objects. Missing required inputs and basename collisions fail.
- A shared CMake package's DLL/SO metadata reaches the app directory. Its original cached runtime binary is temporarily moved aside, and the app still executes using its copied library.
- Native and CMake-package static CRT policies agree. A conflicting package CRT is rejected.
- Recursive file globs exclude selected/custom build directories and existing build trees, so generated PCH files are not imported as project sources. Added/removed source matches and optional runtime files are discovered automatically.
- Modules support public/private dependencies, same-binary grouping, API import/export annotations, and module/binary-cycle errors. Modular shared groups and a monolithic shared owner are linked and consumed by real executables.
- Direct Ninja execution refreshes included Lua settings. Local package source changes restore automatically, while unchanged packages preserve native no-op behavior. Ambient CL changes update the execution environment/fingerprint.
- A valid replacement graph removes declared obsolete outputs inside the build tree. Renames, removed source objects, obsolete DLLs and disappearing optional-runtime files are covered.

## Limits

- Real execution covers **Windows MSVC** and **Android NDK Clang targeting ARM64/x64**. Linux/macOS compiler execution and xcodebuild/LLDB remain untested in this Windows session. Xcode OpenStep syntax was parsed with openstep-parser; schemes/workspace XML and the generated build driver were checked.
- Selection covers Windows/Linux/macOS/Android x64/ARM64 profiles and appropriate backend flags. Windows ARM64 is selectable but not executed here; Android shared libraries are verified by ELF machine values. iOS/UWP/GDK, full APK deployment and native/cross host-tool graph combinations remain outside this implementation.
- Android Studio root Gradle Exec tasks were run with Gradle 8.7 and Android Studio JBR 21. The generated CMake bridge built and staged real NDK libraries. Full AGP sync/AAR packaging and device launch/debug were not executed; no APK application scaffold is generated.
- RC literal local includes and resource payloads are scanned recursively. Macro-generated/conditional paths still need explicit `resourceinputs`; this is not a complete RC preprocessor.
- PCH is per compilation group; external/cross-target manual PCH reuse is not supported.
- Runtime files are copied flat beside consumers. This is not a general installation, packaging, signing or deployment subsystem.
- Build checks re-evaluate manifests/includes/globs and package inputs. Compiler/SDK changes invalidate native actions. Fingerprints probe SDK metadata and representative files, not every SDK file or complete ABI compatibility. Each Ninja invocation incurs configuration-check time.
- Existing MSVC `getenv` deprecation warnings remain in AxlumBuild's own bootstrap build.

See [TOOLCHAINS.md](TOOLCHAINS.md), [DSL.md](DSL.md) and [the runnable example](../examples/native-features/README.md).

## IDE/actions/workspace/matrix execution

- `examples/workspace-suite` links its referenced Common library, generated header
  and post-build publication into a runnable app (`suite=42`). Source changes,
  failure/recovery, no-op and diamond references are tested in temporary paths
  containing spaces. Visual Studio's generated solution is built with real MSBuild.
- `examples/cross-library` uses a target-platform Lua condition and API export macro.
  NDK r27c Clang 18.0.3 produced ARM64 and x86_64 ELF64 shared libraries (machine
  183 and 62). Matrix entries keep host and Android outputs separate. A follow-up
  build outside the initial SDK shell preserves no-op behavior.
- Android Studio's generated `axlum_android` CMake target invokes AxlumBuild and
  synchronizes JNI files by configuration/ABI. Gradle's generated Exec task also
  executes a real native build. Test-only Gradle/openstep-parser downloads are in
  ignored `.cache/`, not project runtime/bootstrap dependencies.
- Generated IDE ownership metadata prunes obsolete project/scheme files, preserves
  unknown files and retains other formats when regenerating a single format.

See [IDE_PROJECTS.md](IDE_PROJECTS.md) and [WORKSPACES_AND_ACTIONS.md](WORKSPACES_AND_ACTIONS.md).

## .NET SDK, NativeAOT and CLI diagnostics

The selected .NET SDK is **10.0.301**, with installed SDKs 7.0.410/8.0.422 also
present. Tests use the selected SDK's matching target framework. AxlumBuild requires
SDK 8+ for artifact isolation; the public examples target net10.0. The pipeline uses
only official `dotnet restore/build/publish`, with no Mono backend.

- Real source-generator compilation consumes AdditionalFiles and emits a value;
  modifying the input changes the running application's value. Reflection verifies
  custom assembly metadata, including XML-sensitive characters. Nullable errors and
  analyzer warnings exercise warning-as-error, exceptions, suppression and recovery.
- A locally packed analyzer NuGet package is restored through source-root
  NuGet.Config, despite generated projects living outside the source tree. This
  verifies genuine SDK package resolution without depending on a public feed.
- NativeAOT console publication runs with an invalid DOTNET_ROOT and has no CLR
  header/runtimeconfig. NativeLib=Shared exports `axlum_add`; a separate Python
  process calls it through ctypes, including its P/Invoke into a compiled Vendor
  C++ DLL, and receives 42. PE architecture, exports and imports are inspected.
- Missing requested exports, declared dependencies and an intentionally corrupted
  transitive DLL import fail publication while retaining the previous manifest.
  Publication hashes match files, identical binaries retain timestamps, obsolete
  owned files are pruned and unknown user files survive.
- Framework-dependent/self-contained publication uses the same official SDK path.
  Republishing a directory removes obsolete CoreCLR files. Invalid AOT/self-contained,
  single-file, Mono RID and output-escape combinations produce diagnostics.
- CLI tests execute repeated/positional targets, jobs, selected-target rebuild,
  compiler failure logging, no-op/dry-run and JSON/trace generation. Failed actions
  appear as failed phases; Ninja only logs successful action timings.

The three new suites passed together **3/3 in 218.55 seconds**. Complete regression
results are recorded above. NativeAOT execution was tested on Windows x64; Linux/
macOS AOT and their native image/runtime behavior still need host validation.

The installed SDK restored **Microsoft.DotNet.ILCompiler 10.0.9**, producing a real
NU1903 audit warning for CVE-2026-111497. Audit output was preserved. Microsoft's
[published advisory](https://github.com/advisories/GHSA-p3jp-9v83-76w9) lists 10.0.14
as the patched .NET 10 package version. These fixture results establish build
behavior; use a serviced SDK/runtime pack containing the fix for released AOT
artifacts. The machine's global SDK installation was not changed.

## Final supported-platform parity checks

Final source builds in build/ with installed-library mode disabled
(`AXLUM_USE_INSTALLED_RHODIUM=OFF`). The pinned submodule produces
`build/third_party/rhodium/rhodium_core.lib`; Rhodium CLI/tests/install remain OFF.
Only its Core dependency is built, without changing Rhodium source.

- The affected native-plan suite passed (0.10 seconds) for native policies,
  shared automatic/manual PCH, qualified module interfaces, Apple frameworks and
  iOS dylib planning.
- The combined native-plan/parity-smoke selection passed 2/2 (35.72 seconds).
- After integrating direct SDK/IDE graph preparation, the affected parity-smoke
  alone passed (42.80 seconds). It compiles a real shared PCH, cross-workspace
  module/library chain, native DLL groups and a managed consumer; both normal
  build-tree execution and publication return modular interop=42. A direct SDK
  build of the generated IDE project also prepares/runs that native chain.
- Mixed SDK/native solution entries, managed launch metadata, Android APK module
  and NativeActivity manifest/launch entries were checked in the same smoke.

No full regression or repeated NativeAOT publication was run for this change.
Apple/iOS host execution, AGP APK packaging and device debugging still require
native hosts/SDKs/devices; generated/planned support is not execution evidence.
See [SUPPORTED_PLATFORM_PARITY.md](SUPPORTED_PLATFORM_PARITY.md).

## Final five-gap closure

The normal build/ executable includes framework compiler/PCH search paths,
external PCH inputs, C++14/Latest, tracked MSVC XDC/XML documentation and Lua/C++
IDE customization. The Rhodium submodule remains the Core dependency; no Rhodium
source edits or tests were added.

- Updated native-plan passed (0.11 seconds): direct/transitive framework compiler
  paths, external PCH consumption without creation, C++14/Latest flags, doc merge
  edges and a public C++ IDE customizer callback writing escaped properties/debugger
  settings.
- final-gaps passed (23.49 seconds): real MSVC C++14 external-PCH application and
  independent Latest application execute; documented/undocumented translation
  units produce valid merged XML. No-op preserves timestamps; a doc-comment edit
  updates XML without rebuilding the producer PCH; clean removes the owned XML.
  Lua customizations are checked in VS XML/filter/user files, VSCode typed JSON,
  Xcode PBX build settings and Android Studio Gradle output.
- Only affected small tests ran; no full regression or NativeAOT publication rerun.
  GNU/Apple-specific execution and platform/device validation retain their existing
  limits; compiler planning is not represented as host execution.

Contracts and example: [FINAL_GAPS.md](FINAL_GAPS.md),
[final-gaps](../examples/final-gaps/README.md).

## Target selection and existing native projects

The normal `build/axlum-build.exe` passed `tests/TargetSelection.py`: native
vcxproj mapped Shipping/Development configurations, real MSBuild compile/link and
execution (`external=42`), no-op timestamp preservation, SDK rebuild/clean,
isolated outputs/intermediates, unchanged source project, original ProjectGuid
solution inclusion, Debug-only IDE filtering, unsupported target/dependency
diagnostics and empty ARM64 matrix skips. The fixture lives under `build/`, since
the local MSBuild file tracker omitted outputs in system TEMP.

The executable was built with the Rhodium Core submodule, CLI/tests disabled;
no Rhodium source modifications or Rhodium tests were required. Only the planner
and this focused smoke were used, with no full regression or AOT publication.
Other operating systems and IDE/device execution retain their existing validation
limits. See [TARGETS_AND_NATIVE_PROJECTS.md](TARGETS_AND_NATIVE_PROJECTS.md).
