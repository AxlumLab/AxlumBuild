'use strict';

function validate(project) {
    if (project.version !== 1 || !Array.isArray(project.entries) || !project.entries.length)
        throw new Error('Unsupported or empty Axlum editor metadata. Regenerate IDE projects.');
    for (const field of ['executable', 'sourceDirectory'])
        if (typeof project[field] !== 'string' || !project[field]) throw new Error(`Missing ${field}`);
    const ids = new Set();
    for (const entry of project.entries) {
        if (!entry.id || ids.has(entry.id) || !entry.buildDirectory || !Array.isArray(entry.targets))
            throw new Error('Invalid Axlum configuration metadata');
        ids.add(entry.id);
        const names = new Set();
        for (const target of entry.targets) {
            if (!target.name || names.has(target.name)) throw new Error('Invalid Axlum target metadata');
            names.add(target.name);
        }
    }
    return project;
}

function selection(project, saved = {}) {
    const entry = project.entries.find(e => e.id === saved.entry) || project.entries[0];
    const prior = saved.targets?.[entry.id] || {};
    const visible = entry.targets.filter(t => t.visible !== false);
    const runnable = visible.filter(t => t.launch);
    const preferred = runnable.find(t => t.name === entry.startupTarget) || runnable[0];
    return {
        entry,
        build: prior.build === 'all' || visible.some(t => t.name === prior.build) ? prior.build : preferred?.name || 'all',
        run: runnable.some(t => t.name === prior.run) ? prior.run : preferred?.name,
        visible, runnable
    };
}

function saveSelection(saved, state) {
    return { entry: state.entry.id, targets: { ...saved.targets, [state.entry.id]: { build: state.build, run: state.run } } };
}

function launchConfiguration(target, configurations) {
    const base = target.launch;
    const personal = configurations.find(c => c.preLaunchTask === base.preLaunchTask || c.name === base.name);
    // Use the current generated artifact paths; retain personal launch settings.
    const result = { ...base, ...personal };
    for (const field of ['program', 'projectPath'])
        if (base[field] !== undefined) result[field] = base[field];
    if (result.request !== 'launch') throw new Error('Axlum executable commands require a launch configuration. Use Run and Debug for attach configurations.');
    if (result.type !== 'cppdbg') { delete result.MIMode; delete result.miDebuggerPath; }
    if (['lldb', 'lldb-dap', 'coreclr'].includes(result.type) && Array.isArray(result.environment)) {
        result.env = { ...Object.fromEntries(result.environment.map(e => [e.name, e.value])), ...result.env };
        delete result.environment;
    }
    if (['cppdbg', 'cppvsdbg'].includes(result.type) && result.env) {
        const env = { ...Object.fromEntries((result.environment || []).map(e => [e.name, e.value])), ...result.env };
        result.environment = Object.entries(env).map(([name, value]) => ({ name, value }));
        delete result.env;
    }
    if (['lldb', 'lldb-dap'].includes(result.type)) {
        if (result.stopOnEntry === undefined && result.stopAtEntry !== undefined) result.stopOnEntry = result.stopAtEntry;
        delete result.stopAtEntry;
    } else if (['cppdbg', 'cppvsdbg'].includes(result.type)) {
        if (result.stopAtEntry === undefined && result.stopOnEntry !== undefined) result.stopAtEntry = result.stopOnEntry;
        delete result.stopOnEntry;
    }
    return result;
}

module.exports = { validate, selection, saveSelection, launchConfiguration };
