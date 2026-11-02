/* amix-curl.h -- forced into every compile: what SVR4 on AMIX lacks.  Its
   sys/socket.h has the constants and structures but declares no function. */
#ifndef AMIX_CURL_H
#define AMIX_CURL_H
#include <sys/types.h>
typedef int socklen_t;          /* SVR4's socket calls take an int */
struct sockaddr;
struct msghdr;
int socket(int, int, int);
int socketpair(int, int, int, int *);
int bind(int, const struct sockaddr *, int);
int connect(int, void *, int);          /* as curl's configure test declares it */
int listen(int, int);
int accept(int, struct sockaddr *, int *);
int recv(int, void *, int, int);
int recvfrom(int, void *, int, int, struct sockaddr *, int *);
int recvmsg(int, struct msghdr *, int);
int send(int, const void *, int, int);
int sendto(int, const void *, int, int, const struct sockaddr *, int);
int sendmsg(int, const struct msghdr *, int);
int getsockopt(int, int, int, void *, int *);
int setsockopt(int, int, int, const void *, int);
int getsockname(int, struct sockaddr *, int *);
int getpeername(int, struct sockaddr *, int *);
int shutdown(int, int);
int fchmod(int, mode_t);       /* in libc, not in sys/stat.h */
struct timeval;
int select(int, void *, void *, void *, struct timeval *);   /* in libc, declared nowhere */
#endif
