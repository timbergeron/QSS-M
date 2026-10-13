"""Exercise production server-switch/disconnect packets and endpoint parsing.

Catches lost chat at disconnect, announcements on reconnect/signon/demo/quit,
missing destination ports, and command injection through a destination string.
Run: python3 Misc/stress/test_announce_transfer.py (requires cc).
"""

from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
client = (ROOT / "Quake/cl_main.c").read_text()
address = (ROOT / "Quake/net_address.c").read_text()


def function(declaration):
    start = client.index(declaration)
    return client[start:client.index("\n}", start) + 2] + "\n"


source = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
typedef enum { false, true } qboolean;
#define NET_NAMELEN 128
#define SIGNONS 4
#define SDL_assert assert
#define q_strcasecmp strcasecmp
#define q_strncasecmp strncasecmp
#include "q_ctype.h"
#include "net_address.h"
#include "net_ws.h"
static size_t q_strlcpy(char *out, const char *in, size_t size) {
    size_t len = strlen(in), n = len < size - 1 ? len : size - 1;
    memcpy(out, in, n); out[n] = 0; return len;
}
static int q_snprintf(char *out, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int n = vsnprintf(out, size, fmt, args); va_end(args); return n;
}
static char *va(const char *fmt, ...) {
    static char out[1024]; va_list args; va_start(args, fmt);
    vsnprintf(out, sizeof(out), fmt, args); va_end(args); return out;
}
'''
source += re.sub(r'^#include .*\n', '', address, flags=re.M)
source += r'''
enum { ca_disconnected, ca_connected, ca_dedicated, key_message = 9 };
enum { clc_disconnect = 2, clc_stringcmd = 4 };
typedef struct { float value; } cvar_t;
typedef struct { unsigned char data[1024]; int cursize; } sizebuf_t;
struct qsocket_s { char resolved[256]; };
static struct qsocket_s old_socket;
static struct {
    int state, signon, demonum, demoplayback, demorecording, timedemo;
    int demoreelplayback, demopaused, netdrops_request_time;
    int demo_had_overtime, demo_marker_count, demo_record_frame_count;
    struct qsocket_s *netcon;
    sizebuf_t message;
    char userinfo[64];
    struct { FILE *file; int chunked, percent; char temp[64]; } download;
} cls;
static struct { int maxclients, modtype, intermission, sendprespawn, matchinp;
    double match_pause_time; void *worldmodel; } cl;
