# Terminal descriptions for what connects to AMIX today: xterm-256color, tmux,
# alacritty, kitty and the like, from ncurses' terminfo.src, in
# /usr/share/lib/terminfo, and AMIX's /etc/profile.d-style fallback amix-term.sh.
COMPONENT_NAME    = terminfo
COMPONENT_VERSION = 6.6

include ../../../make-rules/shared.mk

SRC = ../../library/ncurses/ncurses-$(COMPONENT_VERSION)/misc/terminfo.src
TERMS = xterm-256color xterm-color xterm-16color screen-256color tmux tmux-256color linux \
	rxvt rxvt-256color rxvt-unicode rxvt-unicode-256color alacritty foot xterm-kitty kitty \
	st st-256color vte vte-256color gnome gnome-256color konsole konsole-256color putty \
	putty-256color iterm iterm2 wezterm ms-terminal contour ghostty

# 65536 colour pairs need a format AMIX's and old readers do not take: 32767 will do.
build:
	test -f $(SRC) || $(MAKE) -C ../../library/ncurses build
	mkdir -p build/ti
	for t in $(TERMS); do tic -I -r -x -e $$t $(SRC) 2>/dev/null | \
		sed -e 's/pairs#0x10000/pairs#0x7fff/' -e 's/pairs#65536/pairs#32767/' > build/$$t.ti; \
		[ -s build/$$t.ti ] && tic -o build/ti build/$$t.ti 2>/dev/null; done

stage:
	rm -rf $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)/usr/share/lib/terminfo $(STAGE_DIR)/usr/local/lib
	cp -R build/ti/. $(STAGE_DIR)/usr/share/lib/terminfo/
	cp files/amix-term.sh $(STAGE_DIR)/usr/local/lib/
	tar --format=ustar --numeric-owner --owner=0 --group=0 -C $(STAGE_DIR) -cf build/terminfo-amix.tar usr

.PHONY: build stage
