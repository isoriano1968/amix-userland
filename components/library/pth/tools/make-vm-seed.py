"""Create a NoCloud seed ISO using a public SSH key, never a password.

Usage: python make-vm-seed.py public-key-file output.iso
Requires pycdlib in the host Python environment.
"""
import io
import sys
from pathlib import Path
import pycdlib

key = Path(sys.argv[1]).read_text().strip()
if not key.startswith("ssh-ed25519 ") or "\n" in key:
    raise SystemExit("Expected one ed25519 public key")
userdata = """#cloud-config
hostname: pthreads-amix-build
manage_etc_hosts: true
users:
  - name: builder
    shell: /bin/bash
    groups: [sudo]
    sudo: ALL=(ALL) NOPASSWD:ALL
    lock_passwd: true
    ssh_authorized_keys:
      - KEY_VALUE
ssh_pwauth: false
disable_root: true
package_update: true
packages:
  - build-essential
  - bison
  - flex
  - texinfo
  - python3
  - curl
  - xz-utils
  - zlib1g-dev
  - libgmp-dev
  - libmpfr-dev
  - libmpc-dev
  - patch
  - git
  - file
  - openssh-server
runcmd:
  - [sh, -c, 'touch /home/builder/build-environment-ready']
""".replace("KEY_VALUE", key)
metadata = "instance-id: pthreads-amix-linux-20261028\nlocal-hostname: pthreads-amix-build\n"
iso = pycdlib.PyCdlib()
iso.new(interchange_level=3, joliet=3, rock_ridge="1.09", vol_ident="cidata")
for iso_name, name, content in (
    ("/USERDATA.;1", "user-data", userdata),
    ("/METADATA.;1", "meta-data", metadata),
):
    data = content.encode("utf-8")
    iso.add_fp(io.BytesIO(data), len(data), iso_path=iso_name,
               rr_name=name, joliet_path="/" + name)
iso.write(sys.argv[2])
iso.close()
print("Created NoCloud seed ISO")
