#!/bin/sh
# run-tests.sh -- QuickJS-ng's tests/ on AMIX with qjs, in place of
# run-test262 (which needs threads): a test with a "negative:" header passes
# when qjs fails with that error type; tests marked skip-if-tcc (Atomics,
# which a build without C11 atomics lacks, as tcc) and the fixtures upstream's
# tests.conf excludes are skipped; test_builtin.js runs from amix/ with its
# lines on qjs.getStringKind (a run-test262 internal) blanked, so that its
# own file name and line numbers stay.  bug1661.js fails with
# qjs on every platform (only run-test262 handles its rejection).  120 s CPU.
cd `dirname $0`/tests || exit 1
ulimit -t 120
LD_LIBRARY_PATH=..; export LD_LIBRARY_PATH
pass=0; fail=0; skip=0
for f in *.js; do
	case $f in fixture_*|empty.js|microbench.js|test_worker*|test_std.js|assert.js) skip=`expr $skip + 1`; continue;; esac
	if grep skip-if-tcc $f > /dev/null; then echo "SKIP $f (Atomics)"; skip=`expr $skip + 1`; continue; fi
	t=$f
	if [ $f = test_builtin.js ]; then
		mkdir -p amix && cp assert.js amix/ &&
		sed '/qjs\.getStringKind/,/);/s/.*//' $f > amix/$f; t=amix/$f
	fi
	neg=`sed -n '/^negative:/,/^---/s/^ *type: *//p' $f`
	../qjs $t > ../out.txt 2>&1
	r=$?
	if [ -n "$neg" ]; then
		if [ $r -ne 0 ] && grep "$neg" ../out.txt > /dev/null; then ok=1; else ok=0; fi
	else
		if [ $r -eq 0 ]; then ok=1; else ok=0; fi
	fi
	if [ $ok = 1 ]; then pass=`expr $pass + 1`
	else fail=`expr $fail + 1`; echo "FAIL $f ($r)"; sed -n 1,3p ../out.txt; fi
done
rm -rf ../out.txt amix
echo "pass $pass fail $fail skip $skip"
