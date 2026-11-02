/* amix-ssh.h -- forced into every file: what AMIX's headers lack */
#define BIT_ZERO_ON_LEFT
#define BIG_ENDIAN 4321
#define LITTLE_ENDIAN 1234
#define BYTE_ORDER 4321
#define _PATH_MAILDIR "/var/mail"
#define SSIZE_MAX 2147483647
#define S_ISSOCK(m) (((m) & 0170000) == 0140000)
#define connect amix_connect
/* what configure gives *-*-sysv4.2*, which --host=m68k-elf never reaches:
   SVR4's setuid() after seteuid() away from root fails, and so on */
#define USE_PIPES 1
#define SETEUID_BREAKS_SETUID 1
#define BROKEN_SETREUID 1
#define BROKEN_SETREGID 1
#define PASSWD_NEEDS_USERNAME 1
#define LOCKED_PASSWD_STRING "*LK*"
/* SVR4 STREAMS ptys: /dev/ptmx and /dev/pts/N (configure cannot look on a cross build) */
#define HAVE_DEV_PTMX 1
/* pushing ptem makes the pty sshd's own controlling tty, so the shell gets
   none ("no job control"): a sacrificial pty first, OpenSSH bugzilla #245 */
#define SSHD_ACQUIRES_CTTY 1
#define dirname amix_dirname
