#!/bin/sh
# run.sh PKG -- the read side against tests/repo (published with OI's pkgsend):
# set-publisher, refresh (signatures), list, info, contents, verify.
# Works in a temporary image root; on the host, not on AMIX.
PKG=${1:?usage: run.sh path/to/pkg [path/to/amix-hw]}
HW=$2
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


# ---- step 3: install, update, uninstall ---------------------------------
newroot() {	# newroot DIR: an empty image with passwd/group, publisher set, catalog fetched
	mkdir -p "$1/etc"; chmod 0755 "$1/etc"
	printf 'root:x:%s:%s::/:\nbin:x:%s:%s::/:\n' "$(id -u)" "$(id -g)" "$(id -u)" "$(id -g)" > "$1/etc/passwd"
	printf 'root::%s:\nbin::%s:\n' "$(id -g)" "$(id -g)" > "$1/etc/group"
	"$PKG" -R "$1" set-publisher -O "$here/repo" && "$PKG" -R "$1" refresh
}
P() { "$PKG" -R "$I" --min-free 0 "$@"; }
I="$T/inst"; newroot "$I"
expect "install: only AMIX without -R" 1 "only AMIX" env -u PKG_IMAGE "$PKG" install hello
expect "install -n"               0 "install    library/world" P install -n hello@1.0
expect "install -n changes nothing" 1 "" test -d "$I/var/pkg/installed"
expect "install hello@1.0 (+ world)" 0 "install    demo/hello" P install hello@1.0
expect "list: both"               0 "library/world" P list
expect "hello runs"               0 "hello 1"       sh "$I/usr/local/bin/hello"
expect "link made"                0 ""              test -L "$I/usr/local/bin/hi"
expect "verify after install"     0 ""              P verify
expect "no .pkgtmp left"          0 ""              sh -c "! find '$I' -name '*.pkgtmp*' | grep -q ."
expect "no journal left"          1 ""              test -f "$I/var/pkg/journal"
expect "paths written"            0 "usr/local/bin/hello	demo/hello" cat "$I/var/pkg/paths"
expect "install again: nothing to do" 0 "nothing to do" P install hello@1.0
echo "greeting=mine" > "$I/etc/hello.conf"
expect "update"                   0 "update     demo/hello 1.0,2.1-1 -> 1.1,2.1-2" P update
expect "update kept my conf"      0 "greeting=mine" cat "$I/etc/hello.conf"
expect "update: package conf .new" 0 "greeting=hello" cat "$I/etc/hello.conf.new"
expect "update: new hello"        0 "hello 2"       sh "$I/usr/local/bin/hello"
expect "update: header gone"      1 ""              test -e "$I/usr/local/include/hello.h"
expect "update: empty dir gone"   1 ""              test -d "$I/usr/local/include"
expect "verify after update"      0 ""              P verify
expect "update: nothing left"     0 "nothing to do" P update
expect "history"                  0 "update	ok	demo/hello@1.1,2.1-2" P history
expect "uninstall world: needed"  1 "demo/hello needs library/world" P uninstall world
expect "uninstall hello"          0 "kept as /etc/hello.conf.pkgsave" P uninstall hello
expect "uninstall: files gone"    1 ""              test -e "$I/usr/local/bin/hello"
expect "uninstall: link gone"     1 ""              test -L "$I/usr/local/bin/hi"
expect "uninstall: bin dir gone"  1 ""              test -d "$I/usr/local/bin"
expect "uninstall: world stays"   0 ""              test -f "$I/usr/local/lib/libworld.so.1"
expect "uninstall: list"          0 "library/world" P list
expect "uninstall world"          0 "remove     library/world" P uninstall world
expect "uninstall: lib gone"      1 ""              test -d "$I/usr/local/lib"
expect "uninstall: etc stays"     0 ""              test -f "$I/etc/passwd"

