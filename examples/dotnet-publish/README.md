# Standard .NET publication and NativeAOT

Requires .NET SDK 10, NativeAOT's SDK packages and a compatible native toolchain.
The SDK restores its official packages; AxlumBuild does not invoke ILC directly.

```powershell
build/axlum-build.exe configure -f examples/dotnet-publish/axlum.build.lua -B build/publish --config Release
build/axlum-build.exe publish -B build/publish -t AotHello --rid win-x64 --timing
build/publish/publish/AotHello/win-x64/aot/AotHello.exe
build/axlum-build.exe publish -B build/publish -t AotLibrary --rid win-x64 --timing
```

The console prints `standard dotnet aot=42`. The shared library exports `axlum_add`
and calls a native C++ dependency; Vendor.dll is copied and its imports checked.
Publication manifests contain SHA-256, native exports/imports and dependency status.
RID/platform must match the configured native toolchain; regenerate with the
appropriate platform/architecture on other hosts.

For ordinary SDK publication, use `--no-aot --framework-dependent` or
`--no-aot --self-contained`. Modes/RIDs have isolated outputs, and obsolete owned
runtime files are removed when republishing the same output directory.

Only the official desktop .NET SDK/NativeAOT pipeline is supported. No Mono path
or application deployment/signing workflow is included. See [DOTNET.md](../../docs/DOTNET.md).

The tested machine's SDK selected ILCompiler 10.0.9 and emitted NU1903. Use a
serviced SDK/NativeAOT pack for released binaries; the actual package warning and
patched version are recorded in [validation](../../docs/NATIVE_VALIDATION.md).
