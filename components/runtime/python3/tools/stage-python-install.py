"""Build a private /usr/local payload; publish this recipe, not the binaries."""
from pathlib import Path
import hashlib
import os
import pprint
import re
import runpy
import shutil
import subprocess
import tarfile

project = Path.cwd()
source = Path(os.environ.get('PYTHON_SOURCE', str(Path.home() / 'src/upstream/Python-3.11.18')))
build = Path(os.environ.get('PYTHON_BUILD', str(Path.home() / 'build/python311-amix')))
compiler = Path(os.environ.get('CC', str(Path.home() / 'opt/m68k-amix/bin/m68k-elf-gcc')))
pth = Path(os.environ.get('PTH_PREFIX', str(Path.home() / 'opt/pth-amix')))
zlib = Path(os.environ.get('ZLIB_SOURCE', str(Path.home() / 'src/upstream/zlib-1.3.1')))
stage = Path(os.environ.get('PYTHON_STAGE', str(project / 'build/python-install')))
if stage.exists():
    raise SystemExit('Use a fresh build/python-install staging directory')
payload = stage / 'payload'
configuration = '/usr/local/lib/python3.11/config-3.11'
documentation = '/usr/local/share/doc/python3-amix'
entries = []

def put(src, destination, executable=False):
    path = payload / destination.lstrip('/')
    path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, path)
    path.chmod(0o755 if executable else 0o644)
    entries.append(destination)
    return path

def generated(text, destination):
    temporary = stage / 'generated' / Path(destination).name
    temporary.parent.mkdir(parents=True, exist_ok=True)
    temporary.write_text(text)
    return put(temporary, destination)

strip = str(compiler.parent / 'm68k-elf-strip')
ar = str(compiler.parent / 'm68k-elf-ar')
binary = put(build / 'python', '/usr/local/bin/python3.11', True)
subprocess.run([strip, str(binary)], check=True)
subprocess.run(['python3', str(Path.home() / 'src/gcc14-cross-amix/tools/check-68060-safe.py'), str(binary)], check=True)
library = put(build / 'libpython3.11.a', '/usr/local/lib/libpython3.11.a')
subprocess.run([strip, '--strip-debug', str(library)], check=True)
compat = stage / 'libamixpythoncompat.a'
subprocess.run([ar, 'rcs', str(compat), *(str(project / 'build/python-compat' / ('amix-' + name + '.o'))
                                      for name in ('math', 'wide', 'clock', 'time', 'environ'))], check=True)
put(compat, configuration + '/libamixpythoncompat.a')
put(zlib / 'libz.a', configuration + '/libz.a')
put(project / 'tools/python3.11-config.sh', '/usr/local/bin/python3.11-config', True)
excluded = {'test', 'tests', 'idlelib', 'tkinter', 'turtledemo', 'ensurepip', 'venv', '__pycache__'}
for path in sorted((source / 'Lib').rglob('*')):
    relative = path.relative_to(source / 'Lib')
    if path.is_file() and not any(part in excluded for part in relative.parts):
        if path.suffix not in {'.pyc', '.pyo', '.exe', '.dll', '.so', '.a', '.o'}:
            put(path, '/usr/local/lib/python3.11/' + relative.as_posix())
for path in sorted((source / 'Include').rglob('*.h')):
    put(path, '/usr/local/include/python3.11/' + path.relative_to(source / 'Include').as_posix())
put(build / 'pyconfig.h', '/usr/local/include/python3.11/pyconfig.h')
put(project / 'ports/python/amix-python.h', '/usr/local/include/python3.11/amix-python.h')
for path in sorted((project / 'ports/python/include').glob('*.h')):
    put(path, '/usr/local/include/python3.11/amixcompat/' + path.name)
put(source / 'Misc/python.man', '/usr/local/man/man1/python3.11.1')

