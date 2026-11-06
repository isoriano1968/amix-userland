# AMIX development tools for VS Code: runs on the Linux PC, not on AMIX.
#   make build     package build/vscode-amix-$(COMPONENT_VERSION).vsix
#   make install   install it into VS Code
COMPONENT_NAME    = vscode
COMPONENT_VERSION = 0.3.1

include ../../../make-rules/shared.mk

# vsce ignores package.json's repository "directory": give it the
# component's place in amix-userland for the README's relative links
REPO_URL = https://github.com/isoriano1968/amix-userland
COMP_DIR = components/developer/vscode
VSIX     = $(BUILD_DIR)/vscode-amix-$(COMPONENT_VERSION).vsix

build: $(VSIX)

$(VSIX): package.json README.md $(shell find src snippets syntaxes images samples -type f -o -type l)
	mkdir -p $(BUILD_DIR)
	npx --yes @vscode/vsce package --skip-license -o $@ \
		--baseContentUrl $(REPO_URL)/blob/HEAD/$(COMP_DIR) \
		--baseImagesUrl $(REPO_URL)/raw/HEAD/$(COMP_DIR)

install: $(VSIX)
	code --install-extension $(VSIX)

.PHONY: build install
