---
name: axlum-build
description: Develop, troubleshoot, and use AxlumBuild, including its Lua build DSL, Ninja graph, native/.NET builds, IDE output, CLI, installation, and embedded Rhodium integration. Cross-check Rhodium for dependency restore, lockfile, artifact, or SDK/profile issues. Do not activate merely because an unrelated project uses C++ or CMake.
---

# AxlumBuild

Locate whether the problem belongs to build planning, dependency management, or their adapter before editing. This skill does not independently authorize builds in another repository, dependency upgrades, commits, pushes, or releases.

## Locate the worktrees

This package lives at `<AxlumBuild>/skill/axlum-build`; the repository root is two directories above it, not necessarily the shell's current directory. The actual provider is `<AxlumBuild>/third_party/rhodium`, pinned by the consumer's gitlink. The independent development repository is usually its sibling `Rhodium` (locally `E:/Projects/Rhodium`). If absent, inspect `.gitmodules` and user-supplied locations rather than guessing an installed SDK path.

Read applicable `AGENTS.md`, the current root CMake file, and relevant documentation. Use CodeGraph before code exploration in each repository that has `.codegraph/`; do not create an index automatically. Inspect each worktree's status and compare the independent Rhodium HEAD, submodule HEAD, and recorded gitlink. The independent repository's latest code is not necessarily linked by the consumer.

## Route by responsibility

| Problem | Starting points |
| --- | --- |
| Arguments, help, dispatch, configure | `src/cli/Application.*`; keep `Main.cpp` a thin entrypoint |
| Workspace/target/module/filter and file expansion | `src/manifest/LuaManifest.cpp`, `Composition.cpp`, `src/model/Model.*` |
| Compile/link/PCH, binary grouping, actions, runtime copying | `src/graph/`, `src/backend/Ninja.*` |
| Package recipes, restore, artifact interfaces, fingerprints | `src/packages/Rhodium.*`, `PackageManifest.hpp` |
| SDK/profile/doctor | `ToolchainDiscovery` in `src/toolchain/Toolchain.*`; detection comes from Rhodium |
| Official SDK builds/publish and native image validation | `src/managed/` |
| Install/publish/run/clean | `src/core/BuildOperations.*`, `Diagnostics.*` |
| IDE output | `src/backend/Ide.*` |

These are search anchors, not promises about future paths. Read `docs/DSL.md` and `docs/CLI.md` for contracts, and `docs/NATIVE_VALIDATION.md` for execution boundaries. Historical reports are not evidence of tests run in the current task.

## Make the change at the right layer

- Ninja schedules project builds. Rhodium handles dependency acquisition/build/cache and SDK profiles. Do not add another SDK detector in AxlumBuild or turn Rhodium into the project build graph.
- Separate bootstrap dependencies from user-project dependencies. AxlumBuild links its Git-submodule `Rhodium::Core` through CMake and obtains bootstrap libraries through vcpkg. User projects use embedded Rhodium; installing the Rhodium CLI is not a prerequisite.
- For restore, lockfile, CRT, artifact, or profile failures, read [the bridge guide](references/bridge.md) and compare the linked provider implementation. Do not adapt solely to the independent repository's newest README.
- Follow public/private/interface propagation, module/workspace aliases, runtime files, and incremental action inputs/outputs through to the final consumer. Successful configuration alone does not prove a fix.
- Use official dotnet/MSBuild/NativeAOT only. Mono, UWP, GDK, other consoles, and all 32-bit targets are excluded. Distinguish backend planning, IDE generation, and real platform execution.

## Style and minimal validation

Public APIs use `namespace axlum` and responsibility-specific classes instead of header-level free functions. Keep CLI handling in `Application`. Types and source filenames use PascalCase; functions/locals use lowerCamelCase; instance members use `m_`; static/global variables use `s_`. Follow `.clang-format` and the author header in `docs/CODE_STYLE.md`. Keep build declarations in root `CMakeLists.txt`; do not replicate Iridium's directory organization.

Use the normal `build/` directory rather than silently switching to an installed sibling library. MSVC/Ninja bootstrap needs a developer environment and matching vcpkg triplet/CRT; retain `/bigobj` for the Debug Lua binding translation unit.

Within existing authorization, choose the smallest relevant check: static checks for formatting/docs, `native-plan` for planning, one offline package fixture for the bridge, or `command-lifecycle` for command behavior. Do not routinely run full regressions, repeat AOT, or run Rhodium's own tests. Check current Rhodium execution constraints against existing user authorization before deciding whether a necessary question remains.

Report the repositories/interfaces changed, checks actually executed, unverified platforms, and any outstanding gitlink change. Handle tags, commits, pushes, and releases only within the user's authorization.
