"""Stage GNU Pth for the simple /usr/local/gnu install.sh."""
from pathlib import Path
import hashlib
import os
import shutil
import tarfile

project = Path.cwd()
prefix = Path(os.environ.get("PTH_PREFIX", str(Path.home() / "opt/pth-amix")))
upstream = Path(os.environ.get("PTH_SOURCE", str(Path.home() / "src/upstream/pth-2.0.7")))
stage = project / "build/gnu-prefix-install"
destination_prefix = "/usr/local/gnu"
payload = stage / ("payload" + destination_prefix)
paths = []

def install(source, relative, executable=False, rewrite=False):
    target = payload / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    if rewrite:
        content = Path(source).read_text().replace(str(prefix), destination_prefix)
        if str(Path.home()) in content:
            raise SystemExit("Build-host path remains in config helper")
        target.write_text(content)
    else:
        shutil.copyfile(source, target)
    target.chmod(0o755 if executable else 0o644)
    paths.append((target, destination_prefix + "/" + relative))

for name in ("pth.h", "pthread.h"):
    install(prefix / "include" / name, "include/" + name)
for name in ("libpth.a", "libpthread.a"):
    install(prefix / "lib" / name, "lib/" + name)
for name in ("pth-config", "pthread-config"):
    install(prefix / "bin" / name, "bin/" + name, True, True)
for name in ("pth-config.1", "pthread-config.1"):
    install(upstream / name, "man/man1/" + name)
for name in ("pth.3", "pthread.3"):
    install(upstream / name, "man/man3/" + name)
install(upstream / "COPYING", "share/doc/gnu-pth/COPYING")
install(project / "docs/GNU-PTH.md", "share/doc/gnu-pth/PORT-NOTES.md")

receipt = stage / "BUILD-RECEIPT.txt"
receipt.write_text("""GNU Pth 2.0.7 AMIX port, release 1
Compiler: GCC 14.2.0 with AMIX ABI/specs
CFLAGS: -O2 -m68060 -std=gnu89 -fno-strict-aliasing
Backend: mcsc/sc/mc; GNU pthread emulation; soft syscall mappings
Upstream archive SHA256:
72353660c5a2caafd601b20e12e75d865fd88f6cf1a088b306a3963f0bc77232
Port fixes: global errno, public timespec, yielding pthread_once
Physical acceptance: upstream std/pthread, application proof, wrapped pipe I/O,
timed condition wait, initializer exit/nesting, pigz -p 2 with exact comparison
Acceptance uname identity: 68060-261027-ia
Install prefix: /usr/local/gnu; static libraries only
Installation: install.sh, with a file manifest; no native package manager
Publication: build recipe and patches only
""")
install(receipt, "share/doc/gnu-pth/BUILD-RECEIPT.txt")
manifest = stage / "INSTALL-FILES"
manifest.write_text("\n".join(p for _, p in paths) +
                    "\n/usr/local/gnu/share/doc/gnu-pth/INSTALL-FILES" +
                    "\n/usr/local/gnu/share/doc/gnu-pth/SHA256SUMS\n")
install(manifest, "share/doc/gnu-pth/INSTALL-FILES")
checksums = stage / "SHA256SUMS"
checksums.write_text("".join(hashlib.sha256(p.read_bytes()).hexdigest() + "  " + dest + "\n"
                              for p, dest in paths))
install(checksums, "share/doc/gnu-pth/SHA256SUMS")
shutil.copyfile(project / "install.sh", stage / "install.sh")
dist = project / "dist"
dist.mkdir(exist_ok=True)
archive = dist / "gnu-pth-amix-install.tar.gz"
with tarfile.open(archive, "w:gz") as output:
    output.add(stage / "payload", arcname="payload")
    output.add(stage / "install.sh", arcname="install.sh")
print("Staged %d files for %s" % (len(paths), destination_prefix))
print(hashlib.sha256(archive.read_bytes()).hexdigest(), archive)
