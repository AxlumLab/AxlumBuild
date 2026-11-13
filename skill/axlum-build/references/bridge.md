# Embedded Rhodium troubleshooting

## Three locations, potentially different revisions

The consumer is AxlumBuild, its linked provider is `third_party/rhodium`, and the provider development worktree is the sibling Rhodium repository. Substitute actual paths in these read-only checks:

```powershell
git -C E:/Projects/AxlumBuild status --short
git -C E:/Projects/AxlumBuild ls-tree HEAD third_party/rhodium
git -C E:/Projects/AxlumBuild/third_party/rhodium rev-parse HEAD
git -C E:/Projects/Rhodium rev-parse HEAD
git -C E:/Projects/Rhodium status --short
```

Keep consumer-only compile interface adaptation in AxlumBuild. Locate generic parsing/restore/detection defects in Rhodium, then decide whether to update the independent repository and consumer pin according to the request. Do not silently patch vendored sources, commit provider changes, or copy worktree files without accounting for the gitlink. For an explicitly requested main upgrade, fetch and verify the actual commit; do not assume a fetch necessarily creates `origin/main`. A verified `FETCH_HEAD` may be needed.

## Trace the adapter

`LuaManifest` -> Rhodium `LuaRuntime/LuaBindings/LuaDependencyReader` -> AxlumBuild `PackageManifest` -> `PackageManager::resolvePackages` -> Rhodium `DependencyOverrides`, `DependencyRestoreRequest`, `DependencyResolver` -> `LockFile/PackageArtifactResolver` -> package targets and consumer compile/link/runtime interfaces.

Trace SDK selection through `ToolchainDiscovery` -> `ToolchainDetector/ToolchainRegistry` and the restore request's `ToolchainSnapshot`. Dependencies and consumers must agree on configuration, architecture, compiler, SDK, CRT, linkage, and PIC where applicable. Matching tool paths or library filenames does not establish ABI compatibility; MSVC import libraries and static libraries are not interchangeable.

### Check the filename migration seam

Rhodium's `Path::manifest/lockFile/stateRoot` prefers `rhodium.lua`, `rhodium.lock.toml`, and `.rhodium`, with compatibility handling for `tpkg.lua`, `tpkg.lock.toml`, and `.tpkg`. AxlumBuild's bridge has previously hardcoded `tpkg.lock.toml`; documentation can also retain legacy names.

If restore succeeds but a lockfile or artifact cannot be found, inspect which path each implementation actually selected, whether workspace roots match, and whether old and new names coexist. Do not copy/rename lockfiles or clear caches to hide a mismatch. A compatibility fix should use the provider's path-selection contract and preserve existing data semantics. This is a search lead, not a claim that future versions retain the defect.

Lockfiles, artifact IDs, and cache layout are persistent Rhodium contracts. Do not change them incidentally. Locked restore does not necessarily freeze mutable local source contents; inspect actual fingerprints and invalidation before deciding what must restore. Start with package/config/profile diagnostics, logs, and artifact metadata, then invalidate only the affected scope.

## Keep the DSLs separate

An AxlumBuild `axlum.build.lua` can contain:

```lua
workspace("Demo")
    configuration("Debug", "Release")
    architecture("x64")
rhodium.require_local("headers", {
    path = "packages/headers", build = "header_only",
    artifacts = { include_dirs = {"include"} }
})
target("App")
    kind("ConsoleApp")
    files("src/Main.cpp")
    packages("headers")
```

Standalone Rhodium uses `package/version/default_config/require` in `rhodium.lua` and exports CMake integration. SDK target `Rhodium::Core` differs from consumer targets such as `rhodium::fmt`; inspect actual generated files for legacy `tpkg::` names.

## Execution boundaries

Read current worktree instructions, Rhodium `CONTRIBUTING.md`, and `docs/contributing/agent-guide.md`. The inspected guide says "Do not compile" and "Do not run tests". Determine whether the current task or existing session authorization already covers the required action; do not ask again when it does. If a necessary action genuinely lacks authorization, identify the exact applicable constraint. Building an embedded dependency and running Rhodium's standalone tests are distinct actions.

MSVC/Ninja bootstrap needs initialized tools; project builds may recover their environment through Rhodium. Match cached configuration and CRT instead of assuming old `install-static/static-deps` paths exist. Put real vcxproj incremental fixtures under normal `build/`: the local MSBuild file tracker has previously omitted outputs under system TEMP.
