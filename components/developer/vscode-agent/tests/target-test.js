'use strict';
// Run on Target end to end, with a stand-in for the vscode API: the
// extension's src/target.js builds a project with make, sends the program to
// a host build of vscode-agent and runs it in a pseudo terminal.
//   node tests/target-test.js AGENT_BINARY
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const Module = require('module');
const { EventEmitter } = require('events');
const { spawn } = require('child_process');

const bin = path.resolve(process.argv[2] || 'build/host/vscode-agent');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'vscode-target-test-'));
const proj = path.join(work, 'proj');
const tmpdir = path.join(work, 'tmp');
fs.mkdirSync(proj);
fs.mkdirSync(tmpdir);
fs.writeFileSync(path.join(proj, 'Makefile'),
  'all: hello\nhello: hello.in\n\tcp hello.in hello\n\tchmod 755 hello\n');
fs.writeFileSync(path.join(proj, 'hello.in'),
  '#!/bin/sh\necho "hello $1 DISPLAY=$DISPLAY"\nread x\necho "you typed $x"\nexit 4\n');

// --- a small vscode -----------------------------------------------------------
const messages = [];
const terminals = [];
const commands = {};
const secretStore = {};
const config = {
  'agent.port': 0, 'agent.listenAddress': '127.0.0.1', 'agent.autoStart': true,
  'run.program': 'hello', 'run.args': ['world'], 'run.display': ':0'
};
class VEmitter {
  constructor() { this.l = []; this.event = (f) => { this.l.push(f); return { dispose() {} }; }; }
  fire(v) { for (const f of this.l) f(v); }
}
const vscode = {
  EventEmitter: VEmitter,
  StatusBarAlignment: { Left: 1 },
  ProgressLocation: { Notification: 15 },
  window: {
    createStatusBarItem: () => ({ show() {}, dispose() {}, text: '' }),
    createOutputChannel: () => ({ append() {}, appendLine(l) { messages.push(`out: ${l}`); }, show() {} }),
    showInformationMessage: async (m) => { messages.push(`info: ${m}`); },
    showWarningMessage: async (m) => { messages.push(`warn: ${m}`); },
    showErrorMessage: async (m) => { messages.push(`error: ${m}`); },
    withProgress: async (o, f) => f(),
    activeTextEditor: undefined,
    createTerminal: (o) => {
      const t = { name: o.name, text: '', show() {} };
      o.pty.onDidWrite((d) => { t.text += d; });
      t.pty = o.pty;
      terminals.push(t);
      setImmediate(() => o.pty.open({ columns: 90, rows: 25 }));
      return t;
    }
  },
  workspace: {
    getConfiguration: () => ({ get: (k) => config[k] }),
    onDidChangeConfiguration: () => ({ dispose() {} })
  },
  commands: { registerCommand: (n, f) => { commands[n] = f; return { dispose() {} }; } }
};
const realLoad = Module._load;
Module._load = function (req, ...rest) {
  return req === 'vscode' ? vscode : realLoad.call(this, req, ...rest);
};

const target = require('../../vscode/src/target.js');

function waitFor(cond, ms, what) {
  return new Promise((res, rej) => {
    const t0 = Date.now();
    const iv = setInterval(() => {
      if (cond()) { clearInterval(iv); res(); } else if (Date.now() - t0 > ms) {
        clearInterval(iv);
        rej(new Error(`timeout: ${what}\n${messages.join('\n')}`));
      }
    }, 50);
  });
}

(async () => {
  const subs = [];
  const ctx = {
    subscriptions: subs,
    secrets: { get: async (k) => secretStore[k], store: async (k, v) => { secretStore[k] = v; } },
    workspaceState: { get: () => undefined, update: async () => {} }
  };
  let agentProc;
  try {
    // a key exists: the listener starts by itself (port 0 = any free port)
    secretStore['amix.agentKey'] = require('../../vscode/src/agent-link.js').newKey();
    // a fixed port: the agent's config must name it
    const net = require('net');
    const port = await new Promise((res) => {
      const s = net.createServer().listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); });
    });
    config['agent.port'] = port;
    target.activate(ctx, {
      cfg: () => ({ makeCommand: 'make', makeArgs: [] }),
      makeEnv: () => ({}),
      findMakeDir: () => proj,
      workspaceRoot: () => proj
    });
    await waitFor(() => messages.some((m) => m.includes('listening')), 5000, 'listener');

    const conf = path.join(work, 'agent.conf');
    fs.writeFileSync(conf, `connect 127.0.0.1:${port}\nsecret ${secretStore['amix.agentKey']}\n` +
      `tmpdir ${tmpdir}\nreserve-mb 1\nretry 1\n`, { mode: 0o600 });
    agentProc = spawn(bin, ['-f', '-c', conf], { stdio: 'ignore' });
    await waitFor(() => messages.some((m) => m.includes('is connected')), 10000, 'agent sign-in');

    // the configuration text has the key and FS-UAE's address
    const text = target.configText();
    assert.ok(text.includes(`secret   ${secretStore['amix.agentKey']}`));
    assert.ok(text.includes(`connect  10.0.2.2:${port}`));

    await commands['amix.runOnTarget']();
    assert.ok(fs.existsSync(path.join(proj, 'hello')), 'make ran');
    assert.strictEqual(terminals.length, 1, messages.join('\n'));
    const t = terminals[0];
    await waitFor(() => t.text.includes('hello world DISPLAY=:0'), 10000, 'program output');
    t.pty.handleInput('fine\r');
    await waitFor(() => t.text.includes('[hello exited with 4]'), 10000, 'exit');
    assert.ok(t.text.includes('you typed fine'), t.text);
    console.log('ok   build, send, run in a pseudo terminal, input, exit code');

    // a broken build sends nothing
    fs.writeFileSync(path.join(proj, 'Makefile'), 'all:\n\tfalse\n');
    await commands['amix.runOnTarget']();
    assert.strictEqual(terminals.length, 1);
    assert.ok(messages.some((m) => m.includes('the build failed')));
    console.log('ok   a failed build sends nothing');
    console.log('all passed');
  } catch (e) {
    console.log(`FAIL ${e.stack || e}`);
    process.exitCode = 1;
  } finally {
    for (const s of subs) if (s.dispose) s.dispose();
    if (agentProc) agentProc.kill('SIGTERM');
    await new Promise((r) => setTimeout(r, 500));
    fs.rmSync(work, { recursive: true, force: true });
    process.exit(process.exitCode || 0);
  }
})();
