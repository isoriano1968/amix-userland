# Architecture decisions

## 0001: discover before selecting a scheduler

Status: accepted, 2026-10-28.

We will not assume stock AMIX limitations describe the updated target, nor
assume a module loader provides shared-address-space threads. Initial work
consists of inventory and independent capability probes. The execution model
remains undecided.

## 0002: Python is the strategic target; pigz is an early test

Status: accepted as project direction, 2026-10-28.

Python offers scripting and build-system benefits. Its dependency surface is
too broad to isolate early scheduler bugs efficiently. Small tests and threaded
pigz therefore precede Python acceptance. Exact application versions remain
undecided until their requirements have been audited.

## 0003: preserve existing AMIX installations and ABI boundaries

Status: accepted, 2026-10-28.

Application experiments use separate build and deployment directories. No
system libc replacement, global compiler change, kernel installation, or boot
change is part of the initial discovery stage. Application and kernel objects
may need different toolchains. Credentials and vendor-owned sysroot material
stay outside Git.
