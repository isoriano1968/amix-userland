"""tftpd.py DIR: TFTP from one socket (UDP 1069, slirp needs replies from the
listening port); get serves DIR's files, put writes DIR/put/<basename>."""
import os, socket, struct, sys
ROOT = sys.argv[1]; os.makedirs(os.path.join(ROOT, "put"), exist_ok=True)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("0.0.0.0", 1069))
rd, wr = {}, {}
while True:
    pkt, a = s.recvfrom(2048)
    op = struct.unpack("!H", pkt[:2])[0]
    if op in (1, 2):
        name = os.path.basename(pkt[2:].split(b"\0")[0].decode("latin1"))
        if op == 1:
            p = os.path.join(ROOT, name)
            if not os.path.isfile(p):
                s.sendto(struct.pack("!HH", 5, 1) + b"not found\0", a); continue
            rd[a] = open(p, "rb").read()
            s.sendto(struct.pack("!HH", 3, 1) + rd[a][:512], a)
        else:
            wr[a] = [open(os.path.join(ROOT, "put", name), "wb"), 0]
            s.sendto(struct.pack("!HH", 4, 0), a)
        print(("get " if op == 1 else "put ") + name, flush=True)
    elif op == 4 and a in rd:
        blk = struct.unpack("!H", pkt[2:4])[0]
        d = rd[a]
        if blk * 512 > len(d):
            del rd[a]; continue
        s.sendto(struct.pack("!HH", 3, (blk + 1) & 0xffff) + d[blk * 512:(blk + 1) * 512], a)
    elif op == 3 and a in wr:
        blk = struct.unpack("!H", pkt[2:4])[0]
        f = wr[a]
        if blk == (f[1] + 1) & 0xffff:
            f[0].write(pkt[4:]); f[1] = blk
        s.sendto(struct.pack("!HH", 4, blk), a)
        if len(pkt) - 4 < 512:
            f[0].close(); del wr[a]
