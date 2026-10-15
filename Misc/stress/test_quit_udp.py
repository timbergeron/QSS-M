"""Exercise real browser UDP queries against delayed and silent local servers.

Run with python3 Misc/stress/test_quit_udp.py (cc + SDL3, or Visual Studio on Windows).
Checks normal deadlines, delayed replies, and cancellation of concurrent queries.
"""

from pathlib import Path
import os
import shlex
import shutil
import socketserver
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
host = (ROOT / "Quake/host_cmd.c").read_text()
common = (ROOT / "Quake/common.c").read_text()
compare_start = common.index("int q_strcasecmp(")
compare = common[compare_start:common.index("\n}", compare_start) + 2]
start = host.index("static SDL_AtomicInt server_queries_abort;")
end = host.index("\n}\n", host.index("char *UDP_QueryPlayers(", start)) + 3

QUERY_SOURCE = r'''
#include "quakedef.h"
#include "arch_def.h"
#include "net_sys.h"
#include "net_defs.h"
#include "q_ctype.h"
#ifdef _WIN32
#include "wsaerror.h"
#endif
#include <assert.h>
int net_hostport = 26000, DEFAULTnet_hostport = 26000;
double Sys_DoubleTime(void) {
    return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}
void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
int q_snprintf(char *out, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    int result = vsnprintf(out, size, fmt, args); va_end(args); return result;
}
static int network_order(int value) { return (int)htonl((unsigned int)value); }
int (*BigLong)(int) = network_order;
''' + compare + "\n" + host[start:end]

SOURCE = QUERY_SOURCE + r'''

typedef struct {
    const char *address;
    qboolean players;
    SDL_AtomicInt started;
    int ping;
    char *names;
} query_t;

static int query_worker(void *data) {
    query_t *query = data;
    SDL_SetAtomicInt(&query->started, 1);
    if (query->players) query->names = UDP_QueryPlayers(query->address, 16);
    else query->ping = UDP_Ping_Host(query->address);
    return 0;
}

int main(int argc, char **argv) {
    assert(argc == 4);
#ifdef _WIN32
    WSADATA winsock;
    assert(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
#endif
    assert(SDL_Init(0));
    double start = Sys_DoubleTime();
    assert(UDP_Ping_Host(argv[1]) == -1);
    double ping_deadline = Sys_DoubleTime() - start;
    assert(ping_deadline >= 1.4 && ping_deadline < 2.5);
    start = Sys_DoubleTime();
    assert(!UDP_QueryPlayers(argv[1], 2));
    double player_deadline = Sys_DoubleTime() - start;
    assert(player_deadline >= 0.25 && player_deadline < 1.0);

    char resolved[256];
    start = Sys_DoubleTime();
    assert(UDP_Ping_HostResolved(argv[2], resolved, sizeof(resolved)) >= 0);
    assert(Sys_DoubleTime() - start >= 0.1); /* reply arrives after multiple slices */
    assert(!strcmp(resolved, argv[2]));
    char *names = UDP_QueryPlayers(argv[3], 2);
    assert(names && !strcmp(names, "alice (7), bob (-99)"));
    free(names);

    enum { WORKERS = 4 };
    query_t queries[WORKERS] = {0};
    SDL_Thread *threads[WORKERS];
    for (int i = 0; i < WORKERS; ++i) {
        queries[i].address = argv[1];
        queries[i].players = i & 1;
        threads[i] = SDL_CreateThread(query_worker, "udp-query-test", &queries[i]);
        assert(threads[i]);
    }
    start = Sys_DoubleTime();
    for (int i = 0; i < WORKERS; ++i) {
        while (!SDL_GetAtomicInt(&queries[i].started)) {
            assert(Sys_DoubleTime() - start < 2);
            SDL_Delay(1);
        }
    }
    SDL_Delay(100);
    start = Sys_DoubleTime();
    NET_CancelServerQueries();
    NET_CancelServerQueries();
    for (int i = 0; i < WORKERS; ++i) {
        SDL_WaitThread(threads[i], NULL);
        if (queries[i].players) assert(!queries[i].names);
        else assert(queries[i].ping == -1);
    }
    double canceled = Sys_DoubleTime() - start;
    assert(canceled < 0.5);
    start = Sys_DoubleTime();
    assert(UDP_Ping_Host("do-not-resolve.invalid:26000") == -1);
    assert(!UDP_QueryPlayers("do-not-resolve.invalid:26000", 16));
    assert(Sys_DoubleTime() - start < 0.1);
    SDL_Quit();
    printf("PASS: normal ping/player deadlines, delayed replies, resolved address, concurrent cancellation, late queries\n");
    printf("Silent server: ping %.0f ms; players %.0f ms; four canceled queries joined in %.1f ms\n",
        ping_deadline * 1000, player_deadline * 1000, canceled * 1000);
    return 0;
}
'''


