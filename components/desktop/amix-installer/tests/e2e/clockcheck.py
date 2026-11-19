#!/usr/bin/env python3
"""clockcheck.py LOG [MAX]: the AMIX clock against this computer's, from the
installer's log: the "foundation clock installed <AMIX date>" line carries
the time this computer received it (HH:MM:SS at its start) and AMIX's own
date(1) output; both in local time (FS-UAE AMIX in this computer's zone).
Passes when they are at most MAX seconds apart (default 5)."""
import re
import sys

log, most = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 5
line = None
for l in open(log, errors="replace"):
    if "foundation clock installed" in l:
        line = l
if not line:
    sys.exit("clock: no 'foundation clock installed' line in %s" % log)
host = re.match(r"(\d\d):(\d\d):(\d\d) ", line)
amix = re.search(r"clock installed \w+ \w+ +\d+ (\d\d):(\d\d):(\d\d) ", line)
if not host or not amix:
    sys.exit("clock: cannot read the times in: %s" % line.strip())
sec = lambda m: int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
d = (sec(host) - sec(amix)) % 86400
d = min(d, 86400 - d)
print("clock: AMIX %s, this computer %s: %d s apart" % (
    ":".join(amix.groups()), ":".join(host.groups()), d))
sys.exit(0 if d <= most else 1)
