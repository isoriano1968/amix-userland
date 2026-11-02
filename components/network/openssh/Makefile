# OpenSSH 10.2 for AMIX: ssh, scp, sftp, ssh-keygen and sshd, on OpenSSL.
COMPONENT_NAME    = openssh
COMPONENT_VERSION = 10.2p1
COMPONENT_ARCHIVE = openssh-$(COMPONENT_VERSION).tar.gz
COMPONENT_URL     = https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/$(COMPONENT_ARCHIVE)
COMPONENT_SHA256  = ccc42c0419937959263fa1dbd16dafc18c56b984c03562d2937ce56a60f798b2
CLEAN_FILES       = openssh-$(COMPONENT_VERSION) $(COMPONENT_ARCHIVE)

include ../../../make-rules/shared.mk

export PATH := $(AMIX_TOOLS):$(PATH)
S  = openssh-$(COMPONENT_VERSION)
R  = $(AMIX_SYSROOT)
W  = -Wno-error=implicit-function-declaration -Wno-error=int-conversion \
	-Wno-error=incompatible-pointer-types -Wno-error=implicit-int
CC_AMIX = m68k-elf-gcc -m68060 -fcommon -std=gnu99 -U__linux__ -Ulinux -D__amix__ -DSVR4 -w $(W)

build: $(COMPONENT_ARCHIVE)
	rm -rf $(S) build
	tar xzf $(COMPONENT_ARCHIVE)
	for p in patches/*.patch; do patch -p1 < $$p || exit 1; done
	mkdir -p build
	$(CC_AMIX) -O2 -c files/amix-net.c -o build/amix-net.o
	cd build && CC="$(CC_AMIX)" CFLAGS="-O2 -fno-split-wide-types" CPPFLAGS="-I$(R)/usr/local/include" \
		LDFLAGS="-L$(R)/usr/local/lib -Wl,-rpath-link,$(R)/usr/local/lib -Wl,-R,/usr/local/lib -Wl,--allow-multiple-definition" \
		LIBS="$(CURDIR)/build/amix-net.o -lamixc99 -lsocket -lnsl -lgen" \
		../$(S)/configure --host=m68k-elf --build=x86_64-linux-gnu --prefix=/usr/local --sysconfdir=/etc/ssh \
		--with-ssl-dir=$(R)/usr/local --with-zlib=$(R)/usr/local --without-pam --without-stackprotect \
		--without-hardening --disable-strip --with-privsep-path=/var/empty --with-pid-dir=/var/run > configure.log 2>&1
	# the forced header only now: configure takes what cc -E prints
	$(MAKE) -C build -j8 CC="$(CC_AMIX) -include $(CURDIR)/files/amix-ssh.h" > build/make.log 2>&1 || { grep -E ' error|undefined reference' build/make.log | head -20; exit 1; }

PROGS = ssh scp sftp ssh-add ssh-agent ssh-keygen ssh-keyscan sshd
LIBX  = sftp-server ssh-keysign sshd-session sshd-auth

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)$(PREFIX)/bin $(STAGE_DIR)$(PREFIX)/libexec $(STAGE_DIR)/etc/ssh
	for p in $(PROGS); do cp build/$$p $(STAGE_DIR)$(PREFIX)/bin/ && m68k-elf-strip $(STAGE_DIR)$(PREFIX)/bin/$$p; done
	for p in $(LIBX); do [ ! -f build/$$p ] || { cp build/$$p $(STAGE_DIR)$(PREFIX)/libexec/ && m68k-elf-strip $(STAGE_DIR)$(PREFIX)/libexec/$$p; }; done
	cp $(S)/ssh_config $(STAGE_DIR)/etc/ssh/
	sed -e 's|^#HostKey /etc/ssh/ssh_host_rsa_key||' -e 's|^#HostKey|HostKey|' \
	    -e 's|^#PermitRootLogin prohibit-password|PermitRootLogin yes|' \
	    -e 's|^#PrintMotd yes|PrintMotd no|' \
	    -e 's|/usr/libexec/sftp-server|$(PREFIX)/libexec/sftp-server|' $(S)/sshd_config > $(STAGE_DIR)/etc/ssh/sshd_config
	chmod 644 $(STAGE_DIR)/etc/ssh/sshd_config $(STAGE_DIR)/etc/ssh/ssh_config
	mkdir -p $(STAGE_DIR)/etc/init.d
	cp files/sshd.rc $(STAGE_DIR)/etc/init.d/sshd
	cp files/sshd-setup.sh $(STAGE_DIR)$(PREFIX)/libexec/sshd-setup
	chmod 744 $(STAGE_DIR)/etc/init.d/sshd
	chmod 755 $(STAGE_DIR)$(PREFIX)/libexec/sshd-setup
	chmod 755 $(STAGE_DIR)$(PREFIX)/bin/* $(STAGE_DIR)$(PREFIX)/libexec/*
	tar --format=ustar --numeric-owner --owner=0 --group=0 --mode=go-w -C $(STAGE_DIR) -cf build/openssh-amix.tar usr etc

.PHONY: build stage
