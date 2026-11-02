# GNU tools, the set

A metapackage: no files of its own. `gnu-tools.manifest` lists the tools, `make build
stage` builds them all and makes `build/gnu-tools.tar`. Each tool also builds and
installs alone from its own component.
