# Composed workspace and incremental build actions

```powershell
build/axlum-build.exe configure -f examples/workspace-suite/axlum.build.lua -B build/suite --preset dev
build/axlum-build.exe build -B build/suite
build/suite/bin/SuiteApp.exe
build/axlum-build.exe matrix -f examples/workspace-suite/axlum.build.lua -B build/suite-matrix
build/axlum-build.exe ide -f examples/workspace-suite/axlum.build.lua -B build/suite-projects --format all
```

Expected: `suite=42`. The Common workspace generates a header, compiles a static
library and publishes a separate binary. The app consumes its public native
interface. Editing Common/value.txt automatically rebuilds the affected actions;
an unchanged build does no native work. Python must be on PATH for the actions.

The `dev` and `release` matrix entries use isolated output trees. IDE output is
under `<B>/ide`, with VSCode, VS, Xcode and Android Studio entry points. Regenerate
on the host where the IDE/toolchain will run. See the main docs for platform limits.
