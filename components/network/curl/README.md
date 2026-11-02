# curl for AMIX

curl 8.23 in `/usr/local`: the `curl` tool, and libcurl for building on the box
(`libcurl.a`, the headers in `include/curl`, `libcurl.pc` and `curl-config`). TLS
through `library/openssl`, compression through `library/zlib`, certificates from
`/etc/ssl/cert.pem` (`network/ca-certificates`).

    curl -O https://example.com/file.tar.gz

## AMIX changes

- `patches/0001-amix-blocking-connect.patch`: AMIX's sockets (STREAMS underneath)
  report a non-blocking connect writable, with `SO_ERROR` 0, before it is up, and
  the first send fails with `ENOTCONN`. curl connects blocking on AMIX; the
  kernel's connect timeout applies instead of curl's.
- `patches/0002-amix-socklen.patch`: `curl/system.h` uses `int` for the socket
  length on AMIX, which has no `socklen_t`, so programs on the box can include
  `<curl/curl.h>` as is.
- `files/amix-curl.h`, forced into the build only: `socklen_t` and the socket,
  `select` and `fchmod` prototypes SVR4's headers leave out.
- `-fno-split-wide-types`: gcc 14's 64-bit subtraction bug (`sub.l mem,mem`).

libcurl is static: libtool does not make shared libraries for this target.
