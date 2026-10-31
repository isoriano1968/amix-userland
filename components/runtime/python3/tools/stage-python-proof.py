"""Stage pure Python standard-library files for the private hardware proof."""
from pathlib import Path
import hashlib
import os
import tarfile
import io

source = Path(os.environ.get("PYTHON_SOURCE", str(Path.home() / "src/upstream/Python-3.11.18")))
build = Path(os.environ.get("PYTHON_BUILD", str(Path.home() / "build/python311-amix")))
excluded = {"test", "tests", "idlelib", "tkinter", "turtledemo", "ensurepip", "venv", "__pycache__"}
destination = Path("dist/python311-amix-stdlib.tar.gz")
destination.parent.mkdir(exist_ok=True)
entries = {}
for path in (source / "Lib").rglob("*.py"):
    relative = path.relative_to(source / "Lib")
    if not any(part in excluded for part in relative.parts):
        entries["py311/lib/python3.11/" + relative.as_posix()] = path
for path in build.glob("_sysconfigdata_*.py"):
    entries["py311/lib/python3.11/" + path.name] = path
entries["py311/LICENSE.PSF"] = source / "LICENSE"
entries["py311/smoke.py"] = Path("tests/python3-smoke.py")
with tarfile.open(destination, "w:gz") as archive:
    for name, path in sorted(entries.items()):
        data = path.read_bytes()
        info = tarfile.TarInfo(name)
        info.mode = 0o644
        info.size = len(data)
        archive.addfile(info, io.BytesIO(data))
print("Staged pure Python standard library:", len(entries), "files")
print(hashlib.sha256(destination.read_bytes()).hexdigest(), destination, destination.stat().st_size)
