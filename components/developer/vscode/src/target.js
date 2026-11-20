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
// where the AMIX Installer keeps the key of an agent it set up
const SETTLE_MS = 400;     // a new terminal's size settles within this
const INSTALLER_KEY = path.join(os.homedir(), '.local/share/amix-installer/vscode-agent.key');

let ctx;
let hooks;            // { cfg, makeEnv, findMakeDir, workspaceRoot }
let server = null;
let key = '';
let current = null;   // the agent Run on Target uses
let statusItem;
let out;
const announced = new Set();
const warned = new Set();      // hosts told about a refused key

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
    statusItem.tooltip = `Listening on port ${s.port} for vscode-agent; none has signed in. Click for how to set up an AMIX target.`;
    statusItem.command = 'amix.setupTarget';
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

function goodKey(v) {
  return v.length >= 16 && !/\s/.test(v);
}

// the key in a file: a vscode-agent.key (the key on its first line) or a
// vscode-agent.conf (its secret line); '' when there is none
function keyFromFile(f) {
  let text;
  try { text = fs.readFileSync(f, 'utf8'); } catch (e) { return ''; }
  const lines = text.split(/\r?\n/).map((l) => l.trim()).filter((l) => l && !l.startsWith('#'));
  const sec = lines.find((l) => /^secret\s/.test(l));
  const v = sec ? sec.replace(/^secret\s+/, '') : (lines[0] || '');
  return goodKey(v) ? v : '';
}

// what was typed, when it names a file (/... or ~/...) that exists
function typedPath(x) {
  const t = x.trim();
  if (!/^(\/|~\/|~$)/.test(t)) return '';
  const f = t.startsWith('~') ? path.join(os.homedir(), t.slice(1)) : t;
  try { return fs.statSync(f).isFile() ? f : ''; } catch (e) { return ''; }
}

// the key the AMIX Installer wrote when it put the agent on AMIX, or ''
function installerKey() {
  return keyFromFile(INSTALLER_KEY);
}

