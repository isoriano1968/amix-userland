/*
 * net_c1.c -- ioquake3's network on the Z3660's core 1, in place of
 * qcommon/net_ip.c: core 1 has no sockets here, so only the loopback
 * between the image's own client and server (net_chan.c does that part).
 * NET_Sleep is where the engine waits for its next frame: it takes the
 * 68k's commands meanwhile.
 */
#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "c1q3.h"

void sys_c1_poll(void);

void NET_Init(void)
{
    Cmd_AddCommand("net_restart", NET_Restart_f);
}

void NET_Shutdown(void)
{
}

void NET_Restart_f(void)
{
}

void NET_Config(qboolean enableNetworking)
{
    (void)enableNetworking;
}

void NET_Sleep(int msec)
{
    if (msec < 0)
        msec = 0;
    sys_c1_poll();
    if (msec > 0)
        c1p_sleep_us((unsigned)msec * 1000u);
}

void Sys_SendPacket(int length, const void *data, netadr_t to)
{
    (void)length; (void)data; (void)to;
}

qboolean Sys_StringToAdr(const char *s, netadr_t *a, netadrtype_t family)
{
    (void)family;
    Com_Memset(a, 0, sizeof(*a));
    if (!Q_stricmp(s, "localhost") || !strcmp(s, "127.0.0.1")) {
        a->type = NA_LOOPBACK;
        return qtrue;
    }
    return qfalse;
}

qboolean NET_CompareBaseAdrMask(netadr_t a, netadr_t b, int netmask)
{
    (void)netmask;
    return a.type == b.type && (a.type == NA_LOOPBACK || a.type == NA_BOT ||
                                (a.type == NA_IP && !memcmp(a.ip, b.ip, 4)));
}

qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    return NET_CompareBaseAdrMask(a, b, -1);
}

qboolean NET_CompareAdr(netadr_t a, netadr_t b)
{
    if (!NET_CompareBaseAdr(a, b))
        return qfalse;
    return a.type == NA_LOOPBACK || a.type == NA_BOT || a.port == b.port;
}

qboolean NET_IsLocalAddress(netadr_t adr)
{
    return adr.type == NA_LOOPBACK;
}

const char *NET_AdrToString(netadr_t a)
{
    static char s[NET_ADDRSTRMAXLEN];

    switch (a.type) {
    case NA_LOOPBACK:
        return "loopback";
    case NA_BOT:
        return "bot";
    case NA_IP:
        Com_sprintf(s, sizeof(s), "%d.%d.%d.%d", a.ip[0], a.ip[1], a.ip[2], a.ip[3]);
        return s;
    default:
        return "unknown";
    }
}

const char *NET_AdrToStringwPort(netadr_t a)
{
    static char s[NET_ADDRSTRMAXLEN];

    if (a.type != NA_IP)
        return NET_AdrToString(a);
    Com_sprintf(s, sizeof(s), "%s:%hu", NET_AdrToString(a), (unsigned short)BigShort(a.port));
    return s;
}

qboolean Sys_IsLANAddress(netadr_t adr)
{
    return adr.type == NA_LOOPBACK;
}

void Sys_ShowIP(void)
{
    Com_Printf("no network on core 1 (loopback only)\n");
}

void NET_JoinMulticast6(void)
{
}

void NET_LeaveMulticast6(void)
{
}
