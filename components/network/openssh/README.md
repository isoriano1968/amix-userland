# OpenSSH for AMIX

OpenSSH 10.2 in `/usr/local`: `ssh`, `scp`, `sftp`, `ssh-keygen`, `ssh-agent` and `sshd`, with configuration in `/etc/ssh`. Needs `library/openssl` and `library/zlib`, and `/dev/urandom` from the `random` driver in amix-gate.

## Starting sshd

The package carries `/etc/init.d/sshd` and `/usr/local/libexec/sshd-setup`. Run the setup once as root: it creates the `sshd` user and group, `/var/empty`, the Ed25519 and ECDSA host keys and the rc links, and keeps anything already there.

    /usr/local/libexec/sshd-setup
    /etc/init.d/sshd start

There is no RSA host key, because generating one takes minutes on a 68060. sshd leaves the motd to the shell's profile (`PrintMotd no`), so it shows once. `root` can log in with a password or a key (`PermitRootLogin yes`); for a key, put the public key in `/.ssh/authorized_keys`, `/.ssh` mode 700.

## X11 forwarding

`ssh -X user@amiga` shows AMIX's X programs on the client's screen
(`patches/0002-amix-sshd-config.patch`: `X11Forwarding yes`). sshd uses the
X11R6.3 `xauth` in `/usr/x11r6/bin` (from `x11/x11r6.3`'s `build-shared.sh`):
AMIX's own X11R5 one turns the hex letters of a cookie into digits. And
`/etc/ssh/sshrc` (`files/sshrc`) stores the forwarded cookie under
`localhost:N` too, where AMIX's Xlib looks for it. Programs on the shared
X11R6.3 libraries work; the X11R5 ones in `/usr/X/bin` still find no cookie.
