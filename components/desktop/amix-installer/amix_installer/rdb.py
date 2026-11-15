"""An empty AMIX data disk: RDB plus one UNIX partition, no file system.

The RDB is made from the core disk's own RDB (the geometry and fields AMIX
already accepts): the RDSK block with the new size, and the UNIX_Root PART
block renamed, resized and not bootable.  The partition stays empty; AMIX
makes the file system on it (mkfs -F ufs), so the host never writes UFS.
The rest of the file is left sparse (zeros).
"""
import os
import struct

from . import hdf

BSIZE = 512
MAX_CYLS = 65535          # what the AMIX core disks use at most


def _put(b, off, v):
    struct.pack_into(">I", b, off, v & 0xffffffff)


def _sum(b):
    """RDB checksum: the first SummedLongs longs add up to 0."""
    n = struct.unpack_from(">I", b, 4)[0]
    _put(b, 8, 0)
    s = sum(struct.unpack_from(">%dI" % n, b, 0)) & 0xffffffff
    _put(b, 8, -s)


def geometry(size_bytes, secs):
    """(cylinders, heads) for a disk of about size_bytes, at most MAX_CYLS cylinders."""
    blocks = size_bytes // BSIZE
    heads = 1
    while blocks // (heads * secs) > MAX_CYLS:
        heads += 1
    return blocks // (heads * secs), heads


def make_data_disk(template, out, size_bytes, name="UNIX_Data"):
    """Write an empty data disk to out (a new file), RDB from template's."""
    with hdf.Image(template) as im:
        parts = im.read_rdb()
        root = next((p for p in parts if p.kind == "amix-root"), None)
        if root is None:
            raise hdf.NotAnImage("the core disk has no AMIX root partition to copy")
        rblk = im.rdb_block
        rdsk = bytearray(im.read(rblk * BSIZE, BSIZE))
        # the root's PART block: walk the list again to find its block number
        nxt = struct.unpack_from(">I", rdsk, 28)[0]
        part = None
        while nxt != 0xffffffff:
            b = im.read(nxt * BSIZE, BSIZE)
            n = b[36]
            if b[37:37 + n].decode("latin-1") == root.name:
                part = bytearray(b)
                break
            nxt = struct.unpack_from(">I", b, 16)[0]
    secs = struct.unpack_from(">I", rdsk, 68)[0]
    lowcyl = root.lowcyl
    cyls, heads = geometry(size_bytes, secs)
    if cyls <= lowcyl + 16:
        raise ValueError("data disk too small")
    cylblocks = heads * secs
    pblk = rblk + 1

    # RDSK: new geometry, one partition, no bad blocks, file systems or init code
    _put(rdsk, 16, BSIZE)
    _put(rdsk, 24, 0xffffffff)          # BadBlockList
    _put(rdsk, 28, pblk)                # PartitionList
    _put(rdsk, 32, 0xffffffff)          # FileSysHeaderList
    _put(rdsk, 36, 0xffffffff)          # DriveInit
    _put(rdsk, 64, cyls)
    _put(rdsk, 72, heads)
    _put(rdsk, 80, cyls)                # Park
    _put(rdsk, 96, cyls)                # WritePreComp
    _put(rdsk, 100, cyls)               # ReducedWrite
    _put(rdsk, 140, cyls - 1)           # HiCylinder
    _put(rdsk, 144, cylblocks)          # CylBlocks
    _put(rdsk, 152, pblk)               # HighRDSKBlock
    _sum(rdsk)

    # PART: the root's, renamed, the whole disk after the RDB cylinders
    _put(part, 16, 0xffffffff)          # Next
    _put(part, 20, struct.unpack_from(">I", part, 20)[0] & ~hdf.PBFF_BOOTABLE)
    nb = name.encode("latin-1")[:31]
    part[36] = len(nb)
    part[37:68] = nb + b"\0" * (31 - len(nb))
    _put(part, 128 + 12, heads)         # de_Surfaces
    _put(part, 128 + 20, secs)          # de_BlocksPerTrack
    _put(part, 164, lowcyl)
    _put(part, 168, cyls - 1)
    _put(part, 188, 0)                  # BootPri
    _sum(part)

    with open(out, "xb") as f:
        f.write(b"\0" * (rblk * BSIZE))
        f.write(bytes(rdsk))
        f.write(bytes(part))
        f.truncate(cyls * cylblocks * BSIZE)
        f.flush()
        os.fsync(f.fileno())
    return cyls * cylblocks * BSIZE
