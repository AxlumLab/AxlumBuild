# Module binary planning

`axlum.build.lua` declares three modules in one executable target. Base and Extra
share Core; Logic forms a second shared library. ModuleApp links both binaries.
`public_deps` propagates the dependency's compile interface; `private_deps` makes
it available only to the declaring module. Private definitions stay private.

```powershell
build/axlum-build.exe configure -f examples/modules/axlum.build.lua -B build/modules
build/axlum-build.exe build -B build/modules -t ModuleApp
build/modules/bin/ModuleApp.exe
```

Expected output: `module result=42`. On Windows Core.dll and Logic.dll are beside
the executable and their import libraries are in lib/. On Linux the shared
libraries are produced in lib/ and copied beside the executable.

Change `linktype("Modular")` to `linktype("Monolithic")`, then invoke **build**
again. The module sources are merged directly into ModuleApp, the API macros
become empty, and previously declared DLLs/copies are removed. A separate
configure invocation is unnecessary. Module dependencies must remain acyclic,
and grouping must not create a cycle between binaries.

Module names and binary group names are scoped to their target. A binary has a
Ninja target name such as `ModuleApp__Core`, with output basename Core. Use
ordinary target dependencies for references between independently owned target
graphs. This example exercises the build system's native modules, not C++20
language modules or engine script bindings.
