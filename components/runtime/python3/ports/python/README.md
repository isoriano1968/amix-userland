# CPython 3.11 AMIX application port

See [the measured results and build recipe](../../docs/PORT-NOTES.md).

The adapters here are private to the cross-built Python executable. Do not
install their headers into the system include directory. GNU Pth remains the
threading implementation; these files address the older AMIX C interface.

`amix-math.c` derives some fallbacks from CPython, under the Python Software
Foundation licence in [LICENSE.PSF](LICENSE.PSF). The generated source patch
retains CPython's terms. Original adapters are available under the
[project's MIT licence](../../LICENSE).
