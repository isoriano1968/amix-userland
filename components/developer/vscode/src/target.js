'use strict';

// Run on Target: the AMIX side is vscode-agent (components/developer/
// vscode-agent), which dials in to the listener here.  The program is built
// with make, sent into the agent's session folder and run on a pty there;
// its terminal is a VS Code terminal.

const vscode = require('vscode');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { AgentServer, newKey } = require('./agent-link');

const KEY = 'amix.agentKey';
const FS_UAE_HOST = '10.0.2.2';

let ctx;
let hooks;            // { cfg, makeEnv, findMakeDir, workspaceRoot }
let server = null;
let key = '';
let current = null;   // the agent Run on Target uses
let statusItem;
let out;
const announced = new Set();

function settings() {
  const c = vscode.workspace.getConfiguration('amix');
  return {
    port: c.get('agent.port') || 7777,
    address: c.get('agent.listenAddress') || '0.0.0.0',
    autoStart: c.get('agent.autoStart') !== false,
    program: c.get('run.program') || '',
    args: c.get('run.args') || [],
    display: c.get('run.display') === undefined ? ':0' : c.get('run.display'),
    env: c.get('run.env') || {},
    files: c.get('run.files') || [],
    buildFirst: c.get('run.buildFirst') !== false,
    fetchCore: c.get('run.fetchCore') !== false
  };
}

function log(line) {
  if (!out) out = vscode.window.createOutputChannel('AMIX Target');
  out.appendLine(line);
}

function agents() {
  return server ? [...server.agents] : [];
}

function updateStatus() {
  if (!statusItem) return;
  const list = agents();
  if (current && current.closed) current = null;
  if (!current && list.length) current = list[0];
  if (!server) {
    statusItem.text = '$(debug-disconnect) AMIX target: off';
    statusItem.tooltip = 'No agent listener. Click to start it (AMIX: Start Agent Listener).';
    statusItem.command = 'amix.startAgentListener';
  } else if (!current) {
    const s = settings();
    statusItem.text = '$(radio-tower) AMIX target: waiting';
    statusItem.tooltip = `Listening on port ${s.port} for vscode-agent. Click for the agent's configuration.`;
    statusItem.command = 'amix.showAgentConfig';
  } else {
    statusItem.text = `$(vm-connect) AMIX target: ${current.name}`;
    statusItem.tooltip = [
      `${current.name} (${current.info.address}), programs run as ${current.info.user}`,
      current.info.uname,
      `${current.info.free} KB free in ${current.info.dir}`,
      list.length > 1 ? 'Click to choose another target.' : 'Click to run (AMIX: Run on Target).'
    ].join('\n');
    statusItem.command = list.length > 1 ? 'amix.selectTarget' : 'amix.runOnTarget';
  }
}

async function loadKey() {
  key = (await ctx.secrets.get(KEY)) || '';
  return key;
}

async function startListener(quiet) {
  if (server) {
    if (!quiet) vscode.window.showInformationMessage(`AMIX: the agent listener runs on port ${settings().port}.`);
    return true;
  }
  await loadKey();
  if (!key) {
    if (!quiet) {
      const pick = await vscode.window.showWarningMessage(
        'AMIX: there is no agent key yet. Create one, or set the key the AMIX Installer gave you.',
        'Create Agent Key', 'Set Agent Key');
      if (pick === 'Create Agent Key') return createKey();
      if (pick === 'Set Agent Key') return setKey();
    }
    return false;
  }
  const s = settings();
  const srv = new AgentServer(() => key);
  try {
    await srv.listen(s.port, s.address);
  } catch (e) {
    if (!quiet) vscode.window.showErrorMessage(`AMIX: cannot listen on ${s.address}:${s.port}: ${e.message}`);
    log(`listen ${s.address}:${s.port}: ${e.message}`);
    return false;
  }
  server = srv;
  log(`listening on ${s.address}:${s.port}`);
  srv.on('agent', (a) => {
    log(`agent ${a.name} (${a.info.address}) signed in, runs as ${a.info.user}: ${a.info.uname}`);
    if (!announced.has(a.name)) {
      announced.add(a.name);
      vscode.window.showInformationMessage(`AMIX: ${a.name} is connected (programs run as ${a.info.user}).`);
    }
    a.on('close', () => { log(`agent ${a.name} left`); updateStatus(); });
    updateStatus();
  });
  srv.on('reject', (r) => {
    log(`refused ${r.address}: ${r.reason}`);
    if (/key/.test(r.reason)) {
      vscode.window.showWarningMessage(`AMIX: an agent at ${r.address} was refused: ${r.reason}.`);
    }
  });
  srv.on('error', (e) => log(`listener: ${e.message}`));
  updateStatus();
  return true;
}

function stopListener() {
  if (server) server.close();
  server = null;
  current = null;
  updateStatus();
}

function addresses() {
  const list = [];
  for (const ifs of Object.values(os.networkInterfaces())) {
    for (const i of ifs || []) {
      if ((i.family === 'IPv4' || i.family === 4) && !i.internal) list.push(i.address);
    }
  }
  return list;
}

