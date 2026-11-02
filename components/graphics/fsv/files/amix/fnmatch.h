/* fnmatch.h -- AMIX's libc has no fnmatch(); fsv's wildcard colors need it */
#ifndef FSV_AMIX_FNMATCH_H
#define FSV_AMIX_FNMATCH_H
#define FNM_NOMATCH	1
#define FNM_NOESCAPE	0x01
#define FNM_PATHNAME	0x02
#define FNM_PERIOD	0x04
#define FNM_FILE_NAME	FNM_PATHNAME
int fnmatch( const char *pattern, const char *string, int flags );
#endif
