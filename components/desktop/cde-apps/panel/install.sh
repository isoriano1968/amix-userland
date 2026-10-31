#!/bin/sh
# install.sh -- (on the A3000, as root, in this directory) the AMIX control in
# the front panel; then Restart Workspace Manager.  Panel edits made in CDE are
# kept in $HOME/.dt/types/fp_dynamic and win over this file.
set -e
for d in /etc/dt/appconfig/types/C /etc/dt/appconfig/icons/C; do [ -d $d ] || mkdir -p $d; done
cp amix.fp /etc/dt/appconfig/types/C/
rm -f /etc/dt/appconfig/types/C/ztopcde.fp
cp AmixPanel.l.pm AmixPanel.m.pm AmixPanel.t.pm /etc/dt/appconfig/icons/C/
echo "installed: amix.fp, AmixPanel icons"
