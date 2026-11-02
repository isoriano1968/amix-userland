/* getopt and getopt_long for AMIX: its getopt has no optional arguments */
#include <stdio.h>
#include <string.h>
#include <stddef.h>

char *optarg;
int optind = 1, opterr = 1, optopt;
static const char *next;
struct amix_option { const char *name; int has_arg; int *flag; int val; };

static int
short_opt(int argc, char *const *argv, const char *opts)
{
	const char *p;

	if (*opts == '+' || *opts == '-')
		opts++;
	if (!next || !*next) {
		const char *a = optind < argc ? argv[optind] : 0;

		if (!a || a[0] != '-' || !a[1])
			return -1;
		next = a + 1;
		optind++;
	}
	optopt = *next++;
	p = optopt == ':' ? 0 : strchr(opts, optopt);
	if (!p) {
		if (opterr)
			fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], optopt);
		return '?';
	}
	optarg = 0;
	if (p[1] == ':') {
		if (*next) {
			optarg = (char *)next;
			next = 0;
		} else if (p[2] != ':') {
			if (optind >= argc) {
				if (opterr)
					fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], optopt);
				return *opts == ':' ? ':' : '?';
			}
			optarg = argv[optind++];
		}
	}
	return optopt;
}

int
getopt(int argc, char *const *argv, const char *opts)
{
	return short_opt(argc, argv, opts);
}

int
getopt_long(int argc, char *const *argv, const char *opts, const struct amix_option *lo, int *idx)
{
	const char *a = (!next || !*next) && optind < argc ? argv[optind] : 0;

	if (a && a[0] == '-' && a[1] == '-') {
		const char *eq = strchr(a + 2, '=');
		size_t n = eq ? (size_t)(eq - a - 2) : strlen(a + 2);
		int i, found = -1;

		optind++;
		if (!a[2])
			return -1;
		for (i = 0; lo[i].name; i++)
			if (!strncmp(lo[i].name, a + 2, n)) {
				found = i;
				if (!lo[i].name[n])
					break;
			}
		if (found < 0) {
			if (opterr)
				fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], a);
			return '?';
		}
		optarg = 0;
		if (eq)
			optarg = (char *)eq + 1;
		else if (lo[found].has_arg == 1 && optind < argc)
			optarg = argv[optind++];
		if (idx)
			*idx = found;
		if (lo[found].flag) {
			*lo[found].flag = lo[found].val;
			return 0;
		}
		return lo[found].val;
	}
	return short_opt(argc, argv, opts);
}
