# Advanced native and IDE features

These features cover framework interfaces, external PCHs, dialect selection,
native documentation and IDE customization.
They do not change the unsupported-platform boundary or claim new host execution
evidence. Rhodium remains a pinned submodule built as a Core dependency.

## Framework compile interfaces

`frameworkdirs` enters both compiler/PCH and linker argv. Concrete framework bundle
paths contribute their parent directory. Framework search paths propagate through
public dependency interfaces, including native packages and qualified modules.
Rhodium framework names combined with library directories containing the matching
bundle are also recognized. System frameworks continue to use the selected SDK.

This enables `<Vendor/Header.h>` imports from custom bundles. It does not embed or
sign framework bundles in an application distribution.

## C++14 and Latest

```lua
cppdialect("C++14")
-- or cppdialect("Latest") / cppdialect("C++Latest")
```

MSVC uses `/std:c++14` or `/std:c++latest`. GCC/Clang Latest probes supported dialect
switches from newest to oldest using an empty translation unit, then caches the
choice for that compiler identity in the process. This selects a supported compiler
mode, including experimental modes; it is not a promise of complete conformance.
Existing C++17/20/23 choices remain available. VS native project metadata reflects
the target's language standard; compile_commands retains actual compiler arguments.

## External PCH

```lua
pchheader("include/Pch.hpp")
pchfile("prebuilt/pch.pch")
pchobject("prebuilt/pch.obj") -- MSVC creator object, when needed
```

Paths may use `@build/` to consume another declared producer. No PCH creation action
is emitted. The header and PCH binary become compilation inputs; a supplied creator
object is linked once per binary. External artifacts are not owned or removed by
the consumer's clean action unless they belong to a declared dependency producer.

MSVC uses Yu/FI/Fp; Clang uses include-pch. GCC requires `pchfile` to be the adjacent
`<pchheader>.gch` file, matching GCC's lookup contract, with invalid-PCH diagnostics
enabled. A creator object is MSVC-specific. `pchsource` and `pchfile` are mutually
exclusive. Use the same compiler, dialect, ABI, options, definitions and headers as
the producer; compiler diagnostics enforce binary compatibility. Existing automatic
and shared-PCH modes remain available.

## Native C++ documentation

```lua
cppdocumentation(true)
target("Library")
    kind("StaticLibrary")
    files({"src/**.cpp"})
    documentationdir("api-docs/${target}")
```

Compiler actions emit tracked `.xdc` files using `/doc`; xdcmake
merges them into `<documentationdir>/<output-name>.xml`. The default directory is
`<B>/docs/<target>`. XML outputs join target completion, incrementality, stale-output
ownership and clean. C# retains its separate `documentationfile` setting.

The option participates in workspace defaults, filters and module overrides.
Documenting an explicit PCH creator also tracks its XDC. MSVC's xdcmake must be
installed; other native compilers reject this option rather than pretending to emit
MSVC documentation. Doxygen or other documentation systems can use codegen/hooks.

## IDE customizers

Lua declarations apply at workspace or target scope and respect filters:

```lua
ide_customize("vs", {
    properties = { MyBuildProperty = "value<&>" },
    debugger = { LocalDebuggerCommandArguments = "--sample" },
    project_end = [[<PropertyGroup><MyExtension>enabled</MyExtension></PropertyGroup>]],
    filters_begin = [[<ItemGroup><Filter Include="Extra" /></ItemGroup>]],
    user_end = [[<PropertyGroup><MyUserSetting>enabled</MyUserSetting></PropertyGroup>]]
})
ide_customize("vscode", {
    patch = {
        launch = { stopAtEntry = true, args = {"--sample"} },
        task = { presentation = {echo = false} },
        workspace = {settings = {["editor.tabSize"] = 4}}
    }
})
```

Property names are validated and values escaped. Native VS projects receive
conditional build properties, debugger properties in `.vcxproj.user`, and raw
project/filter/user fragments. Project/filter/user fragments use the first entry;
build/debug properties are evaluated per configuration. User/filter files are
generated only when explicitly customized. Their ownership is tracked on
regeneration; unknown, unowned files remain untouched when no customization applies.

VSCode JSON merge patches have `launch`, `task`, `configuration`, and `workspace`
sections. `debugger` overrides string launch fields; workspace `properties` applies
to C/C++ configuration fields. Lua tables preserve JSON booleans, arrays and objects;
an explicit JSON string is accepted for null/empty-array operations.

Xcode `properties` adds build settings and `debugger` adds/overrides LaunchAction
attributes. Project fragments insert PBX object text. Android Studio `properties`
adds Gradle extra properties, project fragments add Gradle declarations, and
`debugger` supplies Android App run-configuration options. JSON patches are VSCode
specific; filter/user fragments are VS-specific. Raw fragments are trusted project
code and must follow their format's syntax. SDK-owned C# project content remains
controlled by its managed DSL/existing csproj rather than native VS XML fragments.

The public C++ interface in `backend/Ide.hpp` accepts a vector of
`IDEProjectCustomizer` callbacks on `IDEGenerator::generateIDE`. Each receives the configuration
context, target/format and mutable `IDECustomization`, after workspace and target
declarations have merged. An embedder can compute properties, debugger settings,
fragments or patches without changing generator internals. Referenced workspaces
carry their own defaults into their targets.
An empty target name denotes format/workspace-level configuration.

## Minimal checks

`native-plan` covers framework compile propagation, external PCH planning,
C++14/Latest switches, documentation graph edges and a C++ customizer callback.
`final-gaps` builds a small real Windows example, consumes a generated PCH without
recreating it, executes C++14/Latest applications, parses merged XML documentation,
checks no-op/update/clean behavior and validates Lua customization in all four IDE
outputs. No full regression, NativeAOT rerun or Rhodium test is required.

See [the runnable example](../examples/final-gaps/README.md). Apple/GNU-specific
execution still needs the matching host/compiler; planner checks are not host tests.

Primary references: [MSVC documentation comments](https://learn.microsoft.com/en-us/cpp/build/reference/doc-process-documentation-comments-c-cpp?view=msvc-170),
[GCC dialect options](https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html),
[Clang dialect support](https://clang.llvm.org/cxx_status.html).
