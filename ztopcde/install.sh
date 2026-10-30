#!/bin/sh
# install.sh -- (on the A3000, as root, in this directory) put ztopcde and its
# CDE files in place; then Reload Actions and Restart Workspace Manager.
set -e
cp ztopcde /opt/amix/bin/ztopcde
chmod 755 /opt/amix/bin/ztopcde
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C; do [ -d $d ] || mkdir -p $d; done  # AMIX mkdir -p fails on existing ones
cp ztopcde.dt /etc/dt/appconfig/types/C/
cp icons/Z3660.l.pm icons/Z3660.m.pm icons/Z3660.t.pm /etc/dt/appconfig/icons/C/
echo "installed: /opt/amix/bin/ztopcde, ztopcde.dt, Z3660 icons"
