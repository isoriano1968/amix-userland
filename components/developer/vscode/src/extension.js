const vscode = require('vscode');
const fs = require('fs');
const path = require('path');

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

let extensionRoot = '';
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
    vscode.commands.registerCommand('amix.buildCurrentExecutable', () => buildCurrent('exe')),
    vscode.commands.registerCommand('amix.compileCurrentObject', () => buildCurrent('object')),
    vscode.commands.registerCommand('amix.compileDriverObject', () => buildCurrent('driver')),
    vscode.commands.registerCommand('amix.make', () => runMake(false)),
    vscode.commands.registerCommand('amix.makeClean', () => runMake(true)),
    vscode.commands.registerCommand('amix.makeGcc272', () => runMake(false, 'gcc272')),
    vscode.commands.registerCommand('amix.makeGcc14', () => runMake(false, 'gcc14')),
    vscode.commands.registerCommand('amix.makeCleanGcc272', () => runMake(true, 'gcc272')),
    vscode.commands.registerCommand('amix.makeCleanGcc14', () => runMake(true, 'gcc14')),
    vscode.commands.registerCommand('amix.newCdeHelloProject', newCdeHelloProject),
    // for tasks.json: ${command:amix.fixNeededScript}
    vscode.commands.registerCommand('amix.fixNeededScript', fixNeededScript)
  );
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
  const gcc14Sysroot = expand(c.get('gcc14.sysroot'));
  let toolchainPath, prefix, sysroot, crtDir, cpu, extraCFlags, includeDirs;

  if (toolchain === 'gcc14') {
    toolchainPath = expand(c.get('gcc14.toolchainPath'));
    prefix = c.get('gcc14.toolPrefix') || 'm68k-elf-';
    sysroot = gcc14Sysroot;
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
    toolchainPath = expand(c.get('toolchainPath'));
    prefix = `${target}-`;
    sysroot = expand(c.get('sysroot'));
    crtDir = expand(c.get('crtDir')).replace('${config:amix.sysroot}', sysroot);
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
    makeCommand: c.get('makeCommand') || 'make',
    makeArgs: c.get('makeArgs') || []
  };
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
  statusItem.text = `$(tools) AMIX ${c.label}`;
  statusItem.tooltip = `AMIX toolchain: ${c.label} (${c.gcc})\nClick to switch between gcc 2.7.2.3 and gcc 14.`;
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
    'amix.toolchainPath': k.toolchainPath,
    'amix.target': k.target,
    'amix.sysroot': k.sysroot,
    'amix.crtDir': k.crtDir,
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
async function newCdeHelloProject() {
  const folders = vscode.workspace.workspaceFolders || [];
  let parent;
  if (folders.length > 0) {
    parent = folders[0].uri.fsPath;
  } else {
    const picked = await vscode.window.showOpenDialog({
      canSelectFiles: false,
      canSelectFolders: true,
      openLabel: 'Create CDE Hello World here'
    });
    if (!picked || picked.length === 0) return;
    parent = picked[0].fsPath;
  }

  const name = await vscode.window.showInputBox({
    prompt: 'Folder for the CDE Hello World project',
    value: 'cde-hello',
    validateInput: (v) => {
      if (!v || /[\\/]/.test(v) || v === '.' || v === '..') return 'A plain folder name';
      if (fs.existsSync(path.join(parent, v))) return `${v} already exists`;
      return null;
    }
  });
  if (!name) return;

  const src = path.join(extensionRoot, 'samples', 'cde-hello');
  const dest = path.join(parent, name);
  fs.mkdirSync(dest);
  for (const f of ['hello.c', 'Makefile', 'amix-fix-needed', 'README.md']) {
    fs.copyFileSync(path.join(src, f), path.join(dest, f));
  }
  fs.chmodSync(path.join(dest, 'amix-fix-needed'), 0o755);

  if (folders.length === 0) {
    await vscode.commands.executeCommand('vscode.openFolder', vscode.Uri.file(dest));
    return;
  }
  const doc = await vscode.workspace.openTextDocument(path.join(dest, 'hello.c'));
  await vscode.window.showTextDocument(doc);
  vscode.window.showInformationMessage(
    `CDE Hello World in ${name}: run "AMIX: Run Make in AMIX Project" (${cfg().label}), then copy hello to the A3000.`
  );
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