# Generate target sysconfig data using CPython's cross-build machinery.
subprocess.run(['make', 'pybuilddir.txt'], cwd=build, check=True)
data_files = list(build.glob('build/lib.*/_sysconfigdata_*.py'))
if len(data_files) != 1:
    raise SystemExit('Expected one target sysconfig data file')
variables = runpy.run_path(str(data_files[0]))['build_time_vars']
if variables['prefix'] != '/usr/local':
    raise SystemExit('Rebuild CPython with --prefix=/usr/local')
compat_flags = configuration + '/libamixpythoncompat.a'
variables.update(CC='m68k-elf-gcc', CXX='m68k-elf-g++', AR='m68k-elf-ar', RANLIB='m68k-elf-ranlib',
                 CPPFLAGS='-I/usr/local/include/python3.11/amixcompat -I/usr/local/gnu/include '
                          '-std=gnu11 -include /usr/local/include/python3.11/amix-python.h',
                 LIBS=compat_flags + ' -lpthread -ldl -lsocket -lnsl',
                 LDFLAGS='-L/usr/local/lib -L' + configuration + ' -L/usr/local/gnu/lib')
for key, value in list(variables.items()):
    if isinstance(value, str):
        variables[key] = value.replace(str(pth), '/usr/local/gnu').replace(str(zlib), configuration)
generated('build_time_vars = ' + pprint.pformat(variables, sort_dicts=True) + '\n',
          '/usr/local/lib/python3.11/' + data_files[0].name)
makefile = (build / 'Makefile').read_text()
for key in ('CC', 'CXX', 'AR', 'RANLIB', 'CPPFLAGS', 'LIBS', 'LDFLAGS'):
    makefile = re.sub(r'^' + key + r'\s*=.*$', key + '= ' + variables[key], makefile, flags=re.M)
generated((build / 'Modules/Setup.local').read_text().replace(str(zlib), configuration),
          configuration + '/Setup.local')
generated(makefile, configuration + '/Makefile')
put(source / 'LICENSE', documentation + '/LICENSE.PSF')
put(project / 'README.md', documentation + '/README.md')
put(project / 'docs/PORT-NOTES.md', documentation + '/PORT-NOTES.md')
for name in ('python3-smoke.py', 'python3-stdlib.py'):
    put(project / 'tests' / name, documentation + '/' + name)
put(project / 'probes/python-embed.c', documentation + '/python-embed.c')
generated('Python 3.11.18 AMIX; GCC 14.2; -O2 -m68060; GNU Pth pthread emulation\n'
          'Install prefix: /usr/local; GNU Pth dependency: /usr/local/gnu\n'
          'Executable SHA256: ' + hashlib.sha256(binary.read_bytes()).hexdigest() + '\n',
          documentation + '/BUILD-RECEIPT.txt')
generated('/usr/local/bin/python3 python3.11\n/usr/local/bin/python3-config python3.11-config hard\n'
          '/usr/local/man/man1/python3.1 python3.11.1\n' + configuration + '/libpython3.11.a ../../libpython3.11.a\n',
          documentation + '/INSTALL-LINKS')
generated('\n'.join(entries + [documentation + '/INSTALL-FILES', documentation + '/SHA256SUMS']) + '\n',
          documentation + '/INSTALL-FILES')
generated(''.join(hashlib.sha256((payload / name.lstrip('/')).read_bytes()).hexdigest() + '  ' + name + '\n'
                  for name in entries), documentation + '/SHA256SUMS')
shutil.copyfile(project / 'install.sh', stage / 'install.sh')
destination = project / 'dist/python3-amix-install.tar.gz'
destination.parent.mkdir(exist_ok=True)
with tarfile.open(destination, 'w:gz') as archive:
    archive.add(payload, arcname='payload')
    archive.add(stage / 'install.sh', arcname='install.sh')
print('Staged', len(entries), 'files and 4 links for /usr/local')
print(hashlib.sha256(destination.read_bytes()).hexdigest(), destination, destination.stat().st_size)
