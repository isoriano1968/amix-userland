#!/usr/bin/env python3
"""netrecv.py FILE [PORT] -- accept one TCP connection (default port 5556) and save what it sends to FILE; pair: netput on AMIX."""
import socket, sys
path, port = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 5556
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", port))
srv.listen(1)
print("netrecv: %s on port %d" % (path, port), flush=True)
c, a = srv.accept()
n = 0
with open(path, "wb") as f:
    while True:
        b = c.recv(65536)
        if not b:
            break
        f.write(b); n += len(b)
c.close()
print("netrecv: %d bytes from %s" % (n, a[0]), flush=True)
