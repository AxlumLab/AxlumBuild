'use strict';
const vscode = require('vscode');

// Subscribe before starting: a short process can exit before executeTask resolves.
class TaskRun {
    constructor(task, onFinish = () => {}) {
        this.cancelled = false;
        this.finished = false;
        this.completion = new Promise(resolve => { this.resolveCompletion = resolve; });
        this.started = new Promise((resolve, reject) => { this.resolveStarted = resolve; this.rejectStarted = reject; });
        const early = [];
        const observe = (execution, kind, code) => {
            if (!this.execution) early.push([execution, kind, code]);
            else if (execution === this.execution) this.observe(kind, code);
        };
        this.onFinish = onFinish;
        this.listeners = [
            vscode.tasks.onDidStartTask(e => observe(e.execution, 'start')),
            vscode.tasks.onDidEndTaskProcess(e => observe(e.execution, 'process', e.exitCode)),
            vscode.tasks.onDidEndTask(e => observe(e.execution, 'end'))
        ];
        vscode.tasks.executeTask(task).then(execution => {
            this.execution = execution;
            for (const [value, kind, code] of early) if (value === execution) this.observe(kind, code);
        }, error => {
            this.rejectStarted(error);
            this.finish(undefined);
        });
    }

    observe(kind, code) {
        if (this.finished) return;
        if (kind === 'start') {
            this.hasStarted = true;
            this.resolveStarted(true);
            if (this.cancelled) this.execution.terminate();
        } else if (kind === 'process') this.finish(code);
        else this.endTimer = setTimeout(() => this.finish(undefined), 0);
    }

    finish(code) {
        if (this.finished) return;
        this.finished = true;
        clearTimeout(this.endTimer);
        for (const listener of this.listeners) listener.dispose();
        this.resolveStarted(code !== undefined);
        const ok = !this.cancelled && code === 0;
        this.resolveCompletion(ok);
        this.onFinish({ ok, code, cancelled: this.cancelled });
    }

    cancel() { this.cancelled = true; if (this.hasStarted && !this.finished) this.execution.terminate(); }
}

module.exports = { TaskRun };
