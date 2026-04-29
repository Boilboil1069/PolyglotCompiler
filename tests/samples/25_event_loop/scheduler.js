// scheduler.js — Minimal microtask-style FIFO scheduler.
// Part of the PolyglotCompiler sample matrix.

'use strict';

const queue = [];

function schedule(label, delay) {
    queue.push({ label: String(label), delay: Number(delay) | 0 });
    queue.sort((a, b) => a.delay - b.delay);
    return queue.length;
}

function drain() {
    const out = queue.slice();
    queue.length = 0;
    return out;
}

module.exports = { schedule, drain };

