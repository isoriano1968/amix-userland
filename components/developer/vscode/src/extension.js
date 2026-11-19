const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

// The two AMIX cross toolchains.  gcc 2.7.2.3 is the native-compatible one and
// the only one for kernel code; gcc 14 (gcc14-cross-amix) is for userland only.
const TOOLCHAINS = {
  gcc272: {
    label: 'gcc 2.7.2.3',
    detail: 'm68k-cbm-sysv4-gcc: kernel, drivers and userland'
  },
  gcc14: {
    label: 'gcc 14',
    detail: 'm68k-elf-gcc (gcc14-cross-amix): userland only, usually -m68060'
  }
};

// Where each toolchain may be installed, first found wins: the OpenIndiana
// packages (developer/amix/gcc-27, gcc-14 and developer/amix/sysroot under
// /opt), then the hand-built layout under ~/opt.  A setting overrides.
const LAYOUTS = {
  gcc272: [
    { kind: 'package', bin: '/opt/amix-cross/bin', sysroot: '/opt/m68k-amix-sysroot' },
    { kind: 'home', bin: '~/opt/amix-cross/bin', sysroot: '~/opt/amix-cross/m68k-cbm-sysv4/sysroot' }
  ],
  gcc14: [
    { kind: 'package', bin: '/opt/m68k-amix/bin', sysroot: '/opt/m68k-amix-sysroot' },
    { kind: 'home', bin: '~/opt/m68k-amix/bin', sysroot: '~/opt/m68k-amix-sysroot' }
  ]
};
const PKG = {
  gcc272: 'developer/amix/gcc-27',
  gcc14: 'developer/amix/gcc-14',
  sysroot: 'developer/amix/sysroot',
  dlmkit: 'developer/amix/dlmkit',
  all: 'metapackages/amix-cross-dev',
  // X11R6.3, Motif, CDE (and amixc99, libXp) for the CDE sample
  desktop: 'metapackages/amix-cross-dev-desktop'
};

let extensionRoot = '';
let output;
let statusItem;

function activate(context) {
  extensionRoot = context.extensionPath;

  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
  statusItem.command = 'amix.selectToolchain';
  updateStatus();
  statusItem.show();

  context.subscriptions.push(
    statusItem,
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration('amix')) updateStatus();
    }),
    vscode.commands.registerCommand('amix.configureWorkspace', configureWorkspace),
    vscode.commands.registerCommand('amix.selectToolchain', selectToolchain),
    vscode.commands.registerCommand('amix.checkSetup', checkSetup),
    vscode.commands.registerCommand('amix.buildCurrentExecutable', () => buildCurrent('exe')),
    vscode.commands.registerCommand('amix.compileCurrentObject', () => buildCurrent('object')),
    vscode.commands.registerCommand('amix.compileDriverObject', () => buildCurrent('driver')),
    vscode.commands.registerCommand('amix.make', () => runMake(false)),
    vscode.commands.registerCommand('amix.makeClean', () => runMake(true)),
    vscode.commands.registerCommand('amix.makeGcc272', () => runMake(false, 'gcc272')),
    vscode.commands.registerCommand('amix.makeGcc14', () => runMake(false, 'gcc14')),
    vscode.commands.registerCommand('amix.makeCleanGcc272', () => runMake(true, 'gcc272')),
    vscode.commands.registerCommand('amix.makeCleanGcc14', () => runMake(true, 'gcc14')),
    vscode.commands.registerCommand('amix.newCdeHelloProject', () => newSampleProject('cde')),
    vscode.commands.registerCommand('amix.newConsoleHelloProject', () => newSampleProject('console')),
    // for tasks.json: ${command:amix.fixNeededScript}
    vscode.commands.registerCommand('amix.fixNeededScript', fixNeededScript)
  );
  // Run on Target: vscode-agent on AMIX dials in (src/target.js)
  require('./target').activate(context, {
    cfg: () => cfg(),
    makeEnv,
    findMakeDir,
    workspaceRoot
  });
}

