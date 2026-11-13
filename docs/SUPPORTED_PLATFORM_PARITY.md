# Supported-platform parity

Supported native architectures are x64 and ARM64. UWP, GDK, other console platforms
and all 32-bit architectures are explicitly out of scope, not pending migrations.
The managed backend is the official .NET SDK only. Engine bindings, hot reload,
store deployment/signing and legacy build-script compatibility remain separate concerns.

The subsequent five-gap closure (framework compile paths, external PCH, C++14/
Latest, native documentation and IDE customization) is specified in [FINAL_GAPS.md](FINAL_GAPS.md).

## Native policies

The Lua DSL now exposes these workspace/target/module settings:

| Function | Contract |
| --- | --- |
| `sanitizers({"Address", "Undefined"})` | Case-insensitive address/thread/memory/undefined names; compile and link flags, including static-library dependency propagation |
| `inlining("Off"/"Default"/"On"/"Aggressive")` | MSVC Ob0/Ob2/Ob3; GNU/Clang no-inline/inline-functions |
| `functionlevellinking(bool)` | MSVC Gy or GNU function sections |
| `runtimechecks(bool)` | MSVC RTC1; requires optimization Off, incompatible with ASan |
| `stringpooling(bool)` | MSVC GF; GNU/Clang constant-merging switches |
| `intrinsics(bool)` | MSVC Oi or GNU builtin policy |
| `buffersecuritycheck(bool)` | MSVC GS or GNU strong stack protector |
| `debugcrt(bool)` | Explicit MSVC debug/release CRT independent of configuration name; dependency policies must agree |
| `sharedpch(bool)` | Reuse compatible PCH creators within the graph |

MSVC exposes AddressSanitizer only. MemorySanitizer requires Linux Clang. Mutually
incompatible sanitizers and known option conflicts fail planning; actual sanitizer
runtime availability remains a compiler/SDK requirement. Raw flags remain available.

PCH sharing compares full compiler argv, resolved header/creator, working directory
and prerequisite outputs. Automatic PCHs with differing parameters remain isolated.
Explicit shared creators with incompatible contracts fail rather than reusing an
invalid PCH. Reused MSVC creator objects are linked once per binary.

## Apple frameworks and iOS

`frameworks({"Foundation", "Cocoa"})`, `frameworkdirs({...})` and paths such as
`frameworks({"Vendor/MySDK.framework"})` produce Apple framework search/link arguments.
Static/header-only dependency interfaces propagate them. Rhodium's framework
metadata enters the same model. Non-Apple toolchains reject framework declarations.
Framework binary inputs are tracked when a concrete bundle path is supplied.
The linker does not automatically embed/sign framework bundles; use explicit build
actions for distribution layouts.

`.m`/`.mm` sources use Objective-C/Objective-C++ on Apple toolchains and do not
consume a C++ PCH. macOS and iOS use dylib/install-name/loader-relative conventions.
`--platform ios --arch arm64` selects Rhodium's iphoneos profile; `--platform
ios-simulator --arch arm64|x64` selects its iphonesimulator profile. Detected SDK
roots and deployment versions are passed to Clang. Xcode configurations record
the selected SDK and architecture. Run these commands on a Mac with Xcode/SDKs.
This is native library/executable toolchain support, not an IPA/signing service.

## Qualified module interfaces

At target scope:

```lua
private_modules({"common::Utility/Math"})
public_modules({"Runtime/Core"})
```

Inside a module, `public_deps`/`private_deps` still accept local module names and
also accept qualified `Target/Module` references. Workspace aliases precede the
target name, as above. Resolution selects the exporting binary: its owning static/
shared library in Monolithic mode or its generated binary group in Modular mode.
Headers, public definitions and API import/export macros follow the module's
public interface. References link existing binaries rather than recompiling the
same sources. Missing/ambiguous declarations and module/binary/target cycles fail.
A Modular owner containing only modules becomes an aggregate instead of an empty
DLL. Group names stay scoped to their target, with output-collision diagnostics.

The publish command traverses this expanded graph, so native DLLs generated for
module groups and their package/runtime dependencies reach managed/AOT publications.
Native dependency/export checks and publication manifests remain active.

## IDE integration

Visual Studio solutions include real C# SDK/existing projects beside native Makefile
projects. Each configured entry has a distinct managed project identity and maps
to its actual SDK configuration. The aggregate build invokes AxlumBuild once; SDK
projects remain visible/editable and can be selected individually. Native source
items use ClCompile/ClInclude, and Rebuild invokes the actual rebuild command.
Generated SDK projects prepare the common graph before an ordinary IDE/SDK build;
AxlumBuild's own SDK invocations suppress that hook to prevent recursion. Design
time IntelliSense builds skip it. Generated projects share the configured artifact
layout with CLI builds, and tracked native/runtime copies go beside managed output.

VSCode adds managed project folders and executable launch configurations. Generated
projects use the CoreCLR adapter with the configured SDK artifact path; existing
projects delegate project discovery to C# Dev Kit's dotnet adapter. Native launch
configurations retain their existing debugger. Install the corresponding VSCode
C#/C++ debugger extensions.

Android SharedLibrary targets can declare:

```lua
android_applicationid("org.example.game")
android_activity("android.app.NativeActivity") -- default
android_min_sdk(23)
android_target_sdk(35)
-- android_manifest("AndroidManifest.xml") -- optional complete application manifest
```

Android Studio generation adds an application module per declaration, depending
on the native AAR bridge. Enable it with `-PaxlumAndroid=true`. Gradle provides
`assembleDebug`, `installDebug`, and `:app_Target:axlumRunDebug`; `-PaxlumDevice=serial`
selects a device for launch. Android Studio gets an Android App run configuration.
The default manifest uses NativeActivity and the target's output library name;
that library must implement ANativeActivity_onCreate. Supply a complete manifest
when using a different application/activity. SDK 35/AGP 8.6.1/Gradle 8.7 and an NDK
remain installation requirements. Release signing remains application-owned.

## Minimal validation

Only the affected native planner suite and one combined parity smoke are selected;
no full regression or repeated NativeAOT publication is required for this change.
The planner covers native policies, compatible automatic/manual PCH reuse,
qualified module imports and Apple/iOS link planning. The combined smoke builds
cross-workspace native modules and a real shared PCH, publishes a managed consumer,
calls the resulting native DLL chain before/after publication, builds a generated
SDK project directly as an IDE would, and checks mixed IDE/mobile project outputs.
See `tests/NativePlan.cpp`, `tests/ParitySmoke.py`, and the runnable
[parity-suite](../examples/parity-suite/README.md).

Windows execution does not establish macOS/iOS host execution, LLDB operation,
Android AGP packaging or device-debug validation. Those require the corresponding
installed host, SDK and device; they are not reported as completed tests.
