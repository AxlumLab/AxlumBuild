# Using the AxlumBuild agent skill

Entry point: [`axlum-build/SKILL.md`](axlum-build/SKILL.md). `agents/openai.yaml` supplies UI metadata; `references/` provides guidance to load only when relevant. Each skill includes the cross-repository boundary and works without installing the other package.

## Use directly without installation

Ask a file-capable agent:

```text
Read E:/Projects/AxlumBuild/skill/axlum-build/SKILL.md first, then use it to handle this issue:
<workspace, actual command/error, profile/configuration, and expected behavior>
```

Replace the example root on other machines. From the repository root, the relative path is `skill/axlum-build/SKILL.md`. A repository's custom `skill/` directory is not guaranteed to be automatically discovered; explicitly requesting that the agent read the entry point works without installation.

## Optional Codex installation

Run in this repository's root using PowerShell:

```powershell
$taskSkillRoot = if ($env:CODEX_HOME) {
    Join-Path $env:CODEX_HOME 'skills'
} else {
    Join-Path $env:USERPROFILE '.codex/skills'
}
$taskDestination = Join-Path $taskSkillRoot 'axlum-build'
if (Test-Path -LiteralPath $taskDestination) {
    throw 'A skill with this name already exists; compare versions before updating.'
}
New-Item -ItemType Directory -Path $taskSkillRoot -Force | Out-Null
Copy-Item -LiteralPath './skill/axlum-build' -Destination $taskDestination -Recurse
```

Start a new Codex session and check its available skills. Invoke `$axlum-build` explicitly:

```text
Use $axlum-build to investigate this issue, fix the root cause within scope, and perform only the smallest relevant authorized checks.
```

Implicit selection remains enabled by default; actual discovery depends on the client. Installed copies do not automatically synchronize with repository changes. Compare updates before replacing personal customizations.

## Example requests

- "Use $axlum-build to add a local header-only dependency to this C++ workspace and update axlum.build.lua."
- "Use $axlum-build to investigate restore success followed by a missing link library; compare the submodule and sibling Rhodium revisions."
- "Use $axlum-build to fix target clean/run behavior, with one relevant check and no AOT run."

Include the package/source revision, profile, platform/architecture, Debug/Release configuration, actual command, and diagnostic output where available. For cross-repository work, supply both roots or allow discovery through sibling directories and `.gitmodules`.

## Permissions and maintenance

Loading a skill does not independently authorize builds, tests, network restore, gitlink updates, commits, pushes, or releases. Apply the current task, existing session authorization, and current repository instructions. Do not repeat permission questions when already authorized. For Rhodium's default no-build/no-test agent rules, first determine whether the user has explicitly covered the required action.

Validate the skill itself, adapting the creator-script location if needed:

```powershell
python "$env:USERPROFILE/.codex/skills/.system/skill-creator/scripts/quick_validate.py" ./skill/axlum-build
```

This validates metadata/structure, not repository builds or tests. Verify API/path guidance against current source when maintaining the skill; troubleshooting leads are not fixed-version guarantees.
