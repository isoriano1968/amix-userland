const vscode = require('vscode');
const fs = require('fs');
const path = require('path');

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand('amix.configureWorkspace', configureWorkspace),
    vscode.commands.registerCommand('amix.buildCurrentExecutable', () => buildCurrent('exe')),
    vscode.commands.registerCommand('amix.compileCurrentObject', () => buildCurrent('object')),
    vscode.commands.registerCommand('amix.compileDriverObject', () => buildCurrent('driver'))
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

function cfg() {
  const c = vscode.workspace.getConfiguration('amix');
  const target = c.get('target');
  const toolchainPath = expand(c.get('toolchainPath'));
  const sysroot = expand(c.get('sysroot'));
  const crtDir = expand(c.get('crtDir')).replace('${config:amix.sysroot}', sysroot);
  const cpu = c.get('cpu');
  return {
    target,
    toolchainPath,
    sysroot,
    crtDir,
    cpu,
    gcc: path.join(toolchainPath, `${target}-gcc`),
    nm: path.join(toolchainPath, `${target}-nm`),
    extraCFlags: c.get('extraCFlags') || [],
    kernelDefines: c.get('kernelDefines') || []
  };
}

function expand(value) {
  if (!value) return '';
  const home = process.env.HOME || process.env.USERPROFILE || '';
  return value.replace(/\$\{env:HOME\}/g, home).replace(/^~(?=$|[\\/])/, home);
}

async function configureWorkspace() {
  const root = workspaceRoot();
  const settingsDir = path.join(root, '.vscode');
  fs.mkdirSync(settingsDir, { recursive: true });

  const c = cfg();
  const settings = {
    'amix.toolchainPath': c.toolchainPath,
    'amix.target': c.target,
    'amix.sysroot': c.sysroot,
    'amix.crtDir': c.crtDir,
    'C_Cpp.default.compilerPath': c.gcc,
    'C_Cpp.default.intelliSenseMode': 'gcc-x64',
    'C_Cpp.default.includePath': [
      '${workspaceFolder}/**',
      path.join(c.sysroot, 'usr/include'),
      path.join(c.sysroot, 'usr/sys'),
      path.join(c.sysroot, 'usr/sys/amiga/inc')
    ],
    'C_Cpp.default.defines': [
      'SVR4',
      'SVR40'
    ]
  };

  const tasks = {
    version: '2.0.0',
    tasks: [
      {
        label: 'AMIX: build current executable',
        type: 'shell',
        command: 'bash',
        options: {
          env: {
            AMIX_SYSROOT: c.sysroot,
            AMIX_CRT_DIR: c.crtDir
          }
        },
        args: [
          '-lc',
          `case "\${file}" in *.c) exec ${quote(c.gcc)} -m${c.cpu} "\${file}" -o "\${fileDirname}/\${fileBasenameNoExtension}" ;; *) echo "AMIX: active file must be a .c source file, not \${fileBasename}" >&2; exit 2 ;; esac`
        ],
        group: {
          kind: 'build',
          isDefault: true
        },
        problemMatcher: '$amix-gcc'
      },
      {
        label: 'AMIX: compile current object',
        type: 'shell',
        command: 'bash',
        options: {
          env: {
            AMIX_SYSROOT: c.sysroot,
            AMIX_CRT_DIR: c.crtDir
          }
        },
        args: [
          '-lc',
          `case "\${file}" in *.c) exec ${quote(c.gcc)} -c -m${c.cpu} ${c.extraCFlags.map(quote).join(' ')} "\${file}" -o "\${fileDirname}/\${fileBasenameNoExtension}.o" ;; *) echo "AMIX: active file must be a .c source file, not \${fileBasename}" >&2; exit 2 ;; esac`
        ],
        group: 'build',
        problemMatcher: '$amix-gcc'
      },
      {
        label: 'AMIX: compile current driver object',
        type: 'shell',
        command: 'bash',
        options: {
          env: {
            AMIX_SYSROOT: c.sysroot,
            AMIX_CRT_DIR: c.crtDir
          }
        },
        args: [
          '-lc',
          `case "\${file}" in *.c) exec ${quote(c.gcc)} -c -m${c.cpu} ${[
            ...c.extraCFlags,
            ...c.kernelDefines,
            `-I${path.join(c.sysroot, 'usr/include')}`,
            `-I${path.join(c.sysroot, 'usr/sys')}`,
            `-I${path.join(c.sysroot, 'usr/sys/amiga/inc')}`
          ].map(quote).join(' ')} "\${file}" -o "\${fileDirname}/\${fileBasenameNoExtension}.o" ;; *) echo "AMIX: active file must be a .c source file, not \${fileBasename}" >&2; exit 2 ;; esac`
        ],
        group: 'build',
        problemMatcher: '$amix-gcc'
      },
      {
        label: 'AMIX: nm current output',
        type: 'shell',
        command: quote(c.nm),
        args: [
          '-a',
          '${fileDirname}/${fileBasenameNoExtension}'
        ],
        problemMatcher: []
      }
    ]
  };

  writeJson(path.join(settingsDir, 'settings.json'), settings);
  writeJson(path.join(settingsDir, 'tasks.json'), tasks);

  vscode.window.showInformationMessage('AMIX workspace settings and tasks written.');
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
  const c = cfg();
  const cpu = `-m${c.cpu}`;
  let command;

  if (kind === 'exe') {
    command = [
      envPrefix(c),
      quote(c.gcc),
      cpu,
      quote(file),
      '-o',
      quote(path.join(parsed.dir, parsed.name))
    ].join(' ');
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
      ...c.extraCFlags.map(quote),
      ...c.kernelDefines.map(quote),
      quote(`-I${path.join(c.sysroot, 'usr/include')}`),
      quote(`-I${path.join(c.sysroot, 'usr/sys')}`),
      quote(`-I${path.join(c.sysroot, 'usr/sys/amiga/inc')}`),
      quote(file),
      '-o',
      quote(path.join(parsed.dir, `${parsed.name}.o`))
    ].join(' ');
  }

  const terminal = vscode.window.createTerminal('AMIX Build');
  terminal.show();
  terminal.sendText(command);
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

module.exports = {
  activate,
  deactivate
};
