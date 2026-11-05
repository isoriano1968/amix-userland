# pip for the system Python 3.11 of AMIX (runtime/python3), from its upstream
# wheel.  pip.conf in sys.prefix (/usr/local) makes the system Python's pip
# install --user by default: nothing installed by hand goes into the system
# site-packages; a venv reads its own prefix and is not affected.
COMPONENT_NAME    = pip
COMPONENT_VERSION = 26.3
COMPONENT_ARCHIVE = pip-$(COMPONENT_VERSION)-py3-none-any.whl
COMPONENT_URL     = https://files.pythonhosted.org/packages/07/25/ee8c38ae72d66b844ce8eb9711ba905dba21ca1a8a0c960057c41cd23337/pip-26.3-py3-none-any.whl
COMPONENT_SHA256  = f3b60b20ffaadd4415ed7957212a38ac63fa4c5fa375bd0f4a797777940fad13
STAGE_EXTRA       = install -m 644 files/pip.conf $(STAGE_DIR)/usr/local/pip.conf

include ../../../../make-rules/shared.mk
include ../../../../make-rules/python.mk
