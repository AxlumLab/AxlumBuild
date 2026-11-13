# Native command lifecycle

```powershell
build/axlum-build.exe -S examples/cli-lifecycle -B build/lifecycle --config Release
build/axlum-build.exe run -B build/lifecycle App --env AXLUM_RUN_VALUE=example -- "hello world"
build/axlum-build.exe publish -B build/lifecycle -t App --prefix build/lifecycle-install
build/axlum-build.exe --install build/lifecycle --prefix build/lifecycle-install --component development
build/axlum-build.exe clean -B build/lifecycle --mode intermediates -t App
build/axlum-build.exe uninstall --prefix build/lifecycle-install
```

App consumes a shared Library and a tracked Data.txt runtime file. Installation
includes their binary closure, explicit headers and Notice.txt under
share/CliLifecycle. See [CLI.md](../../docs/CLI.md).
