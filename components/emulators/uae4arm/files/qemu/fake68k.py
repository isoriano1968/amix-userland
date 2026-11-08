#!/usr/bin/env python3
"""fake68k.py -- the 68k's side of c1uae (files/amix/c1uae.c) against the
REAL core 1 image running in qemu-system-arm -M xilinx-zynq-a9, whose DDR is
a shared file (run.sh): the same file names, flags, chunks, config text and
commands as c1uae, at the same board offsets, big-endian as the 060 writes.

  fake68k.py DDR_FILE -model A500 -kick ROM -df0 ADF [-cfg LINE]... [-bench N] [-secs S]

Prints core 1's log as it comes, and every 2 s the frames, the 68k pc and
core 1's timer sample (pc lr sp, then code addresses on its stack), for
arm-none-eabi-addr2line -e build/core1/uae.elf.
"""
import mmap
import struct
import sys
import time

RTG = 0x18000000                # board offset 0 as core 1 sees it
UA = RTG + 0x07AFD000           # C1UA_OFF
CTL = RTG + 0x07AFF000          # HB_CTL_OFF
WIN, WIN_SIZE = RTG + 0x07100000, 0x600000
NAMES, NAME_LEN, TEXT, LOG, LOG_LEN = 0x200, 120, 0xA00, 0xE00, 0x200
MAGIC = 0x43315541
(W_MAGIC, W_VERSION, W_STATE, W_CMD, W_ARG, W_RESULT, W_FRAMES, W_FRAME_US, W_AVG, W_SKIPPED,
 W_UNDERRUNS, W_CPU_HZ, W_BTLO, W_BTHI, W_TICK_HZ, W_PC, W_CHF, W_CHO, W_CHL, W_DIRTY) = range(20)
W_OVL_PITCH, W_VOLUME, W_IN_HEAD, W_IN_TAIL, W_LOG_HEAD, W_ERR, W_OWNER = 23, 28, 29, 30, 31, 32, 33
W_BM_STATE, W_SAMPLE, W_SAMPLE_END, W_FILES = 34, 40, 60, 64
CMD_CHUNK, CMD_BENCH = 1, 7
STATES = ['IDLE', 'LOADING', 'RUNNING', 'STOPPED', 'FAILED']
HB_W_FAULT, HB_W_FAULT_ADDR, HB_W_FAULT_PC, HB_W_STAGE = 20, 21, 22, 24


def main():
    a = sys.argv[1:]
    ddr = a.pop(0)
    model, kick, df0, cfg, bench, secs = 'A500', None, None, [], 3000, 120
    while a:
        o = a.pop(0)
        if o == '-model': model = a.pop(0)
        elif o == '-kick': kick = a.pop(0)
        elif o == '-df0': df0 = a.pop(0)
        elif o == '-cfg': cfg.append(a.pop(0))
        elif o == '-bench': bench = int(a.pop(0))
        elif o == '-secs': secs = float(a.pop(0))
        else: sys.exit('fake68k: ' + o + '?')
    f = open(ddr, 'r+b')
    m = mmap.mmap(f.fileno(), 0)

    def get(w, base=UA): return struct.unpack_from('>L', m, base + 4 * w)[0]
    def put(w, v, base=UA): struct.pack_into('>L', m, base + 4 * w, v & 0xFFFFFFFF)

    t0 = time.time()
    while get(W_MAGIC) != MAGIC or get(W_STATE) != 0:
        if time.time() - t0 > 30: sys.exit('fake68k: core 1 not up (stage %d)' % get(HB_W_STAGE, CTL))
        time.sleep(0.2)
    print('fake68k: core 1 up, ARM %d MHz' % (get(W_CPU_HZ) // 1000000))
    log_seen = get(W_LOG_HEAD)

    def show_log():
        nonlocal log_seen
        h = get(W_LOG_HEAD)
        if h - log_seen > LOG_LEN: log_seen = h - LOG_LEN
        s = bytes(m[UA + LOG + (i % LOG_LEN)] for i in range(log_seen, h))
        log_seen = h
        if s: sys.stdout.write(s.decode('latin-1')); sys.stdout.flush()

    files = []
    for path, rw in ((kick, 0), (df0, 1)):
        data = open(path, 'rb').read()
        flags = rw | (2 if data[0] == 0x14 and data[1] == 0x11 and data[3] == 0x4E else 0)
        files.append(('%d-%s' % (len(files), path.rsplit('/', 1)[-1][:100]), data, flags))
    for i, (name, data, flags) in enumerate(files):
        nb = name.encode()[:NAME_LEN - 1].ljust(NAME_LEN, b'\0')
        m[UA + NAMES + i * NAME_LEN:UA + NAMES + (i + 1) * NAME_LEN] = nb
        put(W_FILES + i * 3, len(data))
        put(W_FILES + i * 3 + 1, flags)
        off = 0
        while off < len(data):
            n = min(len(data) - off, WIN_SIZE)
            m[WIN:WIN + n] = data[off:off + n]
            put(W_CHF, i); put(W_CHO, off); put(W_CHL, n)
            put(W_ARG, 0); put(W_CMD, CMD_CHUNK)
            t1 = time.time()
            while get(W_CMD):
                if time.time() - t1 > 30: sys.exit('fake68k: chunk not taken')
                time.sleep(0.01)
            if get(W_RESULT): sys.exit('fake68k: chunk refused, err %d' % get(W_ERR))
            off += n
        print('fake68k: %s %d bytes, flags %d' % (name, len(data), flags))
    text = 'model=%s\nvar=uae4arm_resolution=640x256\nvar=uae4arm_linedoubling=on\n' % model
    text += 'cfg=kickstart_rom_file=%s\n' % files[0][0]
    text += ''.join('cfg=%s\n' % c for c in cfg)
    text += 'content=%s\n' % files[1][0]
    tb = text.encode() + b'\0'
    m[UA + TEXT:UA + TEXT + len(tb)] = tb
    put(W_VOLUME, 256); put(W_IN_HEAD, 0); put(W_IN_TAIL, 0); put(W_OVL_PITCH, 0)
    put(W_ARG, bench); put(W_CMD, CMD_BENCH)
    t0 = time.time(); last = 0
    while time.time() - t0 < secs:
        time.sleep(0.25)
        show_log()
        st = get(W_STATE)
        if time.time() - last > 2 or st >= 3:
            last = time.time()
            s = [get(w) for w in range(W_SAMPLE, W_SAMPLE_END)]
            print('fake68k: %5.1f s %s frames %d avg %d us 68k pc %08x bm %s | sample %d pc %08x lr %08x sp %08x stack %s'
                  % (time.time() - t0, STATES[st] if st < 5 else st, get(W_FRAMES), get(W_AVG), get(W_PC),
                     chr(get(W_BM_STATE)) if get(W_BM_STATE) else '-', s[0], s[1], s[2], s[3],
                     ' '.join('%08x' % v for v in s[4:] if v)))
            if get(HB_W_FAULT, CTL):
                print('fake68k: core 1 FAULT %d at pc %08x addr %08x' % (get(HB_W_FAULT, CTL),
                      get(HB_W_FAULT_PC, CTL), get(HB_W_FAULT_ADDR, CTL)))
                return 2
            if st >= 3:
                return 0 if st == 3 else 1
    print('fake68k: still running after %d s' % secs)
    return 3


if __name__ == '__main__':
    sys.exit(main())
