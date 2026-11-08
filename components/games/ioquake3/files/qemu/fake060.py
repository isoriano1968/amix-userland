#!/usr/bin/env python3
"""fake060.py -- the 68k's side of c1q3 against the REAL core 1 image
(build/core1/q3.elf) running in qemu-system-arm -M xilinx-zynq-a9, whose DDR
is a shared file (run.sh): the same paths, commands, input events and
write-back as c1q3, at the board's offsets, big-endian as the 060 writes.

  fake060.py DDR_FILE [options] [-- engine command line words]
    -base DIR        game data: DIR/<game>/*.pk3 etc. become /base/<game>/...
    -home DIR        the user's files: DIR/... become /home/..., written ones come back here
    -at SECS CMD     a console command at SECS after START (several allowed)
    -key SECS NAME   a key press (K_ number or a character) at SECS
    -shot SECS FILE  the overlay as a PPM at SECS
    -secs S          give up after S seconds (default 300)
    -w W -h H        the picture's size (default 640x480)
    -remote-over MB  files bigger than this are served by request (blocks), as c1q3 does

Prints core 1's log as it comes, and every 5 s the state, frames, frame
time, heap left and core 1's timer sample (pc lr sp + stack code addresses,
for arm-none-eabi-addr2line -e build/core1/q3.elf).
"""
import mmap
import os
import struct
import sys
import time

RTG = 0x18000000                    # board offset 0 as core 1 sees it
PG = RTG + 0x07AFD000               # C1Q3_OFF
CTL = RTG + 0x07AFF000              # HB_CTL_OFF
WIN, CHUNK_MAX = RTG + 0x07100000, 0x00400000
REQ, REQ_MAX = WIN + 0x00400000, 0x001F0000
LOG, LOG_LEN = WIN + 0x005F0000, 0x10000
OVL0 = RTG + 0x06000000
HDMI, HDMI_SIZE = RTG + 0x07CE0000, 30720
IN_OFF, IN_N, TEXT, TEXT_LEN = 0x400, 256, 0x800, 0x800
MAGIC = 0x43315133
(W_MAGIC, W_VERSION, W_STATE, W_CMD, W_ARG, W_RESULT, W_FRAMES, W_FRAME_US, W_AVG, W_UNDERRUNS,
 W_CPU_HZ, W_TICK_HZ, W_ERR, W_OWNER, W_IN_HEAD, W_IN_TAIL, W_LOG_HEAD, W_FILE, W_OFF, W_LEN,
 W_SIZE, W_FLAGS, W_DIRTY, W_OVL_PITCH, W_OVL_ROW0, W_OVL_MAXW, W_OVL_MAXH, W_VOLUME, W_HEAP_KB,
 W_WIDTH, W_HEIGHT, W_MOUSE, W_FRAME_MS_MAX) = range(33)
W_SAMPLE, W_SAMPLE_END = 40, 60
CMD_OPEN, CMD_CHUNK, CMD_START, CMD_EXEC, CMD_FLUSH, CMD_STOP = 1, 2, 3, 4, 5, 6
W_REQ_SEQ, W_REQ_DONE, W_REQ_FILE, W_REQ_OFF, W_REQ_LEN, W_REQ_RESULT = 35, 36, 60, 61, 62, 63
FILE_REMOTE = 2
REMOVED = 0xFFFFFFFF
STATES = ['IDLE', 'LOADING', 'RUNNING', 'QUIT', 'FAILED']
EV_KEY, EV_CHAR = 1, 2
HB_W_FAULT, HB_W_FAULT_ADDR, HB_W_FAULT_PC, HB_W_STAGE = 20, 21, 22, 24
KEYS = {'enter': 13, 'escape': 27, 'tab': 9, 'space': 32, 'up': 132, 'down': 133, 'left': 134,
        'right': 135, 'console': 96}


