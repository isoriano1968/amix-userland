# cde-dtlogin: how CDE starts on AMIX: startcde from the console and the
# dtlogin graphical login, installed off (dtconfig -e turns it on).  Our own
# files (files/), and desktop/cde's Xconfig for /etc/dt/config.
#   make stage   build/stage
COMPONENT_NAME    = cde-dtlogin
COMPONENT_VERSION = 1.0

include ../../../make-rules/shared.mk

CDE_XCONFIG = ../cde/build/stage/usr/dt/config/Xconfig

build:
	@test -f $(CDE_XCONFIG) || { echo "no $(CDE_XCONFIG): make -C ../cde stage"; exit 1; }
	sh -n files/startcde && sh -n files/Xamix-dt && sh -n files/dtlogin.rc && sh -n files/dtconfig

stage: build
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/bin $(STAGE_DIR)/usr/dt/bin $(STAGE_DIR)/etc/init.d \
		$(STAGE_DIR)/etc/dt/config
	cp files/startcde $(STAGE_DIR)/usr/bin/startcde
	cp files/Xamix-dt $(STAGE_DIR)/usr/dt/bin/Xamix-dt
	cp files/dtconfig $(STAGE_DIR)/usr/dt/bin/dtconfig
	chmod 755 $(STAGE_DIR)/usr/bin/startcde $(STAGE_DIR)/usr/dt/bin/Xamix-dt \
		$(STAGE_DIR)/usr/dt/bin/dtconfig
	# off: no rc2.d link (dtconfig -e makes S99dtlogin and the K10 links)
	cp files/dtlogin.rc $(STAGE_DIR)/etc/init.d/dtlogin
	chmod 744 $(STAGE_DIR)/etc/init.d/dtlogin
	cp $(CDE_XCONFIG) $(STAGE_DIR)/etc/dt/config/Xconfig
	cp files/Xservers $(STAGE_DIR)/etc/dt/config/Xservers
	chmod 644 $(STAGE_DIR)/etc/dt/config/Xconfig $(STAGE_DIR)/etc/dt/config/Xservers
	$(manifest_files)

.PHONY: build stage
