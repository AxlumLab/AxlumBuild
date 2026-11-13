# Build controls and reports

See [CLI.md](CLI.md) for `--parallel`, `--clean-first`, detailed clean modes,
`run`, native installation, target inspection and Rhodium SDK reports.

```powershell
build/axlum-build.exe build -B build/project -t Runtime -t App -j 8 --verbose --why-rebuild --timing `
  --logfile build/logs/project.log --report build/logs/result.json --trace build/logs/trace.json
build/axlum-build.exe rebuild -B build/project -t App -j 4
build/axlum-build.exe build -B build/project Runtime App --dry-run
```

- `-t/--target` repeats, and build/clean/rebuild also accept positional targets.
  Qualified workspace aliases work. Omission selects all targets.
- `-j/--jobs` controls Ninja parallelism; SDK actions receive the same MSBuild
  project parallelism. It is not a hard CPU limit on compiler/ILC internals.
- `rebuild` cleans the selected Ninja closure and builds it again. SDK managed
  actions use `dotnet build -t:Rebuild`; ordinary native builds retain Ninja's
  normal incremental behavior.
- `-v/--verbose` prints execution commands. Root `-v/--version` remains the version
  flag; subcommand `-v` is verbosity.
- `--why-rebuild` uses Ninja `-d explain` for missing inputs/outputs, command changes
  and dependency invalidation. Configuration checks are intentionally part of the
  graph; no native work may still include a reconfiguration check.
- `build --dry-run` requests Ninja's incremental plan without running build actions.
- `--logfile` streams merged stdout/stderr to the terminal and a file, including
  compiler failures. Child output is drained while the process runs.
- `--timing` prints elapsed time and completed Ninja action count. `matrix` supports
  jobs/verbosity/why/timing per entry. `publish` supports jobs, logging, reports and
  trace as well as detailed SDK verbosity.

## JSON results

Build/clean/rebuild write `<B>/axlum.last-build.json` unless `--report` overrides it.
Publish writes `<B>/axlum.last-publish.json`. Schema version 1 contains command,
build directory, requested targets/jobs, overall exit code/duration/error, phase
arguments/duration/status and completed Ninja action records (outputs, command
hash, start/duration). Reports are written for execution failures too. Errors
before command execution, such as invalid CLI arguments/unconfigured directories,
remain ordinary CLI errors.

`--trace` writes Chrome Trace Event JSON with phase spans and Ninja action spans.
Multi-output Ninja records are grouped into one action. Append offsets and
compaction-aware record comparison prevent old log records from being reported as
new work. The trace can be opened in a Chrome-trace-compatible viewer.

Action timings come from `.ninja_log`, not private compiler instrumentation.
Successful actions and overall/phase failures are represented; failed compiler
actions do not have successful Ninja log records. SDK internals appear within
their SDK/Ninja phase rather than as individual Roslyn/ILC tasks. Configuration
generators are included in completed Ninja action counts. For finer SDK profiling,
use the SDK's own tools on an existing project.

The build executor remains [Ninja](https://ninja-build.org/manual.html); AxlumBuild
provides stable entry points and reports rather than duplicating its scheduler.
