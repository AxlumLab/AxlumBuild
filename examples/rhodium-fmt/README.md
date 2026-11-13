# Embedded Rhodium: Git package

`configure` fetches fmt 10.2.1 through Rhodium's embedded Git fetcher, exports its headers, writes `tpkg.lock.toml`, and attaches the package to `FmtHello`. The first configure needs network access. No Rhodium CLI is required.

```sh
build/axlum-build configure -f examples/rhodium-fmt/axlum.build.lua -B .axlum/fmt --config release
build/axlum-build build -B .axlum/fmt --target FmtHello
.axlum/fmt/bin/FmtHello
build/axlum-build configure -f examples/rhodium-fmt/axlum.build.lua -B .axlum/fmt --config release --locked
```

Windows executables have the `.exe` suffix. Expected output: `embedded rhodium: 42`.

`version` maps to `ref`; Rhodium records the resolved commit in the lockfile. It has no package registry: `source` must be explicit. `--locked` rejects changed dependency inputs or an absent lock entry. The lockfile can be committed; `.rhodium/` is a local cache. The offline `../rhodium-local` example also demonstrates building a package library and exporting its interface to another target.
