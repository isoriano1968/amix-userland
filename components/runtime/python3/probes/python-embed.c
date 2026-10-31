/* Check the installed headers/config helper/static embedding libraries. */
#include <Python.h>

int main(void)
{
    int result;
    Py_Initialize();
    result = PyRun_SimpleString("import sys, json\n"
                               "assert sys.prefix == '/usr/local', sys.prefix\n"
                               "assert json.loads('{\"value\": 42}')['value'] == 42\n"
                               "print('PYTHON_EMBED_SDK_PASS', sys.prefix)\n");
    if (Py_FinalizeEx() < 0) return 120;
    return result != 0;
}