C="$T/conflict"; newroot "$C"
mkdir -p "$C/usr/local/lib"; echo mine > "$C/usr/local/lib/libworld.so.1"
expect "conflict: unowned file"   1 "libworld.so.1 (library/world) is there already" "$PKG" -R "$C" --min-free 0 install hello
expect "conflict: nothing installed" 1 ""           test -d "$C/var/pkg/installed"
expect "conflict: file untouched" 0 "mine"          cat "$C/usr/local/lib/libworld.so.1"
expect "conflict: no .pkgtmp"     0 ""              sh -c "! find '$C' -name '*.pkgtmp*' | grep -q ."

S="$T/svr4"; newroot "$S"; mkdir -p "$S/var/sadm/install"
printf '/usr/local d none 0755 bin bin core\n/usr/local/bin/hello f none 0555 bin bin core\n' > "$S/var/sadm/install/contents"
expect "conflict: SVR4 file"      1 "belongs to an AMIX (SVR4) package" "$PKG" -R "$S" --min-free 0 install hello
printf '/usr d none 0755 bin bin core\n/usr/local d none 0755 bin bin core\n' > "$S/var/sadm/install/contents"
expect "SVR4 directories are shared" 0 "install"    "$PKG" -R "$S" --min-free 0 install hello@1.0
expect "uninstall keeps SVR4 dirs" 0 ""             sh -c "'$PKG' -R '$S' uninstall hello >/dev/null && '$PKG' -R '$S' uninstall world >/dev/null && test -d '$S/usr/local'"

Q="$T/space"; newroot "$Q"
expect "space limit"              1 "not enough space" "$PKG" -R "$Q" --min-free 100000000 install hello
expect "space: nothing changed"   1 ""              test -d "$Q/usr/local"

K="$T/crash"; newroot "$K"
expect "crash while preparing"    99 "test crash at prepared" env PKG_TEST_CRASH=prepared "$PKG" -R "$K" --min-free 0 install hello@1.0
expect "crash left .pkgtmp files" 0 ""              sh -c "find '$K' -name '*.pkgtmp' | grep -q ."
expect "next run undoes it"       0 "undoing an interrupted change" "$PKG" -R "$K" --min-free 0 install hello@1.0
expect "undone, then installed"   0 ""              sh -c "! find '$K' -name '*.pkgtmp*' | grep -q . && '$PKG' -R '$K' verify"
expect "crash while committing"   99 "test crash at committing" env PKG_TEST_CRASH=committing "$PKG" -R "$K" --min-free 0 update
expect "next run finishes it"     0 "finishing an interrupted change" "$PKG" -R "$K" --min-free 0 update
expect "finished: 1.1 verifies"   0 "1.1,2.1-2"     sh -c "'$PKG' -R '$K' verify && '$PKG' -R '$K' list hello"

L="$T/lock"; newroot "$L"; mkdir -p "$L/var/pkg"
echo $$ > "$L/var/pkg/lock"
expect "lock held"                1 "another pkg"   "$PKG" -R "$L" --min-free 0 install hello
echo 999999 > "$L/var/pkg/lock"
expect "stale lock taken over"    0 "install"       "$PKG" -R "$L" --min-free 0 install hello
expect "lock released"            1 ""              test -f "$L/var/pkg/lock"
# a directory moved to another disk: /usr/local as a symlink to a directory
M="$T/moved"; newroot "$M"; mkdir -p "$M/data/usr-local" "$M/usr"; chmod 0755 "$M/usr"; ln -s ../data/usr-local "$M/usr/local"
expect "dir as symlink: install"  0 ""              "$PKG" -R "$M" --min-free 0 install hello
expect "dir as symlink: verify"   0 ""              "$PKG" -R "$M" verify
expect "files on the other disk"  0 ""              test -f "$M/data/usr-local/bin/hi"

