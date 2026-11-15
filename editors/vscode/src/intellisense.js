'use strict';
const vscode = require('vscode');
const fs = require('node:fs/promises');
const path = require('node:path');

// Axlum emits argument arrays, so no shell parsing (or compiler execution) is needed.
function configuration(command, info) {
    const args = command.arguments;
    const result = { compilerPath: args[0], includePath: [], defines: [], forcedInclude: [], compilerArgs: [] };
    const absolute = value => path.resolve(command.directory, value);
    for (let i = 1; i < args.length; ++i) {
        const arg = args[i];
        const match = /^(\/external:I|\/I|-isystem|-iquote|-I|\/D|-D|\/FI|-include)(.*)$/.exec(arg);
        if (match) {
            const value = match[2] || args[++i];
            if (!value) continue;
            if (['/D', '-D'].includes(match[1])) result.defines.push(value);
            else if (['/FI', '-include'].includes(match[1])) result.forcedInclude.push(absolute(value));
            else result.includePath.push(absolute(value));
        } else if (/^(\/std:|-std=)/.test(arg)) result.standard = arg.replace(/^(\/std:|-std=)/, '');
        else if (/^(--target=|--sysroot=|-m|\/arch:|-f)/.test(arg)) result.compilerArgs.push(arg);
    }
    result.standard ||= /\.c$/i.test(command.file) ? info.cStandard : info.cppStandard;
    return result;
}

class ConfigurationProvider {
    name = 'Axlum Build';
    extensionId = 'axlum.axlum-build';
    commands = [];
    dispose() {}
    find(file) {
        const normal = value => process.platform === 'win32' ? path.resolve(value).toLowerCase() : path.resolve(value);
        const wanted = normal(file);
        const direct = this.commands.find(c => normal(c.file) === wanted);
        if (direct) return direct;
        // Header-only files inherit the closest source/include-root context, not a union of incompatible defines.
        if (!/\.(h|hh|hpp|hxx|inl|ipp)$/i.test(file)) return undefined;
        let best, score = 0;
        for (const command of this.commands) {
            for (const dir of [path.dirname(command.file), ...command.configuration.includePath]) {
                const prefix = normal(dir) + path.sep;
                if (wanted.startsWith(prefix) && prefix.length > score) { best = command; score = prefix.length; }
            }
        }
        return best;
    }
    async canProvideConfiguration(uri) { return !!this.find(uri.fsPath); }
    async provideConfigurations(uris) {
        return uris.flatMap(uri => { const command = this.find(uri.fsPath); return command ? [{ uri, configuration: command.configuration }] : []; });
    }
    async canProvideBrowseConfiguration() { return this.commands.length > 0; }
    async canProvideBrowseConfigurationsPerFolder() { return true; }
    async provideFolderBrowseConfiguration(uri) { return uri.fsPath === this.folder ? this.provideBrowseConfiguration() : null; }
    async provideBrowseConfiguration() {
        return { browsePath: [...new Set(this.commands.flatMap(c => [path.dirname(c.file), ...c.configuration.includePath]))],
            compilerPath: this.commands[0]?.configuration.compilerPath, standard: this.info.cppStandard };
    }
}

async function sync(controller) {
    const settings = vscode.workspace.getConfiguration('axlum');
    if (!settings.get('syncIntelliSense', true)) return;
    const info = controller.state.entry.intelliSense || {};
    if (!info.compileCommands) return;
    const scope = controller.folder ? vscode.ConfigurationTarget.WorkspaceFolder : vscode.ConfigurationTarget.Workspace;
    const cpp = vscode.workspace.getConfiguration('C_Cpp', controller.folder?.uri);
    const update = async (config, key, value, target = scope) => {
        if (JSON.stringify(config.get(key)) !== JSON.stringify(value)) await config.update(key, value, target);
    };
    for (const key of ['compileCommands', 'compilerPath', 'cppStandard', 'cStandard'])
        if (info[key] !== undefined) await update(cpp, `default.${key}`, info[key]);
    const clangExtension = vscode.extensions.getExtension('llvm-vs-code-extensions.vscode-clangd');
    const mode = settings.get('languageService', 'auto');
    const useClangd = mode === 'clangd' || (mode === 'auto' && !!clangExtension);
    // Only one semantic engine should publish diagnostics for a document.
    await update(cpp, 'intelliSenseEngine', useClangd ? 'disabled' : 'default');
    if (clangExtension) {
        const clang = vscode.workspace.getConfiguration('clangd');
        await update(clang, 'enable', useClangd, vscode.ConfigurationTarget.Workspace);
        if (useClangd) {
            const args = clang.get('arguments', []);
            const filtered = [];
            for (let i = 0; i < args.length; ++i) {
                if (args[i] === '--compile-commands-dir') { ++i; continue; }
                if (!args[i].startsWith('--compile-commands-dir=')) filtered.push(args[i]);
            }
            filtered.push(`--compile-commands-dir=${path.dirname(info.compileCommands)}`);
            const changed = JSON.stringify(args) !== JSON.stringify(filtered);
            await update(clang, 'arguments', filtered, vscode.ConfigurationTarget.Workspace);
            if (changed && clangExtension.isActive) await vscode.commands.executeCommand('clangd.restart');
        } else if (clangExtension.isActive) {
            await vscode.commands.executeCommand('clangd.shutdown');
        }
    }
    if (!useClangd) {
        const extension = vscode.extensions.getExtension('ms-vscode.cpptools');
        if (!extension) return;
        if (!controller.cppProvider) {
            const exports = await extension.activate();
            const api = exports.getApi(6);
            const provider = new ConfigurationProvider();
            controller.cppProvider = provider;
            controller.cppApi = api;
            controller.context.subscriptions.push(api, provider);
            api.registerCustomConfigurationProvider(provider);
        }
        const provider = controller.cppProvider;
        provider.info = info;
        provider.folder = controller.folder?.uri.fsPath;
        const commands = JSON.parse(await fs.readFile(info.compileCommands, 'utf8'));
        provider.commands = commands.filter(c => Array.isArray(c.arguments)).map(c => ({ ...c,
            file: path.resolve(c.directory, c.file), configuration: configuration(c, info) }));
        // The combined graph also contains host tools. Prefer the selected target platform for duplicate sources.
        const host = path.join(controller.state.entry.buildDirectory, 'host') + path.sep;
        provider.commands.sort((a, b) => Number((a.output || '').startsWith(host)) - Number((b.output || '').startsWith(host)));
        await update(cpp, 'default.configurationProvider', provider.extensionId);
        controller.cppApi.notifyReady(provider);
        controller.cppApi.didChangeCustomConfiguration(provider);
        controller.cppApi.didChangeCustomBrowseConfiguration(provider);
    }
    controller.output.appendLine(`Language service: ${useClangd ? 'clangd' : 'C/C++'}; compilation database: ${info.compileCommands}`);
}

module.exports = { sync, configuration, ConfigurationProvider };
