#!/bin/sh
# dns-enable.sh -- switch AMIX from /etc/hosts-only lookups to DNS (8.8.8.8),
# as in https://amigaunix.com/doku.php/networking.  Run as root.  Every file
# replaced is kept: libsocket.so.hosts, netconfig.TCP, resolv.conf.orig.
#
# ln -f, not cp: it replaces the directory entry, so running programs (inetd,
# ...) keep the old library instead of seeing it rewritten under them.
NS=${1:-8.8.8.8}
set -e
[ -f /usr/lib/libsocket.so.hosts ] || cp -p /usr/lib/libsocket.so /usr/lib/libsocket.so.hosts
ln -f /usr/lib/libsockdns.so /usr/lib/libsocket.so
[ -f /etc/netconfig.TCP ] || cp -p /etc/netconfig /etc/netconfig.TCP
ln -f /etc/netconfig.DNS /etc/netconfig
[ -f /etc/resolv.conf ] && [ ! -f /etc/resolv.conf.orig ] && cp -p /etc/resolv.conf /etc/resolv.conf.orig
echo "nameserver $NS" > /etc/resolv.conf
# a "proper" domain gets appended to every lookup (wiki: General weirdness)
echo "domain: `domainname`  (/etc/domain: `cat /etc/domain 2>/dev/null`)"
sum /usr/lib/libsocket.so /usr/lib/libsockdns.so
cat /etc/resolv.conf
echo "undo: ln -f /usr/lib/libsocket.so.hosts /usr/lib/libsocket.so; ln -f /etc/netconfig.TCP /etc/netconfig"
