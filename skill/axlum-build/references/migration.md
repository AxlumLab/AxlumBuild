# Consumer migration

Compare real contracts before removing the old project definitions: target names,
public/private dependencies, compile definitions/CRT, platform conditions, host
generators, generated native/managed sources, runtime deployment, tests, installation
and downstream tool/package exports. Configuration success is not build parity.

Use one canonical recipe directory (for example `libraries/`). Replace legacy
recipes with `rhodium.require`/`rhodium.require_local`/`rhodium.system` declarations
and update both include-relative paths and workspace-relative local package paths.
Keep revisions and source URLs stable unless dependency changes are requested.
Keep local overrides and artifacts out of commits. Report an unavailable upstream
source even when a local override makes the development build pass.

Preserve consumer-facing CMake helpers when they remain a supported interface.
They can live under a compatibility directory and be installed by AxlumBuild.
Check exported paths after relocating an installation. Kit archives and headers
do not imply a complete dependency-free SDK: Rhodium development artifacts are
not automatically installed as independent libraries.

Generated install headers use explicit `@build/` inputs and an appropriate
`installheaderbase`, which can itself be build-relative. Exclude test/probe outputs
from a distributable SDK when they are not part of its contract.

Validate a real build, unchanged rebuild, affected tests and any preserved install
contract. State which platform/configuration was executed. Update current build
instructions and AGENTS.md; dated historical reports may keep historical commands.
Do not delete local build/cache directories merely to remove tracked CMake files.

For VS Code extension changes, run the relevant Node tests and syntax checks;
`editor-integration` runs an isolated Windows VS Code host with real build, run,
debug and Testing API behavior. Package using `editors/vscode/package.py` and
install the resulting VSIX only within the task's scope. Existing workspaces may
need Reload Window and one reconfigure to obtain new metadata.

When committing an authorized multi-repository migration, review each index and
remote separately. Preserve unrelated local edits/deletions, exclude caches,
generated artifacts and local planning files, and push normal commits without
force. Do not update the Rhodium gitlink merely because another checkout is newer.
