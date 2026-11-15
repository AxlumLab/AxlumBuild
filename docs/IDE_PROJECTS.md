# IDE projects

All generators consume the same configured workspace, action graph, native outputs
and compiler arguments as Ninja. They delegate builds to AxlumBuild rather than
creating a second set of compiler/linker rules.

```powershell
build/axlum-build.exe ide -f examples/workspace-suite/axlum.build.lua `
  -B build/suite -o build/suite-projects --format all
```

Formats: `vscode`, `vs`, `xcode`, `android-studio`, `all`. Without a preset, IDE
generation configures each declared configuration for `--arch` (default x64).
`--preset name` can be repeated to select declared matrix entries. Each entry has
a separate directory beneath `-B`. Generation fails when a selected toolchain is
unavailable, rather than generating commands for a different architecture.

## VSCode

Open `vscode/<workspace>.code-workspace`. It contains source-folder navigation,
process build/clean tasks and executable debug configurations. Matching standalone
`.vscode/tasks.json`, `launch.json` and `c_cpp_properties.json` are also generated.
Compiler paths/database paths are absolute, and each configuration points at its
own `compile_commands.json`. Native debug entries use cppvsdbg on MSVC and
cppdbg/GDB on Linux and lldb-dap on macOS. Install the corresponding debugger
extension and executable (see the extension README). Cross-compiled
executables are not offered as host launch targets.

For remembered configuration/build/run selections, install the local
[Axlum Build extension](../editors/vscode/README.md). It opens a dedicated
sidebar in the left activity bar with separate configuration, build-target and run-target controls.
Three status-bar shortcuts provide Build, Run and Debug; hovering shows the target/configuration.
During tasks Build becomes Cancel and the launch shortcuts are disabled. Ctrl+Shift+B builds the selected target; F5/Ctrl+F5 in that sidebar
incrementally build the selected executable and then debug/run it. Build failure
or cancellation prevents launching a stale executable. Selections are independent
and remembered for each configuration. Switching configuration synchronizes
Microsoft C/C++ default compiler and compilation database settings; explicit
`c_cpp_properties.json` values or another provider can override these defaults.
Clangd settings remain user-managed. Generated tasks/launch entries still work
without the extension. `axlum.ide.json` is the versioned editor metadata protocol;
the extension watches it and does not reconfigure just because a window opens.

The sidebar's Reconfigure action saves files and replays the original IDE command,
including presets and CLI options, then reloads the available targets. Refresh
Panel only reloads metadata. Normal native/managed Run delegates to the CLI and
does not require a debugger extension; custom pre-launch tasks or envFile use a
debugger no-debug launch. Debug checks adapter availability before building,
preserves custom adapter types and pre-launch tasks, and retains personal settings.
For native F5 outside the sidebar, choose **Axlum: Selected Target** in Run and Debug;
it resolves the sidebar's current configuration and executable when invoked. Explicit
fixed-target/attach configurations remain available. Stock generated picker tasks
are upgraded by the plugin to use the same selection. Build/Run/Debug save dirty
existing files by default (`axlum.saveBeforeBuild`). Once a program starts, the
build controls unlock; Running Programs provides independent stop controls.
CMake-style F7/Shift+F7/Shift+F5/Ctrl+Shift+F5 aliases are available when CMake Tools
is not in its full project mode. See the extension README for details.

## Visual Studio

Open `vs/<workspace>.sln`. Native `.vcxproj` Makefile projects expose source files,
include directories, definitions, configured artifacts and debugger commands.
Build/clean/rebuild invoke AxlumBuild. Native startup projects are enabled for
build-before-debug; requests sharing a build directory are serialized on Windows.
The explicit `all` project builds the full workspace. Configurations use their
configuration names, with separate x64/ARM64 platforms.
This generator targets VS 2022's v143 C++ project support and writes the complete
VS 2022 solution version header. External project references use relative Windows
paths where possible. A real VS open is covered by the optional Windows editor
integration test. On the current VS 17.14 installation, SDK C# solutions still
trigger a nonfunctional migration prompt, including solutions created by the
official `dotnet new sln` command with SDK 8 and SDK 10. Native-only solutions
open without migration. This mixed-project host issue remains unresolved; changing
the generated version header or project paths alone does not eliminate it.
Solution folders organize applications, tests, libraries, tools and generators.
Native `.vcxproj.filters` mirror source directories, with generated files under
`Generated`. Use target-scoped `ide_target` to customize groups, labels, visibility
and the initial startup project. Existing VS startup preferences remain user-owned.
Real generated/existing C# SDK projects are included beside the native projects,
with SDK configuration mappings. A source SDK project is included once across
configurations; distinct generated project paths retain distinct identities. VS Code also
includes runnable managed launch configurations in the common source workspace.

The adjacent `.slnLaunch` offers one launch profile per visible executable in
VS 2022 17.11+. Enable **Tools > Options > Environment > Preview Features >
Enable Multi-Project Launch Profiles** if needed, then select an executable in
the startup toolbar dropdown. Configuration and platform remain separate choices.
Personal profile changes are merged during regeneration. Older versions can use
**Set as Startup Project**. The format follows Microsoft's
[launch profile documentation](https://learn.microsoft.com/en-us/visualstudio/ide/how-to-set-multiple-startup-projects?view=vs-2022).

## Xcode

Open `xcode/<workspace>.xcworkspace` or `.xcodeproj`. The OpenStep project contains
source references and external-build targets. Shared schemes select an entry and
provide runnable executable paths. A generated Python driver maps Xcode's action
and configuration to the common build/clean commands. Python 3 is required.

Regenerate on macOS using a macOS AxlumBuild executable and selected Apple profile;
projects contain host tool paths. macOS compile/link planning uses the selected
target/sysroot/deployment flags, dylib install names and loader-relative runtime
paths. Xcode formats and driver commands have Windows-side checks, including an
OpenStep parser check; actual xcodebuild/LLDB execution still needs macOS validation.
External targets do not provide a duplicate Xcode-native compilation pipeline.

## Android Studio

Open the `android-studio` Gradle directory. Its root project uses Gradle's built-in
base plugin and exposes per-entry/target Exec tasks, `axlumBuild`, and saved Gradle
run configurations under `.idea/runConfigurations`. These tasks directly invoke
the configured native builds. No Gradle wrapper binary is vendored.

The optional `native` Android library module is enabled with `-PaxlumAndroid=true`.
It pins AGP 8.6.1 / Gradle 8.7, compileSdk 35 and CMake 3.22.1; install the matching
SDK/NDK and a supported JDK (17 or 21). Supply `local.properties` with `sdk.dir`.
Its CMake source-index targets expose native sources to Android Studio, while
`axlum_android` delegates shared-library builds to AxlumBuild's Android profile.
JNI libraries are synchronized separately for Debug/Release and ABI. Removed
staged libraries are pruned. Android ABIs currently covered are arm64-v8a/x86_64.

The root Gradle Exec path and the generated NDK/CMake bridge have real execution
coverage, including ARM64 ELF verification. Full AGP sync/AAR packaging, device
launch/debug still require the matching SDK/device for execution validation.
Targets declaring `android_applicationid` generate application modules, NativeActivity
manifests, Android App run configurations and install/launch Gradle entry points.
See [parity-suite](../examples/parity-suite/README.md) and
[the detailed contracts](SUPPORTED_PLATFORM_PARITY.md). An Android shared-library
example is [cross-library](../examples/cross-library/axlum.build.lua).

## Regeneration and ownership

`ide_customize` and public C++ `IDEProjectCustomizer` callbacks customize native VS
properties/debugger/filter/user files, VSCode JSON sections, Xcode settings/scheme
attributes and Android Studio Gradle/run options. See [FINAL_GAPS.md](FINAL_GAPS.md).
VS `.vcxproj.user` files are seeded only when absent and never overwritten or
deleted. Filters are generated and can be extended using customization fragments.

VS Code tasks, launch files and the workspace use a generated baseline in
`.axlum-generated` to merge subsequent updates with personal edits. Arguments,
environment, working directories and custom launch/task entries survive regeneration.
JSONC comments and trailing commas are accepted; formatting/comments are preserved
in a `.user-backup` when a file needs rewriting. Invalid JSONC stops generation
instead of overwriting the file. Keep the baseline alongside generated files.

Rerun `ide` after changing target lists or IDE configurations. Generated files are
recorded in `axlum.ide-files.json`; regeneration removes obsolete owned project,
scheme and task files inside the selected output tree. It preserves unknown user
files. Generating one format preserves the other formats. Keep source folders
separate from the generated project directory.

Format conventions follow the official [VS Makefile project documentation](https://learn.microsoft.com/en-us/cpp/build/reference/creating-a-makefile-project?view=msvc-170),
[VSCode tasks](https://code.visualstudio.com/docs/debugtest/tasks),
[VSCode debugging](https://code.visualstudio.com/docs/cpp/launch-json-reference), and
[Android Gradle/CMake integration](https://developer.android.com/studio/projects/gradle-external-native-builds).


## Build target selection

`--format desktop` generates VS Code and Visual Studio together.
VS Code's default build task (`Ctrl+Shift+B`) prompts for a build target, including
`all`. Four visible tasks build a selected target, build all, clean a selected
target or run a selected target. With multiple configurations, tasks also prompt
for the configuration. Hidden per-launch build tasks supply precise dependencies.
The Run and Debug dropdown selects the executable and configuration; F5 and
Ctrl+F5 use the same entry and build incrementally before launching. Duplicate
`(run existing)` entries are removed. Host tools are omitted from launch choices
unless explicitly marked as the startup target. Build/cache directories are hidden
from workspace navigation and search. Install the local Axlum Build extension
for remembered selections and C/C++ default IntelliSense configuration switching.

Visual Studio uses configuration names (`Debug`, `Release`) and architecture
platforms (`x64`, `ARM64`). Build an individual project to build that target and
its dependencies; select a runnable startup project for debugging. The `all`
project builds the complete workspace. Target names are not configurations.
Presets sharing a configuration/platform pair must be generated separately.
Concurrent VS build requests for the same output directory are serialized on
Windows; Ninja parallelizes the selected target's actions.

Builds check saved configuration inputs without loading Lua or rediscovering the
toolchain. Source edits go through Ninja's ordinary dependency graph. Changes to
build scripts, options, environment, local package inputs or directory membership
trigger reconfiguration. Dynamic source generators are checked only for the CLI's
selected dependency closure; direct Ninja checks all dynamic roots. A changed
output manifest regenerates the compilation graph before compilation continues.
Native `run -B <directory> <target>` builds incrementally, reloads the artifact
index, then launches the selected executable.

The VS Code extension also exposes declared tests and discovered GoogleTest cases
in the native Testing sidebar, with CTest execution and native executable test
debugging. See [extension testing](../editors/vscode/README.md#testing) and
[GoogleTest discovery](DSL.md#googletest-discovery).
