# External tool integration boundaries

## Iridiumc / arbitrary code generation

`codegen` is the only common mechanism. It accepts a concrete host executable, argv array, source/build inputs, build outputs and optional GCC-style depfile. It is **not** aware of reflection declarations, ANTLR, ReflectKit, a C ABI, NativeAOT calling convention or C# binding metadata.

When adding Iridiumc, use a manifest target with `tool = "iridiumc"` (or an absolute tool path), enumerate every user-visible generated file in `outputs`, and model source inputs and codegen dependencies. If the generator produces variable output sets, introduce a stable output manifest and a graph reconfiguration mechanism rather than suppressing validation.

A host-built generator may use `tool = "@target/Iridiumc"` and a direct dependency. Building a host executable while cross-compiling for a different target is **not yet supported**. Do not mark cross-compiling host tools as implemented.

## Rhodium

Rhodium is pinned as a Git submodule in `third_party/rhodium` and linked via `Rhodium::Core`. Its Lua runtime and bindings supply metadata, includes and platform conditions. Build-specific declarations live in AxlumBuild, without changes to Rhodium.

User-project package management uses the embedded `DependencyResolver` and structured `PackageArtifactResolver` APIs. Lua `rhodium.require` / `rhodium.require_local` declare recipes; `packages` / `public_packages` attach private/public compile and link interfaces. Configure restores source, builds/exports package artifacts and reads Rhodium's existing lockfile/cache format. Build/graph refreshes restore only changed package inputs; unchanged cached artifacts are validated and reused. The AxlumBuild/Rhodium C++ bridge lives in `src/packages/`; Rhodium source is unchanged. See [DSL.md](DSL.md) and the package examples for the supported contract and limits.

The legacy `src/extensions/DependencyProvider.hpp` is still an unused future generic provider contract. NuGet/MSBuild remain responsible for managed dependencies. Exported package runtime DLL/SO files now enter tracked native runtime copies. Full install/package deployment, registries and a general ABI compatibility protocol remain separate concerns. The same Rhodium toolchain profile is used by package and native actions.

## Future schema/versioning

Version the manifest and any provider protocol before exposing a binary plugin ABI. Prefer an out-of-process provider CLI with a validated JSON schema initially, avoiding C++ STL types across independently compiled DLL/DSO ABI boundaries.

## Incremental hooks and composition

Explicit `prebuild`/`postbuild` actions and `workspace_reference` project composition now share the ordinary action graph. See [WORKSPACES_AND_ACTIONS.md](WORKSPACES_AND_ACTIONS.md). IDE generators delegate to the same configured graph, including hooks and referenced targets.
