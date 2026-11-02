# GNU Wget for AMIX

Wget 1.25 in `/usr/local/bin`, with `/etc/wgetrc` as upstream ships it. HTTPS
through `library/openssl`; the certificates come from OpenSSL's default path,
`/usr/local/ssl/cert.pem`, which `network/ca-certificates` links to
`/etc/ssl/cert.pem`.

## AMIX changes

Built the way `components/gnu/gnu.mk` builds the GNU tools, with their compat
layer (`../../gnu/compat`: `printf` with `%j`, `%z`, `%t`, getopt, `strtoll`).

- `patches/0001-amix-flock-types.patch`: gnulib's `flock.c` includes
  `<sys/types.h>` first; AMIX's `<sys/file.h>` needs its types.
- `patches/0002-amix-hsts-strtoll.patch`: AMIX's `sscanf` has no `%lld`, so the
  HSTS database (`~/.wget-hsts`) reads its two 64-bit times through `strtoll`.