function deactivate() {}

function workspaceRoot() {
  const folders = vscode.workspace.workspaceFolders;
  if (!folders || folders.length === 0) {
    throw new Error('Open a workspace folder first.');
  }
  return folders[0].uri.fsPath;
}

// The settings of a toolchain: the selected one, or the one named.
function cfg(which) {
  const c = vscode.workspace.getConfiguration('amix');
  const toolchain = which || selectedToolchain();
  const g14prefix = c.get('gcc14.toolPrefix') || 'm68k-elf-';
  const g14 = layout('gcc14', expand(c.get('gcc14.toolchainPath')), g14prefix);
  const gcc14Sysroot = expand(c.get('gcc14.sysroot')) || g14.sysroot;
  let toolchainPath, prefix, sysroot, crtDir, cpu, extraCFlags, includeDirs, where;

  if (toolchain === 'gcc14') {
    toolchainPath = g14.bin;
    prefix = g14prefix;
    sysroot = gcc14Sysroot;
    where = g14.kind;
    crtDir = path.join(sysroot, 'usr/ccs/lib');
    cpu = c.get('gcc14.cpu') || '68060';
    extraCFlags = c.get('gcc14.extraCFlags') || [];
    includeDirs = [
      'usr/include',
      'usr/motif/include',
      'usr/x11r6/include',
      'usr/dt/include',
      'usr/local/include'
    ].map((d) => path.join(sysroot, d));
  } else {
    const target = c.get('target') || 'm68k-cbm-sysv4';
    prefix = `${target}-`;
    const g27 = layout('gcc272', expand(c.get('toolchainPath')), prefix);
    toolchainPath = g27.bin;
    sysroot = expand(c.get('sysroot')) || g27.sysroot;
    where = g27.kind;
    crtDir = expand(c.get('crtDir')).replace('${config:amix.sysroot}', sysroot) ||
      path.join(sysroot, 'usr/ccs/lib');
    cpu = c.get('cpu') || '68020';
    extraCFlags = c.get('extraCFlags') || [];
    includeDirs = [
      'usr/include',
      'usr/sys',
      'usr/sys/amiga/inc'
    ].map((d) => path.join(sysroot, d));
  }

  const tool = (name) => path.join(toolchainPath, `${prefix}${name}`);
  return {
    toolchain,
    label: TOOLCHAINS[toolchain].label,
    target: prefix.replace(/-$/, ''),
    prefix,
    toolchainPath,
    where,
    sysroot,
    crtDir,
    motifSysroot: gcc14Sysroot,
    cpu,
    includeDirs,
    gcc: tool('gcc'),
    nm: tool('nm'),
    extraCFlags,
    kernelDefines: c.get('kernelDefines') || [],
    fixNeeded: toolchain === 'gcc14' && c.get('gcc14.fixNeeded') !== false,
    makeCommand: c.get('makeCommand') || defaultMake(),
    makeArgs: c.get('makeArgs') || []
  };
}

// the AMIX Makefiles are GNU make; on illumos/Solaris "make" is Sun make
function defaultMake() {
  return process.platform === 'sunos' ? 'gmake' : 'make';
}

// The install place of a toolchain: the one whose bin holds <prefix>gcc, or
// the one the toolchainPath setting names, else the packaged one.
function layout(toolchain, explicitBin, prefix) {
  const all = LAYOUTS[toolchain].map((l) => ({ ...l, bin: expand(l.bin), sysroot: expand(l.sysroot) }));
  if (explicitBin) {
    const same = all.find((l) => path.resolve(l.bin) === path.resolve(explicitBin));
    return same || { kind: 'setting', bin: explicitBin, sysroot: all[all.length - 1].sysroot };
  }
  return all.find((l) => fs.existsSync(path.join(l.bin, `${prefix}gcc`))) || all[0];
}

