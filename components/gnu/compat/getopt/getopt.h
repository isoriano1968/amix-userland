/* getopt.h -- getopt_long for AMIX: compat/amix-gnu.c */
#ifndef AMIX_GETOPT_H
#define AMIX_GETOPT_H
#include <stdio.h>
#include <unistd.h>
extern char *optarg;
extern int optind, opterr, optopt;
struct option { const char *name; int has_arg; int *flag; int val; };
#define no_argument 0
#define required_argument 1
#define optional_argument 2
int getopt_long(int, char *const *, const char *, const struct option *, int *);
#endif
