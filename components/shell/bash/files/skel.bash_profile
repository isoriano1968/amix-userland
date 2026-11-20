# ~/.bash_profile -- a login bash (the console, telnet, ssh).  bash has read
# AMIX's /etc/profile; a login shell does not read /etc/bash/bashrc by
# itself, so it comes here, then your ~/.bashrc.
[ -r /etc/bash/bashrc ] && . /etc/bash/bashrc
[ -r "$HOME/.bashrc" ] && . "$HOME/.bashrc"
