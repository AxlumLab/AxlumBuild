'use strict';
const vscode = require('vscode');
const fs = require('node:fs/promises');
const path = require('node:path');
const model = require('./model');
const { createPanel, openPanel } = require('./panel');
const { TaskRun } = require('./task-run');
const { registerEditorEntries, syncEditorEntries } = require('./editor-entries');

class Controller {
    constructor(context) {
        this.context = context;
        this.saved = context.workspaceState.get('selection', {});
        this.busy = false;
        this.runs = new Map();
        this.nextRunId = 1;
        this.status = 'Ready';
        this.output = vscode.window.createOutputChannel('Axlum Build');
        context.subscriptions.push(this.output);
        this.panel = createPanel(this);
        this.bars = ['build', 'run', 'debug'].map((action, index) => {
            const bar = vscode.window.createStatusBarItem(`axlum.${action}`, vscode.StatusBarAlignment.Left, 50 - index);
            bar.name = `Axlum ${action}`;
            context.subscriptions.push(bar);
            return bar;
        });
    }

    async reload() {
        if (!vscode.workspace.isTrusted) return;
        if (this.busy) { this.reloadPending = true; return; }
        let file = vscode.workspace.getConfiguration('axlum').get('projectFile');
        const workspaceFile = vscode.workspace.workspaceFile;
        if (!file && workspaceFile?.scheme === 'file') {
            const candidate = path.join(path.dirname(workspaceFile.fsPath), 'axlum.ide.json');
            try { await fs.access(candidate); file = candidate; } catch { /* Ordinary workspace. */ }
        }
        this.watcher?.dispose();
        if (!file) { this.project = undefined; this.state = undefined; await this.tests?.refresh(); this.render(); return; }
        if (!path.isAbsolute(file)) file = path.resolve(workspaceFile ? path.dirname(workspaceFile.fsPath) : vscode.workspace.workspaceFolders?.[0]?.uri.fsPath || '.', file);
        this.projectFile = file;
        this.watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(path.dirname(file), path.basename(file)));
        for (const event of ['onDidChange', 'onDidCreate', 'onDidDelete'])
            this.watcher[event](() => {
                clearTimeout(this.reloadTimer);
                this.reloadTimer = setTimeout(() => this.guard(() => this.reload()), 150);
            });
        try {
            this.project = model.validate(JSON.parse(await fs.readFile(file, 'utf8')));
            this.state = model.selection(this.project, this.saved);
            this.folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(this.project.sourceDirectory));
            await this.persist();
            await this.syncIntelliSense();
            await syncEditorEntries(this);
            await this.tests?.refresh();
        } catch (error) {
            this.project = undefined;
            this.render();
            throw error;
        }
        this.render();
    }

    async guard(action) {
        try { return await action(); }
        catch (error) {
            this.output.appendLine(String(error.stack || error));
            this.status = error.message;
            this.render();
            vscode.window.showErrorMessage(`Axlum: ${error.message}`);
            return false;
        }
    }

    requireProject() {
        if (!vscode.workspace.isTrusted) throw new Error('Trust this workspace before building.');
        if (!this.project) throw new Error('Open an Axlum-generated .code-workspace first.');
    }

    async persist() {
        this.saved = model.saveSelection(this.saved, this.state);
        await this.context.workspaceState.update('selection', this.saved);
    }

    async syncIntelliSense() {
        try { await require('./intellisense').sync(this); }
        catch (error) { this.output.appendLine(`Language service configuration failed: ${error.message}`); }
    }

    async select(kind, value) {
        this.requireProject();
        if (this.busy) throw new Error('Wait for the current operation or cancel it before changing targets.');
        const items = kind === 'configuration'
            ? this.project.entries.map(e => ({ label: e.id, description: `${e.configuration} / ${e.architecture}`, value: e.id }))
            : (kind === 'run' ? this.state.runnable : [{ name: 'all', display_name: 'All targets', group: 'Build' }, ...this.state.visible])
                .map(t => ({ label: t.display_name || t.name, description: t.group, value: t.name }));
        const picked = value === undefined
            ? await vscode.window.showQuickPick(items, { title: `Axlum: select ${kind}`, placeHolder: 'Selection is remembered for this workspace', matchOnDescription: true })
            : items.find(item => item.value === value);
        if (!picked) {
            if (value !== undefined) throw new Error(`Unavailable ${kind}: ${value}`);
            return;
        }
        if (kind === 'configuration') {
            this.saved.entry = picked.value;
            this.state = model.selection(this.project, this.saved);
            await this.syncIntelliSense();
        } else this.state[kind] = picked.value;
        await this.persist();
        if (kind === 'configuration') await this.tests?.refresh();
        this.render();
        return picked.value;
    }

    render() {
        const active = !!this.project;
        vscode.commands.executeCommand('setContext', 'axlum.active', active);
        vscode.commands.executeCommand('setContext', 'axlum.busy', this.busy);
        vscode.commands.executeCommand('setContext', 'axlum.hasRuns', this.runs.size > 0);
        this.panel.refresh();
        const labels = ['$(tools) Build', '$(play) Run', '$(debug-alt) Debug'];
        const actions = ['build', 'run', 'debug'];
        this.bars.forEach((bar, index) => {
            const target = index === 0 ? this.state?.build : this.state?.run;
            const enabled = active && !this.busy && !!target;
            bar.text = this.busy && index === 0 ? '$(loading~spin) Cancel' : labels[index];
            bar.command = this.busy && index === 0 ? 'axlum.stop' : enabled ? `axlum.${actions[index]}` : undefined;
            bar.color = enabled || (this.busy && index === 0) ? undefined : new vscode.ThemeColor('disabledForeground');
            bar.tooltip = this.busy ? `${this.status}${index === 0 ? ' (click to cancel)' : ''}`
                : `Axlum ${index === 0 ? 'Build target' : 'Run target'}: ${target || 'Not selected'} (${this.state?.entry.id || ''})${index > 0 ? '; incrementally builds before launch' : ''}`;
            if (active) bar.show(); else bar.hide();
        });
    }

    createTask(action, target, entry) {
        const task = new vscode.Task(
            { type: 'axlum', action, target, configuration: entry.id },
            this.folder || vscode.TaskScope.Workspace,
            `${action} ${target} (${entry.id})`, 'Axlum',
            new vscode.ProcessExecution(this.project.executable, [action, '-B', entry.buildDirectory, '-t', target], { cwd: this.project.sourceDirectory }),
            entry.problemMatcher || '$gcc'
        );
        task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Shared, showReuseMessage: false };
        return task;
    }

    async executeBuild(action, target, entry) {
        return this.executeTask(this.createTask(action, target, entry));
    }

    get execution() { return this.operation?.runner?.execution; }

    async executeTask(task) {
        const operation = this.operation;
        if (!operation || operation.cancelled) return false;
        const runner = new TaskRun(task);
        operation.runner = runner;
        try {
            await runner.started;
            if (operation.cancelled) runner.cancel();
            return await runner.completion;
        } finally {
            if (operation.runner === runner) operation.runner = undefined;
        }
    }

    async saveBeforeBuild(force = false) {
        if (!force && !vscode.workspace.getConfiguration('axlum').get('saveBeforeBuild', true)) return;
        // saveAll can omit dirty documents which are loaded but not in an editor
        // tab (for example edits applied by another extension).
        for (const document of vscode.workspace.textDocuments)
            if (document.isDirty && !document.isUntitled && !await document.save())
                throw new Error(`Save failed: ${document.fileName}. No build or executable was started.`);
        if (!await vscode.workspace.saveAll(false))
            throw new Error('Save failed; no build or executable was started.');
    }

    launchConfiguration(selected, overrides = {}) {
        const configurations = vscode.workspace.getConfiguration('launch', this.folder?.uri).get('configurations', []);
        const personal = model.launchConfiguration(selected, configurations);
        const { type, name, program, projectPath, ...custom } = overrides;
        if (custom.request && custom.request !== 'launch') throw new Error('Axlum Selected Target requires a launch request.');
        const launch = model.launchConfiguration({ launch: { ...personal, ...custom } }, []);
        if (launch.preLaunchTask === selected.launch.preLaunchTask) delete launch.preLaunchTask;
        return launch;
    }

    async operate(action, requested = {}, onAcquire = () => {}) {
        this.requireProject();
        if (this.busy) throw new Error('An Axlum build/configure operation is already running.');
        const entry = requested.configuration
            ? this.project.entries.find(e => e.id === requested.configuration) : this.state.entry;
        if (!entry) throw new Error(`Unavailable configuration: ${requested.configuration}`);
        const launching = ['run', 'debug', 'resolveDebug'].includes(action);
        const target = requested.target || (action === 'buildAll' ? 'all'
            : launching || action === 'buildRun' ? this.state.run : this.state.build);
        if (!target) throw new Error('This configuration has no runnable executable.');
        const selected = entry.targets.find(t => t.name === target);
        if ((target !== 'all' && !selected) || (launching && !selected?.launch))
            throw new Error(`Unavailable target: ${target} (${entry.id})`);
        const runKey = `${entry.buildDirectory}:${target}`;
        const running = [...this.runs.values()].find(run => run.key === runKey);
        if (action === 'run' && running) {
            this.showRun(running.id);
            this.status = `${target} is already running. Stop it before restarting.`;
            this.render();
            return true;
        }
        const operation = { cancelled: false, disposables: [] };
        this.operation = operation;
        this.busy = true;
        this.status = `${action}: ${target} (${entry.id})`;
        this.render();
        onAcquire(operation);
        try {
            await this.saveBeforeBuild();
            if (operation.cancelled) return false;
            let launch = launching ? this.launchConfiguration(selected, requested.launchOverrides) : undefined;
            if (launching && action !== 'run') await this.requireDebugger(launch);
            if (operation.cancelled) return false;
            if (!await this.executeBuild(action === 'clean' ? 'clean' : 'build', target, entry)) {
                this.output.appendLine(`${action} ${target}: build failed or cancelled; no executable started.`);
                this.status = 'Build failed or cancelled; no executable started.';
                return false;
            }
            if (operation.cancelled) return false;
            if (launching) {
                if (action === 'run' && !launch.preLaunchTask && !launch.envFile) {
                    const environment = { ...Object.fromEntries((launch.environment || []).map(e => [e.name, e.value])), ...launch.env };
                    const args = ['run', '-B', entry.buildDirectory, '-t', target, '--no-build'];
                    if (launch.cwd) args.push('--cwd', launch.cwd);
                    for (const [key, value] of Object.entries(environment)) if (value !== null) args.push('--env', `${key}=${value}`);
                    args.push('--', ...(launch.args || []));
                    const terminalName = `Launch - ${target} (${entry.id})`;
                    const task = new vscode.Task({ type: 'process', target, configuration: entry.id },
                        this.folder || vscode.TaskScope.Workspace, terminalName, 'Axlum',
                        new vscode.ProcessExecution(this.project.executable, args, { cwd: this.project.sourceDirectory }));
                    task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, focus: true,
                        panel: vscode.TaskPanelKind.Dedicated, showReuseMessage: false, clear: false };
                    return await this.startRun(task, { target, configuration: entry.id, key: runKey, terminalName }, operation);
                }
                await this.requireDebugger(launch);
                if (operation.cancelled) return false;
                launch.__axlumTarget = target;
                launch.__axlumConfiguration = entry.id;
                launch.__axlumRunKey = runKey;
                if (action === 'resolveDebug') {
                    this.status = `Prepared debugger: ${target} (${entry.id}).`;
                    return launch;
                }
                const ok = await vscode.debug.startDebugging(this.folder, launch, { noDebug: action === 'run' });
                this.status = ok ? 'Debug session started.' : 'Debugger failed to start; see the Debug Console.';
                return ok;
            }
            this.status = 'Operation completed.';
            return true;
        } finally { await this.finishOperation(operation); }
    }

    async startRun(task, description, operation) {
        const id = String(this.nextRunId++);
        const run = { id, ...description };
        this.runs.set(id, run);
        run.runner = new TaskRun(task, result => {
            this.runs.delete(id);
            const status = result.cancelled ? 'stopped' : result.ok ? 'exited' : `failed (exit ${result.code ?? 'unknown'})`;
            this.output.appendLine(`${description.target} (${description.configuration}): ${status}.`);
            if (!this.busy) this.status = `${description.target}: ${status}.`;
            this.render();
        });
        // Cancellation during the transition from build to launch still owns this
        // process. After start, only the per-program stop controls own it.
        operation.runner = run.runner;
        const started = await run.runner.started;
        if (operation.cancelled) { run.runner.cancel(); return false; }
        if (!started) return false;
        this.status = `${description.target} started in its launch terminal.`;
        return true;
    }

    showRun(id) {
        const run = this.runs.get(String(id));
        if (!run) return;
        if (run.session) { vscode.commands.executeCommand('workbench.view.debug'); return; }
        vscode.window.terminals.find(terminal => terminal.name.includes(run.terminalName))?.show();
    }

    async stopRun(id) {
        if (id && typeof id === 'object') id = id.runId;
        if (id === undefined) {
            const items = [...this.runs.values()].map(run => ({ label: run.target, description: run.configuration, value: run.id }));
            const chosen = items.length === 1 ? items[0] : await vscode.window.showQuickPick(items, { title: 'Axlum: Stop Running Program' });
            if (!chosen) return;
            id = chosen.value;
        }
        const run = this.runs.get(String(id));
        if (run?.session) await vscode.debug.stopDebugging(run.session);
        else run?.runner.cancel();
    }

    commandValue(name) {
        this.requireProject();
        const target = this.state.runnable.find(t => t.name === this.state.run);
        const values = { buildDirectory: this.state.entry.buildDirectory, activeConfiguration: this.state.entry.id,
            buildTarget: this.state.build, launchTargetName: this.state.run,
            launchTargetPath: target?.launch.program, launchTargetDirectory: target?.launch.program ? path.dirname(target.launch.program) : undefined };
        if (values[name] === undefined) throw new Error(`No ${name} is available for the selected target.`);
        return values[name];
    }

    async requireDebugger(launch) {
        const providers = vscode.extensions.all.filter(e => (e.packageJSON.contributes?.debuggers || []).some(d => d.type === launch.type));
        if (!providers.length) {
            const recommended = { cppvsdbg: 'ms-vscode.cpptools', cppdbg: 'ms-vscode.cpptools', 'lldb-dap': 'llvm-vs-code-extensions.lldb-dap', lldb: 'vadimcn.vscode-lldb', coreclr: 'ms-dotnettools.csharp', dotnet: 'ms-dotnettools.csdevkit' };
            throw new Error(`Missing debugger ${launch.type}. Install/enable ${recommended[launch.type] || 'an extension providing that debugger'}.`);
        }
        if (launch.type === 'cppvsdbg' && process.platform !== 'win32') throw new Error('cppvsdbg requires Windows. Select GDB or LLDB for this host.');
        if (launch.type === 'cppdbg' && launch.MIMode === 'lldb' && !launch.miDebuggerPath)
            throw new Error('LLDB MI requires lldb-mi and miDebuggerPath. Prefer the lldb-dap adapter on macOS.');
        const tool = launch.type === 'cppdbg' ? launch.miDebuggerPath || 'gdb'
            : launch.type === 'lldb-dap' ? vscode.workspace.getConfiguration('lldb-dap', this.folder?.uri).get('executable-path') : undefined;
        if (tool && !tool.includes('${')) {
            const names = process.platform === 'win32' && !path.extname(tool) ? [tool + '.exe', tool] : [tool];
            const candidates = names.flatMap(name => path.isAbsolute(name) ? [name] : (process.env.PATH || '').split(path.delimiter).map(dir => path.join(dir, name)));
            const found = await Promise.all(candidates.map(file => fs.stat(file).then(stat => stat.isFile(), () => false)));
            if (!found.some(Boolean)) throw new Error(`Debugger executable not found: ${tool}. Configure miDebuggerPath / lldb-dap.executable-path or PATH.`);
        }
        return `${launch.type} — ${providers.map(e => e.id).join(', ')}`;
    }

    async checkDebugger() {
        this.requireProject();
        const target = this.state.runnable.find(t => t.name === this.state.run);
        if (!target) throw new Error('No executable selected.');
        const launch = model.launchConfiguration(target, vscode.workspace.getConfiguration('launch', this.folder?.uri).get('configurations', []));
        this.status = `${await this.requireDebugger(launch)}\n${launch.program || launch.projectPath}\nAdapter found; launching also requires compatible debugger tools, symbols and platform support.`;
        this.render();
        return launch;
    }

    async reconfigure() {
        this.requireProject();
        if (this.busy) throw new Error('An Axlum operation is already running.');
        const replay = this.project.reconfigure;
        if (!replay || !Array.isArray(replay.args) || replay.args[0] !== 'ide' || !replay.args.every(a => typeof a === 'string'))
            throw new Error('This metadata lacks a reconfigure command. Generate IDE projects once with the updated Axlum CLI.');
        const operation = { cancelled: false, disposables: [] };
        this.operation = operation;
        this.busy = true; this.status = 'Reconfiguring and updating IDE projects...'; this.render();
        try {
            await this.saveBeforeBuild(true);
            if (operation.cancelled) return false;
            const task = new vscode.Task({ type: 'process' }, this.folder || vscode.TaskScope.Workspace, 'Reconfigure workspace', 'Axlum',
                new vscode.ProcessExecution(this.project.executable, replay.args, { cwd: replay.cwd || this.project.sourceDirectory }));
            const ok = await this.executeTask(task);
            this.status = ok ? 'Reconfiguration completed.' : 'Reconfiguration failed or cancelled; see the terminal.';
            this.reloadPending = true;
            return ok;
        } finally { await this.finishOperation(operation); }
    }

    async finishOperation(operation) {
        for (const disposable of operation.disposables) disposable.dispose();
        if (this.operation !== operation) return;
        this.operation = undefined;
        this.busy = false;
        if (this.reloadPending) { this.reloadPending = false; await this.reload(); }
        this.render();
    }

    stopOperation(operation) {
        if (this.operation !== operation) return;
        operation.cancelled = true;
        operation.runner?.cancel();
    }
    stop() { if (this.operation) this.stopOperation(this.operation); }
    dispose() { clearTimeout(this.reloadTimer); this.watcher?.dispose(); this.panel?.dispose(); }
}

