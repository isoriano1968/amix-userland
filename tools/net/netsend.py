#!/usr/bin/env python3
"""netsend.py FILE [PORT] -- serve FILE once over TCP (default port 5555); pair: netget on AMIX."""
import socket, sys
path, port = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 5555
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", port))
srv.listen(1)
print("netsend: %s on port %d" % (path, port), flush=True)
c, a = srv.accept()
with open(path, "rb") as f:
    while True:
        b = f.read(65536)
        if not b:
            break
        c.sendall(b)
c.close()
print("netsend: sent to %s" % a[0], flush=True)
