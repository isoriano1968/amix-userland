/*
 * amixcast.c -- streaming on AMIX in a CDE window: the folders of
 * amixcast.py on the PC (H.264 + AAC transcoded there) and of the DLNA
 * servers on the network (found and browsed with libupnp, dlna.c), and
 * ffplay on what is picked, the Z3660's core 1 showing H.264.  A DLNA
 * server's file plays as it is, from the server.
 * The tree is a list: "+" and "-" in front of the folders, opened on demand.
 *   amixcast [host [port]]     default: $HOME/.amixcast, else 192.168.0.171 8092
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ToggleB.h>
#include "dlna.h"

#define FFPLAY      "/opt/amix/bin/ffplay"
#define MAXNODE     8000
#define REPLY_MAX   (256 * 1024)

extern int socket(), connect();

struct node {
    char *id, *title;
    char *url;                  /* a DLNA server's file: where it is */
    char kind;                  /* d folder, v a i ? file */
    int secs, depth, open;
    int src;                    /* -1 the PC's amixcast.py, else a DLNA server */
};

static XtAppContext app;
static Widget list, status, playb;
static struct node node[MAXNODE];
static int nnode;
static char host[64] = "192.168.0.171";
static int port = 8092;
static int kbit = 0;            /* 0: measured */
static int auto_kbit;

static void say(const char *s)
{
    XmString x = XmStringCreateLocalized((char *)s);

    XtVaSetValues(status, XmNlabelString, x, NULL);
    XmStringFree(x);
    XmUpdateDisplay(status);
}

/* GET path: the body into a static buffer, its length in *len; NULL on failure */
static char *get(const char *path, int *len)
{
    static char *buf;
    struct sockaddr_in sa;
    char req[512], *body;
    int s, n, total = 0;

    if (!buf && !(buf = malloc(REPLY_MAX + 1)))
        return NULL;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr.s_addr = inet_addr(host);
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0 || connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) {
        if (s >= 0)
            close(s);
        return NULL;
    }
    sprintf(req, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    write(s, req, strlen(req));
    while (total < REPLY_MAX && (n = read(s, buf + total, REPLY_MAX - total)) > 0)
        total += n;
    close(s);
    buf[total] = 0;
    if (strncmp(buf, "HTTP/", 5) || !strstr(buf, " 200 ") || !(body = strstr(buf, "\r\n\r\n")))
        return NULL;
    body += 4;
    if (len)
        *len = total - (int)(body - buf);
    return body;
}

/* what the list shows for a node */
static XmString row(int i)
{
    static const char *const mark[] = { "[-] ", "[+] " };
    char line[400];
    int k, o = 0;

    for (k = 0; k < node[i].depth && o < 80; k++, o += 3)
        memcpy(line + o, "   ", 3);
    if (node[i].kind == 'd')
        sprintf(line + o, "%s%.300s", mark[!node[i].open], node[i].title);
    else if (node[i].secs)
        sprintf(line + o, "    %.300s  (%d:%02d)", node[i].title, node[i].secs / 60, node[i].secs % 60);
    else
        sprintf(line + o, "    %.300s", node[i].title);
    return XmStringCreateLocalized(line);
}

static void node_insert(int at, const struct node *n)
{
    memmove(&node[at + 1], &node[at], (size_t)(nnode - at) * sizeof node[0]);
    node[at] = *n;
    nnode++;
}

static void node_free(int k)
{
    free(node[k].id);
    free(node[k].title);
    free(node[k].url);
}

/* a DLNA server's folder entry, inserted below its folder */
struct addctx {
    int at, depth, src, n;
};

static void dlna_add(void *ctx, char kind, const char *id, const char *title, int secs, const char *url)
{
    struct addctx *a = ctx;
    struct node c;
    XmString x;

    if (nnode >= MAXNODE - 1)
        return;
    memset(&c, 0, sizeof c);
    c.kind = kind;
    c.id = strdup(id);
    c.title = strdup(title);
    c.url = url ? strdup(url) : NULL;
    c.secs = secs;
    c.depth = a->depth;
    c.src = a->src;
    node_insert(a->at + a->n, &c);
    x = row(a->at + a->n);
    XmListAddItemUnselected(list, x, a->at + a->n + 1);
    XmStringFree(x);
    a->n++;
}

