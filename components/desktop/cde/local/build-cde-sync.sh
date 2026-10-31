#!/bin/sh
# build-cde-sync.sh -- copy every git-tracked file of src-cde that differs
# into the build copy build-cde (which also holds generated files and links)
H=$(cd "$(dirname "$0")" && pwd); S=$H/src-cde/cde-2.2.4a; B=$H/build-cde
cd "$S" && git ls-files | while read -r f; do
	cmp -s "$f" "$B/$f" || { mkdir -p "$B/$(dirname "$f")" && cp -p "$f" "$B/$f" && echo "synced $f"; }
done
