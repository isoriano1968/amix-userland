/* cxxcompat.h -- forced into every C++ compile of Blender 2.31a for AMIX
 * (build.py).  2003 C++ relied on headers arriving through others and on
 * iostream names without std::; gcc 14 provides neither by itself. */
#ifndef AMIX_CXXCOMPAT_H
#define AMIX_CXXCOMPAT_H
/* AMIX's C headers first, before any source file's own `using namespace
   std`: parsed after it, their size_t is ambiguous with std::size_t */
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* AMIX declares fd_set (sys/select.h) but no select(); libc has it */
extern "C" int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <fstream>
/* not `using namespace std`: AMIX's size_t is not gcc's std::size_t, and the
   using-directive makes every size_t ambiguous; only the iostream names */
using std::cout; using std::cerr; using std::endl; using std::flush;
using std::ostream; using std::istream; using std::ios;
using std::ifstream; using std::ofstream; using std::fstream;
#endif
