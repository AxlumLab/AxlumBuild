# Axlum Build for VS Code

This repository owns the extension source, tests and dependency-free VSIX packager.
Open an Axlum-generated `.code-workspace`, then click **Axlum Build** in the left
activity bar or run **Axlum: Open Build Panel**. The native sidebar separates
configuration, build target, and run target; it opens no editor tab. Set
`axlum.openPanelOnStartup` to true to focus it automatically.

Three compact status-bar shortcuts build, run and debug the selected targets.
Hover to see the target and configuration. While a build/configure operation runs, Build becomes
Cancel and Run/Debug are disabled. Running programs do not lock the build controls. The shortcuts are hidden outside Axlum workspaces.

Selections persist
per workspace/configuration. Building uses the build target; running/debugging
builds the run target and its dependencies, regardless of the build selection.
Failed or cancelled builds never launch a stale executable. The cancel button
stops only the current build/configure task. Hidden/unavailable targets are excluded from the selectors.

Run first builds the selected executable, then opens a dedicated **Launch - target**
terminal, separate from build output. Subsequent runs of the same configuration and
target reuse that terminal when idle; closing it creates a fresh terminal next time.
Build tasks never reuse the launch terminal. Program output remains visible after exit.
Once the program starts, configuration/target selection and builds are available again.
**Running Programs** lists native/managed launches and Axlum debug sessions, each with
its own Stop action. **Axlum: Stop Running Program** also offers a picker. Clicking Run
for an already running target reveals that session; stop it before restarting. Different
targets/configurations can run concurrently. Run returns when the process is launched;
its exit result is reported in the terminal, sidebar status and Axlum output channel.

Build/Run/Debug save modified existing files before executing. If saving fails, the
operation stops. Set `axlum.saveBeforeBuild` to false to manage saving yourself;
Reconfigure always saves. Untitled documents still require Save As.

## One selection across editor entry points

The Tasks menu exposes **Axlum: Build selected target**, **Build all**, **Build launch
target**, **Clean selected target** and **Run selected target**. They read the current
sidebar selection when executed, including tasks discovered before a configuration
switch. Stock generated picker tasks are upgraded to this behavior on project load;
explicit custom commands and fixed target tasks are preserved. Tasks can also declare
`{"type":"axlum","action":"build"}`; optional `target` and `configuration` pin a task.

Select **Axlum: Selected Target** in VS Code's Run and Debug dropdown to use the same
run target with native F5/Ctrl+F5. It resolves the current adapter, executable and
per-target personal launch settings at invocation, and builds before debugging.
Existing fixed-target launch and attach configurations remain available by explicit
selection. The plugin adds this selected-target entry without replacing user entries.

Command variables for personal tasks/launch configurations:
`axlum.buildDirectory`, `axlum.activeConfiguration`, `axlum.buildTarget`,
`axlum.launchTargetName`, `axlum.launchTargetPath`, `axlum.launchTargetDirectory`.
Use `${command:axlum.launchTargetPath}`, for example. These queries do not build;
use **Axlum: Build launch target** as a pre-launch task when a build is needed.

| Shortcut | Action |
|---|---|
| F7 | Build selected target |
| Shift+F7 | Select a build target and build |
| Shift+F5 (outside debug sessions) | Debug selected run target |
| Ctrl+Shift+F5 (outside debug sessions) | Run selected target |
| Ctrl+Shift+B / Cmd+Shift+B | Build selected target |
| F5 / Ctrl+F5 while the Axlum sidebar has focus | Debug / Run selected target |

The CMake-style F7/Shift+F7/Shift+F5/Ctrl+Shift+F5 aliases are enabled in Axlum
workspaces only when CMake Tools' full project mode is inactive, avoiding competing
bindings when both extensions manage projects. Native debug-session shortcuts,
including Shift+F5 to stop debugging, retain their standard meaning.

## Reconfiguration

**Reconfigure** saves files, replays the original IDE generation command, then
reloads the target/configuration lists and IntelliSense paths. The command retains
the manifest, output/build roots, selected presets, architecture, toolchain,
platform, CLI option overrides and lock policy. This refreshes all entries in the
generated workspace. Configuration switching itself does not run this command.
**Refresh panel** only reads saved metadata and never runs a configure/build.

Old metadata without a replay command still supports build/run; regenerate it once
with the updated CLI to enable reconfiguration. Failed configuration is reported
in the panel/terminal; it does not clear remembered selections. Files are watched
for subsequent metadata regeneration, including delete/recreate.

