# Embedded Rhodium: local packages

This example works offline. `rhodium.require_local` restores a header-only package and builds a small CMake static-library package. `public_packages` exports the package compile/link interface through `MathWrapper`; the application consumes the transitive header, definition and static library.

From the repository root, in a native compiler environment:

```sh
build/axlum-build configure -f examples/rhodium-local/axlum.build.lua -B .axlum/rhodium-debug --config debug
build/axlum-build build -B .axlum/rhodium-debug --target PackageHello
.axlum/rhodium-debug/bin/PackageHello
build/axlum-build configure -f examples/rhodium-local/axlum.build.lua -B .axlum/rhodium-debug --config debug --locked
```

On Windows use `axlum-build.exe` / `PackageHello.exe`. Output: `package result=42`.

Rhodium is embedded in AxlumBuild: no Rhodium executable is invoked. State lives in the workspace's `.rhodium/` (or an existing `.tpkg/`), with `tpkg.lock.toml` recording package/configuration/toolchain identities. Commit the lockfile for reproducibility; keep caches out of source control. Rerun configure to import local package changes. Use separate build directories for Debug and Release.

For a Git dependency example, see `../rhodium-fmt`.
