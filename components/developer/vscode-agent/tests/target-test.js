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
  '#!/bin/sh\necho "hello $1 DISPLAY=$DISPLAY size `stty size`"\nread x\necho "you typed $x"\nexit 4\n');

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
      // as VS Code: open at one size, the settled size right after
      setImmediate(() => { o.pty.open({ columns: 90, rows: 25 }); o.pty.setDimensions({ columns: 100, rows: 30 }); });
      return t;
    }
  },
  workspace: {
    getConfiguration: () => ({ get: (k) => config[k] }),
    onDidChangeConfiguration: () => ({ dispose() {} })
  },
  commands: { registerCommand: (n, f) => { commands[n] = f; return { dispose() {} }; } },
  debug: {
    registerDebugConfigurationProvider: (t, p) => { debugProviders[t] = p; return { dispose() {} }; },
    registerDebugAdapterDescriptorFactory: (t, f) => { debugFactories[t] = f; return { dispose() {} }; }
  },
  DebugAdapterInlineImplementation: class { constructor(a) { this.implementation = a; } }
};
const debugProviders = {};
const debugFactories = {};
const realLoad = Module._load;
Module._load = function (req, ...rest) {
  return req === 'vscode' ? vscode : realLoad.call(this, req, ...rest);
};

// a home of its own: the AMIX Installer's key file lives there
process.env.HOME = path.join(work, 'home');
const instKeyDir = path.join(work, 'home', '.local', 'share', 'amix-installer');
fs.mkdirSync(instKeyDir, { recursive: true });
const instKey = require('../../vscode/src/agent-link.js').newKey();
fs.writeFileSync(path.join(instKeyDir, 'vscode-agent.key'), instKey + '\n', { mode: 0o600 });

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
    // no agent yet: Check Setup says how to get one
    assert.ok(target.statusLines().some((l) => /AMIX Installer/.test(l)), target.statusLines().join('\n'));

    const conf = path.join(work, 'agent.conf');
    fs.writeFileSync(conf, `connect 127.0.0.1:${port}\nsecret ${secretStore['amix.agentKey']}\n` +
      `tmpdir ${tmpdir}\nreserve-mb 1\nretry 1\n`, { mode: 0o600 });
    agentProc = spawn(bin, ['-f', '-c', conf], { stdio: 'ignore' });
    await waitFor(() => messages.some((m) => m.includes('is connected')), 10000, 'agent sign-in');

    // the configuration text has the key and FS-UAE's address
    // the setup guide and Check Setup's lines name the target once it is there
    assert.ok(target.statusLines().some((l) => /^ok\s+target /.test(l)), target.statusLines().join('\n'));
    assert.ok(target.setupGuide().includes('developer/vscode-agent'));
    assert.ok(commands['amix.setupTarget'], 'setup command registered');
    const text = target.configText();
    assert.ok(text.includes(`secret   ${secretStore['amix.agentKey']}`));
    assert.ok(text.includes(`connect  10.0.2.2:${port}`));

    await commands['amix.runOnTarget']();
    assert.ok(fs.existsSync(path.join(proj, 'hello')), 'make ran');
    assert.strictEqual(terminals.length, 1, messages.join('\n'));
    const t = terminals[0];
    await waitFor(() => t.text.includes('hello world DISPLAY=:0'), 10000, 'program output');
    assert.ok(t.text.includes('size 30 100'), `started at the settled size: ${t.text}`);
    t.pty.handleInput('fine\r');
    await waitFor(() => t.text.includes('[hello exited with 4]'), 10000, 'exit');
    assert.ok(t.text.includes('you typed fine'), t.text);
    console.log('ok   build, send, run in a pseudo terminal, input, exit code');

    // launch.json: type amix, through the inline adapter (F5 = with debugging)
    assert.ok(debugProviders.amix && debugFactories.amix, 'amix launch type registered');
    const cfg = debugProviders.amix.resolveDebugConfiguration(undefined, {});
    assert.strictEqual(cfg.type, 'amix');
    const ad = debugFactories.amix.createDebugAdapterDescriptor({}).implementation;
    const msgs = [];
    ad.onDidSendMessage((m) => msgs.push(m));
    ad.handleMessage({ seq: 1, type: 'request', command: 'initialize', arguments: {} });
    ad.handleMessage({ seq: 2, type: 'request', command: 'launch',
      arguments: { type: 'amix', request: 'launch', program: 'hello', args: ['launch'], cwd: proj } });
    await waitFor(() => terminals.length === 2, 10000, 'launch terminal');
    const t2 = terminals[1];
    await waitFor(() => t2.text.includes('hello launch'), 10000, 'launch output');
    t2.pty.handleInput('x\r');
    await waitFor(() => msgs.some((m) => m.event === 'terminated'), 10000, 'terminated');
    const ev = (n) => msgs.find((m) => m.event === n);
    assert.ok(ev('initialized'));
    assert.ok(msgs.find((m) => m.command === 'launch' && m.success), JSON.stringify(msgs));
    assert.strictEqual(ev('exited').body.exitCode, 4);
    assert.ok(msgs.some((m) => m.event === 'output' && /without debugging/.test(m.body.output)));
    console.log('ok   launch.json type amix: initialize, launch, output, exit code 4, terminated');

    // Ctrl+F5 (noDebug) and a disconnect stop the program
    const ad2 = debugFactories.amix.createDebugAdapterDescriptor({}).implementation;
    const m2 = [];
    ad2.onDidSendMessage((m) => m2.push(m));
    ad2.handleMessage({ seq: 1, type: 'request', command: 'launch',
      arguments: { program: 'hello', args: ['stop'], noDebug: true, buildFirst: false } });
    await waitFor(() => terminals.length === 3 && terminals[2].text.includes('hello stop'), 10000, 'run 3');
    ad2.handleMessage({ seq: 2, type: 'request', command: 'disconnect', arguments: {} });
    await waitFor(() => terminals[2].text.includes('ended by signal 1'), 10000, 'stopped');
    assert.ok(!m2.some((m) => m.event === 'output' && /without debugging/.test(m.body.output)));
    console.log('ok   Ctrl+F5 runs without the debugging note; disconnect stops the program');

    // an agent with the installer's key is refused once, with the offer to use it
    const conf2 = path.join(work, 'agent2.conf');
    fs.writeFileSync(conf2, `connect 127.0.0.1:${port}\nsecret ${instKey}\n` +
      `tmpdir ${tmpdir}\nreserve-mb 1\nretry 1\n`, { mode: 0o600 });
    const agent2 = spawn(bin, ['-f', '-c', conf2], { stdio: 'ignore' });
    try {
      await waitFor(() => messages.some((m) => /refused.*Use that key\?/.test(m)), 10000, 'installer key offer');
      await new Promise((r) => setTimeout(r, 2500));       // it dials again: no second warning
      assert.strictEqual(messages.filter((m) => /Use that key\?/.test(m)).length, 1, messages.join('\n'));
    } finally {
      agent2.kill('SIGTERM');
    }
    console.log("ok   an agent with the installer's key: one offer to use that key");

    // a broken build sends nothing
    fs.writeFileSync(path.join(proj, 'Makefile'), 'all:\n\tfalse\n');
    await commands['amix.runOnTarget']();
    assert.strictEqual(terminals.length, 3);
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
