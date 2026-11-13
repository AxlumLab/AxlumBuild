# Managed DSL and source generator

Requires .NET SDK 10 for the committed TFM. The generator references the selected
SDK's Roslyn API assemblies directly, so no external analyzer package is needed.

```powershell
build/axlum-build.exe configure -f examples/managed-dsl/axlum.build.lua -B build/managed --config Release
build/axlum-build.exe build -B build/managed -t ManagedApp -j 2
dotnet build/managed/dotnet/ManagedApp/artifacts/bin/ManagedApp/release/Managed.Sample.dll
```

Expected: `value=42 metadata=sdk<&> nullable=True`. Editing value.txt changes generated
code. Setting its content to `warn` emits AXL001, promoted to an error by the DSL.
Generated C# sources are under `<B>/dotnet/ManagedApp/generated`.

This demonstrates constants, nullable, analyzers/generators, AdditionalFiles,
editor configs, warning policy, XML docs, metadata, assembly identity, optimization
and portable symbols. See [DOTNET.md](../../docs/DOTNET.md).
