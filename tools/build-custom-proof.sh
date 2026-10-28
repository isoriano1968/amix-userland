#!/bin/sh
# Historical custom-runtime proof only. GNU Pth is the implementation target.
# Run from the repository root. This never installs or deploys anything.
set -eu
: "${CC:?Set CC to the GCC 14 AMIX compiler executable with installed specs}"
if test ! -x "$CC"; then
    echo 'CC must be one executable path, not a shell command with flags' >&2
    exit 1
fi
compiler_version=`"$CC" -dumpversion`
case "$compiler_version" in
    14|14.*) ;;
    *) echo "Expected GCC 14, got: $compiler_version" >&2; exit 1 ;;
esac
mkdir -p build
"$CC" --version > build/compiler.txt
echo '-O2 -m68060 -std=gnu89 -Wall -Wextra -fno-strict-aliasing' > build/flags.txt
"$CC" -O2 -m68060 -std=gnu89 -Wall -Wextra -fno-strict-aliasing \
    probes/context-roundtrip.c -o build/context-roundtrip
"$CC" -O2 -m68060 -std=gnu89 -Wall -Wextra -fno-strict-aliasing \
    probes/context-stack.c -o build/context-stack
"$CC" -O2 -m68060 -std=gnu89 -Wall -Wextra -fno-strict-aliasing \
    probes/context-state.c -o build/context-state
"$CC" -O2 -m68060 -std=gnu89 -Wall -Wextra -fno-strict-aliasing -Ipoc \
    poc/pthread.c poc/proof.c -o build/pthread-proof
"$CC" -O2 -m68060 -std=gnu89 -Wall -Wextra -fno-strict-aliasing -Ipoc \
    poc/pthread.c poc/limits.c -o build/pthread-limits
"$CC" -O2 -m68060 -std=gnu89 -Wall -Wextra \
    probes/bounded-run.c -o build/bounded-run
echo 'Cross-build complete. Execute each binary in a target scratch directory.'
echo 'Run the toolchain 68060 instruction-safety checker before deployment.'
