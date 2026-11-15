'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { validate, selection, saveSelection, launchConfiguration } = require('../src/model');

const app = { name: 'App', visible: true, launch: { name: 'Debug:App', preLaunchTask: 'build:Debug:App', type: 'cppvsdbg', request: 'launch', program: '/new/app', args: [] } };
const project = { version: 1, executable: '/axlum', sourceDirectory: '/source', entries: [
    { id: 'Debug', buildDirectory: '/debug', startupTarget: 'App', targets: [app, { name: 'Lib' }] },
    { id: 'Release', buildDirectory: '/release', targets: [{ name: 'Lib' }] }
] };

test('per-configuration selections and removed targets reconcile without leaking run targets', () => {
    validate(project);
    let saved = saveSelection({}, { ...selection(project), build: 'Lib' });
    const release = selection(project, { ...saved, entry: 'Release' });
    assert.equal(release.build, 'all'); assert.equal(release.run, undefined);
    saved = saveSelection(saved, release);
    assert.equal(selection(project, { ...saved, entry: 'Debug' }).build, 'Lib');
    assert.equal(selection(project, { entry: 'removed', targets: { Debug: { run: 'removed' } } }).run, 'App');
});
test('hidden targets cannot be selected, including restored stale preferences', () => {
    const hidden = structuredClone(project);
    hidden.entries[0].targets[0].visible = false;
    const state = selection(hidden, { targets: { Debug: { build: 'App', run: 'App' } } });
    assert.equal(state.build, 'all'); assert.equal(state.run, undefined);
});
test('launch keeps personal arguments and environment but follows generated artifacts', () => {
    const result = launchConfiguration(app, [{ name: 'My app', preLaunchTask: 'build:Debug:App', program: '/stale/app', args: ['--custom'], environment: [{ name: 'MODE', value: 'test' }], cwd: '/personal' }]);
    assert.equal(result.program, '/new/app'); assert.equal(result.cwd, '/personal');
    assert.deepEqual(result.args, ['--custom']); assert.equal(result.environment[0].value, 'test');
});
test('unknown protocol and duplicate identities fail explicitly', () => {
    assert.throws(() => validate({ ...project, version: 2 }));
    assert.throws(() => validate({ ...project, entries: [project.entries[0], project.entries[0]] }));
});
test('personal debugger adapters and custom pre-launch tasks survive; attach stays in VS Code', () => {
    const launch = launchConfiguration(app, [{ name: 'Debug:App', type: 'lldb-dap', preLaunchTask: 'Prepare assets', environment: [{name:'MODE', value:'test'}] }]);
    assert.equal(launch.type, 'lldb-dap');
    assert.equal(launch.preLaunchTask, 'Prepare assets');
    assert.deepEqual(launch.env, {MODE:'test'});
    assert.equal(launch.environment, undefined);
    assert.throws(() => launchConfiguration(app, [{name:'Debug:App', request:'attach'}]), /attach/);
});

test('default build is all and run selects the first executable independently', () => {
    const fresh = structuredClone(project);
    delete fresh.entries[0].startupTarget;
    fresh.entries[0].targets.unshift({ ...app, name: 'Zeta' });
    const state = selection(fresh);
    assert.equal(state.build, 'all');
    assert.equal(state.run, 'Zeta');
    const saved = saveSelection({}, { ...state, run: 'App' });
    assert.equal(selection(fresh, saved).build, 'all');
    assert.equal(selection(fresh, saved).run, 'App');
});