// etc/amix-sysroot.release: key=value lines (version, kind, created, ...)
function sysrootMarker(sysroot) {
  try {
    const text = fs.readFileSync(path.join(sysroot, 'etc/amix-sysroot.release'), 'utf8');
    const m = {};
    for (const line of text.split('\n')) {
      const i = line.indexOf('=');
      if (i > 0) m[line.slice(0, i).trim()] = line.slice(i + 1).trim();
    }
    return m;
  } catch (e) {
    return null;
  }
}

function sysrootLabel(sysroot) {
  const m = sysrootMarker(sysroot);
  if (!m) return fs.existsSync(sysroot) ? 'sysroot (no marker)' : 'no sysroot';
  // package=developer/amix/sysroot@2.1.2, else amix-release=2.1c ..., else kind=local
  const pkg = m.package && m.package.includes('@') ? m.package.slice(m.package.lastIndexOf('@') + 1) : '';
  const v = pkg || m.version || m['amix-release'] || (m.kind || '').split(' ')[0];
  return v ? `sysroot ${v}` : 'sysroot';
}

function selectedToolchain() {
  const t = vscode.workspace.getConfiguration('amix').get('toolchain');
  return TOOLCHAINS[t] ? t : 'gcc272';
}

function expand(value) {
  if (!value) return '';
  const home = process.env.HOME || process.env.USERPROFILE || '';
  return value.replace(/\$\{env:HOME\}/g, home).replace(/^~(?=$|[\\/])/, home);
}

function updateStatus() {
  if (!statusItem) return;
  const c = cfg();
  const found = fs.existsSync(c.gcc);
  const m = sysrootMarker(c.sysroot);
  statusItem.text = `$(${found ? 'tools' : 'warning'}) AMIX ${c.label} · ${sysrootLabel(c.sysroot)}`;
  statusItem.tooltip = [
    `AMIX toolchain: ${c.label} (${c.gcc}${found ? '' : ', not found'})`,
    `Sysroot: ${c.sysroot}${m ? ` (${Object.entries(m).map(([k, v]) => `${k}=${v}`).join(', ')})` : ''}`,
    'Click to switch between gcc 2.7.2.3 and gcc 14.'
  ].join('\n');
}

async function selectToolchain() {
  const current = selectedToolchain();
  const items = Object.entries(TOOLCHAINS).map(([id, t]) => {
    const c = cfg(id);
    return {
      id,
      label: `${id === current ? '$(check) ' : ''}${t.label}`,
      description: c.gcc,
      detail: `${t.detail}${fs.existsSync(c.gcc) ? '' : ' -- not found at this path'}`
    };
  });
  const pick = await vscode.window.showQuickPick(items, {
    placeHolder: 'AMIX toolchain for builds, Makefiles and IntelliSense'
  });
  if (!pick || pick.id === current) return;

  const hasWorkspace = (vscode.workspace.workspaceFolders || []).length > 0;
  await vscode.workspace.getConfiguration('amix').update(
    'toolchain',
    pick.id,
    hasWorkspace ? vscode.ConfigurationTarget.Workspace : vscode.ConfigurationTarget.Global
  );
  updateStatus();

  const c = cfg(pick.id);
  if (!fs.existsSync(c.gcc)) {
    vscode.window.showWarningMessage(`AMIX: ${c.gcc} does not exist; check the amix.* toolchain settings.`);
  }
  if (hasWorkspace && fs.existsSync(path.join(workspaceRoot(), '.vscode', 'tasks.json'))) {
    const answer = await vscode.window.showInformationMessage(
      `AMIX toolchain is now ${c.label}. Regenerate .vscode/settings.json and tasks.json for it?`,
      'Regenerate'
    );
    if (answer === 'Regenerate') await configureWorkspace();
  }
}