/* the children of node i below it: the PC's list or a DLNA server's folder */
static int fill(int i)
{
    char path[128], *p, *e;
    const int depth = node[i].depth + 1;
    int at = i + 1, first = at, n = 0;
    XmString x;

    if (node[i].src >= 0) {
        struct addctx a;

        a.at = at;
        a.depth = depth;
        a.src = node[i].src;
        a.n = 0;
        return dlna_browse(node[i].src, node[i].id, dlna_add, &a) < 0 ? -1 : a.n;
    }
    sprintf(path, "/list?id=%.100s", node[i].id);
    if (!(p = get(path, NULL)))
        return -1;
    for (; *p && nnode < MAXNODE - 1; p = e + 1) {
        struct node c;
        char *f[5];
        int k = 0;

        if (!(e = strchr(p, '\n')))
            break;
        *e = 0;
        f[0] = p;
        while (k < 4 && (f[k + 1] = strchr(f[k], '\t')) != NULL) {
            *f[k + 1]++ = 0;
            k++;
        }
        memset(&c, 0, sizeof c);
        c.depth = depth;
        c.src = -1;
        if (f[0][0] == 'd' && k == 2) {
            c.kind = 'd';
            c.id = strdup(f[1]);
            c.title = strdup(f[2]);
        } else if (f[0][0] == 'f' && k == 4) {
            c.kind = f[3][0];
            c.id = strdup(f[1]);
            c.secs = atoi(f[2]);
            c.title = strdup(f[4]);
        } else
            continue;
        node_insert(at + n, &c);
        x = row(at + n);
        XmListAddItemUnselected(list, x, at + n + 1);
        XmStringFree(x);
        n++;
    }
    (void)first;
    return n;
}

static void toggle(int i)
{
    XmString x;
    int e, k;

    if (node[i].open) {
        for (e = i + 1; e < nnode && node[e].depth > node[i].depth; e++)
            ;
        for (k = i + 1; k < e; k++)
            node_free(k);
        if (e > i + 1) {
            XmListDeleteItemsPos(list, e - i - 1, i + 2);
            memmove(&node[i + 1], &node[e], (size_t)(nnode - e) * sizeof node[0]);
            nnode -= e - i - 1;
        }
        node[i].open = 0;
    } else {
        say("Reading the folder ...");
        k = fill(i);
        if (k < 0) {
            say(node[i].src < 0 ? "The PC does not answer: is amixcast.py running?"
                                : "The DLNA server does not answer");
            return;
        }
        node[i].open = 1;
        say(k ? " " : "The folder is empty");
    }
    x = row(i);
    XmListReplaceItemsPos(list, &x, 1, i + 1);
    XmStringFree(x);
    XmListSelectPos(list, i + 1, False);
}

/* the line's speed, kbit/s of what can be played on it (with a margin) */
static int measure(void)
{
    struct timeval a, b;
    double t;
    int len = 0, k;

    say("Measuring the network ...");
    gettimeofday(&a, NULL);
    if (!get("/probe", &len) || len <= 0)
        return 600;
    gettimeofday(&b, NULL);
    t = (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_usec - a.tv_usec) / 1e6;
    k = (int)((double)len * 8.0 / 1000.0 / (t > 0.01 ? t : 0.01) * 0.6);
    return k < 300 ? 300 : k > 4000 ? 4000 : k;
}

static void play(int i)
{
    char url[256], path[128], msg[400], *p;
    int k, secs = node[i].secs, pid;

    if (node[i].kind == 'd') {
        toggle(i);
        return;
    }
    if (node[i].src >= 0) {
        /* from the DLNA server as it is: H.264 goes to core 1, the rest
           ffplay decodes on the 060 */
        if (node[i].kind != 'v' && node[i].kind != 'a') {
            say("Only videos and music so far");
            return;
        }
        sprintf(msg, "Playing from %.60s: %.300s", dlna_name(node[i].src), node[i].title);
        say(msg);
        pid = fork();
        if (pid == 0) {
            execl(FFPLAY, "ffplay", "-nostats", "-autoexit", "-window_title", node[i].title, node[i].url,
                  (char *)NULL);
            _exit(1);
        }
        if (pid < 0)
            say("Cannot start ffplay");
        return;
    }
    if (node[i].kind != 'v') {
        say("Only videos so far");
        return;
    }
    k = kbit;
    if (!k) {
        if (!auto_kbit)
            auto_kbit = measure();
        k = auto_kbit;
    }
    if (!secs) {
        say("Asking for its length ...");
        sprintf(path, "/info?id=%.100s", node[i].id);
        if ((p = get(path, NULL)) != NULL)
            secs = node[i].secs = atoi(p);
    }
    sprintf(url, "http://%s:%d/stream?id=%.100s&kbit=%d&d=%d&t=0", host, port, node[i].id, k, secs);
    sprintf(msg, "Playing at %d kbit/s: %.300s", k, node[i].title);
    say(msg);
    pid = fork();
    if (pid == 0) {
        execl(FFPLAY, "ffplay", "-nostats", "-autoexit", "-window_title", node[i].title, url, (char *)NULL);
        _exit(1);
    }
    if (pid < 0)
        say("Cannot start ffplay");
}

static int picked(void)
{
    int *pos, n, i;

    if (!XmListGetSelectedPos(list, &pos, &n))
        return -1;
    i = n ? pos[0] - 1 : -1;
    XtFree((char *)pos);
    return i;
}

static void defaction_cb(Widget w, XtPointer c, XtPointer d)
{
    XmListCallbackStruct *l = (XmListCallbackStruct *)d;

    play(l->item_position - 1);
}

static void play_cb(Widget w, XtPointer c, XtPointer d)
{
    const int i = picked();

    if (i >= 0)
        play(i);
    else
        say("Pick a video first");
}

static void rate_cb(Widget w, XtPointer c, XtPointer d)
{
    if (((XmToggleButtonCallbackStruct *)d)->set)
        kbit = (int)(long)c;
}