# plain HTTP (http.c): the repository and the installer's get/put
if command -v python3 >/dev/null 2>&1; then
	W="$T/web"; cp -R "$here/repo" "$W"
	python3 "$here/httpd.py" "$W" "$T/port" & HP=$!
	i=0; while [ ! -f "$T/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
	U="http://127.0.0.1:$(cat "$T/port")"
	H="$T/http"; newroot "$H"
	expect "http: set-publisher"  0 ""              "$PKG" -R "$H" set-publisher -O "$U" amix
	expect "http: refresh"        0 ""              "$PKG" -R "$H" refresh
	expect "http: install"        0 ""              "$PKG" -R "$H" --min-free 0 install hello
	expect "http: verify"         0 ""              "$PKG" -R "$H" verify
	expect "http get to file"     0 ""              "$PKG" http get "$U/publisher/amix/catalog/catalog.attrs" "$T/attrs"
	expect "http get same bytes"  0 ""              cmp "$T/attrs" "$W/publisher/amix/catalog/catalog.attrs"
	expect "http get redirect"    0 "catalog"       "$PKG" http get "$U/moved"
	expect "http get 404"         1 "HTTP 404"      "$PKG" http get "$U/nosuch"
	expect "http put"             0 ""              sh -c "echo step 1 | '$PKG' http put '$U/report/msg'"
	expect "http put body"        0 "step 1"        cat "$W/put.out"
	S="$T/https"; mkdir -p "$S"; "$PKG" -R "$S" set-publisher -O https://example.org amix >/dev/null 2>&1
	expect "https refused"        1 "no https"      "$PKG" -R "$S" refresh
	kill $HP 2>/dev/null; wait $HP 2>/dev/null
	expect "no server"            1 ""              "$PKG" http get "$U/x"
else
	echo "skip http (no python3)"
fi

# origins (0.5): a mirror, one off, the first that answers is the one in use
TAB=$(printf '\t')
R3="$T/root3"; mkdir -p "$R3"
expect "two origins"              0 ""              "$PKG" -R "$R3" set-publisher -O "$T/nosuch" -g "file://$here/repo" amix
expect "publisher -t"             0 "amix${TAB}file://$here/repo${TAB}on${TAB}-" "$PKG" -R "$R3" publisher -t
expect "refresh: the next origin" 0 "trying file://" "$PKG" -R "$R3" refresh
expect "it is in use now"         0 "file://$here/repo${TAB}on${TAB}active" "$PKG" -R "$R3" publisher -t
expect "--disable-origin"         0 ""              "$PKG" -R "$R3" set-publisher --disable-origin "file://$here/repo"
expect "an origin off"            0 "file://$here/repo${TAB}off" "$PKG" -R "$R3" publisher -t
expect "refresh: none on answers" 1 ""              "$PKG" -R "$R3" refresh
expect "--enable-origin"          0 ""              "$PKG" -R "$R3" set-publisher --enable-origin "file://$here/repo"
expect "-G one"                   0 ""              "$PKG" -R "$R3" set-publisher -G "$T/nosuch"
expect "-G unknown"               1 "not an origin" "$PKG" -R "$R3" set-publisher -G "$T/nosuch"
expect "unset-publisher"          0 ""              "$PKG" -R "$R3" unset-publisher amix
expect "no origins left"          0 "(none)"        "$PKG" -R "$R3" publisher

# search and tab-separated lists (0.5)
expect "search: in a summary"     0 "demo/hello"    "$PKG" -R "$R" search LOUDER
expect "search: nothing"          1 "no packages match" "$PKG" -R "$R" search zzzz
expect "list -at"                 0 "library/world${TAB}" "$PKG" -R "$R" list -at world
expect "list -at: newest, requires" 0 "demo/hello${TAB}1.0,2.1-1${TAB}iu${TAB}-${TAB}1.1,2.1-2${TAB}library/world${TAB}Says hello, louder" "$PKG" -R "$R" list -at hello

# org.amix.cpu 68060: refused on a 68020/030 (0.5)
PUB="$here/../../../../tools/amix-publish"
if [ -x "$PUB" ] && command -v python3 >/dev/null 2>&1; then
	mkdir -p "$T/cpusrc"; echo fast > "$T/cpusrc/f"
	printf '%s\n' 'set name=pkg.fmri value=pkg://amix/demo/fast@1.0,2.1-1' \
		'set name=pkg.summary value="for the 68060"' 'set name=org.amix.cpu value=68060' \
		'dir path=usr owner=root group=sys mode=0755' 'dir path=usr/local owner=root group=bin mode=0755' \
		'dir path=usr/local/share owner=root group=bin mode=0755' \
		'file f path=usr/local/share/fast.txt owner=root group=bin mode=0444' > "$T/fast.p5m"
	"$PUB" -s "$T/cpurepo" -d "$T/cpusrc" "$T/fast.p5m" >/dev/null 2>&1
	R4="$T/root4"; mkdir -p "$R4"
	"$PKG" -R "$R4" set-publisher -O "$T/cpurepo" amix && "$PKG" -R "$R4" refresh
	expect "cpu in the catalog"   0 "demo/fast${TAB}1.0,2.1-1${TAB}--${TAB}68060" "$PKG" -R "$R4" list -at
	expect "68060 package on a 030" 1 "68060 only" env PKG_CPU=030 "$PKG" -R "$R4" install demo/fast
	expect "nothing installed"    1 ""              test -f "$R4/usr/local/share/fast.txt"
	expect "68060 package on a 060" 0 ""            env PKG_CPU=060 "$PKG" -R "$R4" install demo/fast
	expect "installed"            0 ""              test -f "$R4/usr/local/share/fast.txt"
	# org.amix.hw: a board it needs (0.5.2)
	printf '%s\n' 'set name=pkg.fmri value=pkg://amix/demo/core1@1.0,2.1-1' \
		'set name=pkg.summary value="on core 1"' 'set name=org.amix.hw value=z3660-core1|zz9000' \
		'dir path=usr owner=root group=sys mode=0755' 'dir path=usr/local owner=root group=bin mode=0755' \
		'dir path=usr/local/share owner=root group=bin mode=0755' \
		'file f path=usr/local/share/core1.txt owner=root group=bin mode=0444' > "$T/core1.p5m"
	"$PUB" -s "$T/cpurepo" -d "$T/cpusrc" "$T/core1.p5m" >/dev/null 2>&1
	"$PKG" -R "$R4" refresh
	expect "hw in the catalog"    0 "demo/core1${TAB}.*${TAB}on core 1${TAB}z3660-core1|zz9000" "$PKG" -R "$R4" list -at core1
	expect "hw: none here"        1 "needs the board z3660-core1|zz9000 (org.amix.hw), and this machine has none" env PKG_HW=none "$PKG" -R "$R4" install demo/core1
	expect "hw: nothing installed" 1 ""             test -f "$R4/usr/local/share/core1.txt"
	expect "hw: other board"      1 "this machine has z3660" env PKG_HW=z3660 "$PKG" -R "$R4" install demo/core1
	expect "hw: the board"        0 ""              env PKG_HW=z3660,z3660-core1 "$PKG" -R "$R4" install demo/core1
	expect "hw: installed"        0 ""              test -f "$R4/usr/local/share/core1.txt"
else
	echo "skip cpu (no tools/amix-publish)"
fi

# a file moving between packages, and pkg updating itself first (0.5.1)
if [ -x "$PUB" ] && command -v python3 >/dev/null 2>&1; then
	mkdir -p "$T/mvsrc"; echo one > "$T/mvsrc/db"; echo pkg1 > "$T/mvsrc/p1"; echo pkg2 > "$T/mvsrc/p2"
	mvpkg() {	# mvpkg [-x] STEM VERSION [FILE PATH]...: -x without the gates
		x=; [ "$1" = -x ] && { x=--no-needed-check; shift; }
		s=$1 v=$2; shift 2
		{ echo "set name=pkg.fmri value=pkg://amix/$s@$v"
		  echo "set name=pkg.summary value=\"$s\""
		  echo 'dir path=usr owner=root group=sys mode=0755'
		  echo 'dir path=usr/local owner=root group=bin mode=0755'
		  echo 'dir path=usr/local/share owner=root group=bin mode=0755'
		  echo 'dir path=usr/local/share/mv owner=root group=bin mode=0755'
		  while [ $# -gt 1 ]; do
			echo "file $1 path=$2 owner=root group=bin mode=0444"; shift 2
		  done; } > "$T/mv.p5m"
		"$PUB" $x -s "$T/mvrepo" -d "$T/mvsrc" "$T/mv.p5m" >/dev/null 2>&1 || { echo "(publish $s@$v refused)"; return 1; }
	}
	mvpkg demo/a 1.0,2.1-1 db usr/local/share/mv/DB
	mvpkg demo/b 1.0,2.1-1
	mvpkg sysutils/amix-pkg 0.5.0,2.1-1 p1 usr/local/share/mv/pkg
	M="$T/mvroot"; mkdir -p "$M/etc"
	cp "$I/etc/passwd" "$I/etc/group" "$M/etc/"; echo "sys::$(id -g):" >> "$M/etc/group"
	"$PKG" -R "$M" set-publisher -O "$T/mvrepo" amix && "$PKG" -R "$M" refresh
	expect "move: install a, b, pkg"  0 ""          "$PKG" -R "$M" --min-free 0 install demo/a demo/b sysutils/amix-pkg
	expect "move: a has DB"           0 "usr/local/share/mv/DB	demo/a" cat "$M/var/pkg/paths"
	mvpkg demo/a 1.0,2.1-2
	mvpkg demo/b 1.0,2.1-2 db usr/local/share/mv/DB
	mvpkg sysutils/amix-pkg 0.5.1,2.1-1 p2 usr/local/share/mv/pkg
	"$PKG" -R "$M" refresh
	expect "move: update b alone takes a along" 0 "update     demo/a" "$PKG" -R "$M" --min-free 0 update -n demo/b
	expect "move: self-update first"  0 "pkg updates itself first" "$PKG" -R "$M" --min-free 0 update
	expect "move: pkg updated"        0 "pkg2"      cat "$M/usr/local/share/mv/pkg"
	expect "move: DB now in b"        0 "usr/local/share/mv/DB	demo/b" cat "$M/var/pkg/paths"
	expect "move: DB still there"     0 "one"       cat "$M/usr/local/share/mv/DB"
	expect "move: verify"             0 ""          "$PKG" -R "$M" verify
	expect "move: history, two runs"  0 "update	ok	sysutils/amix-pkg@" cat "$M/var/pkg/history"
	# and back, the owner dropping it in the same run: b drops, a takes; the
	# repo gate refuses a before b has dropped it
	expect "publish gate: path in b"  1 ""          mvpkg demo/a 1.0,2.1-3 db usr/local/share/mv/DB
	expect "refused: not in the repo" 1 ""          sh -c "ls '$T/mvrepo/publisher/amix/pkg/demo%2Fa/' | grep -q 2.1-3"
	mvpkg demo/b 1.0,2.1-3
	mvpkg demo/a 1.0,2.1-3 db usr/local/share/mv/DB
	"$PKG" -R "$M" refresh
	expect "move back"                0 ""          "$PKG" -R "$M" --min-free 0 update demo/a demo/b
	expect "move back: DB in a"       0 "usr/local/share/mv/DB	demo/a" cat "$M/var/pkg/paths"
	expect "move back: DB there"      0 "one"       cat "$M/usr/local/share/mv/DB"
	# a real clash still refused: c wants a's DB, a keeps it
	expect "publish gate: path in a"  1 ""          mvpkg demo/c 1.0,2.1-1 db usr/local/share/mv/DB
	mvpkg -x demo/c 1.0,2.1-1 db usr/local/share/mv/DB
	"$PKG" -R "$M" refresh
	expect "clash refused"            1 "belongs to demo/a" "$PKG" -R "$M" --min-free 0 install demo/c
else
	echo "skip move (no tools/amix-publish)"
fi

# amix-hw: the boards, PKG_HW for the test (no board on the host)
if [ -n "$HW" ]; then
	expect "amix-hw: none on the host" 0 ""         sh -c "env -u PKG_HW '$HW' | grep -q . && exit 1 || exit 0"
	expect "amix-hw: list"        0 "z3660-core1"   env PKG_HW=z3660,z3660-core1 "$HW"
	expect "amix-hw -q yes"       0 ""              env PKG_HW=z3660,z3660-core1 "$HW" -q "zz9000|z3660-core1"
	expect "amix-hw -q no"        1 ""              env PKG_HW=z3660 "$HW" -q z3660-core1
	expect "amix-hw -q prefix"    1 ""              env PKG_HW=z3660-core1 "$HW" -q z3660
fi

echo
[ $fails = 0 ] && echo "all passed" || echo "$fails failed"
exit $fails