async function configureWorkspace() {
  const root = workspaceRoot();
  const settingsDir = path.join(root, '.vscode');
  fs.mkdirSync(settingsDir, { recursive: true });

  const c = cfg();
  const k = cfg('gcc272'); // kernel and driver objects: gcc 2.7.2.3 only
  const settings = {
    'amix.toolchain': c.toolchain,
    'C_Cpp.default.compilerPath': c.gcc,
    'C_Cpp.default.intelliSenseMode': 'gcc-x64',
    'C_Cpp.default.includePath': [
      '${workspaceFolder}/**',
      ...c.includeDirs
    ],
    'C_Cpp.default.defines': [
      'SVR4',
      'SVR40'
    ]
  };

  // tasks for both toolchains, labelled by compiler; the selected one's
  // executable build is the default build task
  const notC = 'echo "AMIX: active file must be a .c source file, not ${fileBasename}" >&2; exit 2';
  const toolchainEnv = (t) => ({
    AMIX_TOOLCHAIN: t.toolchain,
    AMIX_SYSROOT: t.sysroot,
    AMIX_CRT_DIR: t.crtDir
  });

  const toolchainTasks = (t) => {
    const fix = t.fixNeeded
      ? ` && python3 "\${command:amix.fixNeededScript}" "\${fileDirname}/\${fileBasenameNoExtension}"`
      : '';
    const selected = t.toolchain === c.toolchain;
    return [
      {
        label: `AMIX: build current executable (${t.label})`,
        type: 'shell',
        command: 'bash',
        options: { env: toolchainEnv(t) },
        args: [
          '-lc',
          `case "\${file}" in *.c) ${quote(t.gcc)} -m${t.cpu} "\${file}" -o "\${fileDirname}/\${fileBasenameNoExtension}"${fix} ;; *) ${notC} ;; esac`
        ],
        group: selected ? { kind: 'build', isDefault: true } : 'build',
        problemMatcher: '$amix-gcc'
      },
      {
        label: `AMIX: compile current object (${t.label})`,
        type: 'shell',
        command: 'bash',
        options: { env: toolchainEnv(t) },
        args: [
          '-lc',
          `case "\${file}" in *.c) exec ${quote(t.gcc)} -c -m${t.cpu} ${t.extraCFlags.map(quote).join(' ')} "\${file}" -o "\${fileDirname}/\${fileBasenameNoExtension}.o" ;; *) ${notC} ;; esac`
        ],
        group: 'build',
        problemMatcher: '$amix-gcc'
      },
      {
        label: `AMIX: nm current output (${t.label})`,
        type: 'shell',
        command: quote(t.nm),
        args: [
          '-a',
          '${fileDirname}/${fileBasenameNoExtension}'
        ],
        problemMatcher: []
      },
      {
        label: `AMIX: make project (${t.label})`,
        type: 'shell',
        command: t.makeCommand,
        args: t.makeArgs,
        options: {
          cwd: '${fileDirname}',
          env: makeEnv(t)
        },
        group: 'build',
        problemMatcher: '$amix-gcc'
      },
      {
        label: `AMIX: make clean (${t.label})`,
        type: 'shell',
        command: t.makeCommand,
        args: [
          'clean',
          ...t.makeArgs
        ],
        options: {
          cwd: '${fileDirname}',
          env: makeEnv(t)
        },
        problemMatcher: []
      }
    ];
  };

  const tasks = {
    version: '2.0.0',
    tasks: [
      ...toolchainTasks(cfg('gcc272')),
      ...toolchainTasks(cfg('gcc14')),
      {
        label: 'AMIX: compile current driver object (gcc 2.7.2.3)',
        detail: 'kernel code is never built with gcc 14',
        type: 'shell',
        command: 'bash',
        options: { env: toolchainEnv(k) },
        args: [
          '-lc',
          `case "\${file}" in *.c) exec ${quote(k.gcc)} -c -m${k.cpu} ${driverFlags(k).map(quote).join(' ')} "\${file}" -o "\${fileDirname}/\${fileBasenameNoExtension}.o" ;; *) ${notC} ;; esac`
        ],
        group: 'build',
        problemMatcher: '$amix-gcc'
      }
    ]
  };

  writeJson(path.join(settingsDir, 'settings.json'), mergeSettings(path.join(settingsDir, 'settings.json'), settings));
  writeJson(path.join(settingsDir, 'tasks.json'), tasks);
  // F5 / Ctrl+F5 on the AMIX target; an existing launch.json is the user's
  if (!fs.existsSync(path.join(settingsDir, 'launch.json'))) {
    writeJson(path.join(settingsDir, 'launch.json'), launchJson(''));
  }

  vscode.window.showInformationMessage(`AMIX workspace settings (${c.label}) and tasks for gcc 2.7.2.3 and gcc 14 written.`);
}

