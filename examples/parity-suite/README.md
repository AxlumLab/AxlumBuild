# Modules, PCH, managed interop and IDEs

```powershell
build/axlum-build.exe configure -f examples/parity-suite/axlum.build.lua -B build/parity --config Release
build/axlum-build.exe build -B build/parity -t ManagedApp -t PchConsumer -j 2
build/axlum-build.exe publish -B build/parity -t ManagedApp --framework-dependent
dotnet build/parity/publish/ManagedApp/portable/managed/ManagedApp.dll
build/axlum-build.exe ide -f examples/parity-suite/axlum.build.lua -B build/parity-ide --format all
```

Expected application output: `modular interop=42`. The call goes through the
NativeFacade DLL, a generated NativeCore binary group and a referenced workspace's
Math module. PchConsumer's two compilation groups share a compatible PCH.
Publishing copies both DLLs without manually listing NativeCore as a runtime file.
Release avoids requiring a developer-only debug CRT on the publication host.

The generated Visual Studio solution includes the real C# SDK project, and VSCode
includes its debugger entry. Android Studio adds an AndroidApp NativeActivity APK
module; enable `-PaxlumAndroid=true`, then select the generated Android App run
configuration or use `:app_AndroidApp:axlumRunDebug`. The minimal activity is a
loadable entry without game/UI behavior. Install the SDK/NDK/Gradle requirements
described in [supported-platform parity](../../docs/SUPPORTED_PLATFORM_PARITY.md).

No Mono, UWP/GDK/console or 32-bit backend is involved.