Switching configuration synchronizes the C/C++ language service with the selected
compilation database. `axlum.languageService` defaults to `auto`: installed clangd
is preferred; otherwise Microsoft C/C++ is used. Select `cpptools` or `clangd` to
choose explicitly. Only one semantic engine is enabled for this workspace, so a
global `Tag Parser` setting does not silently disable semantic checking.

For Microsoft C/C++, Axlum registers a configuration provider with per-source
include paths, defines and compiler settings. Headers inherit the closest source
or include-root context; browsing includes the project and dependency headers.
Explicit `c_cpp_properties.json` settings still take precedence: set its
`configurationProvider` to `axlum.axlum-build` or `${default}`.
For clangd, Axlum sets `--compile-commands-dir` at workspace scope, preserving
other arguments. Configuration changes restart clangd through its extension.
Clangd indexes included headers using the source compilation commands.
Set `axlum.syncIntelliSense` to false to manage language-service settings yourself.

## Debugger support

**Check debugger** reports the selected adapter and missing extensions/tools before
debugging. User-selected adapter types, arguments, working directory, environment
and custom pre-launch tasks are retained. Generated program/project paths follow
the selected target. MI-only fields are removed for DAP adapters and environment /
stop-on-entry fields are normalized between native adapters.

| Target | Default adapter | Requirements / validation |
|---|---|---|
| Windows MSVC | `cppvsdbg` | Microsoft C/C++; real breakpoint, args/env and failure/cancel checks on Windows |
| GCC / other MI targets | `cppdbg`, GDB | Microsoft C/C++ plus GDB; PATH/explicit executable checked; Linux runtime validation pending |
| macOS native | `lldb-dap` | LLVM LLDB DAP extension plus LLDB DAP executable; macOS runtime validation pending |
| Existing .NET SDK executable | `dotnet` | C# Dev Kit; managed debug runtime validation pending |
| Generated managed executable | `coreclr` | Microsoft C# extension; managed debug runtime validation pending |

Native and managed **Run** use the CLI/terminal without requiring a debugger
extension. Configurations with a custom pre-launch task or envFile use the
debugger's no-debug launch to preserve that behavior. Managed CLI run is covered
by the Windows host test. A custom LLDB-MI setup must explicitly provide
`miDebuggerPath` pointing to `lldb-mi`; ordinary `lldb` is not an MI adapter.
Cross-compiled native programs are not offered as local run targets.

## Local installation

From the repository root:

```powershell
python editors/vscode/package.py
code --install-extension editors/vscode/dist/axlum-build-0.3.0.vsix --force
```

Reload VS Code after updating. Open the generated workspace, or set
`axlum.projectFile` to the absolute `axlum.ide.json` path. The extension only runs
in trusted workspaces. No npm install is required. Local packages are not published
to the Marketplace; CI produces a downloadable VSIX artifact.

## Tests

`node --test editors/vscode/test/model.test.js` tests selection and debugger setting
normalization. `tests/EditorIntegration.py` runs a real Windows extension host with
isolated settings: a native sidebar, status-bar shortcuts, two configurations, native and managed run,
dirty-document saving, dynamic task/debug selection, concurrent resident programs and
per-program stopping, source breakpoints, successful/failed reconfiguration, target discovery, missing
debuggers, failure and cancellation. Its optional VS smoke check compares the
known C# migration behavior against an official SDK solution.

## Testing

The native VS Code **Testing** sidebar lists Axlum `test` registrations and
individual `gtest_discover_tests` cases for the selected configuration. Refresh
rereads metadata. Expanding an unbuilt GoogleTest group builds its target to
perform discovery. Run All or a selected test/group builds its registered targets
incrementally, then uses CTest for execution and timeouts. Disabled cases are
skipped. Results and output appear in Test Results; VS Code provides rerun and
rerun-failed actions. Cancelling a run stops its CTest process tree.

Debug is available for native executable tests without an emulator and uses the
existing debugger adapter, with the test's arguments, environment and working
directory. A debugger session stopped without an exit code is reported as skipped,
not a passing test. Command-only and managed test registrations can run through
CTest but do not yet offer test-specific debugging. `axlum.ctestPath` selects the
CTest executable (default: `ctest` on PATH). Tests run sequentially in this version.

After upgrading an existing generated workspace, use **Reconfigure workspace**
once to refresh test metadata with resolved debug arguments and environment.