function configText() {
  const s = settings();
  const lines = [
    '# /etc/vscode-agent.conf on AMIX, as root; then:',
    '#   chmod 600 /etc/vscode-agent.conf && /etc/init.d/vscode-agent start',
    '# (needs the package developer/vscode-agent: pkg install developer/vscode-agent)',
    '#',
    '# FS-UAE (its NAT shows this computer as 10.0.2.2)',
    `connect  ${FS_UAE_HOST}:${s.port}`,
    '# a real Amiga on the network: this computer'
  ];
  for (const a of addresses()) lines.push(`connect  ${a}:${s.port}`);
  lines.push(
    '# the AMIX user the programs run as (not root)',
    'user     CHANGE-ME',
    `secret   ${key}`,
    'log      /var/log/vscode-agent.log',
    '');
  return lines.join('\n');
}

async function showConfig() {
  await loadKey();
  if (!key) return startListener(false);
  const doc = await vscode.workspace.openTextDocument({ content: configText(), language: 'properties' });
  await vscode.window.showTextDocument(doc);
}

async function createKey() {
  await loadKey();
  if (key) {
    const ok = await vscode.window.showWarningMessage(
      'AMIX: replace the agent key? Agents set up with the old key can no longer connect.',
      { modal: true }, 'Replace');
    if (ok !== 'Replace') return false;
  }
  key = newKey();
  await ctx.secrets.store(KEY, key);
  stopListener();
  await startListener(true);
  await showConfig();
  return true;
}

async function setKey() {
  const v = await vscode.window.showInputBox({
    prompt: 'The agent key (the secret line of /etc/vscode-agent.conf on AMIX)',
    password: true,
    ignoreFocusOut: true,
    validateInput: (x) => (x.trim().length >= 16 && !/\s/.test(x.trim()) ? null
      : '16 characters or more, no spaces')
  });
  if (!v) return false;
  key = v.trim();
  await ctx.secrets.store(KEY, key);
  stopListener();
  return startListener(false);
}

async function selectTarget() {
  const list = agents();
  if (!list.length) {
    vscode.window.showInformationMessage('AMIX: no agent is connected.');
    return;
  }
  const pick = await vscode.window.showQuickPick(list.map((a) => ({
    label: a.name, description: `${a.info.address}, as ${a.info.user}`, detail: a.info.uname, a
  })), { placeHolder: 'The AMIX target for Run on Target' });
  if (pick) { current = pick.a; updateStatus(); }
}

// --- the program -----------------------------------------------------------

// an AMIX (m68k ELF) executable?
function isAmixExecutable(file) {
  try {
    const fd = fs.openSync(file, 'r');
    const b = Buffer.alloc(20);
    fs.readSync(fd, b, 0, 20, 0);
    fs.closeSync(fd);
    return b[0] === 0x7f && b.toString('latin1', 1, 4) === 'ELF' && b[5] === 2 &&
      b.readUInt16BE(16) === 2 && b.readUInt16BE(18) === 4;
  } catch (e) {
    return false;
  }
}

async function pickProgram(dir, s) {
  if (s.program) return path.resolve(dir, s.program);
  const found = fs.readdirSync(dir)
    .map((n) => path.join(dir, n))
    .filter((f) => { try { return fs.statSync(f).isFile() && isAmixExecutable(f); } catch (e) { return false; } })
    .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
  if (found.length === 0) return null;
  if (found.length === 1) return found[0];
  const last = ctx.workspaceState.get('amix.run.last');
  const items = found.map((f) => ({ label: path.basename(f), description: f === last ? 'last run' : '', f }));
  const pick = await vscode.window.showQuickPick(items, { placeHolder: 'Which program? (amix.run.program sets it)' });
  return pick ? pick.f : undefined;
}

function make(dir) {
  const c = hooks.cfg();
  const env = { ...process.env, ...hooks.makeEnv(c) };
  log(`$ cd ${dir} && ${c.makeCommand} ${c.makeArgs.join(' ')}`);
  return new Promise((resolve) => {
    const p = spawn(c.makeCommand, c.makeArgs, { cwd: dir, env });
    p.stdout.on('data', (d) => out.append(d.toString()));
    p.stderr.on('data', (d) => out.append(d.toString()));
    p.on('error', (e) => { log(`make: ${e.message}`); resolve(false); });
    p.on('close', (code) => resolve(code === 0));
  });
}