def main():
    a = sys.argv[1:]
    ddr = a.pop(0)
    base = home = None
    at, keys, shots, secs, w, h = [], [], [], 300.0, 640, 480
    remote_over = None
    words = []
    while a:
        o = a.pop(0)
        if o == '-base': base = a.pop(0)
        elif o == '-home': home = a.pop(0)
        elif o == '-at': at.append([float(a.pop(0)), a.pop(0)])
        elif o == '-key': keys.append([float(a.pop(0)), a.pop(0)])
        elif o == '-shot': shots.append([float(a.pop(0)), a.pop(0)])
        elif o == '-secs': secs = float(a.pop(0))
        elif o == '-w': w = int(a.pop(0))
        elif o == '-h': h = int(a.pop(0))
        elif o == '-remote-over': remote_over = int(a.pop(0)) << 20
        elif o == '--': words, a = a, []
        else: sys.exit('fake060: ' + o + '?')
    f = open(ddr, 'r+b')
    m = mmap.mmap(f.fileno(), 0)

    def get(wd, b=PG): return struct.unpack_from('>L', m, b + 4 * wd)[0]
    def put(wd, v, b=PG): struct.pack_into('>L', m, b + 4 * wd, v & 0xFFFFFFFF)

    def text(s):
        bs = s.encode('latin-1')[:TEXT_LEN - 1] + b'\0'
        m[PG + TEXT:PG + TEXT + len(bs)] = bs

    def text_get():
        bs = bytes(m[PG + TEXT:PG + TEXT + TEXT_LEN])
        return bs[:bs.index(b'\0')].decode('latin-1')

    remote = {}                         # core 1's file number -> open file
    req = {'seen': 0, 'n': 0, 'bytes': 0}

    def serve():
        seq = get(W_REQ_SEQ)
        if seq == req['seen']:
            return False
        req['seen'] = seq
        fi, off, ln = get(W_REQ_FILE), get(W_REQ_OFF), get(W_REQ_LEN)
        if fi in remote and ln <= REQ_MAX:
            remote[fi].seek(off)
            data = remote[fi].read(ln)
            m[REQ:REQ + len(data)] = data
            put(W_REQ_RESULT, len(data))
            req['n'] += 1; req['bytes'] += len(data)
        else:
            put(W_REQ_RESULT, 0xFFFFFFFF)
        put(W_REQ_DONE, seq)
        return True

    def command(c, timeout=60):
        put(W_CMD, c)
        t1 = time.time()
        while get(W_CMD):
            if time.time() - t1 > timeout: sys.exit('fake060: command %d not taken' % c)
            serve()
            time.sleep(0.005)
        r = get(W_RESULT)
        return r - (1 << 32) if r & 0x80000000 else r

    t0 = time.time()
    while get(W_MAGIC) != MAGIC or get(W_STATE) != 0:
        if time.time() - t0 > 60: sys.exit('fake060: core 1 not up (stage %d)' % get(HB_W_STAGE, CTL))
        time.sleep(0.2)
    print('fake060: core 1 up after %.1f s, ARM %d MHz, heap %d MB' %
          (time.time() - t0, get(W_CPU_HZ) // 1000000, get(W_HEAP_KB) // 1024))
    log_seen = get(W_LOG_HEAD)

    def show_log():
        nonlocal log_seen
        hd = get(W_LOG_HEAD)
        if hd - log_seen > LOG_LEN: log_seen = hd - LOG_LEN
        s = bytes(m[LOG + (i % LOG_LEN)] for i in range(log_seen, hd))
        log_seen = hd
        if s: sys.stdout.write(s.decode('latin-1')); sys.stdout.flush()

    # the files: base read-only, home writable
    sent, t1 = 0, time.time()
    for top, prefix, rw in ((base, '/base', 0), (home, '/home', 1)):
        if not top: continue
        for d, _, names in os.walk(top):
            for n in sorted(names):
                lp = os.path.join(d, n)
                cp = prefix + '/' + os.path.relpath(lp, top)
                size = os.path.getsize(lp)
                if not rw and remote_over is not None and size > remote_over:
                    text(cp); put(W_SIZE, size); put(W_FLAGS, FILE_REMOTE)
                    fi = command(CMD_OPEN)
                    if fi < 0: sys.exit('fake060: OPEN %s refused' % cp)
                    remote[fi] = open(lp, 'rb')
                    print('fake060: %s (%d bytes) as file %d, by request' % (cp, size, fi))
                    continue
                data = open(lp, 'rb').read()
                text(cp); put(W_SIZE, len(data)); put(W_FLAGS, rw)
                fi = command(CMD_OPEN)
                if fi < 0: sys.exit('fake060: OPEN %s refused (err %d)' % (cp, get(W_ERR)))
                off = 0
                while off < len(data):
                    n2 = min(len(data) - off, CHUNK_MAX)
                    m[WIN:WIN + n2] = data[off:off + n2]
                    put(W_FILE, fi); put(W_OFF, off); put(W_LEN, n2)
                    if command(CMD_CHUNK): sys.exit('fake060: CHUNK refused')
                    off += n2
                sent += len(data)
                print('fake060: %s (%d bytes) as file %d' % (cp, len(data), fi))
    print('fake060: %.1f MB in %.1f s, heap left %d MB' % (sent / 1e6, time.time() - t1, get(W_HEAP_KB) // 1024))

    put(W_OVL_PITCH, w * 4); put(W_OVL_ROW0, 0); put(W_OVL_MAXW, w); put(W_OVL_MAXH, h)
    put(W_VOLUME, 256); put(W_IN_HEAD, 0); put(W_IN_TAIL, 0)
    req['seen'] = get(W_REQ_SEQ)
    line = ' '.join(words)
    text(line)
    command(CMD_START)
    print('fake060: START', line)
    t0 = time.time()
    last, head = 0, 0
    at.sort(); keys.sort(); shots.sort()

    def event(e):
        nonlocal head
        struct.pack_into('>L', m, PG + IN_OFF + 4 * (head % IN_N), e)
        head += 1
        put(W_IN_HEAD, head)

    def flush_all():
        n = 0
        while True:
            r = command(CMD_FLUSH)
            if r < 0: return n
            p, size, off, ln = text_get(), get(W_SIZE), get(W_OFF), get(W_LEN)
            if not home or not p.startswith('/home/') or '/../' in p:
                print('fake060: FLUSH %s ignored' % p); continue
            lp = os.path.join(home, p[6:])
            if size == REMOVED:
                if os.path.exists(lp): os.remove(lp)
                print('fake060: removed', p); n += 1; continue
            os.makedirs(os.path.dirname(lp), exist_ok=True)
            with open(lp + '.part', 'r+b' if off else 'wb') as o:
                o.seek(off); o.write(bytes(m[WIN:WIN + ln]))
            if r == 0:
                os.replace(lp + '.part', lp)
                print('fake060: wrote back %s (%d bytes)' % (p, size)); n += 1

    while True:
        el = time.time() - t0
        if el > secs:
            print('fake060: still running after %d s' % secs); return 3
        t2 = time.time()
        while time.time() - t2 < 0.1:
            if not serve(): time.sleep(0.002)
        show_log()
        st = get(W_STATE)
        while at and at[0][0] <= el:
            text(at.pop(0)[1]); print('fake060: exec', text_get()); command(CMD_EXEC)
        while keys and keys[0][0] <= el:
            k = keys.pop(0)[1]
            code = KEYS[k] if k in KEYS else ord(k[0]) if len(k) == 1 else int(k)
            event(EV_KEY << 24 | 1 << 16 | code); event(EV_KEY << 24 | code)
            if len(k) == 1: event(EV_CHAR << 24 | ord(k))
            print('fake060: key', k)
        while shots and shots[0][0] <= el:
            fn = shots.pop(0)[1]
            pw, ph = get(W_WIDTH) or w, get(W_HEIGHT) or h
            raw = bytes(m[OVL0:OVL0 + w * 4 * ph])
            rgb = bytearray(pw * ph * 3)
            for y in range(ph):
                row = raw[y * w * 4:y * w * 4 + pw * 4]
                rgb[y * pw * 3:(y + 1) * pw * 3] = bytes(b for i in range(0, len(row), 4)
                                                         for b in (row[i + 2], row[i + 1], row[i]))
            open(fn, 'wb').write(b'P6\n%d %d\n255\n' % (pw, ph) + bytes(rgb))
            print('fake060: overlay -> %s (%dx%d)' % (fn, pw, ph))
        if el - last > 5 or st >= 3:
            last = el
            s = [get(i) for i in range(W_SAMPLE, W_SAMPLE_END)]
            pcm = struct.unpack_from('<%dh' % (HDMI_SIZE // 2), m, HDMI)
            rms = int((sum(v * v for v in pcm) / len(pcm)) ** 0.5)
            print('fake060: %5.1f s %s frames %d avg %d ms max %d ms heap %d MB mouse %d dirty %d hdmi rms %d req %d/%dMB | '
                  'sample %d pc %08x lr %08x sp %08x stack %s'
                  % (el, STATES[st] if st < 5 else st, get(W_FRAMES), get(W_AVG) // 1000,
                     get(W_FRAME_MS_MAX), get(W_HEAP_KB) // 1024, get(W_MOUSE), get(W_DIRTY), rms,
                     req['n'], req['bytes'] >> 20,
                     s[0], s[1], s[2], s[3], ' '.join('%08x' % v for v in s[4:] if v)))
            if get(HB_W_FAULT, CTL):
                print('fake060: core 1 FAULT %d at pc %08x addr %08x' % (get(HB_W_FAULT, CTL),
                      get(HB_W_FAULT_PC, CTL), get(HB_W_FAULT_ADDR, CTL)))
                return 2
            if st >= 3:
                show_log()
                print('fake060: %s, %d files written back' % (STATES[st], flush_all()))
                return 0 if st == 3 else 1


if __name__ == '__main__':
    sys.exit(main())
