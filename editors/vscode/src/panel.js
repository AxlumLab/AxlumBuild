'use strict';
const vscode = require('vscode');

function createPanel(controller) {
    const changed = new vscode.EventEmitter();
    const row = (id, label, icon, command, description, enabled = true) => {
        const item = new vscode.TreeItem(label);
        item.id = id;
        item.iconPath = new vscode.ThemeIcon(icon);
        item.description = description;
        item.tooltip = description ? `${label}: ${description}` : label;
        if (command && enabled) item.command = { command: `axlum.${command}`, title: label };
        return item;
    };
    const provider = {
        onDidChangeTreeData: changed.event,
        getTreeItem: item => item,
        getChildren(parent) {
            const { project, state, busy } = controller;
            const ready = !!project && !busy;
            if (!parent) {
                if (!project) return [row('empty', 'Open an Axlum-generated workspace', 'info'), row('reload', 'Refresh Project', 'refresh', 'reload')];
                return [['configuration', 'Configuration', 'settings-gear'], ['build', 'Build', 'tools'], ['run', 'Run and Debug', 'debug-alt']].map(([id, label, icon]) => {
                    const item = row(id, label, icon);
                    item.collapsibleState = vscode.TreeItemCollapsibleState.Expanded;
                    return item;
                }).concat(controller.runs.size ? [Object.assign(row('running', 'Running Programs', 'vm-running', undefined, String(controller.runs.size)),
                    { collapsibleState: vscode.TreeItemCollapsibleState.Expanded })] : []).concat([
                    row('status', busy ? 'Running' : 'Status', busy ? 'loading~spin' : 'info', 'showOutput', controller.status),
                    ...(busy ? [row('stop', 'Cancel Operation', 'debug-stop', 'stop')] : []),
                    row('output', 'Show Output', 'output', 'showOutput')
                ]);
            }
            if (parent.id === 'running') return [...controller.runs.values()].map(run => {
                const item = row(`running:${run.id}`, run.target, 'vm-running', 'showRun', run.configuration);
                item.command.arguments = [run.id];
                item.contextValue = 'axlumRunningProgram';
                item.runId = run.id;
                item.tooltip = `Show ${run.target} (${run.configuration})`;
                return item;
            });
            if (parent.id === 'configuration') return [
                row('selectedConfiguration', 'Configuration / Architecture', 'settings-gear', 'selectConfiguration', `${state.entry.id} (${state.entry.configuration} / ${state.entry.architecture})`, ready),
                row('reconfigure', 'Reconfigure', 'sync', 'reconfigure', undefined, ready && !!project.reconfigure?.args?.length),
                row('reload', 'Refresh Project', 'refresh', 'reload', undefined, ready)
            ];
            if (parent.id === 'build') return [
                row('selectedBuild', 'Build Target', 'target', 'selectBuildTarget', state.build, ready),
                row('buildAction', 'Build', 'tools', 'build', undefined, ready),
                row('clean', 'Clean', 'clear-all', 'clean', undefined, ready)
            ];
            if (parent.id === 'run') return [
                row('selectedRun', 'Run Target', 'list-selection', 'selectRunTarget', state.run || 'No runnable target', ready),
                row('runAction', 'Run', 'play', 'run', undefined, ready && !!state.run),
                row('debug', 'Debug', 'debug-alt', 'debug', undefined, ready && !!state.run),
                row('checkDebugger', 'Check Debugger', 'check', 'checkDebugger', undefined, ready && !!state.run)
            ];
            return [];
        }
    };
    const view = vscode.window.createTreeView('axlum.project', { treeDataProvider: provider });
    return { view, provider, refresh: () => changed.fire(), dispose() { view.dispose(); changed.dispose(); } };
}

async function openPanel() {
    await vscode.commands.executeCommand('workbench.view.extension.axlum');
    await vscode.commands.executeCommand('axlum.project.focus');
}
module.exports = { createPanel, openPanel };
