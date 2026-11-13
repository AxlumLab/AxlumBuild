# Cross-compilation library

The exported `axlumAnswer()` function returns 42. The monolithic shared target
uses the module API annotation on both MSVC and Clang.

```powershell
$env:ANDROID_SDK_ROOT = 'C:/SDK/android'
$env:ANDROID_NDK_ROOT = 'C:/SDK/android/ndk/android-ndk-r27c'
build/axlum-build.exe matrix -f examples/cross-library/axlum.build.lua -B build/cross
build/axlum-build.exe ide -f examples/cross-library/axlum.build.lua -B build/cross-projects --format android-studio --preset host
```

The matrix builds a host DLL and an Android ARM64 shared library. It requires a
working host toolchain and NDK; it does not install an SDK. Android Studio's root
Gradle tasks can build the configured host entry. The optional Android library
module delegates to AxlumBuild through its generated CMake bridge; see
[IDE_PROJECTS.md](../../docs/IDE_PROJECTS.md) for SDK requirements and validation limits.
