#!/bin/sh
# run.sh PKG -- the read side against tests/repo (published with OI's pkgsend):
# set-publisher, refresh (signatures), list, info, contents, verify.
# Works in a temporary image root; on the host, not on AMIX.
PKG=${1:?usage: run.sh path/to/pkg}
here=$(cd "$(dirname "$0")" && pwd)
case $PKG in /*) ;; *) PKG=$(pwd)/$PKG ;; esac
T=$(mktemp -d "${TMPDIR:-/tmp}/amix-pkg-test.XXXXXX")
trap 'rm -rf "$T"' EXIT INT TERM
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
expect() {	# expect NAME RC "grep pattern" command...
	name=$1 want=$2 pat=$3; shift 3
	out=$("$@" 2>&1); rc=$?
	if [ "$rc" != "$want" ]; then bad "$name (rc $rc, want $want): $out"; return; fi
	if [ -n "$pat" ] && ! printf '%s\n' "$out" | grep -q -- "$pat"; then
		bad "$name (no '$pat' in: $out)"; return; fi
	ok "$name"
}
R="$T/root"
mkdir -p "$R"

expect "no origin yet"            1 "set-publisher" "$PKG" -R "$R" refresh
expect "set-publisher"            0 ""              "$PKG" -R "$R" set-publisher -O "file://$here/repo" amix
expect "publisher"                0 "file://"       "$PKG" -R "$R" publisher
expect "refresh"                  0 ""              "$PKG" -R "$R" refresh
expect "catalog kept"             0 ""              test -f "$R/var/pkg/publisher/amix/catalog/catalog.base.C"
expect "list: nothing installed"  1 ""              "$PKG" -R "$R" list
expect "list -a"                  0 "demo/hello  *1.1,2.1-2 *---" "$PKG" -R "$R" list -a
expect "list -a world"            0 "library/world" "$PKG" -R "$R" list -aH world
expect "info -r newest"           0 "Summary: Says hello, louder" "$PKG" -R "$R" info -r hello
expect "info -r @1.0"             0 "Branch: 1"     "$PKG" -R "$R" info -r hello@1.0
expect "info -r unknown"          1 "not in the catalog" "$PKG" -R "$R" info -r nosuch
expect "contents -r"              0 "usr/local/bin/hi" "$PKG" -R "$R" contents -r hello
expect "contents -r -t file"      0 "etc/hello.conf" "$PKG" -R "$R" contents -r -t file hello
expect "contents -r -m"           0 "set name=pkg.fmri value=pkg://amix/demo/hello@1.1" "$PKG" -R "$R" contents -r -m hello

# a tampered repository: refresh must refuse it and keep the catalog it has
cp -R "$here/repo" "$T/badrepo"
sed 's/Says hello, louder/Says hello, LOUDER/' "$here/repo/publisher/amix/catalog/catalog.summary.C" \
	> "$T/badrepo/publisher/amix/catalog/catalog.summary.C"
R2="$T/root2"; mkdir -p "$R2"
"$PKG" -R "$R2" set-publisher -O "$T/badrepo"
expect "refresh: tampered part"   1 "signature does not match" "$PKG" -R "$R2" refresh
expect "nothing kept from it"     1 ""              test -f "$R2/var/pkg/publisher/amix/catalog/catalog.attrs"
cp "$here/repo/publisher/amix/catalog/catalog.summary.C" "$T/badrepo/publisher/amix/catalog/"
m=$(ls "$T/badrepo/publisher/amix/pkg/demo%2Fhello/" | head -1)
printf 'set name=extra value=1\n' >> "$T/badrepo/publisher/amix/pkg/demo%2Fhello/$m"
"$PKG" -R "$R2" refresh >/dev/null 2>&1
expect "info: tampered manifest"  1 "does not match the catalog" "$PKG" -R "$R2" info -r hello@1.0

# an installed demo/hello@1.0, laid out as install will (step 3) do it
python3 -I - "$here/repo" "$R" <<'EOF'
import gzip, os, re, sys, shlex
repo, root = sys.argv[1], sys.argv[2]
d = os.path.join(repo, "publisher/amix/pkg/demo%2Fhello")
man = os.path.join(d, sorted(os.listdir(d))[0])          # 1.0
text = open(man).read()
inst = os.path.join(root, "var/pkg/installed")
os.makedirs(inst, exist_ok=True)
open(os.path.join(inst, "demo%2Fhello"), "w").write(text)
for line in text.splitlines():
    w = shlex.split(line)
    a = dict(x.split("=", 1) for x in w[1:] if "=" in x)
    p = os.path.join(root, a.get("path", ""))
    if w[0] == "dir":
        os.makedirs(p, exist_ok=True); os.chmod(p, int(a["mode"], 8))
    elif w[0] == "file":
        os.makedirs(os.path.dirname(p), exist_ok=True)
        data = gzip.open(os.path.join(repo, "publisher/amix/file", w[1][:2], w[1])).read()
        open(p, "wb").write(data); os.chmod(p, int(a["mode"], 8))
    elif w[0] == "link":
        os.symlink(a["target"], p)
uid, gid = os.getuid(), os.getgid()
open(os.path.join(root, "etc/passwd"), "w").write("root:x:%d:%d::/:\nbin:x:%d:%d::/:\n" % (uid, gid, uid, gid))
open(os.path.join(root, "etc/group"), "w").write("root::%d:\nbin::%d:\n" % (gid, gid))
EOF
expect "list: installed"          0 "demo/hello  *1.0,2.1-1 *iu-" "$PKG" -R "$R" list
expect "list -u"                  0 "demo/hello"    "$PKG" -R "$R" list -u
expect "info: installed"          0 "State: Installed" "$PKG" -R "$R" info hello
expect "contents: installed"      0 "usr/local/include/hello.h" "$PKG" -R "$R" contents hello
expect "verify: clean"            0 ""              "$PKG" -R "$R" verify
echo "greeting=changed" > "$R/etc/hello.conf"
expect "verify: preserved conf changed is fine" 0 "" "$PKG" -R "$R" verify
chmod u+w "$R/usr/local/bin/hello"
printf '#!/bin/sh\necho hacked\n' > "$R/usr/local/bin/hello"; chmod 0555 "$R/usr/local/bin/hello"
expect "verify: changed file"     1 "usr/local/bin/hello: size" "$PKG" -R "$R" verify
chmod u+w "$R/usr/local/bin/hello"
printf '#!/bin/sh\necho hello 9\n' > "$R/usr/local/bin/hello"; chmod 0555 "$R/usr/local/bin/hello"
expect "verify: same size, other content" 1 "content changed" "$PKG" -R "$R" verify hello
chmod 0755 "$R/usr/local/bin/hello"
expect "verify: mode"             1 "mode 0755, should be 0555" "$PKG" -R "$R" verify
rm "$R/usr/local/bin/hi"
expect "verify: missing link"     1 "usr/local/bin/hi: missing" "$PKG" -R "$R" verify
expect "verify -q is quiet"       1 ""              "$PKG" -R "$R" verify -q
out=$("$PKG" -R "$R" verify -q 2>&1)
[ -z "$out" ] && ok "verify -q prints nothing" || bad "verify -q printed: $out"
expect "verify: unknown"          1 "no installed packages match" "$PKG" -R "$R" verify nosuch

echo
[ $fails = 0 ] && echo "all passed" || echo "$fails failed"
exit $fails
