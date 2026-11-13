# Legacy TOML schema

The default manifest is now `axlum.build.lua`; see [DSL.md](DSL.md). This legacy format is still accepted with an explicit `-f` path.

The parser is deliberately **not a complete TOML 1.0 implementation**. It supports `[workspace]`, repeated `[[target]]`, repeated `[[module]]`, quoted strings (double or literal single quotes), one-line arrays of quoted strings, `#` comments, and basic `\\`, `\"`, `\n`, `\r`, `\t` escapes. It rejects unknown sections/keys, duplicate keys, unquoted scalar values and unsupported TOML syntax with a source line. Strings are UTF-8 input; full Windows Unicode path testing is pending.

| Section | Field | Type | Notes |
| --- | --- | --- | --- |
| `[workspace]` | `name` | string | Required; one workspace per manifest. |
| `[[target]]` | `name` | string | Required, globally unique, ASCII letters/digits/`_`/`-` with non-digit first. |
| `[[target]]` | `kind` | string | Required; executable, static_library, shared_library, header_only, dotnet_project, codegen. |
| `[[target]]` | `sources` | string[] | Required collectively with any modules for native target. Paths relative to manifest. |
| `[[target]]` | `public_dependencies` | string[] | Build/link locally and expose dependency's compile interface to consumers. |
| `[[target]]` | `private_dependencies` | string[] | Build/link locally, not exported as compile interface. |
| `[[target]]` | `interface_dependencies` | string[] | Re-export dependency interface to consumers; not compiled/linked directly by declaring target. |
| `[[target]]` | `public_includes`, `private_includes` | string[] | Relative to manifest, or `@build/...`. |
| `[[target]]` | `public_defines`, `private_defines` | string[] | Compiler definitions, e.g. `"FEATURE=1"`. |
| `[[target]]` | `c_flags`, `cxx_flags`, `link_flags` | string[] | Compiler/linker argument arrays; no shell splitting. |
| `[[target]]` | `c_standard`, `cpp_standard` | string | `c11`/`c17`, `c++17`/`c++20`/`c++23` (toolchain-dependent). |
| `[[target]]` | `libraries`, `library_directories` | string[] | `"m"` for GCC `-lm`, `"user32.lib"` on MSVC; path libraries tracked when supplied as paths. |
| `[[target]]` | `project` | string | `dotnet_project` path to an existing `.csproj`. |
| `[[target]]` | `tool`, `args`, `inputs`, `outputs`, `depfile` | string/string[] | `codegen`: tool executable and declared I/O; output and depfile relative to build directory, input relative to manifest or `@build/`. |
| `[[module]]` | `name`, `target`, `sources` | string, string, string[] | Source grouping under existing native target. `name` unique per target. |
| `[[module]]` | compile fields above | string/string[] | Module-local public/private includes/defines and flags; optional module-level language-standard override. |

`[[module]]` cannot declare a new target, linker options, or dependencies. Target and Module data are distinct by design.

## Paths and substitutions

- Native source paths and `.csproj` paths are anchored to the manifest's directory, regardless of caller working directory.
- `@build/relative/file.cpp` names a generated **input** in the configured build directory.
- Codegen `outputs` and `depfile` are **relative to the build directory**, with traversal outside rejected.
- Codegen `args` allow `${workspace}`, `${build}`, `${config}` (substituted as **one argument**, not shell text). `${config}` produces `Debug` or `Release`.
- `tool = "@target/SomeGenerator"` resolves to a host executable target output, which must be a direct public/private dependency of the codegen target. Cross-compiled host tools are not supported.
- Output layout: `<build>/obj/<target>/<module>/...`, `<build>/lib/...`, `<build>/bin/...`, `<build>/dotnet/<target>/...`, `<build>/generated/...` as declared.
- Source files have unique ownership within a workspace. Reusing the identical source file in multiple targets is intentionally rejected at this stage.

## Errors

Unknown target dependencies, target cycles, source ownership collisions, output path duplication, absent files, unsupported source suffixes, malformed keys and invalid kinds all fail configuration. A codegen command that exits successfully but neglects a declared output fails its Ninja action. Failed commands remove their declared outputs.

## Environment and configurations

- Target triple currently equals the supported host triple. No untested cross toolchain is selected silently.
- `CC`/`CXX` override Linux defaults; `--compiler gcc|clang` chooses the family. Windows uses VS2022 MSVC from its initialized environment.
- Build directories are configuration/toolchain specific; `configure` rejects incompatible reuse. The state file records config, target triple and a compiler-tool-path fingerprint.
- The toolchain/ABI fingerprint is **not** an artifact-cache key and does not validate external binary ABI compatibility.
- `build`/`clean` use the selected `-B` directory. `graph` requires a previous `configure` and prints known actions.

This legacy TOML subset does not expose the newer cross-toolchain, module-group,
C# compiler or publication declarations. Use the [Lua DSL](DSL.md) for those
features and [DOTNET.md](DOTNET.md) for official SDK publish/NativeAOT support.
