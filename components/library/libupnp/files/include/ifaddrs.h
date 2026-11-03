/* ifaddrs.h for AMIX: getifaddrs() on SVR4's SIOCGIFCONF (amix-upnp.c) */
#ifndef AMIX_IFADDRS_H
#define AMIX_IFADDRS_H
#include <net/if.h>
#undef ifa_broadaddr		/* net/if.h's struct ifaddr macros */
#undef ifa_dstaddr
#ifndef IFF_MULTICAST
#define IFF_MULTICAST 0x8000	/* not in AMIX's net/if.h: given to broadcast
				   interfaces, which take the searches */
#endif
struct ifaddrs {
	struct ifaddrs *ifa_next;
	char *ifa_name;
	unsigned int ifa_flags;
	struct sockaddr *ifa_addr;
	struct sockaddr *ifa_netmask;
	struct sockaddr *ifa_broadaddr;
	void *ifa_data;
};
int getifaddrs(struct ifaddrs **);
void freeifaddrs(struct ifaddrs *);
#endif
