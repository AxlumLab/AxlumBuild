'use strict';
const vscode = require('vscode');
const fs = require('node:fs/promises');
const path = require('node:path');
const { spawn } = require('node:child_process');

const exactPattern = name => '^' + name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '$';

class TestExplorer {
    constructor(controller, context) {
        this.controller = controller;
        this.view = vscode.tests.createTestController('axlum.tests', 'Axlum Tests');
        this.records = new Map();
        this.registrations = [];
        context.subscriptions.push(this.view, this);
        this.view.refreshHandler = () => this.refresh();
        this.view.resolveHandler = async item => {
            if (!item) return this.refresh();
            const registration = this.registrations.find(r => `group:${r.name}` === item.id);
            if (registration?.target && await controller.guard(() => controller.operate('build', { target: registration.target }))) await this.refresh();
        };
        this.view.createRunProfile('Run', vscode.TestRunProfileKind.Run, (request, token) => this.run(request, token), true);
        this.view.createRunProfile('Debug', vscode.TestRunProfileKind.Debug, (request, token) => this.run(request, token, true), true);
    }
    dispose() { clearTimeout(this.timer); this.watcher?.dispose(); }
    async refresh() {
        const entry = this.controller.state?.entry;
        if (!entry) { this.view.items.replace([]); this.records.clear(); this.registrations = []; this.watcher?.dispose(); this.build = undefined; return; }
        const build = entry.buildDirectory;
        if (this.build !== build) {
            this.build = build;
            this.watcher?.dispose();
            this.watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(build, '{axlum.tests.json,tests/*.json}'));
            for (const event of ['onDidCreate', 'onDidChange', 'onDidDelete']) this.watcher[event](() => {
                clearTimeout(this.timer);
                this.timer = setTimeout(() => this.refresh().catch(e => this.controller.output.appendLine(String(e))), 200);
            });
        }
        let registrations;
        try { registrations = JSON.parse(await fs.readFile(path.join(build, 'axlum.tests.json'), 'utf8')); }
        catch (error) { if (error.code !== 'ENOENT') throw error; registrations = []; }
        const roots = [], records = new Map();
        const leaf = (spec, parent) => {
            const item = this.view.createTestItem(`test:${spec.name}`, parent ? spec.name.slice(parent.label.length + 1) : spec.name);
            item.description = spec.disabled ? 'Disabled' : (spec.labels || []).join(', ');
            records.set(item.id, { item, spec, parent: parent?.id });
            return item;
        };
        for (const spec of registrations) {
            if (!spec.discoveryFile) { roots.push(leaf(spec)); continue; }
            const group = this.view.createTestItem(`group:${spec.name}`, spec.name);
            try {
                const cases = JSON.parse(await fs.readFile(spec.discoveryFile, 'utf8'));
                group.children.replace(cases.map(item => leaf(item, group)));
                group.description = `${cases.length} tests`;
            } catch (error) {
                if (error.code !== 'ENOENT') throw error;
                group.canResolveChildren = true;
                group.description = 'Build to discover tests';
            }
            roots.push(group);
        }
        // A configuration switch may have completed while reading the files.
        if (this.controller.state?.entry !== entry) return;
        this.registrations = registrations;
        this.records = records;
        this.view.items.replace(roots);
    }
    async run(request, token, debug = false) {
        const run = this.view.createTestRun(request);
        const entry = this.controller.state?.entry;
        const include = request.include?.map(i => i.id), exclude = new Set((request.exclude || []).map(i => i.id));
        const matches = record => (!include || include.includes(record.item.id) || include.includes(record.parent)) &&
            !exclude.has(record.item.id) && !exclude.has(record.parent);
        try {
            this.controller.requireProject();
            const targets = new Set(this.registrations.filter(r => !include || include.includes(`group:${r.name}`) ||
                include.includes(`test:${r.name}`) || [...this.records.values()].some(v => matches(v) && v.spec.target === r.target)).map(r => r.target).filter(Boolean));
            for (const target of targets) {
                if (token.isCancellationRequested) return;
                const ok = await this.controller.operate('build', { target, configuration: entry.id }, op => {
                    op.disposables.push(token.onCancellationRequested(() => this.controller.stopOperation(op)));
                    if (token.isCancellationRequested) this.controller.stopOperation(op);
                });
                if (!ok) throw new Error('Test build failed or was cancelled.');
            }
            if (this.controller.state.entry !== entry) throw new Error('Configuration changed; run tests again.');
            await this.refresh();
            const records = [...this.records.values()].filter(matches);
            for (const record of records) run.enqueued(record.item);
            for (const { item, spec } of records) {
                if (token.isCancellationRequested || spec.disabled) { run.skipped(item); continue; }
                run.started(item);
                const start = Date.now();
                const result = debug ? await this.debugCase(spec, entry, token) : await this.executeCase(spec, entry, token, run);
                if (token.isCancellationRequested || result === undefined) run.skipped(item);
                else if (result === 0) run.passed(item, Date.now() - start);
                else run.failed(item, new vscode.TestMessage(`Test exited with code ${result}. See test output.`), Date.now() - start);
            }
        } catch (error) {
            run.appendOutput(`${error.message}\r\n`);
            for (const record of this.records.values()) if (matches(record)) run.errored(record.item, new vscode.TestMessage(error.message));
            this.controller.output.appendLine(`Tests: ${error.message}`);
        } finally { run.end(); }
    }
    executeCase(spec, entry, token, run) {
        return new Promise((resolve, reject) => {
            const executable = vscode.workspace.getConfiguration('axlum').get('ctestPath', 'ctest');
            const child = spawn(executable, ['--test-dir', entry.buildDirectory, '--output-on-failure', '--no-tests=error', '-R', exactPattern(spec.name)],
                { cwd: entry.buildDirectory, windowsHide: true, detached: process.platform !== 'win32' });
            const cancel = () => {
                if (!child.pid || child.exitCode !== null) return;
                if (process.platform === 'win32') spawn('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true }).on('error', () => child.kill());
                else { try { process.kill(-child.pid, 'SIGTERM'); } catch { /* Already exited. */ } }
            };
            const subscription = token.onCancellationRequested(cancel);
            if (token.isCancellationRequested) cancel();
            let skipped = false;
            const output = chunk => { const text = chunk.toString(); skipped ||= /\*\*\*Skipped/.test(text); run.appendOutput(text.replace(/\r?\n/g, '\r\n')); };
            child.stdout.on('data', output); child.stderr.on('data', output);
            child.on('error', error => { subscription.dispose(); reject(error); });
            child.on('close', code => { subscription.dispose(); resolve(skipped ? undefined : code ?? 1); });
        });
    }
    async debugCase(spec, entry, token) {
        const target = entry.targets.find(t => t.name === spec.target);
        if (!target?.launch || spec.emulator?.length || spec.resolved_command[0] !== target.launch.program)
            throw new Error('Debug requires a native test target without an emulator.');
        const launch = await this.controller.operate('resolveDebug', { configuration: entry.id, target: spec.target,
            launchOverrides: { args: spec.resolved_command.slice(1), cwd: spec.resolved_workingdir,
                env: spec.resolved_environment, __axlumTest: spec.name } }, op => {
            op.disposables.push(token.onCancellationRequested(() => this.controller.stopOperation(op)));
        });
        if (!launch || token.isCancellationRequested) return undefined;
        let session, exitCode, finish;
        const done = new Promise(resolve => { finish = resolve; });
        const tracker = vscode.debug.registerDebugAdapterTrackerFactory('*', {
            createDebugAdapterTracker: candidate => candidate.configuration.__axlumTest === spec.name ? {
                onDidSendMessage: message => { if (message.type === 'event' && message.event === 'exited') exitCode = message.body.exitCode; }
            } : undefined
        });
        const started = vscode.debug.onDidStartDebugSession(candidate => {
            if (candidate.configuration.__axlumTest === spec.name) { session = candidate; if (token.isCancellationRequested) vscode.debug.stopDebugging(session); }
        });
        const ended = vscode.debug.onDidTerminateDebugSession(candidate => { if (candidate.configuration.__axlumTest === spec.name) finish(exitCode); });
        const cancelled = token.onCancellationRequested(() => { if (session) vscode.debug.stopDebugging(session); });
        try {
            if (!await vscode.debug.startDebugging(this.controller.folder, launch)) return undefined;
            return await done;
        } finally { tracker.dispose(); started.dispose(); ended.dispose(); cancelled.dispose(); }
    }
}
module.exports = { TestExplorer, exactPattern };