async function activate(context) {
    const controller = new Controller(context);
    context.subscriptions.push(controller);
    registerEditorEntries(controller, context);
    controller.tests = new (require('./testing').TestExplorer)(controller, context);
    context.subscriptions.push(vscode.debug.onDidStartDebugSession(session => {
        const config = session.configuration;
        if (!config.__axlumTarget) return;
        const id = `debug:${session.id}`;
        controller.runs.set(id, { id, session, target: config.__axlumTarget,
            configuration: config.__axlumConfiguration, key: config.__axlumRunKey });
        controller.render();
    }), vscode.debug.onDidTerminateDebugSession(session => {
        controller.runs.delete(`debug:${session.id}`);
        controller.render();
    }));
    for (const name of ['buildDirectory', 'activeConfiguration', 'buildTarget', 'launchTargetName', 'launchTargetPath', 'launchTargetDirectory'])
        context.subscriptions.push(vscode.commands.registerCommand(`axlum.${name}`, () => controller.commandValue(name)));
    for (const [command, action] of Object.entries({
        openPanel: () => openPanel(controller),
        reconfigure: () => controller.reconfigure(), checkDebugger: () => controller.checkDebugger(),
        showOutput: () => controller.output.show(),
        selectConfiguration: value => controller.select('configuration', value),
        selectBuildTarget: value => controller.select('build', value),
        selectRunTarget: value => controller.select('run', value),
        buildWithTarget: async value => { if (await controller.select('build', value)) return controller.operate('build'); },
        build: () => controller.operate('build'), clean: () => controller.operate('clean'),
        run: () => controller.operate('run'), debug: () => controller.operate('debug'),
        stopRun: id => controller.stopRun(id), showRun: id => controller.showRun(id),
        stop: () => controller.stop(), reload: () => controller.reload()
    })) context.subscriptions.push(vscode.commands.registerCommand(`axlum.${command}`, value => controller.guard(() => action(value))));
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(e => {
        if (e.affectsConfiguration('axlum.projectFile') || e.affectsConfiguration('axlum.syncIntelliSense') || e.affectsConfiguration('axlum.languageService'))
            controller.guard(() => controller.reload());
    }));
    await controller.guard(() => controller.reload());
    if (controller.project && vscode.workspace.getConfiguration('axlum').get('openPanelOnStartup', false)) openPanel(controller);
    return { controller };
}

module.exports = { activate };