function driverFlags(k) {
  return [
    ...k.extraCFlags,
    ...k.kernelDefines,
    ...k.includeDirs.map((d) => `-I${d}`)
  ];
}

async function buildCurrent(kind) {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showErrorMessage('Open a C source file first.');
    return;
  }

  const file = editor.document.uri.fsPath;
  if (path.extname(file).toLowerCase() !== '.c') {
    vscode.window.showErrorMessage('AMIX build commands require an active .c source file.');
    return;
  }
  const parsed = path.parse(file);
  let c = cfg();
  if (kind === 'driver' && c.toolchain !== 'gcc272') {
    vscode.window.showInformationMessage('AMIX: kernel and driver objects are built with gcc 2.7.2.3, never gcc 14.');
    c = cfg('gcc272');
  }
  const cpu = `-m${c.cpu}`;
  let command;

  if (kind === 'exe') {
    const exe = path.join(parsed.dir, parsed.name);
    command = [
      envPrefix(c),
      quote(c.gcc),
      cpu,
      quote(file),
      '-o',
      quote(exe)
    ].join(' ');
    if (c.fixNeeded) {
      command += ` && python3 ${quote(fixNeededScript())} ${quote(exe)}`;
    }
  } else if (kind === 'object') {
    command = [
      envPrefix(c),
      quote(c.gcc),
      '-c',
      cpu,
      ...c.extraCFlags.map(quote),
      quote(file),
      '-o',
      quote(path.join(parsed.dir, `${parsed.name}.o`))
    ].join(' ');
  } else {
    command = [
      envPrefix(c),
      quote(c.gcc),
      '-c',
      cpu,
      ...driverFlags(c).map(quote),
      quote(file),
      '-o',
      quote(path.join(parsed.dir, `${parsed.name}.o`))
    ].join(' ');
  }

  const terminal = vscode.window.createTerminal(`AMIX Build (${c.label})`);
  terminal.show();
  terminal.sendText(command);
}

async function runMake(clean, which) {
  const root = workspaceRoot();
  const editor = vscode.window.activeTextEditor;
  const start = editor ? path.dirname(editor.document.uri.fsPath) : root;
  const makeDir = findMakeDir(start, root);

  if (!makeDir) {
    vscode.window.showErrorMessage('No Makefile found in the active file directory or workspace parents.');
    return;
  }

  const c = cfg(which);
  warnHardcodedMakeTools(makeDir, c);
  const env = makeEnv(c);
  const args = clean ? ['clean', ...c.makeArgs] : c.makeArgs;
  const exports = Object.entries(env)
    .map(([k, v]) => `${k}=${quote(v)}`)
    .join(' ');
  const command = [
    `cd ${quote(makeDir)}`,
    `${exports} ${quote(c.makeCommand)} ${args.map(quote).join(' ')}`.trim()
  ].join(' && ');

  const terminal = vscode.window.createTerminal(`AMIX Make (${c.label})`);
  terminal.show();
  terminal.sendText(command);
}

// Copy samples/cde-hello into the workspace (or a folder picked when none is open).
// the sample projects: folder under samples/, name, what to do after
const SAMPLES = {
  cde: {
    dir: 'cde-hello',
    title: 'CDE Hello World',
    run: 'copy hello to the A3000 and start it in a dtterm'
  },
  console: {
    dir: 'console-hello',
    title: 'Console (curses) Hello World',
    run: 'copy hello to the A3000 and start it in any terminal (built with ncurses it needs library/ncurses there)'
  }
};

