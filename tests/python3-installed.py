"""Run the installed interpreter with PYTHONHOME/PYTHONPATH unset."""
import os
from pathlib import Path
import site
import sys
import sysconfig

assert "PYTHONHOME" not in os.environ
assert "PYTHONPATH" not in os.environ
assert sys.prefix == sys.exec_prefix == sys.base_prefix == "/usr/local", sys.prefix
assert Path(sys.executable).resolve() == Path("/usr/local/bin/python3.11")
assert sysconfig.get_path("stdlib") == "/usr/local/lib/python3.11"
assert sysconfig.get_path("include") == "/usr/local/include/python3.11"
assert sysconfig.get_config_var("prefix") == "/usr/local"
assert Path(sysconfig.get_makefile_filename()).is_file()
assert Path(sysconfig.get_config_h_filename()).is_file()
assert Path("/usr/local/include/python3.11/Python.h").is_file()
assert Path("/usr/local/lib/libpython3.11.a").is_file()
assert "/usr/local/lib/python3.11/site-packages" in site.getsitepackages()
print("PYTHON_INSTALLED_PREFIX_PASS", sys.prefix, sys.executable)
