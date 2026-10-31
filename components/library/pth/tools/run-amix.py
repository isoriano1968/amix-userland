"""Run the pinned upstream AMIX Telnet helper with credentials from environment.

This adapter does not write a password file. Set AMIX_HOST, AMIX_USER,
AMIX_PASS in the invoking process; arguments are commands for the target shell.
The upstream helper must exist in local/gcc14-cross-amix/tools/hw.py.
"""
from pathlib import Path
import os

upstream = Path(__file__).resolve().parent.parent / "local/gcc14-cross-amix/tools/hw.py"
source = upstream.read_text()
marker = "_ENV = _load_env()"
if source.count(marker) != 1:
    raise SystemExit("Unexpected upstream helper layout")
source = source.replace(marker, "_ENV = dict(__import__('os').environ)")
command_marker = "        cmd = stage_if_long(s, cmd, i)"
if source.count(command_marker) != 1:
    raise SystemExit("Unexpected upstream command-loop layout")
source = source.replace(command_marker,
    '        print("RUN > " + cmd, flush=True)\n' + command_marker)
print("Telnet login: %s@%s" % (os.environ.get("AMIX_USER", "root"),
                              os.environ.get("AMIX_HOST", "")), flush=True)
exec(compile(source, str(upstream), "exec"),
     {"__name__": "__main__", "__file__": str(upstream)})
