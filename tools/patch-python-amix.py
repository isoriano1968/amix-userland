"""Apply the initial, narrowly scoped CPython 3.11 AMIX platform changes."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
for name in ("configure", "configure.ac"):
    path = root / name
    text = path.read_text()
    if "# AMIX cross-build platform" in text:
        continue
    anchor = '\t*)\n\t\t# for now, limit cross builds to known configurations'
    if text.count(anchor) != 2:
        raise SystemExit("Unexpected CPython cross-platform cases: " + name)
    before, middle, after = text.split(anchor)
    def case(body):
        return ('\tm68k-*-sysv4*)\n'
                '\t\t# AMIX cross-build platform\n'
                '\t\t' + body + '\n\t\t;;\n' + anchor)
    text = (before + case("ac_sys_system=AmigaUNIX") + middle +
            case("_host_cpu=$host_cpu") + after)
    backup = path.with_name(name + ".amix-original")
    if not backup.exists():
        backup.write_text(path.read_text())
    path.write_text(text)
    print("AMIX cross-build cases added:", name)

path = root / "Python/pytime.c"
text = path.read_text()
marker = "amix_python_monotonic(tp)"
if marker not in text:
    anchor = "#elif defined(__hpux)\n"
    if text.count(anchor) != 1:
        raise SystemExit("Unexpected monotonic clock implementation")
    branch = '''#elif defined(__amix__)
    if (amix_python_monotonic(tp) < 0) {
        if (raise_exc) PyErr_SetFromErrno(PyExc_OSError);
        return -1;
    }
    if (info) {
        info->implementation = "AMIX times() elapsed ticks";
        info->resolution = 1.0 / amix_python_clock_hz();
        info->monotonic = 1;
        info->adjustable = 0;
    }

'''
    path.with_name(path.name + ".amix-original").write_text(text)
    path.write_text(text.replace(anchor, branch + anchor))
    print("AMIX monotonic clock branch added")

path = root / "Modules/posixmodule.c"
text = path.read_text()
anchor = "    long size = sysconf(_SC_TTY_NAME_MAX);"
if "AMIX has no _SC_TTY_NAME_MAX" not in text:
    if text.count(anchor) != 1:
        raise SystemExit("Unexpected ttyname buffer allocation")
    branch = '''#if defined(__amix__)
    /* AMIX has no _SC_TTY_NAME_MAX; ttyname_r checks the buffer bound. */
    long size = PATH_MAX;
#else
''' + anchor + "\n#endif"
    path.with_name(path.name + ".amix-original").write_text(text)
    path.write_text(text.replace(anchor, branch))
    print("AMIX ttyname buffer size added")

path = root / "Modules/zlibmodule.c"
text = path.read_text()
if "uint32_t *avail_out" in text:
    if text.count("uint32_t *avail_out") != 4:
        raise SystemExit("Unexpected zlib output-buffer signatures")
    path.with_name(path.name + ".amix-original").write_text(text)
    path.write_text(text.replace("uint32_t *avail_out", "uInt *avail_out"))
    print("Use zlib's actual uInt type for output-buffer pointers")
