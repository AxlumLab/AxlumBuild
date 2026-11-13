# Modules and automatic configuration

## Binary planning

```lua
target("App")
    kind("ConsoleApp")
    linktype("Modular")
    files("src/Main.cpp")
    module("Math")
        binarymodulename("Core")
        apidefine("MATH_API")
        public_includedirs("include")
        files("src/Math.cpp")
    module("Logic")
        binarymodulename("Logic")
        apidefine("LOGIC_API")
        private_deps("Math")
        files("src/Logic.cpp")
```

`public_deps` propagates the dependency's public compile interface;
`private_deps` provides that interface only to the declaring module. Private
include directories/definitions do not propagate. Missing modules, module cycles
and binary-group cycles fail during planning.

- `linktype("Monolithic")` is the default: module objects link/archive directly
  into the owning target. `binarymodulename` is ignored in this mode.
- `linktype("Modular")` creates one shared library per `binarymodulename`, defaulting
  to the module name. Modules sharing a group retain individual compilation/PCH
  settings. An owner with its own sources retains its executable/shared binary;
  an owner consisting entirely of modules becomes an aggregate of those binaries.
  Synthetic target names are `<owner>__<group>`; output basenames use the group
  and platform prefix/suffix. Output/import directories and native defaults inherit
  from the owner; object directories remain isolated. Group names are target-scoped;
  distinct owners must use distinct output directories/group names to avoid collisions.
- `apidefine("MATH_API")` provides a C identifier for annotated public declarations,
  e.g. `MATH_API int mathValue();`. MSVC uses `__declspec(dllexport)` inside
  the producing DLL and `__declspec(dllimport)` outside. GCC/Clang shared producers
  use default-visibility attributes. All modules in one group see its APIs as exports.
  Monolithic executables/static libraries use empty API macros; monolithic shared
  libraries preserve the export/import distinction.
- The owner links its groups and exposes their public interfaces. Group dependencies
  become binary link edges; runtime DLL/SO copies use the ordinary tracked copy
  planner. Cross-target modules use qualified `Target/Module` references, including
  `alias::Target/Module` for a referenced workspace. At target scope use
  `public_modules`/`private_modules`; inside a module use `public_deps`/`private_deps`.
  Resolution imports the module's public interface and selects its actual binary
  group/library. The global module graph is cycle-checked. This models native
  build modules, not C++20 language modules or engine bindings.

See [the executable example](../examples/modules/README.md).

## Configuration checks

Generated Ninja files contain a generator/restat rule invoking AxlumBuild before
each build, including direct `ninja -C <build>`. Lua and `include` files execute
again, globs expand again, package restoration checks local sources, and toolchain
discovery runs again. Rhodium retains package cache/lock/artifact responsibility.
The saved `--locked` policy is honored. Package input fingerprints cover evaluated recipes, consumer CRT policies, local-source file contents, overrides, patches, CMake toolchain files, lockfile and toolchain identity; unchanged inputs reuse validated artifacts. Missing/corrupt artifact metadata triggers restore. `graph` also refreshes the configuration.

The replacement graph is validated before publication. Unchanged graph, environment
and compiler database contents preserve timestamps, so native no-op builds remain
no-ops after the check. A changed compiler/SDK fingerprint changes a tracked
environment input, invalidating native actions. Explicit configure still rejects
mixing configurations/target triples/toolchains; automatic refresh keeps the saved
configuration and permits a changed fingerprint while forcing rebuilds. A build
directory cannot be reassigned to a different manifest.

`axlum.outputs` records action outputs. After writing a valid replacement graph,
previously declared outputs absent from its outputs and inputs are removed only
inside the build directory. This removes renamed binaries, removed-source objects
and obsolete optional-runtime copies. Unknown files and package caches are retained.

Every Ninja invocation pays for Lua/planning/discovery checks. There is no persistent
manifest/glob validation cache or filesystem watcher yet. SDK probes cover metadata,
tools and representative headers/libraries, rather than hashing the entire SDK.
RC scanning tracks literal local includes/resource paths conservatively; use
`resourceinputs` for macro-generated/conditional filenames. Flat runtime copying
does not implement installation, signing or general packaging.

An older configured directory acquires the generator rule automatically when built
through the current CLI. Direct Ninja use of an older plan needs this one-time
CLI build or configure first. For a runtime-loaded MSVC plugin, use
`importlibrary(false)` on the DLL and attach its produced DLL via `runtimefiles`
on the consumer; a plugin already output beside the app still belongs to its
selected-target build closure.
