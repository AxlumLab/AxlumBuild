# Native options, PCH and runtime files

This example builds a DLL/shared library and a mixed C/C++ executable. It demonstrates workspace defaults and configuration filters, typed compiler/linker options, explicit and automatic PCH creators, custom output/object/import-library paths, and tracked runtime copies.

On Windows it also demonstrates `.def` exports, RC resources with declared dependencies, an embedded manifest, delay-loading, PDB outputs and Debug incremental linking. No Developer shell is needed to run AxlumBuild once the executable exists; embedded Rhodium finds VS and the Windows SDK.

From the repository root:

```powershell
build/axlum-build.exe doctor
build/axlum-build.exe configure -f examples/native-features/axlum.build.lua -B .axlum/native-debug --config debug
build/axlum-build.exe build -B .axlum/native-debug --target FeatureApp
Push-Location .axlum/native-debug/products/Debug/app
./feature_app.exe
Pop-Location
```

Expected output: `value=32 data=demo`. Run from the app directory because the example opens `config.txt` relative to its current directory.

`feature_runtime.dll` (or `libfeature_runtime.so`) and `config.txt` are copied beside the executable. `optional.txt` is skipped while absent. The library's original binary is in `products/Debug/runtime`, import library/EXP in `products/Debug/import` on MSVC, and objects/PCH in `objects/Debug/<target>`. Native `targetname` changes filenames while target selection still uses `FeatureApp` / `FeatureRuntime`.

Use a separate build directory for Release. Its configuration enables LTO with PCH. GCC/Clang argument/PCH planning is covered by planner tests; this change's real execution tests use Windows MSVC. See [native DSL documentation](../../docs/DSL.md) and [toolchain discovery](../../docs/TOOLCHAINS.md) for options and boundaries.
