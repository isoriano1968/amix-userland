# Linux development VM

The project has its own Ubuntu 24.04 x86-64 VM, running headlessly under the
already installed VirtualBox 7.2.12. It uses four vCPUs, 8 GiB RAM, and a
40 GiB dynamically allocated VDI disk. No existing VM was modified.

All host-side VM artifacts are under ignored `local/linuxvm/`, including the
VirtualBox registry, disk, SSH key, known-host file, cloud-init seed, and serial
console log. The SSH service is forwarded only to 127.0.0.1:2228; VM login is
`builder` using the project-local key. There is no password login.

Official image: noble-server-cloudimg-amd64.vmdk from Ubuntu's 20261017 build.
SHA256 verified against Ubuntu's HTTPS-hosted SHA256SUMS:

```text
9a1e3959c9f4ad9d4752101fd4ff9a43191fb7d72e61fa20e033f9fd235ce794
```

## Host operations

From the project directory in PowerShell:

```powershell
./tools/linux-vm.ps1 status
./tools/linux-vm.ps1 ssh 'uname -a'
./tools/linux-vm.ps1 ssh 'tail -n 10 ~/toolchain-build.log'
./tools/linux-vm.ps1 stop
./tools/linux-vm.ps1 start
```

Stop requests a graceful ACPI shutdown. Start boots headlessly.

## Paths inside Linux

| Path | Purpose |
|---|---|
| ~/src/pthreads-amix | Project source snapshot; synchronize edits explicitly |
| ~/src/gcc14-cross-amix | Pinned upstream toolchain build source |
| ~/sysroots/amix | Private headers/libraries copied from the existing Windows AMIX tree |
| ~/work/gcc14-amix | Disposable toolchain build tree |
| ~/opt/m68k-amix | Installed AMIX cross-toolchain |
| ~/toolchain-build.log | Toolchain build log |
| ~/toolchain-build.status | Final exit status, created only after build completion |
| ~/src/upstream | Private working copies of pigz and zlib |
| ~/src/upstream/pth-2.0.7 | GNU Pth source with the AMIX port fixes |
| ~/opt/pth-amix | Privately installed GNU headers and m68k static libraries |

Toolchain source revision: 58130fe7a322a19cb4c010a8257444cf1c016a07 from
https://github.com/asokero/gcc14-cross-amix.
Its AGENTS.md and ABI measurements were read before building. The build uses
binutils 2.43.1 and GCC 14.2.0 with the upstream AMIX ABI target header, repaired
headers/specs, C99 compatibility library, and default 68060 code generation.

The sysroot was transferred from a local copy of the AMIX tree (usr/include, usr/ccs/lib,
usr/lib); original Windows files were not modified. It is licensed material
and must never be committed. VM/source archives and credentials are ignored.

After toolchain installation, inside the Linux project directory:

```sh
sh tools/test-host.sh
bash tools/build-gnu-pth.sh
CC="$HOME/opt/m68k-amix/bin/m68k-elf-gcc" sh tools/build-proof.sh
```

Use the compiler directly with its installed AMIX specs. The upstream
amix-cc.sh wrapper requires -o and changes its working directory, so it is not
a drop-in compiler executable for version checks and relative project paths.

Provisioning sources are tools/make-vm-seed.py and tools/linux-build-toolchain.sh.
The Linux host logic tests deliberately use _POSIX_C_SOURCE=2 to avoid pulling
native Linux pthread typedefs into this privately included experimental API.
Those host tests do not establish AMIX ABI, FPU, or kernel correctness.

The complete GCC 14 C/C++ build finished with status 0. Unused MSYS2 setup files
were removed after switching to Linux. GNU Pth and its acceptance results are
documented in GNU-PTH.md.

For agent-controlled remote work, keep visible-session.ps1 windows open using
Start-Process -Verb Open -WindowStyle Normal, with -WatchDirectory pointing at
local/session-queue/linux or amix. This launches independently of tool-runner
cleanup. Queue JSON files contain commands or upload/download jobs; .done marks
completion, not success. Inspect logged exit codes and test output before making
acceptance claims. Credentials remain process-local and never enter queue files.
