"""Read an AMIX disk image (HDF) without writing it.

RDB partitions (AmigaDOS devices/hardblocks.h), the AMIX UFS on UNIX_Root
(4.3BSD FFS as written by SVR4, big-endian; the same reader as pvmk's
svc/ufs.c), and what is installed: SVR4 packages, kernels in /stand, the
size of /usr/local, /usr/gnu and /opt, a pvmk boot partition.

The image is opened read-only (os.O_RDONLY) and nothing here has a write
path: the source is never changed.
"""
import os
import struct

BSIZE = 512

# --- RDB ------------------------------------------------------------------

PBFF_BOOTABLE = 1
PVBB_OFFSET = 0x3c0
PVBB_MAGIC = 0x50564242          # 'PVBB', pvmk include/pvmk/image.h
DOS1 = 0x444f5301


class NotAnImage(Exception):
    pass


def _u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def _i32(b, o):
    return struct.unpack_from(">i", b, o)[0]


def _rdb_ok(b):
    n = _u32(b, 4)
    if n < 2 or n > 128:
        return False
    s = 0
    for i in range(n):
        s = (s + _u32(b, i * 4)) & 0xffffffff
    return s == 0


def dostype_str(t):
    s = "".join(chr(c) if 0x20 <= c < 0x7f else "?" for c in struct.pack(">I", t)[:3])
    return "%s\\%d" % (s, t & 0xff)


class Partition(object):
    def __init__(self, name, dostype, lowcyl, highcyl, cylblocks, flags, pri):
        self.name = name
        self.dostype = dostype
        self.lowcyl = lowcyl
        self.highcyl = highcyl
        self.start = lowcyl * cylblocks              # first 512-byte block
        self.blocks = (highcyl - lowcyl + 1) * cylblocks
        self.bootable = bool(flags & PBFF_BOOTABLE)
        self.pri = pri
        self.kind = "other"

    @property
    def size(self):
        return self.blocks * BSIZE

    def describe(self):
        return {"amix-root": "AMIX root (UFS)", "amix-boot": "AMIX boot",
                "pvmk": "pvmk boot volume", "reserved": "reserved (swap or unused)",
                "amigados": "AmigaDOS"}.get(self.kind, dostype_str(self.dostype))


class Image(object):
    """An open disk image, read-only."""

    def __init__(self, path):
        self.path = path
        self.fd = os.open(path, os.O_RDONLY)
        st = os.fstat(self.fd)
        self.size = st.st_size
        if self.size == 0:                  # a block device: its size from the end
            self.size = os.lseek(self.fd, 0, os.SEEK_END)
        self.rdb_block = None
        self.geometry = None
        self.partitions = []

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def read(self, off, n):
        b = os.pread(self.fd, n, off)
        if len(b) != n:
            raise IOError("short read at %d" % off)
        return b

    def read_rdb(self):
        for blk in range(16):
            b = self.read(blk * BSIZE, BSIZE)
            if b[:4] == b"RDSK" and _rdb_ok(b):
                break
        else:
            raise NotAnImage("no Rigid Disk Block in blocks 0-15: not an Amiga hard disk image")
        self.rdb_block = blk
        cyls, secs, heads, cylblocks = _u32(b, 64), _u32(b, 68), _u32(b, 72), _u32(b, 144)
        self.geometry = (cyls, heads, secs)
        nxt = _u32(b, 28)
        seen = 0
        while nxt != 0xffffffff and seen < 64:
            p = self.read(nxt * BSIZE, BSIZE)
            if p[:4] != b"PART" or not _rdb_ok(p):
                raise NotAnImage("bad PART block at %d" % nxt)
            nlen = min(p[36], 31)
            name = p[37:37 + nlen].decode("latin-1")
            part = Partition(name, _u32(p, 192), _u32(p, 164), _u32(p, 168), cylblocks,
                             _u32(p, 20), _i32(p, 188))
            if part.start * BSIZE + part.size > self.size:
                raise NotAnImage("partition %s ends beyond the image" % name)
            part.kind = self._classify(part)
            self.partitions.append(part)
            nxt = _u32(p, 16)
            seen += 1
        return self.partitions

    def _classify(self, part):
        t = part.dostype
        if t >> 8 == 0x554e49:                         # 'UNI'
            return "amix-root" if t & 0xff == 1 else "amix-boot"
        if t >> 8 == 0x726573:                         # 'res'
            return "reserved"
        if t >> 8 == 0x444f53:                         # 'DOS'
            bb = self.read(part.start * BSIZE, 1024)
            if _u32(bb, 0) == DOS1 and _u32(bb, PVBB_OFFSET) == PVBB_MAGIC:
                return "pvmk"
            return "amigados"
        return "other"


# --- UFS ------------------------------------------------------------------

SBOFF = 8192
SB_MAGIC = 0x011954
NDADDR = 12
IFMT, IFDIR, IFLNK, IFREG = 0o170000, 0o040000, 0o120000, 0o100000


class UfsError(Exception):
    pass


class Inode(object):
    __slots__ = ("ino", "mode", "nlink", "size", "blocks", "raw")


