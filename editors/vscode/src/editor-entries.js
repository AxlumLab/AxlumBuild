'use strict';
const vscode = require('vscode');

const selectedLaunch = { name: 'Axlum: Selected Target', type: 'axlum', request: 'launch' };
const taskNames = {
    build: 'Build selected target', buildAll: 'Build all', buildRun: 'Build launch target',
    clean: 'Clean selected target', run: 'Run selected target'
};

function createSelectedTask(controller, action, fixed = {}) {
    const definition = { type: 'axlum', action, ...fixed };
    const task = new vscode.Task(definition, controller.folder || vscode.TaskScope.Workspace,
        taskNames[action] || action, 'Axlum', new vscode.CustomExecution(async () => {
            const write = new vscode.EventEmitter();
            const close = new vscode.EventEmitter();
            let operation;
            let closed = false;
            return {
                onDidWrite: write.event, onDidClose: close.event,
                open() {
                    if (closed) { close.fire(1); return; }
                    controller.guard(() => controller.operate(action, fixed, op => {
                        operation = op;
                        if (closed) controller.stopOperation(op);
                    })).then(ok => {
                        write.fire(ok ? 'Axlum operation completed.\r\n' : 'Axlum operation failed or cancelled.\r\n');
                        close.fire(ok ? 0 : 1);
                        write.dispose(); close.dispose();
                    });
                },
                close() { closed = true; if (operation) controller.stopOperation(operation); }
            };
        }), []);
    // The real build/launch process has its own terminal and problem matcher.
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Never, panel: vscode.TaskPanelKind.Dedicated, showReuseMessage: false };
    if (action === 'build') task.group = vscode.TaskGroup.Build;
    return task;
}

async function syncEditorEntries(controller) {
    const defaultScope = vscode.workspace.workspaceFile ? vscode.ConfigurationTarget.Workspace : vscode.ConfigurationTarget.WorkspaceFolder;
    const resource = controller.folder?.uri;
    const launch = vscode.workspace.getConfiguration('launch', resource);
    const configurations = launch.get('configurations', []);
    if (!configurations.some(c => c.type === 'axlum' && c.request === 'launch'))
        await launch.update('configurations', [selectedLaunch, ...configurations],
            launch.inspect('configurations')?.workspaceFolderValue !== undefined ? vscode.ConfigurationTarget.WorkspaceFolder : defaultScope);

    const tasks = vscode.workspace.getConfiguration('tasks', resource);
    const existing = tasks.get('tasks', []);
    let changed = false;
    const updated = existing.map(task => {
        const action = Object.keys(taskNames).find(key => task.label === `Axlum: ${taskNames[key]}`);
        // Upgrade only the generator's stock picker tasks. Explicit user commands
        // and fixed per-target preLaunchTask entries retain their own semantics.
        if (!action || task.type !== 'process' || task.command !== controller.project.executable ||
            !Array.isArray(task.args) || task.args.length !== 5 || task.dependsOn || task.isBackground ||
            Object.keys(task.options || {}).some(key => key !== 'cwd') ||
            !task.args.some(arg => String(arg).startsWith('${input:axlum.'))) return task;
        changed = true;
        return { label: task.label, type: 'axlum', action, ...(task.group ? { group: task.group } : {}) };
    });
    if (changed) await tasks.update('tasks', updated,
        tasks.inspect('tasks')?.workspaceFolderValue !== undefined ? vscode.ConfigurationTarget.WorkspaceFolder : defaultScope);
}

function registerEditorEntries(controller, context) {
    context.subscriptions.push(vscode.tasks.registerTaskProvider('axlum', {
        provideTasks: () => controller.project && vscode.workspace.isTrusted
            ? Object.keys(taskNames).map(action => createSelectedTask(controller, action)) : [],
        resolveTask: task => {
            if (!controller.project || !vscode.workspace.isTrusted || !taskNames[task.definition.action]) return undefined;
            const { action, target, configuration } = task.definition;
            return createSelectedTask(controller, action, { ...(target ? { target } : {}), ...(configuration ? { configuration } : {}) });
        }
    }));
    context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('axlum', {
        provideDebugConfigurations: () => controller.project ? [selectedLaunch] : [],
        async resolveDebugConfiguration(folder, config, token) {
            if (token.isCancellationRequested) return null;
            // Axlum is a configuration provider; the installed native/.NET
            // extension supplies the actual adapter after type resolution.
            const result = await controller.guard(() => controller.operate('resolveDebug', { launchOverrides: config }, op => {
                const cancellation = token.onCancellationRequested(() => controller.stopOperation(op));
                op.disposables.push(cancellation);
                if (token.isCancellationRequested) controller.stopOperation(op);
            }));
            return token.isCancellationRequested || !result ? null : result;
        }
    }));
    context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('axlum', {
        provideDebugConfigurations: () => controller.project ? [selectedLaunch] : []
    }, vscode.DebugConfigurationProviderTriggerKind.Dynamic));
}

module.exports = { registerEditorEntries, syncEditorEntries, selectedLaunch };
