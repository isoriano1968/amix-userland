# OpenSSL for AMIX

OpenSSL 3.5 LTS as `libcrypto.so.3` and `libssl.so.3` in `/usr/local/lib`, with headers and `.pc` files. All C: there is no m68k assembler in OpenSSL, so it is slow. `files/configure-amix.sh` turns off threads, DTLS, QUIC and what else a 1991 libc cannot do.

Measured on the 060 with other work running: ChaCha20 0.66 MB/s, AES-128-GCM 0.28 MB/s, Ed25519 verify 0.6 s. Random bytes need `/dev/urandom`, from the `random` driver in amix-gate.
