# cde-dtlogin: how CDE starts on AMIX: startcde from the console and the
# dtlogin graphical login, installed off (dtconfig -e turns it on).  Our own
# files (files/), and desktop/cde's Xconfig for /etc/dt/config.
#   make stage   build/stage
COMPONENT_NAME    = cde-dtlogin
COMPONENT_VERSION = 1.0.6

include ../../../make-rules/shared.mk

CDE_XCONFIG = ../cde/build/stage/usr/dt/config/Xconfig
CDE_XRES    = ../cde/build/stage/usr/dt/config/C/Xresources
LOGO_SRC    = ../../../doc/assets/amix-logo.png

build:
	@test -f $(CDE_XCONFIG) || { echo "no $(CDE_XCONFIG): make -C ../cde stage"; exit 1; }
	sh -n files/startcde && sh -n files/Xamix-dt && sh -n files/dtlogin.rc && sh -n files/dtconfig
	mkdir -p $(BUILD_DIR)
	# dtdetach: dtlogin in a session of its own (the start script); the
	# 68020..68060 baseline, libc only
	$(AMIX_TOOLS)/m68k-elf-gcc -O2 $(AMIX_CPU_BASELINE) -Wall -o $(BUILD_DIR)/dtdetach files/dtdetach.c
	$(AMIX_TOOLS)/m68k-elf-strip $(BUILD_DIR)/dtdetach
	../../../tools/amix-fix-needed $(BUILD_DIR)/dtdetach > /dev/null
	# the login screen: the AMIX logo (16 colours, 8-bit safe) and greeting,
	# in the site's Xresources (/etc/dt/config, read before /usr/dt/config):
	# CDE's own file with the AMIX lines after it
	python3 files/mklogo.py $(LOGO_SRC) $(BUILD_DIR)/AmixLogo.pm 200 16
	{ cat $(CDE_XRES); printf '%s\n' '' \
		'!!######################################################################' \
		'!!  AMIX (desktop/cde-dtlogin): the AMIX logo and greeting' \
		'!!  the full-screen area behind the login box (dtgreet''s table covers' \
		'!!  the root window that Xsetup paints)' \
		'Dtlogin*table.background:	#4f6f8f' \
		'Dtlogin*logo*bitmapFile:	/usr/dt/appconfig/icons/C/AmixLogo.pm' \
		'Dtlogin*greeting.labelString:	Welcome to AMIX on %LocalHost%' \
		'Dtlogin*greeting.persLabelString:	Welcome to AMIX, %s'; } > $(BUILD_DIR)/Xresources

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/bin $(STAGE_DIR)/usr/dt/bin $(STAGE_DIR)/etc/init.d \
		$(STAGE_DIR)/etc/dt/config/C $(STAGE_DIR)/usr/dt/appconfig/icons/C
	cp files/startcde $(STAGE_DIR)/usr/bin/startcde
	cp files/Xamix-dt $(STAGE_DIR)/usr/dt/bin/Xamix-dt
	cp files/dtconfig $(STAGE_DIR)/usr/dt/bin/dtconfig
	cp $(BUILD_DIR)/dtdetach $(STAGE_DIR)/usr/dt/bin/dtdetach
	chmod 755 $(STAGE_DIR)/usr/bin/startcde $(STAGE_DIR)/usr/dt/bin/Xamix-dt \
		$(STAGE_DIR)/usr/dt/bin/dtconfig $(STAGE_DIR)/usr/dt/bin/dtdetach
	# off: no rc2.d link (dtconfig -e makes S99dtlogin and the K10 links)
	cp files/dtlogin.rc $(STAGE_DIR)/etc/init.d/dtlogin
	chmod 744 $(STAGE_DIR)/etc/init.d/dtlogin
	cp $(CDE_XCONFIG) $(STAGE_DIR)/etc/dt/config/Xconfig
	cp files/Xservers $(STAGE_DIR)/etc/dt/config/Xservers
	cp $(BUILD_DIR)/Xresources $(STAGE_DIR)/etc/dt/config/C/Xresources
	cp $(BUILD_DIR)/AmixLogo.pm $(STAGE_DIR)/usr/dt/appconfig/icons/C/AmixLogo.pm
	chmod 644 $(STAGE_DIR)/etc/dt/config/Xconfig $(STAGE_DIR)/etc/dt/config/Xservers \
		$(STAGE_DIR)/etc/dt/config/C/Xresources $(STAGE_DIR)/usr/dt/appconfig/icons/C/AmixLogo.pm
	$(manifest_files)
	$(CPU_SCAN) $(STAGE_DIR)

.PHONY: build stage
