/* sys/ioctl.h for core 1: no devices */
#ifndef C1_SYS_IOCTL_H
#define C1_SYS_IOCTL_H
static inline int ioctl(int fd, unsigned long r, ...) { (void)fd; (void)r; return -1; }
#endif