async function loadKey() {
  key = (await ctx.secrets.get(KEY)) || '';
  // no key of our own yet: the installer's, so its agent signs in at once
  if (!key && installerKey()) {
    key = installerKey();
    await ctx.secrets.store(KEY, key);
    log(`agent key taken from ${INSTALLER_KEY}`);
  }
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
    const host = String(r.address).replace(/:\d+$/, '');
    if (!/key/.test(r.reason) || warned.has(host)) return;
    warned.add(host);           // the agent dials again and again: ask once
    const ik = installerKey();
    if (ik && ik !== key) {
      vscode.window.showWarningMessage(
        `AMIX: an agent at ${r.address} was refused: ${r.reason}. The AMIX Installer `
        + `put an agent on AMIX with the key in ${INSTALLER_KEY}. Use that key?`,
        'Use the Installer\'s Key', 'Set Agent Key').then((b) => {
        if (b === 'Use the Installer\'s Key') useKey(ik);
        else if (b === 'Set Agent Key') setKey();
      });
    } else {
      vscode.window.showWarningMessage(`AMIX: an agent at ${r.address} was refused: ${r.reason}.`,
        'Set Agent Key', 'How to Set Up').then((b) => {
        if (b === 'Set Agent Key') setKey();
        else if (b === 'How to Set Up') showSetup();
      });
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

// What Check Setup and the guide say about Run on Target.
function statusLines() {
  const s = settings();
  const lines = [];
  if (!key) {
    lines.push('MISSING agent key: AMIX: Create Agent Key, or AMIX: Set Agent Key with the AMIX Installer\'s vscode-agent.key'
      + (fs.existsSync(INSTALLER_KEY) ? ` (${INSTALLER_KEY} is there)` : ''));
  }
  else lines.push('ok      agent key set');
  if (!server) lines.push(`absent  agent listener (port ${s.port}): AMIX: Start Agent Listener`);
  else lines.push(`ok      agent listener on ${s.address}:${s.port} (this computer: ${[FS_UAE_HOST + ' from FS-UAE', ...addresses()].join(', ')})`);
  const list = agents();
  if (list.length) {
    for (const a of list) {
      lines.push(`ok      target ${a.name} (${a.info.address}), programs run as ${a.info.user}`
        + (a.info.term ? `, TERM=${a.info.term}` : ''));
      // agents before 0.1.2 say nothing; stock xterm lacks colours and civis
      if (a.info.term && a.info.term !== 'xterm-256color') {
        lines.push(`note    ${a.name} has no xterm-256color: install sysutils/terminfo on AMIX for colours`);
      }
    }
  } else {
    lines.push('absent  AMIX target: no vscode-agent has signed in. AMIX: Set Up an AMIX Target shows how to install it');
    lines.push('        (AMIX Installer, "Develop from a PC", or pkg install developer/vscode-agent on an installed AMIX).');
  }
  return lines;
}

function setupGuide() {
  const s = settings();
  const here = addresses();
  return [
    '# Set up an AMIX target for Run on Target',
    '',
    'Run on Target and the `amix` launch type need **vscode-agent** on the AMIX machine (an Amiga or FS-UAE).',
    'It starts at boot from `/etc/init.d/vscode-agent`, dials this computer on port ' + s.port + ' and signs in with a shared key.',
    'Nothing needs ssh or an open port on AMIX.',
    '',
    '## Now',
    '',
    ...statusLines().map((l) => '    ' + l),
    '',
    '## A. New AMIX with the AMIX Installer (recommended)',
    '',
    '1. Start the AMIX Installer on this computer (OpenIndiana: `pkg install desktop/amix-installer`).',
    '2. On the development step, in **Develop from a PC**, switch on **VS Code agent**; or press **Only the VS Code agent** for a',
    '   small system with just the base and the agent, without the desktop.',
    '3. Install. The installer writes `/etc/vscode-agent.conf` on AMIX with this computer\'s address and a new key,',
    '   and saves the key as `vscode-agent.key` beside its report (the last page names the file).',
    '4. Here: nothing to do when this extension has no key of its own yet: it takes the installer\'s key file',
    '   (`' + INSTALLER_KEY + '`). With a key of its own, the first agent it refuses brings',
    '   **Use the Installer\'s Key**; or run **AMIX: Set Agent Key**, which takes the installer\'s key,',
    '   a key file you choose (`vscode-agent.key` or a copy of `/etc/vscode-agent.conf`), or the key pasted in. The status bar shows',
    '   `AMIX target: <name>` once AMIX has booted.',
    '',
    '**Quick test, just the agent** into an AMIX that is already running (FS-UAE or an Amiga, nothing else changed):',
    'the installer\'s **Just the Agent** button for a running system, or',
    '`amix-installer --running fs-uae --agent-only --net` (`--running amiga` for a real Amiga on the network).',
    'It installs or updates only developer/vscode-agent, writes its configuration with a new key and restarts it;',
    'it prints the key file and the address the agent dials. Then **AMIX: Set Agent Key** as in step 4.',
    '',
    'For Motif/CDE programs (the CDE sample) AMIX needs Motif too: **Agent and Motif**, or `--agent-only --with-motif`',
    '(desktop/motif, with X11R6.3). Without it the program stops with "can\'t find library libXm.so.4".',
    '',
    'The base system plus the agent into a running AMIX: `amix-installer --running fs-uae --vscode-only --net`.',
    '',
    '**A new project folder** (AMIX: New ... Project) may open in Restricted Mode: choose **Yes, I trust the authors**',
    '(or **Manage Workspace Trust**), since the AMIX commands stay off in a folder VS Code does not trust.',
    '',
    '## B. Just the agent, by hand, on an installed AMIX',
    '',
    'The agent is one program that needs nothing beyond a stock AMIX 2.1.',
    '',
    '1. As root on AMIX: `pkg install developer/vscode-agent` (from the AMIX package repository the installer serves); without pkg, copy',
    '   `/usr/local/bin/vscode-agent` and `/etc/init.d/vscode-agent` from the package (ftp, tftp) and make both executable.',
    '2. **AMIX: Show Agent Configuration** here gives the file to save as `/etc/vscode-agent.conf` on AMIX:',
    '   it has this key and the `connect` lines; set `user` to the AMIX login the programs run as (not root).',
    '3. On AMIX: `chmod 600 /etc/vscode-agent.conf && /etc/init.d/vscode-agent start`',
    '',
    '## Addresses',
    '',
    `- FS-UAE: AMIX sees this computer as \`${FS_UAE_HOST}\` (FS-UAE's NAT); nothing else is needed.`,
    `- A real Amiga: one of ${here.length ? here.map((a) => '`' + a + '`').join(', ') : '(no network address found)'}, port ${s.port} open in this computer's firewall.`,
    '',
    'If an agent connects with another key, it is refused and the AMIX Target output names its address.',
    'The agent logs to `/var/log/vscode-agent.log` on AMIX.',
    ''
  ].join('\n');
}

async function showSetup() {
  await loadKey();
  const doc = await vscode.workspace.openTextDocument({ content: setupGuide(), language: 'markdown' });
  await vscode.window.showTextDocument(doc);
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
  const items = [];
  // the AMIX Installer's key, when it ran on this computer
  if (fs.existsSync(INSTALLER_KEY)) {
    items.push({ label: 'Use the AMIX Installer\'s key', description: INSTALLER_KEY, how: 'installer' });
  }
  items.push({ label: 'Choose a key file...', description: 'vscode-agent.key or vscode-agent.conf', how: 'file' });
  items.push({ label: 'Paste a key', description: 'the text of the key, or the path of its file', how: 'paste' });
  const pick = await vscode.window.showQuickPick(items,
    { placeHolder: 'The agent key: the secret line of /etc/vscode-agent.conf on AMIX' });
  if (!pick) return false;
  if (pick.how === 'installer') {
    const v = installerKey();
    if (!v) {
      vscode.window.showErrorMessage(`AMIX: ${INSTALLER_KEY} holds no agent key.`);
      return false;
    }
    return useKey(v);
  }
  if (pick.how === 'file') {
    const dir = path.dirname(INSTALLER_KEY);
    const got = await vscode.window.showOpenDialog({
      canSelectMany: false,
      openLabel: 'Use This Key',
      title: 'The agent key file (vscode-agent.key, or a vscode-agent.conf)',
      defaultUri: vscode.Uri.file(fs.existsSync(dir) ? dir : os.homedir())
    });
    if (!got || !got.length) return false;
    const v = keyFromFile(got[0].fsPath);
    if (!v) {
      vscode.window.showErrorMessage(`AMIX: ${got[0].fsPath} holds no agent key `
        + '(a line of 16 or more characters without spaces, or a secret line).');
      return false;
    }
    return useKey(v);
  }
  const v = await vscode.window.showInputBox({
    prompt: 'The agent key (the secret line of /etc/vscode-agent.conf on AMIX), or the path of a key file',
    password: true,
    ignoreFocusOut: true,
    validateInput: (x) => {
      const f = typedPath(x);
      if (f) return keyFromFile(f) ? null : `${f} holds no agent key`;
      return goodKey(x.trim()) ? null : '16 characters or more, no spaces (or the path of a key file)';
    }
  });
  if (!v) return false;
  const f = typedPath(v);
  if (f) log(`agent key read from ${f}`);
  return useKey(f ? keyFromFile(f) : v.trim());
}

async function useKey(v) {
  key = v;
  warned.clear();
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

// over: a launch configuration's program, args, env, display, files,
// buildFirst, cwd (they win over the settings).  Returns { done, kill } for
// the program started, or undefined when nothing was started.
async function runOnTarget(over = {}) {
  const s = settings();
  for (const k of ['program', 'args', 'env', 'display', 'files', 'buildFirst', 'fetchCore']) {
    if (over[k] !== undefined && over[k] !== null) s[k] = over[k];
  }
  if (!server && !(await startListener(false))) return;
  if (current && current.closed) current = null;
  if (!current) {
    const pick = await vscode.window.showWarningMessage(
      'AMIX: no AMIX target is connected. It needs vscode-agent on AMIX: install it with the AMIX Installer '
      + '("Develop from a PC") or pkg install developer/vscode-agent, then give both sides the same key.',
      'How to Set Up', 'Set Agent Key', 'Show Agent Configuration');
    if (pick === 'How to Set Up') showSetup();
    else if (pick === 'Set Agent Key') setKey();
    else if (pick === 'Show Agent Configuration') showConfig();
    return;
  }
  const agent = current;
  let dir;
  try {
    const root = hooks.workspaceRoot();
    const ed = vscode.window.activeTextEditor;
    dir = over.cwd ? path.resolve(root, over.cwd)
      : hooks.findMakeDir(ed ? path.dirname(ed.document.uri.fsPath) : root, root) || root;
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
  return openTerminal(agent, name, [name, ...s.args], s, dir);
}

function openTerminal(agent, name, argv, s, dir) {
  const write = new vscode.EventEmitter();
  const closeEv = new vscode.EventEmitter();
  let run = null;
  let done = false;
  let size;
  let settle = null;
  let finish;
  const finished = new Promise((res) => { finish = res; });
  const env = { ...s.env };
  if (s.display) env.DISPLAY = s.display;

  const pty = {
    onDidWrite: write.event,
    onDidClose: closeEv.event,
    open(dims) {
      write.fire(`\x1b[2m${name} on ${agent.name}, as ${agent.info.user}\x1b[0m\r\n`);
      size = dims;
      // VS Code resizes a new terminal at once (and again as F5 lays out the
      // debug view): start with the size it settles on, since a curses
      // program that misses a resize draws over the wrong lines
      settle = setTimeout(start, SETTLE_MS);
    },
    handleInput(data) {
      if (run && !done) run.write(Buffer.from(data, 'latin1'));
    },
    setDimensions(d) {
      size = d;
      if (settle) {
        clearTimeout(settle);
        settle = setTimeout(start, SETTLE_MS);
      } else if (run && !done) {
        run.resize(d.columns, d.rows);
      }
    },
    close() {
      if (settle) { clearTimeout(settle); settle = null; done = true; finish({ error: 'closed' }); }
      if (run && !done) run.kill(1);
    }
  };
  function start() {
    settle = null;
    if (done) return;
    run = agent.run(argv, { env, cols: size ? size.columns : 80, rows: size ? size.rows : 24 });
    let tail = '';
    let hinted = false;
    run.on('data', (d) => {
      const text = d.toString('latin1');
      write.fire(text);
      // AMIX's ld.so: "dynamic linker: hello: can't find library libXm.so.4"
      tail = (tail + text).slice(-400);
      if (!hinted && /can.t (find|open) (library|shared object)[^\n]*lib(Xm|Mrm|Dt|Tt)\b/i.test(tail)) {
        hinted = true;
        write.fire('\r\n\x1b[33m[Motif/CDE is not on this AMIX: in the AMIX Installer, use "Agent and Motif" '
          + '(amix-installer --running ... --agent-only --with-motif), or pkg install desktop/motif]\x1b[0m\r\n');
      }
    });
    run.on('exit', async (e) => {
      done = true;
      if (e.lost) {
        write.fire('\r\n\x1b[31m[the connection to the target was lost]\x1b[0m\r\n');
        finish(e);
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
      finish(e);
    });
    run.started.catch((err) => {
      done = true;
      write.fire(`\x1b[31m${name} did not start: ${err.message}\x1b[0m\r\n`);
      finish({ error: err.message });
    });
  }
  const term = vscode.window.createTerminal({ name: `AMIX: ${name} @ ${agent.name}`, pty });
  term.show();
  return {
    name,
    agent: agent.name,
    done: finished,
    kill: (sig = 15) => { if (settle) pty.close(); else if (run && !done) run.kill(sig); }
  };
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
    vscode.commands.registerCommand('amix.setupTarget', showSetup),
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
  // launch.json: "type": "amix" (src/launch.js)
  require('./launch').activate(context, (config) => runOnTarget(config));
}

module.exports = { keyFromFile, typedPath, activate, isAmixExecutable, configText, statusLines, setupGuide };
