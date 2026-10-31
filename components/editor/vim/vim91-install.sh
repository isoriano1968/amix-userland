#!/bin/sh
# vim91-install.sh -- vim 9.1 into /usr/local; the old vim 5.8 stays as vim58
cd /usr/local/bin || exit 1
if [ -f vim ] && [ ! -f vim58 ]; then
	if strings vim | grep 'VIM - Vi IMproved 5.8' > /dev/null; then mv vim vim58; echo "old vim -> vim58"; fi
fi
cd / && tar xf /var/tmp/vim91-amix.tar; rc=$?; echo tar=$rc
[ $rc = 0 ] && rm -f /var/tmp/vim91-amix.tar
ls -la /usr/local/bin/vim /usr/local/bin/vim58 /usr/local/bin/xxd
/usr/local/bin/vim --version | head -2
du -s /usr/local/share/vim/vim91
df /
rm -f /var/tmp/vim91-install.sh