class Server(socketserver.ThreadingUDPServer):
    daemon_threads = True


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        packet, sock = self.request
        mode = self.server.mode
        if mode == "silent":
            return
        if mode == "ping":
            if len(packet) < 5 or packet[4] != 2:
                return
            time.sleep(0.12)
            body = b"\x83fixture:26000\0fixture\0start\0" + bytes([0, 16, 3])
            sock.sendto(struct.pack("!I", 0x80000000 | (len(body) + 4)) + body,
                        self.client_address)
        elif len(packet) >= 6 and packet[4] == 3:
            slot = packet[5]
            if slot > 1:
                return
            time.sleep(0.12 + slot * 0.14)
            name, frags = (b"alice", 7) if slot == 0 else (b"bob", -99)
            body = bytes([0x84, slot]) + name + b"\0" + struct.pack("!ii", 0, frags)
            sock.sendto(struct.pack("!I", 0x80000000 | (len(body) + 4)) + body,
                        self.client_address)


def compile_query_test(source_text, path):
    source = path / "test.c"
    binary = path / ("test.exe" if os.name == "nt" else "test")
    source.write_text(source_text)
    if os.name == "nt":
        vs = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Microsoft Visual Studio"
        devcmds = sorted(vs.glob("*/*/Common7/Tools/VsDevCmd.bat"))
        if not devcmds:
            raise RuntimeError("Visual Studio C compiler not found")
        build = path / "build.cmd"
        build.write_text(
            '@echo off\n'
            f'call "{devcmds[-1]}" -arch=x64 -host_arch=x64 >nul\n'
            'if errorlevel 1 exit /b %errorlevel%\n'
            f'cl /nologo /O2 /Gy /std:c11 /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE '
            f'/I"{ROOT / "Quake"}" /I"{ROOT / "Windows/SDL3/include"}" '
            f'"{source}" "{ROOT / "Quake/net_address.c"}" "{ROOT / "Quake/strlcpy.c"}" '
            f'/Fe:"{binary}" /link /OPT:REF /LIBPATH:"{ROOT / "Windows/SDL3/lib/x64"}" SDL3.lib ws2_32.lib\n'
        )
        subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(build)],
                       cwd=path, check=True)
        shutil.copy2(ROOT / "Windows/SDL3/lib/x64/SDL3.dll", path / "SDL3.dll")
    else:
        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
        subprocess.run([os.environ.get("CC", "cc"), "-O2", "-std=gnu11",
                        "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                        "-ffunction-sections", "-Wl,--gc-sections", "-I", str(ROOT / "Quake"),
                        str(source), str(ROOT / "Quake/net_address.c"),
                        str(ROOT / "Quake/strlcpy.c"), "-o", str(binary), *flags], check=True)
    return binary


def main():
    build_root = ROOT / ".codex-build"
    build_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="quit-udp-", dir=build_root) as tmp:
        path = Path(tmp).resolve()
        assert path.is_relative_to(build_root.resolve())
        binary = compile_query_test(SOURCE, path)
        servers = []
        try:
            for mode in ("silent", "ping", "players"):
                server = Server(("127.0.0.1", 0), Handler)
                server.mode = mode
                servers.append(server)
                threading.Thread(target=server.serve_forever, daemon=True).start()
            subprocess.run([str(binary), *(f"127.0.0.1:{s.server_address[1]}" for s in servers)],
                           check=True, timeout=15)
        finally:
            for server in servers:
                server.shutdown()
                server.server_close()


if __name__ == "__main__":
    main()
