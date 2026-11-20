#!/bin/sh
# mkrepo.sh REPO [STOCK_CONTENTS] -- publish every component with a built
# stage (and the meta packages) into the IPS repository REPO, with
# tools/amix-p5m and tools/amix-publish. A component whose stage and manifest
# disagree is reported and left out; tools/amix-needed-check then checks every
# package's shared library dependencies and fails the run on a miss.
repo=${1:?usage: mkrepo.sh REPO [STOCK_CONTENTS]}
contents=$2
top=$(cd "$(dirname "$0")/../../../.." && pwd)
case $repo in /*) ;; *) repo=$(pwd)/$repo ;; esac
work=$(mktemp -d "${TMPDIR:-/tmp}/mkrepo.XXXXXX") || exit 1
trap 'rm -rf -- "$work"' EXIT INT TERM
cd "$top" || exit 1
n=0
for c in $(ls -d components/*/*/build/stage components/*/*/*/build/stage components/meta/* 2>/dev/null | sed -e 's|/build/stage$||' -e 's|^components/||' | sort -u); do
	ls components/"$c"/*.manifest >/dev/null 2>&1 || continue
	p5m="$work/$(echo "$c" | tr / _).p5m"
	if tools/amix-p5m components/"$c" ${contents:+-c "$contents"} -o "$p5m" 2>/dev/null; then
		tools/amix-publish --no-needed-check -s "$repo" -d components/"$c"/build/stage -d components/"$c" "$p5m" \
			>/dev/null || exit 1
		echo "published $c"
		n=$((n + 1))
	else
		echo "left out $c (its stage and manifest disagree)"
	fi
done
echo "mkrepo: $n packages in $repo"
# the shared library dependency gate, once every package is in (a package
# may come before one it requires above)
tools/amix-needed-check "$repo" || { echo "mkrepo: shared library dependencies missing (above)"; exit 1; }
