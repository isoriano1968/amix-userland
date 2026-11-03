# The Mozilla CA bundle, as curl.se extracts it, for TLS on AMIX: in
# /etc/ssl/cert.pem, and as a hashed directory /etc/ssl/certs (curl's, and
# OpenSSL's default through /usr/local/ssl/certs: only the certificates a
# connection needs are read).
COMPONENT_NAME    = ca-certificates
COMPONENT_VERSION = 2026-10-20
COMPONENT_ARCHIVE = cacert-$(COMPONENT_VERSION).pem
COMPONENT_URL     = https://curl.se/ca/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = 796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe
CLEAN_FILES       = $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

build: $(COMPONENT_ARCHIVE)

# OPENSSLDIR of library/openssl is /usr/local/ssl
stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/etc/ssl $(STAGE_DIR)$(PREFIX)/ssl
	cp $(COMPONENT_ARCHIVE) $(STAGE_DIR)/etc/ssl/cert.pem
	chmod 644 $(STAGE_DIR)/etc/ssl/cert.pem
	sh files/hash-certs.sh $(COMPONENT_ARCHIVE) $(STAGE_DIR)/etc/ssl/certs
	ln -sf /etc/ssl/certs $(STAGE_DIR)$(PREFIX)/ssl/certs
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/ca-certificates-amix.tar etc usr

.PHONY: build stage
