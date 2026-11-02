#!/bin/sh
# sshd-setup: what sshd needs and a package must not overwrite. Run once as
# root; run again it changes nothing. No RSA key: minutes on a 68060, and
# every client of the last ten years takes ed25519.
grep '^sshd:' /etc/group > /dev/null || /usr/sbin/groupadd -g 74 sshd || exit 1
grep '^sshd:' /etc/passwd > /dev/null ||
	/usr/sbin/useradd -u 74 -g sshd -d /var/empty -s /bin/false -c 'sshd privsep' sshd || exit 1
[ -d /var/empty ] || mkdir /var/empty
chown root /var/empty
chgrp sys /var/empty
chmod 755 /var/empty
[ -d /var/run ] || mkdir /var/run
[ -f /var/log/lastlog ] || { > /var/log/lastlog; chmod 644 /var/log/lastlog; }
for t in ed25519 ecdsa; do
	k=/etc/ssh/ssh_host_${t}_key
	[ -f $k ] || /usr/local/bin/ssh-keygen -q -t $t -f $k -N '' || exit 1
done
# always again: a reinstall replaces /etc/init.d/sshd, and old links
# would keep the old script
rm -f /etc/rc2.d/S85sshd /etc/rc0.d/K15sshd
ln /etc/init.d/sshd /etc/rc2.d/S85sshd
ln /etc/init.d/sshd /etc/rc0.d/K15sshd
echo "sshd is set up: /etc/init.d/sshd start, or at the next boot"
