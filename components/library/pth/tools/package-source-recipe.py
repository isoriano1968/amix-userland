"""Package only the publication recipe, patches and standalone test sources."""
from pathlib import Path
import hashlib
import io
import tarfile

files = {
    "README.md": "docs/BUILD-RECIPE.md",
    "docs/GNU-PTH.md": "docs/GNU-PTH.md",
    "docs/BUILD-RECIPE.md": "docs/BUILD-RECIPE.md",
    "poc/proof.c": "poc/proof.c",
    "poc/LICENSE": "poc/LICENSE",
    "install.sh": "install.sh",
}
for pattern in ("patches/*.patch", "patches/README.md", "tests/gnu-*.c",
                "ports/pigz/*.c", "ports/pigz/*.h", "ports/pigz/include/*.h"):
    for path in Path.cwd().glob(pattern):
        relative = path.relative_to(Path.cwd()).as_posix()
        files[relative] = relative
for name in ("build-gnu-pth.sh", "build-proof.sh", "build-pigz-gnu.sh",
             "patch-gnu-pth.py", "stage-amix-install.py",
             "package-gnu-pth.sh"):
    files["tools/" + name] = "tools/" + name
for name in ("context-roundtrip.c", "context-stack.c", "context-state.c", "bounded-run.c"):
    files["probes/" + name] = "probes/" + name
destination = Path("dist/gnu-pth-amix-source-recipe.tar.gz")
destination.parent.mkdir(exist_ok=True)
root = "gnu-pth-amix-recipe/"
manifest = []
with tarfile.open(destination, "w:gz") as archive:
    for packaged, source in sorted(files.items()):
        data = Path(source).read_bytes()
        # Fail closed if a binary or private material enters the explicit list.
        if b"\0" in data or data.startswith(b"\x7fELF"):
            raise SystemExit("Non-text publication entry: " + source)
        info = tarfile.TarInfo(root + packaged)
        info.size = len(data)
        info.mode = 0o644
        archive.addfile(info, io.BytesIO(data))
        manifest.append(hashlib.sha256(data).hexdigest() + "  " + packaged + "\n")
    data = "".join(manifest).encode()
    info = tarfile.TarInfo(root + "SHA256SUMS")
    info.size = len(data)
    info.mode = 0o644
    archive.addfile(info, io.BytesIO(data))
print("Publication: %d text/source files; no compiler, libraries, GNU source archive or AMIX sysroot" % len(files))
print(hashlib.sha256(destination.read_bytes()).hexdigest(), destination)
