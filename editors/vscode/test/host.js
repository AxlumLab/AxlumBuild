'use strict';
const vscode = require('vscode');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const root = process.env.AXLUM_EDITOR_TEST_ROOT;
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

async function until(check, timeout = 20000) {
    const deadline = Date.now() + timeout;
    while (!await check()) {
        if (Date.now() > deadline) throw new Error('Timed out waiting for editor operation');
        await delay(100);
    }
}

async function run() {
    try {
        const extension = vscode.extensions.getExtension('axlum.axlum-build');
        assert(extension);
        const { controller } = await extension.activate();
        assert(controller.project, 'generated workspace activates extension');
        assert(controller.cppProvider, 'C/C++ custom provider is registered');
        const sourceUri = vscode.Uri.file(path.join(root, 'source/Library.cpp'));
        assert(await controller.cppProvider.canProvideConfiguration(sourceUri));
        const headerUri = vscode.Uri.file(path.join(root, 'source/Library.hpp'));
        assert(await controller.cppProvider.canProvideConfiguration(headerUri), 'headers inherit their source context');
        assert((await controller.cppProvider.provideBrowseConfiguration()).browsePath.length);
        assert.equal(vscode.workspace.getConfiguration('C_Cpp', controller.folder.uri).get('intelliSenseEngine'), 'default');

        const tabsBefore = vscode.window.tabGroups.all.flatMap(group => group.tabs).length;
        await vscode.commands.executeCommand('axlum.openPanel');
        const firstPanel = controller.panel;
        await until(() => firstPanel.view.visible);
        assert(extension.packageJSON.contributes.viewsContainers.activitybar.some(c => c.id === 'axlum'));
        assert.deepEqual(firstPanel.provider.getChildren().slice(0, 3).map(item => item.id), ['configuration', 'build', 'run']);
        await vscode.commands.executeCommand('axlum.openPanel');
        assert.equal(controller.panel, firstPanel, 'reuse the native sidebar');
        assert.equal(vscode.window.tabGroups.all.flatMap(group => group.tabs).length, tabsBefore, 'sidebar opens no editor tab');
        assert.deepEqual(controller.bars.map(bar => bar.command), ['axlum.build', 'axlum.run', 'axlum.debug']);
        assert.equal(controller.state.entry.id, 'Debug-x64');
        await vscode.commands.executeCommand('axlum.selectBuildTarget', 'Library');
        await vscode.commands.executeCommand('axlum.selectRunTarget', 'App');
        assert.equal(controller.state.build, 'Library');
        assert.equal(controller.state.run, 'App');
        assert.match(controller.bars[0].tooltip, /Library/);
        assert.match(controller.bars[1].tooltip, /App/);
        assert.equal(firstPanel.provider.getChildren({ id: 'build' })[0].description, 'Library');
        const libraryDoc = await vscode.workspace.openTextDocument(vscode.Uri.file(path.join(root, 'source/Library.cpp')));
        const edit = new vscode.WorkspaceEdit();
        edit.insert(libraryDoc.uri, new vscode.Position(0, 0), '// unsaved build input\n');
        assert(await vscode.workspace.applyEdit(edit));
        assert(libraryDoc.isDirty);
        assert.equal(await vscode.commands.executeCommand('axlum.build'), true);
        assert.equal(libraryDoc.isDirty, false, 'Build saves dirty source files');
        assert.match(await fs.readFile(libraryDoc.uri.fsPath, 'utf8'), /unsaved build input/);
        const configuredTasks = vscode.workspace.getConfiguration('tasks', controller.folder.uri).get('tasks');
        assert.equal(configuredTasks.find(t => t.label === 'Axlum: Build selected target').type, 'axlum');
        assert(vscode.workspace.getConfiguration('launch', controller.folder.uri).get('configurations').some(c => c.type === 'axlum'));
        const selectedTask = (await vscode.tasks.fetchTasks({type:'axlum'})).find(t => t.definition.action === 'build' && !t.definition.configuration);
        assert(selectedTask, 'Tasks menu exposes a build following the sidebar');
        assert.equal(await fs.stat(path.join(root, 'builds/Debug-x64/lib/Library.lib')).then(() => true), true);
        await vscode.commands.executeCommand('axlum.selectConfiguration', 'Release-x64');
        assert.equal(controller.state.build, 'App');
        assert.equal(vscode.workspace.getConfiguration('C_Cpp', controller.folder.uri).get('default.compileCommands'),
            path.join(root, 'builds/Release-x64/compile_commands.json'));
        // This task was discovered in Debug/Library; execution must use Release/App.
        const taskResults = [];
        const taskEnded = vscode.tasks.onDidEndTaskProcess(e => taskResults.push(e));
        const taskExecution = await vscode.tasks.executeTask(selectedTask);
        await until(() => taskResults.some(e => e.execution === taskExecution), 30000);
        assert.equal(taskResults.find(e => e.execution === taskExecution).exitCode, 0);
        taskEnded.dispose();
        assert(await fs.stat(path.join(root, 'builds/Release-x64/bin/App.exe')));
        assert.equal(await vscode.commands.executeCommand('axlum.activeConfiguration'), 'Release-x64');
        assert.equal(await vscode.commands.executeCommand('axlum.launchTargetPath'), path.join(root, 'builds/Release-x64/bin/App.exe'));
        await vscode.commands.executeCommand('axlum.selectConfiguration', 'Debug-x64');
        assert.equal(controller.state.build, 'Library', 'build selection remembered per configuration');
        await vscode.commands.executeCommand('axlum.reload');
        assert.equal(controller.state.run, 'App');
        assert.equal(controller.context.workspaceState.get('selection').targets['Debug-x64'].build, 'Library');
        const manifestPath = path.join(root, 'source/axlum.build.lua');
        const manifest = await fs.readFile(manifestPath, 'utf8');
        await fs.writeFile(manifestPath, manifest + '\ntarget("AddedFromPanel") kind("HeaderOnly")\n');
        assert.equal(await vscode.commands.executeCommand('axlum.reconfigure'), true);
        const options = JSON.parse(await fs.readFile(path.join(root, 'builds/Debug-x64/axlum.options.json'), 'utf8'));
        assert.equal(options.editor_value, 'cli value', 'CLI override retains precedence over preset on reconfigure');
        assert(controller.state.visible.some(t => t.name === 'AddedFromPanel'));
        assert.equal(controller.state.build, 'Library'); assert.equal(controller.state.run, 'App');
        assert.equal((await vscode.commands.executeCommand('axlum.checkDebugger')).type, 'cppvsdbg');
        await assert.rejects(() => controller.requireDebugger({ type: 'not-installed-debugger' }), /Missing debugger/);
        await assert.rejects(() => controller.requireDebugger({ type: 'cppdbg', miDebuggerPath: path.join(root, 'missing-gdb.exe') }), /not found/);
        await vscode.commands.executeCommand('axlum.selectRunTarget', 'ManagedApp');
        assert.equal(await vscode.commands.executeCommand('axlum.run'), true, 'managed run does not require C# debugger extension');
        await until(() => controller.runs.size === 0);
        assert.equal(await fs.readFile(path.join(root, 'ran.txt'), 'utf8'), 'personal-settings');
        await fs.unlink(path.join(root, 'ran.txt'));
        await vscode.commands.executeCommand('axlum.selectRunTarget', 'App');
        // Real C++ debugger, with user args/environment and incremental pre-launch build.
        const buildTerminals = vscode.window.terminals.filter(t => !t.name.includes('Launch -'));
        assert.equal(await vscode.commands.executeCommand('axlum.run'), true);
        await until(async () => await fs.readFile(path.join(root, 'ran.txt'), 'utf8').catch(() => '') === 'personal-settings');
        const launchTerminal = vscode.window.terminals.find(t => t.name.includes('Launch - App'));
        assert(launchTerminal, 'Run opens its own launch terminal');
        assert(!buildTerminals.includes(launchTerminal), 'Run does not reuse a build terminal');
        await until(() => controller.runs.size === 0);
        await fs.unlink(path.join(root, 'ran.txt'));
        assert.equal(await vscode.commands.executeCommand('axlum.run'), true);
        assert.equal(vscode.window.terminals.filter(t => t.name.includes('Launch - App')).length, 1);
        assert(vscode.window.terminals.includes(launchTerminal), 'repeated Run reuses the idle launch terminal');
        await until(() => controller.runs.size === 0);
        assert.equal(await fs.readFile(path.join(root, 'ran.txt'), 'utf8'), 'personal-settings');
        assert.equal(await vscode.commands.executeCommand('axlum.build'), true);
        assert(vscode.window.terminals.includes(launchTerminal), 'Build preserves the launch terminal');
        await until(() => controller.runs.size === 0);
        await fs.unlink(path.join(root, 'ran.txt'));
        // A resident program must not lock target selection, builds or another run.
        await vscode.commands.executeCommand('axlum.selectRunTarget', 'Sleep');
        assert.equal(await vscode.commands.executeCommand('axlum.run'), true);
        const resident = [...controller.runs.values()].find(r => r.target === 'Sleep');
        assert(resident);
        assert.equal(controller.busy, false);
        assert.equal(await vscode.commands.executeCommand('axlum.run'), true, 'same active target is revealed without relaunch/rebuild');
        assert.equal(controller.runs.size, 1);
        await vscode.commands.executeCommand('axlum.selectConfiguration', 'Release-x64');
        await vscode.commands.executeCommand('axlum.selectRunTarget', 'Sleep');
        assert.equal(await vscode.commands.executeCommand('axlum.run'), true);
        assert.equal(controller.runs.size, 2);
        const otherResident = [...controller.runs.values()].find(r => r.id !== resident.id);
        await vscode.commands.executeCommand('axlum.selectBuildTarget', 'Library');
        assert.equal(await vscode.commands.executeCommand('axlum.build'), true);
        await vscode.commands.executeCommand('axlum.selectRunTarget', 'App');
        assert.equal(await vscode.commands.executeCommand('axlum.run'), true);
        await until(() => ![...controller.runs.values()].some(r => r.target === 'App'));
        assert.equal(await fs.readFile(path.join(root, 'ran.txt'), 'utf8'), 'personal-settings');
        assert(controller.runs.has(resident.id));
        await vscode.commands.executeCommand('axlum.stopRun', resident.id);
        await until(() => !controller.runs.has(resident.id));
        assert(controller.runs.has(otherResident.id), 'stopping one program preserves the other');
        await vscode.commands.executeCommand('axlum.stopRun', otherResident.id);
        await until(() => controller.runs.size === 0);
        assert.equal(controller.busy, false);
        await vscode.commands.executeCommand('axlum.selectConfiguration', 'Debug-x64');
        await vscode.commands.executeCommand('axlum.selectRunTarget', 'App');
        await fs.unlink(path.join(root, 'ran.txt'));
        let stopped;
        const tracker = vscode.debug.registerDebugAdapterTrackerFactory('cppvsdbg', {
            createDebugAdapterTracker: () => ({ onDidSendMessage: message => {
                if (message.type === 'event' && message.event === 'stopped') stopped = message.body;
            } })
        });
        const breakpoint = new vscode.SourceBreakpoint(new vscode.Location(vscode.Uri.file(path.join(root, 'source/Main.cpp')), new vscode.Position(2, 0)));
        vscode.debug.addBreakpoints([breakpoint]);
        // Native Run and Debug entry resolves the same current selection as the sidebar.
        assert.equal(await vscode.debug.startDebugging(controller.folder, vscode.workspace.getConfiguration('launch').get('configurations').find(c => c.type === 'axlum')), true);
        await until(() => !!stopped);
        assert.equal(stopped.reason, 'breakpoint');
        assert.equal(vscode.debug.activeDebugSession.configuration.__axlumTarget, 'App');
        assert.equal(vscode.debug.activeDebugSession.configuration.__axlumConfiguration, 'Debug-x64');
        assert.equal(controller.state.build, 'Library', 'debugging never changes the build target');
        await vscode.debug.activeDebugSession.customRequest('continue', { threadId: stopped.threadId });
        await until(async () => await fs.readFile(path.join(root, 'ran.txt'), 'utf8').catch(() => '') === 'personal-settings');
        await until(() => !vscode.debug.activeDebugSession);
        vscode.debug.removeBreakpoints([breakpoint]); tracker.dispose();
        // Testing API builds the test's target, preserves test args/env, and can cancel only its own CTest tree.
        await controller.tests.refresh();
        const testItem = controller.tests.records.get('test:App smoke').item;
        const cancellation = new vscode.CancellationTokenSource();
        let testExit;
        const executeCase = controller.tests.executeCase.bind(controller.tests);
        controller.tests.executeCase = async (...args) => { testExit = await executeCase(...args); return testExit; };
        await controller.tests.run(new vscode.TestRunRequest([testItem]), cancellation.token);
        assert.equal(testExit, 0);
        assert.equal(await fs.readFile(path.join(root, 'tested.txt'), 'utf8'), 'test-settings');
        await fs.unlink(path.join(root, 'tested.txt'));
        await controller.tests.run(new vscode.TestRunRequest([testItem]), cancellation.token, true);
        assert.equal(await fs.readFile(path.join(root, 'tested.txt'), 'utf8'), 'test-settings');
        const sleepItem = controller.tests.records.get('test:Sleep test').item;
        const cancelTest = new vscode.CancellationTokenSource();
        const startedTest = Date.now();
        controller.tests.executeCase = async (...args) => {
            setTimeout(() => cancelTest.cancel(), 500);
            return executeCase(...args);
        };
        await controller.tests.run(new vscode.TestRunRequest([sleepItem]), cancelTest.token);
        assert(Date.now() - startedTest < 10000, 'CTest cancellation kills the sleeping test process');
        controller.tests.executeCase = executeCase;
        cancellation.dispose(); cancelTest.dispose();
        // Failure must never run an existing stale binary.
        await fs.unlink(path.join(root, 'ran.txt'));
        await fs.writeFile(path.join(root, 'source/Main.cpp'), '#error intentional build failure\n');
        assert.equal(await vscode.commands.executeCommand('axlum.run'), false);
        assert.equal(await fs.access(path.join(root, 'ran.txt')).then(() => true, () => false), false);
        assert.equal(controller.busy, false);
        await fs.writeFile(manifestPath, 'this is invalid Lua !');
        assert.equal(await vscode.commands.executeCommand('axlum.reconfigure'), false);
        assert.equal(controller.state.build, 'Library');
        await fs.writeFile(manifestPath, manifest);
        const originalExecutable = controller.project.executable;
        controller.project.executable = path.join(root, 'builds/Debug-x64/bin/Sleep.exe');
        const pending = vscode.commands.executeCommand('axlum.build');
        await until(() => !!controller.execution);
        assert.equal(controller.bars[0].command, 'axlum.stop');
        assert.equal(controller.bars[1].command, undefined);
        assert.equal(firstPanel.provider.getChildren({ id: 'run' })[1].command, undefined);
        const cancelledAt = Date.now();
        await vscode.commands.executeCommand('axlum.stop');
        assert.equal(await pending, false);
        assert(Date.now() - cancelledAt < 10000, 'cancel terminates the process instead of waiting for normal exit');
        controller.project.executable = originalExecutable;
        assert.equal(controller.busy, false);
        await fs.writeFile(path.join(root, 'result.json'), JSON.stringify({ ok: true,
            message: 'PASS: VS Code activation, configuration/target persistence, IntelliSense sync, real build/run/debug, personal launch settings, build failure and cancellation, Testing API run/debug/process-tree cancellation; VS launch profile preservation' }));
    } catch (error) {
        await fs.writeFile(path.join(root, 'result.json'), JSON.stringify({ ok: false, error: String(error.stack || error) }));
        throw error;
    }
}

module.exports = { run };
