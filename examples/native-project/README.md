# Existing native project and target limits

Requires Windows Visual Studio C++ tools/MSBuild.

```powershell
build/axlum-build.exe configure -f examples/native-project/axlum.build.lua -B build/native-project --config Release
build/axlum-build.exe build -B build/native-project -t App
build/native-project/bin/App.exe
build/axlum-build.exe ide -f examples/native-project/axlum.build.lua -B build/native-project-ide --format vs
build/axlum-build.exe matrix -f examples/native-project/axlum.build.lua -B build/native-project-matrix --configure-only
```

Expected output: `external=42`. Root Debug/Release map to the existing project's
Development/Shipping configurations. The original vcxproj is directly included in
the solution; products/intermediates from CLI builds stay under the selected build
directory. Its original output directories remain untouched.

DebugOnly is absent in Release; explicitly requesting it reports the unsupported
configuration. LinuxOnly intentionally names a file unavailable on Windows and is
skipped there. ARM64 matrix entries have no eligible targets and are skipped before
toolchain discovery. Only x64/ARM64 are in scope.

See [the contracts](../../docs/TARGETS_AND_NATIVE_PROJECTS.md).
