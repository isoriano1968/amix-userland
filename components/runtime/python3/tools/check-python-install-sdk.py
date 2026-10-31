"""Cross-build an embedding probe with the staged public config helper."""
from pathlib import Path
import os
import shlex
import subprocess

project = Path.cwd()
stage = Path(os.environ.get('PYTHON_STAGE', str(project / 'build/python-install')))
sdk = stage / 'payload/usr/local'
pth = os.environ.get('PTH_PREFIX', str(Path.home() / 'opt/pth-amix'))
cc = os.environ.get('CC', str(Path.home() / 'opt/m68k-amix/bin/m68k-elf-gcc'))
helper = sdk / 'bin/python3.11-config'

def flags(*options):
    text = subprocess.check_output(['sh', str(helper), *options], text=True)
    return shlex.split(text.replace('/usr/local/gnu', pth).replace('/usr/local', str(sdk)))

output = project / 'build/python-embed'
subprocess.run([cc, *flags('--cflags'), str(project / 'probes/python-embed.c'),
                *flags('--embed', '--ldflags'), '-o', str(output)], check=True)
subprocess.run(['python3', str(Path.home() / 'src/gcc14-cross-amix/tools/check-68060-safe.py'),
                str(output)], check=True)
print('STAGED_PYTHON_SDK_CROSS_LINK_PASS', output)
