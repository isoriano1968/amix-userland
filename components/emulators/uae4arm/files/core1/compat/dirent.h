/* dirent.h for core 1 (bare metal, newlib has none): no directories there.
   opendir fails, so the emulator's directory scans find nothing; files come
   one by one by name (c1_syscalls.c) */
#ifndef C1_DIRENT_H
#define C1_DIRENT_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
struct dirent { ino_t d_ino; unsigned char d_type; char d_name[256]; };
typedef struct c1_dir DIR;
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);
void rewinddir(DIR *);
#ifdef __cplusplus
}
#endif
#endif