/* a folder at the top: the PC's amixcast.py (src -1) or a DLNA server */
static void top_add(int src, const char *title)
{
    struct node c;
    XmString x;

    memset(&c, 0, sizeof c);
    c.kind = 'd';
    c.id = strdup("0");
    c.title = strdup(title);
    c.src = src;
    node_insert(nnode, &c);
    x = row(nnode - 1);
    XmListAddItemUnselected(list, x, nnode);
    XmStringFree(x);
}

static void refresh_cb(Widget w, XtPointer c, XtPointer d)
{
    char t[200];
    int k, n;

    for (k = 0; k < nnode; k++)
        node_free(k);
    nnode = 0;
    XmListDeleteAllItems(list);
    sprintf(t, "PC: amixcast.py at %.60s:%d (transcoded)", host, port);
    top_add(-1, t);
    say("Looking for DLNA servers ...");
    n = dlna_search(3);
    for (k = 0; k < n; k++) {
        sprintf(t, "DLNA: %.150s", dlna_name(k));
        top_add(k, t);
    }
    /* the PC's folders open, as before the DLNA servers came */
    say("Reading the PC's list ...");
    k = fill(0);
    if (k >= 0) {
        XmString x;

        node[0].open = 1;
        x = row(0);
        XmListReplaceItemsPos(list, &x, 1, 1);
        XmStringFree(x);
    }
    sprintf(t, "%s%s%d DLNA server%s.  Double-click a folder to open it, a file to play it",
            k < 0 ? "The PC does not answer.  " : "", n < 0 ? "No libupnp: " : "",
            n < 0 ? 0 : n, n == 1 ? "" : "s");
    say(t);
}

static void quit_cb(Widget w, XtPointer c, XtPointer d)
{
    exit(0);
}

static void config(void)
{
    char path[256];
    const char *home = getenv("HOME");
    FILE *f;

    if (!home)
        return;
    sprintf(path, "%.240s/.amixcast", home);
    if ((f = fopen(path, "r")) != NULL) {
        fscanf(f, "%63s %d", host, &port);
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    static const struct { const char *name; int k; } rates[] = {
        { "Auto", 0 }, { "400", 400 }, { "600", 600 }, { "800", 800 }, { "1000", 1000 }, { "1500", 1500 }
    };
    Widget top, form, rc, row1, b;
    Arg a[10];
    int n, i;

    signal(SIGCHLD, SIG_IGN);           /* no zombies from ffplay */
    config();
    top = XtVaAppInitialize(&app, "Amixcast", NULL, 0, &argc, argv, NULL,
                            XmNtitle, "AmixCast", XmNiconName, "AmixCast", NULL);
    if (argc > 1)
        strncpy(host, argv[1], sizeof host - 1);
    if (argc > 2)
        port = atoi(argv[2]);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top,
                                   XmNwidth, 640, XmNheight, 520, NULL);
    row1 = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
                                   XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, NULL);
    playb = XtVaCreateManagedWidget("Play", xmPushButtonWidgetClass, row1, NULL);
    XtAddCallback(playb, XmNactivateCallback, play_cb, NULL);
    b = XtVaCreateManagedWidget("Reload", xmPushButtonWidgetClass, row1, NULL);
    XtAddCallback(b, XmNactivateCallback, refresh_cb, NULL);
    b = XtVaCreateManagedWidget("Quit", xmPushButtonWidgetClass, row1, NULL);
    XtAddCallback(b, XmNactivateCallback, quit_cb, NULL);
    status = XtVaCreateManagedWidget(" ", xmLabelWidgetClass, form, XmNalignment, XmALIGNMENT_BEGINNING,
                                     XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, row1,
                                     XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, NULL);
    rc = XtVaCreateManagedWidget("rate", xmRowColumnWidgetClass, form, XmNorientation, XmHORIZONTAL,
                                 XmNradioBehavior, True, XmNbottomAttachment, XmATTACH_WIDGET,
                                 XmNbottomWidget, status, XmNleftAttachment, XmATTACH_FORM, NULL);
    XtVaCreateManagedWidget("kbit/s:", xmLabelWidgetClass, rc, NULL);
    for (i = 0; i < (int)(sizeof rates / sizeof rates[0]); i++) {
        b = XtVaCreateManagedWidget(rates[i].name, xmToggleButtonWidgetClass, rc,
                                    XmNset, i == 0 ? XmSET : XmUNSET, NULL);
        XtAddCallback(b, XmNvalueChangedCallback, rate_cb, (XtPointer)(long)rates[i].k);
    }
    n = 0;
    XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    /* scroll bars both ways: long file names scroll, the window stays */
    XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    XtSetArg(a[n], XmNlistSizePolicy, XmCONSTANT); n++;
    XtSetArg(a[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNbottomWidget, rc); n++;
    list = XmCreateScrolledList(form, "tree", a, n);
    XtManageChild(list);
    XtAddCallback(list, XmNdefaultActionCallback, defaction_cb, NULL);
    XtRealizeWidget(top);
    refresh_cb(NULL, NULL, NULL);
    XtAppMainLoop(app);
    return 0;
}