static struct { int active; } sv;
static struct { int active; char host[128]; } cl_pending_connect;
static cvar_t cl_announce_transfer;
static int net_hostport = 26000, key_dest, netquakeio, pausedprint, countdown;
static int prediction_msg_shown, cl_portpingprobe_reconnecting;
static char cl_portpingprobe_pending_host[128], cl_portpingprobe_deferred_host[128];
static char lastconnected[3], lastmphost[128];
static sizebuf_t sent;
static int closes, probes;
static void SZ_Clear(sizebuf_t *msg) { msg->cursize = 0; }
static void MSG_WriteByte(sizebuf_t *msg, int byte) {
    assert(msg->cursize < (int)sizeof(msg->data)); msg->data[msg->cursize++] = byte;
}
static void MSG_WriteString(sizebuf_t *msg, const char *text) {
    do { MSG_WriteByte(msg, (unsigned char)*text); } while (*text++);
}
static int NET_SendUnreliableMessage(struct qsocket_s *sock, sizebuf_t *msg) {
    assert(sock == &old_socket); sent = *msg; return 1;
}
static void NET_Close(struct qsocket_s *sock) { assert(sock == &old_socket); ++closes; }
static const char *NET_QSocketGetResolvedAddressString(const struct qsocket_s *s) {
    return s ? s->resolved : "";
}
static const char *NET_ResolveCacheName(const char *host) { return host; }
static qboolean NET_HostnameCache_AddressesMatch(const char *a, const char *b, int port) {
    net_endpoint_t first, second;
    return NET_ParseEndpoint(a, port, &first) && NET_ParseEndpoint(b, port, &second)
        ? NET_EndpointEqual(&first, &second) : !q_strcasecmp(a, b);
}
static const char *CL_PrepareConnectHost(const char *host) { return host; }
static int CL_HandlePortPingProbe(const char *host) { (void)host; return probes; }
static int CL_ConsumeNextConnectFromMenu(void) { return false; }
static int CL_CancelConnectInternal(int clear) { (void)clear; return false; }
static struct qsocket_s *NET_ConnectNoSlist(const char *host, int skip) {
    (void)host; (void)skip; return NULL;
}
static int NET_DatagramConnectStart(const char *host) { (void)host; return true; }
static void CL_FinalizeConnection(struct qsocket_s *s, const char *h) { (void)s; (void)h; }
#define CL_ClearConnectReturnState() ((void)0)
#define CL_PrintConnectingMessage(h) ((void)(h))
#define CL_ConnectTimingBegin() ((void)0)
#define CL_PrintConnectFailureHints() ((void)0)
#define CL_Translate_Reset() ((void)0)
#define CL_ConnectTimingCancel() ((void)0)
#define CL_AsyncDownload_Cancel() ((void)0)
#define NET_PortPingProbe_RequestAbort() ((void)0)
#define CL_CancelConnect() ((void)0)
#define CL_ClearTypingState() ((void)0)
#define Cvar_MapLock_RestoreAll() ((void)0)
#define V_MapScoped_RestoreServerStuff() ((void)0)
#define Key_EndChat() ((void)0)
#define S_StopAllSounds(a,b) ((void)0)
#define BGM_Pause() ((void)0)
#define CL_OptionalDownloadCache_ClearPending() ((void)0)
#define CL_StopPlayback() (cls.demoplayback = 0)
#define CL_StopRecording() ((void)0)
#define Con_DPrintf(...) ((void)0)
#define Host_ShutdownServer(a) ((void)0)
#define DL_FreeBlocks() ((void)0)
#define CL_ClearIgnoredChats() ((void)0)
#define Info_SetKey(...) ((void)0)
#define Cbuf_AddText(t) ((void)0)
#define V_ResetEffects() ((void)0)
#define unlink(t) ((void)0)
void CL_Disconnect(void);
'''

# Include the complete production disconnect section, including any new helper.
start = client.index("/*\n=====================\nCL_Disconnect\n")
source += client[start:client.index("void CL_Disconnect_f", start)]
source += function("qboolean CL_BeginConnect(")
menu = (ROOT / "Quake/menu.c").read_text()
start = menu.index("static enum console_e\n")
source += menu[start:menu.index("} console_cursor;", start) + len("} console_cursor;")]
source += "\n#define CONSOLE_ITEMS CONSOLE_COUNT\n"
for declaration in ("static int M_Console_GetItemY(", "static int M_Console_SearchBoxY(",
                    "static int M_Console_GetItemAtY("):
    start = menu.index(declaration)
    source += menu[start:menu.index("\n}", start) + 2] + "\n"
source += r'''
static void reset(void) {
    memset(&cls, 0, sizeof(cls)); memset(&cl, 0, sizeof(cl));
    memset(&sent, 0, sizeof(sent)); memset(&cl_pending_connect, 0, sizeof(cl_pending_connect));
    cls.state = ca_connected; cls.signon = SIGNONS; cls.netcon = &old_socket;
    cl.maxclients = 8; cl_announce_transfer.value = 1; sv.active = 0;
    strcpy(lastmphost, "old.example:26000"); strcpy(old_socket.resolved, "192.0.2.1:26000");
    closes = probes = 0;
    net_hostport = 26000;
    MSG_WriteByte(&cls.message, clc_stringcmd); MSG_WriteString(&cls.message, "status\n");
}
static void quiet(const char *host) {
    if (host) assert(CL_BeginConnect(host)); else CL_Disconnect();
    assert(sent.cursize == 1 && sent.data[0] == clc_disconnect && closes == 1);
}
static void announced(const char *host, const char *destination) {
    assert(CL_BeginConnect(host));
    assert(sent.cursize > 2 && sent.data[0] == clc_stringcmd);
    const char *chat = (const char *)sent.data + 1;
    assert(!strncmp(chat, "say \"moving to ", 15));
    assert(strstr(chat, destination));
    assert(!strstr(chat, "status"));
    assert(sent.data[sent.cursize - 1] == clc_disconnect);
    assert(sent.cursize == (int)strlen(chat) + 3 && closes == 1);
}
int main(void) {
    reset(); announced("new.example", "new.example:26000");
    reset(); announced("new.example:27500", "new.example:27500");
    reset(); announced("[2001:db8::2]:27500", "[2001:db8::2]:27500");
    reset(); announced("wss://new.example/quake", "wss://new.example/quake");
    reset(); cl_announce_transfer.value = 0; quiet("new.example");
    reset(); quiet(NULL);
    reset(); quiet("old.example");
    reset(); quiet("OLD.EXAMPLE.:26000");
    reset(); quiet("192.0.2.1");
    reset(); announced("old.example:27500", "old.example:27500");
    reset(); strcpy(lastmphost, "old.example"); net_hostport = 27500;
    announced("old.example:27500", "old.example:27500");
    reset(); strcpy(lastmphost, "old.example"); net_hostport = 27500;
    announced("old.example", "old.example:27500");
    reset(); strcpy(lastmphost, "old.example"); net_hostport = 27500;
    quiet("old.example:26000");
    reset(); strcpy(lastmphost, "[::1]:26000"); quiet("0:0:0:0:0:0:0:1");
    reset(); strcpy(lastmphost, "wss://old.example/quake"); quiet(lastmphost);
    reset(); cls.signon = 2; quiet("new.example");
    reset(); cl.maxclients = 1; quiet("new.example");
    reset(); sv.active = 1; quiet("new.example");
    reset(); quiet("local");
    reset(); quiet("new.example\";quit");
    reset(); quiet("wss://new.example/quake\nquit");
    reset(); quiet("wss://new.example/\";quit");
    reset(); probes = 1; assert(CL_BeginConnect("new.example"));
    assert(sent.cursize == 0 && closes == 0 && cls.state == ca_connected);
    probes = 0; announced("new.example", "new.example:26000");
    reset(); cls.demoplayback = 1; assert(!CL_BeginConnect("new.example"));
    assert(sent.cursize == 0 && closes == 0);
    reset(); cls.state = ca_disconnected; cls.netcon = NULL;
    assert(CL_BeginConnect("new.example") && sent.cursize == 0 && closes == 0);
    for (int i = 0; i < CONSOLE_ITEMS; ++i)
        assert(M_Console_GetItemAtY(M_Console_GetItemY(i) + 4) == i);
    assert(M_Console_SearchBoxY() >= M_Console_GetItemY(CONSOLE_CLEAR_HISTORY) + 8);
    assert(M_Console_SearchBoxY() + 24 <= 200);
    puts("PASS: transfer packet ordering, default-off, ports, reconnects, signon, demos, probe, injection");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-announce-transfer-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(source)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g",
                    "-fsanitize=address,undefined", "-I", str(ROOT / "Quake"),
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