class Ufs(object):
    """Read-only AMIX UFS on one partition of an Image."""

    def __init__(self, img, part):
        self.img = img
        self.base = part.start * BSIZE
        sb = img.read(self.base + SBOFF, 2048)
        if _u32(sb, 1372) != SB_MAGIC:
            raise UfsError("no UFS superblock on %s" % part.name)
        g = lambda o: _i32(sb, o)  # noqa: E731
        self.iblkno, self.cgoffset, self.cgmask = g(16), g(24), g(28)
        self.time, self.fssize, self.dsize, self.ncg = g(32), g(36), g(40), g(44)
        self.bsize, self.fsize, self.frag, self.minfree = g(48), g(52), g(56), g(60)
        self.nindir, self.inopb = g(116), g(120)
        self.state = g(132)
        self.ipg, self.fpg = g(184), g(188)
        self.ndir, self.nbfree, self.nifree, self.nffree = g(192), g(196), g(200), g(204)
        self.clean = sb[209]
        self.fsmnt = sb[212:212 + 64].split(b"\0", 1)[0].decode("latin-1")
        if (self.bsize not in (4096, 8192) or self.fsize < 512 or self.bsize % self.fsize or
                self.frag != self.bsize // self.fsize or not self.ipg or
                self.inopb * 128 != self.bsize or self.nindir != self.bsize // 4):
            raise UfsError("UFS superblock on %s does not make sense" % part.name)
        self._iblk = {}
        self._ind = {}

    # sizes in bytes, as df reports them
    def total_bytes(self):
        return self.dsize * self.fsize

    def free_bytes(self):
        return (self.nbfree * self.frag + self.nffree) * self.fsize

    def used_bytes(self):
        return self.total_bytes() - self.free_bytes()

    def avail_bytes(self):
        """Free space for ordinary users: minus the minfree reserve (df's avail)."""
        reserve = self.dsize * self.minfree // 100 * self.fsize
        return max(0, self.free_bytes() - reserve)

    def capacity_bytes(self):
        return self.used_bytes() + self.avail_bytes()

    # -- inodes, blocks ----------------------------------------------------
    def _frags(self, fa, n):
        return self.img.read(self.base + fa * self.fsize, n * self.fsize)

    def inode(self, ino):
        cg = ino // self.ipg
        if ino < 2 or cg >= self.ncg:
            raise UfsError("bad inode %d" % ino)
        cgstart = self.fpg * cg + self.cgoffset * (cg & ~self.cgmask)
        fa = cgstart + self.iblkno + (ino % self.ipg) // self.inopb * self.frag
        blk = self._iblk.get(fa)
        if blk is None:
            if len(self._iblk) > 256:
                self._iblk.clear()
            blk = self._iblk[fa] = self._frags(fa, self.frag)
        off = (ino % self.inopb) * 128
        raw = blk[off:off + 128]
        i = Inode()
        i.ino = ino
        i.mode = struct.unpack_from(">H", raw, 0)[0]
        i.nlink = struct.unpack_from(">H", raw, 2)[0]
        i.size = struct.unpack_from(">Q", raw, 8)[0]
        i.blocks = _i32(raw, 104)
        i.raw = raw
        return i

    def _indirect(self, fa):
        b = self._ind.get(fa)
        if b is None:
            if len(self._ind) > 64:
                self._ind.clear()
            b = self._ind[fa] = self._frags(fa, self.frag)
        return b

    def bmap(self, ino, lbn):
        if lbn < NDADDR:
            return _u32(ino.raw, 40 + lbn * 4)
        lbn -= NDADDR
        n = self.nindir
        if lbn < n:
            ib = _u32(ino.raw, 88)
        else:
            lbn -= n
            if lbn >= n * n:
                raise UfsError("file too large")
            ib = _u32(ino.raw, 92)
            if not ib:
                return 0
            ib = _u32(self._indirect(ib), lbn // n * 4)
            lbn %= n
        if not ib:
            return 0
        return _u32(self._indirect(ib), lbn * 4)

    def read_inode(self, ino, limit=None):
        size = ino.size if limit is None else min(ino.size, limit)
        out = bytearray()
        lbn = 0
        while len(out) < size:
            n = min(self.bsize, size - len(out))
            fa = self.bmap(ino, lbn)
            if not fa:
                out += b"\0" * n
            else:
                out += self._frags(fa, (n + self.fsize - 1) // self.fsize)[:n]
            lbn += 1
        return bytes(out)

    def entries(self, ino):
        """(name, inode number) of a directory."""
        data = self.read_inode(ino)
        o = 0
        while o + 8 <= len(data):
            d_ino, reclen, namlen = struct.unpack_from(">IHH", data, o)
            if reclen < 8:
                break
            if d_ino:
                yield data[o + 8:o + 8 + namlen].decode("latin-1"), d_ino
            o += reclen

    def lookup(self, path, follow=True, _depth=0):
        """Inode of an absolute path, or None if it does not exist."""
        if _depth > 8:
            raise UfsError("too many symbolic links")
        ino = self.inode(2)
        parts = [p for p in path.split("/") if p]
        for i, name in enumerate(parts):
            if ino.mode & IFMT != IFDIR:
                return None
            nxt = None
            for n, num in self.entries(ino):
                if n == name:
                    nxt = num
                    break
            if nxt is None:
                return None
            ino = self.inode(nxt)
            last = i == len(parts) - 1
            if ino.mode & IFMT == IFLNK and (follow or not last):
                target = self.read_inode(ino).decode("latin-1")
                if not target.startswith("/"):
                    target = "/" + "/".join(parts[:i]) + "/" + target
                rest = "/".join(parts[i + 1:])
                return self.lookup(target + ("/" + rest if rest else ""), follow, _depth + 1)
        return ino

    def exists(self, path):
        try:
            return self.lookup(path) is not None
        except UfsError:
            return False

    def read_file(self, path, limit=1 << 20):
        ino = self.lookup(path)
        if ino is None or ino.mode & IFMT != IFREG:
            return None
        return self.read_inode(ino, limit)

    def listdir(self, path):
        ino = self.lookup(path)
        if ino is None or ino.mode & IFMT != IFDIR:
            return []
        return [(n, self.inode(i)) for n, i in self.entries(ino) if n not in (".", "..")]

    def du(self, path):
        """Bytes allocated under path, on this filesystem, links not followed;
        each inode counted once."""
        top = self.lookup(path, follow=False)
        if top is None or top.mode & IFMT == IFLNK:
            return 0
        seen = set()
        total = 0
        stack = [top]
        while stack:
            ino = stack.pop()
            if ino.ino in seen:
                continue
            seen.add(ino.ino)
            total += ino.blocks * BSIZE
            if ino.mode & IFMT == IFDIR:
                for n, num in self.entries(ino):
                    if n not in (".", "..") and num not in seen:
                        stack.append(self.inode(num))
        return total


# --- what is on it --------------------------------------------------------

def parse_pkginfo(text):
    info = {}
    for line in text.splitlines():
        k, sep, v = line.partition("=")
        if sep and k.strip().isupper():
            info[k.strip()] = v.strip().strip('"')
    return info


class SourceInfo(object):
    """What the installer needs to know about a source image."""

    def __init__(self):
        self.path = None
        self.size = 0
        self.geometry = None
        self.partitions = []
        self.root = None            # Partition
        self.root_total = self.root_used = self.root_avail = 0
        self.root_clean = None
        self.packages = []          # (PKG, NAME, VERSION)
        self.kernels = []           # (name, size)
        self.unix_link = None
        self.movable = {}           # /usr/local etc. -> bytes
        self.found = set()          # paths that exist (probes)
        self.pvmk = None            # Partition or None
        self.notes = []

    def has_core(self):
        return any(p[0] == "core" for p in self.packages)

    def movable_bytes(self):
        return sum(self.movable.values())


MOVABLE = ("/usr/local", "/usr/gnu", "/opt")


def inspect(path, probes=(), progress=None):
    """Read everything the installer shows about a source HDF."""
    info = SourceInfo()
    info.path = path
    with Image(path) as img:
        info.size = img.size
        img.read_rdb()
        info.geometry = img.geometry
        info.partitions = img.partitions
        roots = [p for p in img.partitions if p.kind == "amix-root"]
        if not roots:
            raise NotAnImage("no AMIX root partition (dostype UNI\\1) on this disk")
        info.root = next((p for p in roots if p.name.lower() == "unix_root"), roots[0])
        info.pvmk = next((p for p in img.partitions if p.kind == "pvmk"), None)
        fs = Ufs(img, info.root)
        info.root_total = fs.total_bytes()
        info.root_used = fs.used_bytes()
        info.root_avail = fs.avail_bytes()
        info.root_clean = fs.clean
        if progress:
            progress("packages")
        for name, ino in sorted(fs.listdir("/var/sadm/pkg"), key=lambda e: e[0]):
            text = fs.read_file("/var/sadm/pkg/%s/pkginfo" % name, limit=16384)
            if text is None:
                continue
            pi = parse_pkginfo(text.decode("latin-1"))
            info.packages.append((pi.get("PKG", name), pi.get("NAME", ""), pi.get("VERSION", "")))
        if progress:
            progress("kernels")
        for name, ino in fs.listdir("/stand"):
            if ino.mode & IFMT == IFREG and ino.size > 512 * 1024:
                head = fs.read_inode(ino, 4)
                if head[:4] == b"\x7fELF" or name.endswith("unix") or "unix" in name:
                    info.kernels.append((name, ino.size))
        info.kernels.sort()
        link = fs.lookup("/unix", follow=False)
        if link is not None and link.mode & IFMT == IFLNK:
            info.unix_link = fs.read_inode(link).decode("latin-1")
        for p in probes:
            if fs.exists(p):
                info.found.add(p)
        for d in MOVABLE:
            if progress:
                progress(d)
            info.movable[d] = fs.du(d)
        if not info.has_core():
            info.notes.append("the core package is not installed: not an AMIX core system")
    return info
