# CA certificates for AMIX

The Mozilla root store as curl.se extracts it, dated 2026-10-20, in
`/etc/ssl/cert.pem`: curl's built-in bundle path. `/usr/local/ssl/cert.pem`, the
default file of `library/openssl` (OPENSSLDIR `/usr/local/ssl`), links to it, so
wget and every other OpenSSL program verify servers without options.

A newer bundle: change `COMPONENT_VERSION` and `COMPONENT_SHA256` to a dated
file from https://curl.se/docs/caextract.html.
