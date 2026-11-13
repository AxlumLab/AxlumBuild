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
cppdbg/GDB or LLDB on Unix. Install Microsoft's C/C++ extension. Cross-compiled
executables are not offered as host launch targets.

## Visual Studio

Open `vs/<workspace>.sln`. Native `.vcxproj` Makefile projects expose source files,
include directories, definitions, configured artifacts and debugger commands.
Build/clean/rebuild invoke AxlumBuild. The solution builds one aggregate `all`
project by default, avoiding multiple simultaneous Ninja processes in one build
directory. Individual targets remain available through project Build commands.
Configurations are named after the configured entries, with x64/ARM64 platforms.
This generator targets VS 2022's v143 C++ project support. Real generated/existing
C# SDK projects are included beside the native projects, with entry-specific
identities and SDK configuration mappings. The aggregate build remains the default
to avoid concurrent Ninja invocations. VSCode also includes managed project folders
and runnable managed launch configurations.

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
Uncustomized VS user/filter files are not overwritten by this extension.

Rerun `ide` after changing target lists or IDE configurations. Generated files are
recorded in `axlum.ide-files.json`; regeneration removes obsolete owned project,
scheme and task files inside the selected output tree. It preserves unknown user
files. Generating one format preserves the other formats. Keep source folders
separate from the generated project directory.

Format conventions follow the official [VS Makefile project documentation](https://learn.microsoft.com/en-us/cpp/build/reference/creating-a-makefile-project?view=msvc-170),
[VSCode tasks](https://code.visualstudio.com/docs/debugtest/tasks),
[VSCode debugging](https://code.visualstudio.com/docs/cpp/launch-json-reference), and
[Android Gradle/CMake integration](https://developer.android.com/studio/projects/gradle-external-native-builds).