async function newSampleProject(which) {
  const sample = SAMPLES[which];
  const folders = vscode.workspace.workspaceFolders || [];
  let parent;
  if (folders.length > 0) {
    parent = folders[0].uri.fsPath;
  } else {
    const picked = await vscode.window.showOpenDialog({
      canSelectFiles: false,
      canSelectFolders: true,
      openLabel: `Create ${sample.title} here`
    });
    if (!picked || picked.length === 0) return;
    parent = picked[0].fsPath;
  }

  const name = await vscode.window.showInputBox({
    prompt: `Folder for the ${sample.title} project`,
    value: sample.dir,
    validateInput: (v) => {
      if (!v || /[\\/]/.test(v) || v === '.' || v === '..') return 'A plain folder name';
      if (fs.existsSync(path.join(parent, v))) return `${v} already exists`;
      return null;
    }
  });
  if (!name) return;

  const src = path.join(extensionRoot, 'samples', sample.dir);
  const dest = path.join(parent, name);
  fs.mkdirSync(dest);
  for (const f of ['hello.c', 'Makefile', 'amix-fix-needed', 'README.md']) {
    fs.copyFileSync(path.join(src, f), path.join(dest, f));
  }
  fs.chmodSync(path.join(dest, 'amix-fix-needed'), 0o755);
  // F5 / Ctrl+F5 run it on the AMIX target (the "amix" launch type): its own
  // launch.json when it opens as the workspace, else an entry in the open
  // workspace's launch.json
  if (folders.length === 0) {
    fs.mkdirSync(path.join(dest, '.vscode'));
    writeJson(path.join(dest, '.vscode', 'launch.json'), launchJson('hello'));
  } else {
    addLaunchConfig(path.join(parent, '.vscode', 'launch.json'), {
      ...launchJson('hello').configurations[0],
      name: `Run ${name} on AMIX target`,
      cwd: `\${workspaceFolder}/${name}`
    });
  }

  if (folders.length === 0) {
    await vscode.commands.executeCommand('vscode.openFolder', vscode.Uri.file(dest));
    return;
  }
  const doc = await vscode.workspace.openTextDocument(path.join(dest, 'hello.c'));
  await vscode.window.showTextDocument(doc);
  vscode.window.showInformationMessage(
    `${sample.title} in ${name}: run "AMIX: Run Make in AMIX Project" (${cfg().label}), then ${sample.run}.`
  );
}