async function runOnTarget() {
  const s = settings();
  if (!server && !(await startListener(false))) return;
  if (current && current.closed) current = null;
  if (!current) {
    const pick = await vscode.window.showWarningMessage(
      'AMIX: no target is connected. vscode-agent on AMIX connects here once it has this key and address.',
      'Show Agent Configuration');
    if (pick) showConfig();
    return;
  }
  const agent = current;
  let dir;
  try {
    const root = hooks.workspaceRoot();
    const ed = vscode.window.activeTextEditor;
    dir = hooks.findMakeDir(ed ? path.dirname(ed.document.uri.fsPath) : root, root) || root;
  } catch (e) {
    vscode.window.showErrorMessage(e.message);
    return;
  }
  if (!out) out = vscode.window.createOutputChannel('AMIX Target');
  if (s.buildFirst && (fs.existsSync(path.join(dir, 'Makefile')) || fs.existsSync(path.join(dir, 'makefile')))) {
    if (!(await make(dir))) {
      out.show(true);
      vscode.window.showErrorMessage('AMIX: the build failed; nothing was sent to the target.');
      return;
    }
  }
  const prog = await pickProgram(dir, s);
  if (prog === undefined) return;
  if (!prog || !fs.existsSync(prog)) {
    vscode.window.showErrorMessage(`AMIX: no AMIX program found in ${dir}. Build it, or set amix.run.program.`);
    return;
  }
  await ctx.workspaceState.update('amix.run.last', prog);
  const name = path.basename(prog);
  try {
    await vscode.window.withProgress({
      location: vscode.ProgressLocation.Notification,
      title: `AMIX: sending ${name} to ${agent.name}`
    }, async () => {
      const t0 = Date.now();
      const data = fs.readFileSync(prog);
      await agent.put(name, data, 0o755);
      log(`sent ${name} (${data.length} bytes) to ${agent.name} in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
      for (const f of s.files) {
        const full = path.resolve(dir, f);
        await agent.put(path.basename(full), fs.readFileSync(full), 0o644);
        log(`sent ${path.basename(full)}`);
      }
    });
  } catch (e) {
    vscode.window.showErrorMessage(`AMIX: ${name} was not sent: ${e.message}`);
    return;
  }
  openTerminal(agent, name, [name, ...s.args], s, dir);
}

function openTerminal(agent, name, argv, s, dir) {
  const write = new vscode.EventEmitter();
  const closeEv = new vscode.EventEmitter();
  let run = null;
  let done = false;
  const env = { ...s.env };
  if (s.display) env.DISPLAY = s.display;

  const pty = {
    onDidWrite: write.event,
    onDidClose: closeEv.event,
    open(dims) {
      write.fire(`\x1b[2m${name} on ${agent.name}, as ${agent.info.user}\x1b[0m\r\n`);
      run = agent.run(argv, { env, cols: dims ? dims.columns : 80, rows: dims ? dims.rows : 24 });
      run.on('data', (d) => write.fire(d.toString('latin1')));
      run.on('exit', async (e) => {
        done = true;
        if (e.lost) {
          write.fire('\r\n\x1b[31m[the connection to the target was lost]\x1b[0m\r\n');
          return;
        }
        if (e.signal) {
          write.fire(`\r\n\x1b[33m[${name} ended by signal ${e.signal}${e.core ? ', core dumped' : ''}]\x1b[0m\r\n`);
          if (e.core && s.fetchCore) {
            try {
              const core = await agent.get('core');
              if (core) {
                const f = path.join(dir, `core.${name}`);
                fs.writeFileSync(f, core);
                write.fire(`\x1b[2m[core saved as ${f}]\x1b[0m\r\n`);
              }
            } catch (err) {
              write.fire(`\x1b[2m[core not fetched: ${err.message}]\x1b[0m\r\n`);
            }
          }
        } else {
          write.fire(`\r\n\x1b[2m[${name} exited with ${e.code}]\x1b[0m\r\n`);
        }
      });
      run.started.catch((err) => {
        done = true;
        write.fire(`\x1b[31m${name} did not start: ${err.message}\x1b[0m\r\n`);
      });
    },
    handleInput(data) {
      if (run && !done) run.write(Buffer.from(data, 'latin1'));
    },
    setDimensions(d) {
      if (run && !done) run.resize(d.columns, d.rows);
    },
    close() {
      if (run && !done) run.kill(1);
    }
  };
  const term = vscode.window.createTerminal({ name: `AMIX: ${name} @ ${agent.name}`, pty });
  term.show();
}

function activate(context, h) {
  ctx = context;
  hooks = h;
  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 49);
  updateStatus();
  statusItem.show();
  context.subscriptions.push(
    statusItem,
    vscode.commands.registerCommand('amix.createAgentKey', createKey),
    vscode.commands.registerCommand('amix.setAgentKey', setKey),
    vscode.commands.registerCommand('amix.showAgentConfig', showConfig),
    vscode.commands.registerCommand('amix.startAgentListener', () => startListener(false)),
    vscode.commands.registerCommand('amix.stopAgentListener', stopListener),
    vscode.commands.registerCommand('amix.selectTarget', selectTarget),
    vscode.commands.registerCommand('amix.runOnTarget', runOnTarget),
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration('amix.agent') && server) { stopListener(); startListener(true); }
    }),
    { dispose: stopListener }
  );
  if (settings().autoStart) startListener(true);
}

module.exports = { activate, isAmixExecutable, configText };
