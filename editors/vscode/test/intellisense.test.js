'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function load(vscode = {}) {
    const exports = { exports: {} };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../src/intellisense.js'), 'utf8'), {
        module: exports, require: name => name === 'vscode' ? vscode : require(name), process
    });
    return exports.exports;
}

test('per-file flags preserve defines, forced headers and system includes', () => {
    const { configuration, ConfigurationProvider } = load();
    const directory = path.resolve('fixture');
    const source = { directory, file: path.join(directory, 'src/app.cpp'), arguments: [
        'clang++', '-Iinclude', '-isystem', 'sdk', '-DDEBUG=1', '-include', 'config.h', '-std=c++20', '--target=aarch64-linux-gnu'
    ] };
    const result = configuration(source, {});
    assert.equal(result.includePath[1], path.join(directory, 'sdk'));
    assert.equal(result.defines[0], 'DEBUG=1');
    assert.equal(result.forcedInclude[0], path.join(directory, 'config.h'));
    assert.equal(result.standard, 'c++20');
    assert.equal(result.compilerArgs[0], '--target=aarch64-linux-gnu');
    const provider = new ConfigurationProvider();
    provider.commands = [{ ...source, configuration: result }];
    assert.equal(provider.find(path.join(directory, 'include/api.hpp')).configuration, result);
    assert.equal(provider.find(path.resolve('unrelated/api.hpp')), undefined);
});

test('clangd follows configuration, preserves user flags and restarts only on change', async () => {
    const values = { axlum: {}, C_Cpp: {}, clangd: { arguments: ['--clang-tidy', '--compile-commands-dir', 'old'] } };
    const commands = [];
    const vscode = {
        ConfigurationTarget: { Workspace: 2, WorkspaceFolder: 3 },
        workspace: { getConfiguration: name => ({
            get: (key, fallback) => values[name][key] ?? fallback,
            update: async (key, value) => { values[name][key] = value; }
        }) },
        extensions: { getExtension: () => ({ isActive: true }) },
        commands: { executeCommand: async command => commands.push(command) }
    };
    const { sync } = load(vscode);
    const controller = { state: { entry: { intelliSense: { compileCommands: path.resolve('debug/compile_commands.json') } } }, output: { appendLine() {} } };
    await sync(controller);
    assert.equal(values.clangd.arguments[0], '--clang-tidy');
    assert.equal(values.clangd.arguments.length, 2);
    assert.equal(values.clangd.arguments[1], `--compile-commands-dir=${path.resolve('debug')}`);
    assert.equal(values.C_Cpp.intelliSenseEngine, 'disabled');
    await sync(controller);
    assert.equal(commands.length, 1);
    controller.state.entry.intelliSense.compileCommands = path.resolve('release/compile_commands.json');
    await sync(controller);
    assert.equal(commands.length, 2);
});