// AMIX: Check Development Setup: toolchains, sysroot, libraries, with the
// pkg install line for what is missing (OpenIndiana packages).
function checkSetup() {
  if (!output) output = vscode.window.createOutputChannel('AMIX');
  const lines = [];
  const missingPkgs = [];
  const ok = (b) => (b ? 'ok     ' : 'MISSING');
  const version = (gcc) => {
    try {
      return execFileSync(gcc, ['-dumpversion'], { timeout: 5000, encoding: 'utf8' }).trim();
    } catch (e) {
      return '';
    }
  };

  lines.push(`AMIX development setup (${new Date().toISOString()}), selected: ${cfg().label}`, '');
  const sysroots = new Set();
  for (const id of Object.keys(TOOLCHAINS)) {
    const t = cfg(id);
    const has = fs.existsSync(t.gcc);
    // the gcc 2.7.2.3 wrapper has no -dumpversion (it would try a link)
    const v = has && id === 'gcc14' ? version(t.gcc) : '';
    lines.push(`${ok(has)} ${t.label}: ${t.gcc}${v ? ` (${v})` : ''} [${t.where}]`);
    if (!has) missingPkgs.push(PKG[id]);
    sysroots.add(t.sysroot);
    if (id === 'gcc14') {
      const pcs = ['m68k-amix-pkg-config', 'amix-pkg-config'].map((n) => path.join(t.toolchainPath, n));
      const pc = pcs.find((f) => fs.existsSync(f));
      lines.push(`${pc ? 'ok     ' : 'absent '}   pkg-config wrapper: ${pc || pcs[0]}`);
    }
  }
  lines.push('');
  for (const s of sysroots) {
    const m = sysrootMarker(s);
    const has = fs.existsSync(s);
    lines.push(`${ok(has)} sysroot ${s}`);
    if (m) lines.push(`          marker: ${Object.entries(m).map(([k, v]) => `${k}=${v}`).join(', ')}`);
    else if (has) lines.push('          no etc/amix-sysroot.release: amix-sysroot-check refuses it');
    if (!has) missingPkgs.push(PKG.sysroot);
  }

  const lib = cfg('gcc14').sysroot;
  const libs = [
    ['X11R6.3', 'usr/x11r6/lib', 'libX11.so', 'developer/amix/x11r6.3'],
    ['Motif', 'usr/motif/lib', 'libXm.so', 'developer/amix/motif'],
    ['CDE', 'usr/dt/lib', 'libDtSvc.so', 'developer/amix/cde']
  ];
  lines.push('', `Shared libraries for X/Motif/CDE programs and the CDE sample (in ${lib}):`);
  const desktopMissing = [];
  for (const [name, dir, so, pkg] of libs) {
    let has = false;
    try {
      has = fs.readdirSync(path.join(lib, dir)).some((f) => f.startsWith(so));
    } catch (e) { /* missing directory */ }
    lines.push(`${ok(has)} ${name}: ${path.join(lib, dir)}/${so}* (${pkg})`);
    if (!has) desktopMissing.push(pkg);
  }
  if (desktopMissing.length > 0) missingPkgs.push(PKG.desktop);

  // the console sample: ncurses through pkg-config when the sysroot has it,
  // else AMIX's own SVR4 curses, linked statically
  const ncPc = path.join(lib, 'usr/local/lib/pkgconfig/ncurses.pc');
  const svr4 = path.join(lib, 'usr/ccs/lib/libcurses.a');
  lines.push('', 'curses for console programs and the console sample:');
  if (fs.existsSync(ncPc)) {
    lines.push(`ok      ncurses: ${ncPc} (the program needs library/ncurses on AMIX)`);
  } else if (fs.existsSync(svr4)) {
    lines.push(`ok      AMIX SVR4 curses: ${svr4} (static; no ncurses.pc in the sysroot)`);
  } else {
    lines.push(`MISSING curses: neither ${ncPc} nor ${svr4}`);
    missingPkgs.push(PKG.sysroot);
  }
  lines.push('', 'Run on Target (vscode-agent on AMIX):');
  for (const l of require('./target').statusLines()) lines.push(l);
  const dlm = '/opt/amix-dlmkit';
  lines.push(`${fs.existsSync(dlm) ? 'ok     ' : 'absent '} driver kit (dlmkit, optional): ${dlm}`);

  const uniq = [...new Set(missingPkgs)];
  if (uniq.length > 0) {
    lines.push('', process.platform === 'sunos'
      ? `To install the missing pieces (sudo, or pfexec with a pkg profile):\n  sudo pkg install ${uniq.join(' ')}\n  (or ${PKG.all} for everything)`
      : `Missing pieces (OpenIndiana package names: ${uniq.join(' ')}): install the matching cross builds, or set the amix.* paths.`);
  }
  output.clear();
  output.appendLine(lines.join('\n'));
  output.show(true);
  updateStatus();
  return lines;
}

function fixNeededScript() {
  return path.join(extensionRoot, 'samples', 'cde-hello', 'amix-fix-needed');
}

function findMakeDir(start, root) {
  let dir = start;
  const stop = path.parse(root).root;

  while (dir && dir !== stop) {
    if (fs.existsSync(path.join(dir, 'Makefile')) || fs.existsSync(path.join(dir, 'makefile'))) {
      return dir;
    }
    if (dir === root) {
      break;
    }
    dir = path.dirname(dir);
  }

  return null;
}

