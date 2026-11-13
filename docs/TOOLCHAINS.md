# Toolchain discovery

AxlumBuild uses the embedded Rhodium `ToolchainDetector` and `ToolchainRegistry`. It does not invoke a Rhodium CLI, synthesize a second package-manager toolchain, or require `cl.exe` to be on the caller's PATH.

```powershell
build/axlum-build.exe doctor
build/axlum-build.exe configure -f examples/native-features/axlum.build.lua -B .axlum/features --toolchain windows-msvc-x64
build/axlum-build.exe build -B .axlum/features --target FeatureApp
```

`doctor` displays Rhodium's detected/overridden profiles, including compiler path/version, SDK kind/version/root, sysroot, architecture, completeness and missing requirements. It no longer keeps an independent PATH tool inventory or SDK search implementation. Use `doctor --workspace <source> --json` for structured results and `--strict` to fail if no supported complete profile exists.

Native selection covers x64/ARM64 profiles for MSVC Windows, GCC/Clang Linux, Apple Clang macOS/iOS and Clang Android NDK. `--arch` and `--platform` constrain profile selection; incompatible or incomplete profiles fail explicitly. Windows native builds and real Android ARM64/x64 shared libraries have execution coverage. Apple/Linux cross paths have not been executed in this Windows session. MinGW/UWP/GDK/console/32-bit backends are not enabled.

## Selection and overrides

- `--compiler auto|msvc|gcc|clang` selects a compiler family.
- `--arch x64|arm64` and `--platform windows|linux|macos|android` select target dimensions. NDK profiles use target/sysroot flags; Windows NDK launchers are replaced with direct clang executables and GNU response-file quoting.
- Android SDK/NDK/API hints are saved in the execution environment and reused when absent from a later caller. Explicit caller values override them.
- `--toolchain <id>` selects a Rhodium profile and must also satisfy any explicit compiler-family constraint.
- With `--compiler auto` and no explicit profile, `.rhodium/local.toml` can select a preferred profile. Rhodium honors an existing `.tpkg/` state directory as well.

```toml
preferred_toolchain = "my-msvc-x64"
```

Fresh auto-detection is combined with Rhodium's override layers. For the same ID, later layers win:

1. Freshly detected profiles (stale auto-detection cache entries are not substituted).
2. Global user profiles, normally `~/.rhodium/toolchains.toml`.
3. `<workspace>/config/toolchains.toml`.
4. `<workspace>/toolchains.toml`.
5. `<workspace>/.rhodium/toolchains/user.toml`.

A user record is a normal Rhodium `[[toolchain]]` with `id`, `platform`, `host_arch`, `target_arch`, `compiler_kind`, compiler/linker/archiver/Ninja paths and optional SDK/tools/environment metadata. Rhodium expands and validates these records. For MSVC, provide valid `system_include_dirs` and `system_lib_dirs`; AxlumBuild can derive INCLUDE/LIB/LIBPATH from them when no explicit environment value is supplied. Explicit user environment values are preserved.

Auto-detected VS profiles retain toolchain variables, rather than saving every unrelated variable printed by VsDevCmd. On Windows, child-process environment keys are merged case-insensitively, avoiding duplicate `Path`/`PATH` entries.

## Configure and execution

Configure stores the selected profile ID, Ninja path and fingerprint in `axlum.state`, and its execution environment in `axlum.environment.json`. Generated Ninja actions load that environment. Native compiler paths are absolute; MSVC system include paths also appear as external include arguments in `compile_commands.json` for editor tools.

The fingerprint includes Rhodium's compiler/SDK metadata, selected tool paths, tool file sizes/timestamps, representative SDK headers/libraries and relevant execution environment. Ambient compiler switches/search paths (including CL/_CL_ and CPATH) are captured even for user profiles unless explicitly supplied by the profile. PATH is excluded from the fingerprint; the execution environment still records it.

Before each build, including direct Ninja use, a generator rule reloads the saved manifest/configuration, discovers the toolchain and refreshes package artifacts and build actions. Explicit profile requests remain pinned; automatic selection re-evaluates project preferences. Fingerprint changes update a tracked environment input and force native actions to rebuild. Explicit configure continues to reject incompatible reuse of a build directory; use separate directories when switching configuration or target triple. The locked-package flag is saved for refreshes.

Package restore uses the **same selected Rhodium profile**, including its compiler, SDK and environment. MSVC package CRT policies are matched to their consumers, and conflicting consumers/recipes are rejected. A static CRT selection maps to `/MTd` or `/MT`, and dynamic CRT to `/MDd` or `/MD`.

This fingerprint is not a full content-addressed ABI proof. The SDK probes do not hash every SDK file or prove complete ABI compatibility. Bootstrapping AxlumBuild itself still uses CMake and an initialized C++ compiler environment (for example, Developer PowerShell when building with MSVC + Ninja).