function warnHardcodedMakeTools(makeDir, c) {
  const makefile = ['Makefile', 'makefile']
    .map((name) => path.join(makeDir, name))
    .find((candidate) => fs.existsSync(candidate));

  if (!makefile) {
    return;
  }

  let text;
  try {
    text = fs.readFileSync(makefile, 'utf8');
  } catch {
    return;
  }

  if (c.toolchain === 'gcc14' && /-D_KERNEL\b/.test(text)) {
    vscode.window.showWarningMessage(
      'AMIX: this Makefile builds kernel code (-D_KERNEL), which needs gcc 2.7.2.3, not gcc 14. Switch with "AMIX: Select Toolchain".'
    );
  }

  const hardcoded = [];
  for (const line of text.split(/\r?\n/)) {
    if (/^\t\s*(cc|gcc)\b/.test(line)) hardcoded.push('cc');
    if (/^\t\s*ld\b/.test(line)) hardcoded.push('ld');
    if (/^\t\s*ar\b/.test(line)) hardcoded.push('ar');
  }

  const unique = [...new Set(hardcoded)];
  if (unique.length === 0) {
    return;
  }

  vscode.window.showWarningMessage(
    `AMIX Makefile uses hardcoded ${unique.join(', ')} in recipes. Prefer $(CC), $(LD), and $(AR) for cross builds.`
  );
}

// settings.json may hold other settings: keep them when it parses as JSON.
function mergeSettings(file, settings) {
  try {
    return { ...JSON.parse(fs.readFileSync(file, 'utf8')), ...settings };
  } catch {
    return settings;
  }
}

// a launch.json with the "amix" launch type (src/launch.js)
function launchJson(program) {
  return {
    version: '0.2.0',
    configurations: [{
      type: 'amix',
      request: 'launch',
      name: 'Run on AMIX target',
      cwd: '${workspaceFolder}',
      program: program || '',
      args: []
    }]
  };
}

// a configuration into launch.json, which may hold others (kept when it
// parses as JSON; a launch.json with comments is left alone)
function addLaunchConfig(file, config) {
  let data = { version: '0.2.0', configurations: [] };
  if (fs.existsSync(file)) {
    try {
      data = JSON.parse(fs.readFileSync(file, 'utf8'));
    } catch {
      vscode.window.showWarningMessage(`AMIX: ${file} is not plain JSON; add "${config.name}" (type amix) by hand.`);
      return;
    }
    if (!Array.isArray(data.configurations)) data.configurations = [];
    if (data.configurations.some((c) => c.name === config.name)) return;
  }
  data.configurations.push(config);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  writeJson(file, data);
}

function writeJson(file, value) {
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`);
}

function quote(value) {
  if (/^[A-Za-z0-9_./:=@%+-]+$/.test(value)) {
    return value;
  }
  return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

function envPrefix(c) {
  return `AMIX_SYSROOT=${quote(c.sysroot)} AMIX_CRT_DIR=${quote(c.crtDir)}`;
}

function makeEnv(c) {
  const existingPath = process.env.PATH || '';
  const p = c.prefix;
  return {
    PATH: `${c.toolchainPath}${path.delimiter}${existingPath}`,
    AMIX_TOOLCHAIN: c.toolchain,
    AMIX_SYSROOT: c.sysroot,
    AMIX_CRT_DIR: c.crtDir,
    AMIX_MOTIF_SYSROOT: c.motifSysroot,
    AMIX_CROSS_TARGET: c.target,
    AMIX_CROSS_PREFIX: path.dirname(c.toolchainPath),
    CC: `${p}gcc`,
    AS: `${p}as`,
    LD: `${p}ld`,
    AR: `${p}ar`,
    NM: `${p}nm`,
    RANLIB: `${p}ranlib`,
    OBJDUMP: `${p}objdump`,
    STRIP: `${p}strip`
  };
}

module.exports = {
  activate,
  deactivate
};
