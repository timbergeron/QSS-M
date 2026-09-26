"""Exercise production MULTICAST_INIT routing and reliable frame delivery.

Run with: python3 Misc/stress/test_multicast_init.py
Compiles the real buffer, signon, multicast and reliable-update functions with
small server fixtures; requires a C compiler, but no game assets or network.
"""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
common = (ROOT / "Quake/common.c").read_text()
header = (ROOT / "Quake/common.h").read_text()
server = (ROOT / "Quake/sv_main.c").read_text()
server_h = (ROOT / "Quake/server.h").read_text()
extensions = (ROOT / "Quake/pr_ext.c").read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef enum { false, true } qboolean;
typedef unsigned char byte;
#define Q_memcpy memcpy
#define Con_Printf printf
static void fatal(const char *message, ...) {
    fprintf(stderr, "%s\n", message);
    exit(2);
}
#define Host_Error fatal
#define Sys_Error fatal
'''
source += re.search(r"typedef struct sizebuf_s.*?} sizebuf_t;", header, re.S)[0]
source += "\n" + re.search(r"^#define MAX_SIGNON_BUFFERS .*$", server_h, re.M)[0]
source += "\n" + re.search(r"^#define SIGNON_SIZE\s+.*$", server, re.M)[0]
source += "\n" + re.search(r"typedef enum multicast_e.*?} multicast_t;", extensions, re.S)[0]
source += r'''
enum { ss_loading, ss_active, svc_updatefrags };
typedef struct { int unused; } mleaf_t;
typedef struct { mleaf_t *leafs; } model_t;
typedef struct { struct { float origin[3], frags; } v; } edict_t;
typedef struct {
    qboolean active, pextknown, knowntoqc;
    unsigned int protocol_pext2;
    int old_frags;
    edict_t *edict;
    sizebuf_t message, datagram;
} client_t;
static struct {
    int state, num_signon_buffers;
    sizebuf_t *signon, *signon_buffers[MAX_SIGNON_BUFFERS];
    sizebuf_t multicast, reliable_datagram, datagram;
} sv;
static struct { int maxclients; client_t *clients; } svs;
static struct { model_t *worldmodel; } vm, *qcvm = &vm;
static client_t *host_client;
/* No visibility queries should occur for INIT/ALL_R. */
static mleaf_t *Mod_PointInLeaf(float *org, model_t *model) {
    (void)org; (void)model; fatal("unexpected visibility query"); return NULL;
}
static byte *Mod_LeafPVS(mleaf_t *leaf, model_t *model) {
    (void)leaf; (void)model; fatal("unexpected visibility query"); return NULL;
}
static void *Hunk_AllocName(size_t size, const char *name) {
    (void)name;
    void *memory = calloc(1, size);
    assert(memory);
    return memory;
}
'''
for declaration in (
    "void SZ_Clear (", "void *SZ_GetSpace (", "void SZ_Write (",
    "void MSG_WriteByte (", "void MSG_WriteShort (",
):
    source += function(common, declaration)
source += function(server, "static void SV_AddSignonBuffer (void)\n{")
source += function(server, "void SV_ReserveSignonSpace (int numbytes)\n{")
source += function(extensions, "static void PF_multicast_internal(")
source += function(extensions, "static void SV_Multicast(multicast_t to, float *org, int msg_entity, unsigned int requireext2)\n{")
source += function(server, "void SV_UpdateToReliableMessages (void)\n{")
source += r'''
static void expect_payload(sizebuf_t *buffer) {
    /* svc_print followed by "init\n" and its terminating NUL. */
    static const byte expected[] = {8, 'i', 'n', 'i', 't', '\n', 0};
    assert(buffer->cursize == sizeof(expected));
    assert(!memcmp(buffer->data, expected, sizeof(expected)));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int scenario = atoi(argv[1]);
    byte multicast[64] = {8, 'i', 'n', 'i', 't', '\n', 0};
    byte broadcast[64] = {0}, messages[3][64] = {{0}};
    client_t clients[3] = {{0}};
    edict_t edicts[3] = {{0}};
    sv.multicast = (sizebuf_t){.data=multicast, .maxsize=64, .cursize=7};
    sv.reliable_datagram = (sizebuf_t){.data=broadcast, .maxsize=64};
    svs.clients = clients;
    svs.maxclients = 3;
    for (int i = 0; i < 3; ++i) {
        clients[i].active = i != 2;
        clients[i].pextknown = true;
        clients[i].protocol_pext2 = i == 1 ? 0 : 1;
        clients[i].edict = &edicts[i];
        clients[i].message = (sizebuf_t){.data=messages[i], .maxsize=64};
    }
    SV_AddSignonBuffer();
    sv.state = scenario == 4 ? ss_loading : ss_active;
    if (scenario == 3) {
        sv.signon->cursize = sv.signon->maxsize - 1;
        memset(sv.signon->data, 0x5a, sv.signon->cursize);
    }
    SV_Multicast(MULTICAST_INIT, NULL, 0, scenario == 2 ? 1 : 0);
    assert(sv.multicast.cursize == 0);
    SV_UpdateToReliableMessages();
    assert(sv.reliable_datagram.cursize == 0);
    switch (scenario) {
    case 0: /* A later join can still replay the data after the frame flush. */
        expect_payload(sv.signon);
        break;
    case 1: /* Existing clients receive one copy, not two. */
        expect_payload(&clients[0].message);
        expect_payload(&clients[1].message);
        break;
    case 2: /* The frame broadcast must not bypass the extension filter. */
        assert(clients[1].message.cursize == 0);
        expect_payload(&clients[0].message);
        expect_payload(sv.signon);
        break;
    case 3: /* A late init message must fit even when the current buffer cannot. */
        assert(sv.num_signon_buffers == 2);
        expect_payload(sv.signon);
        for (int i = 0; i < sv.signon_buffers[0]->cursize; ++i)
            assert(sv.signon_buffers[0]->data[i] == 0x5a);
        expect_payload(&clients[0].message);
        break;
    case 4: /* Loading-time init remains stored without broadcasting. */
        expect_payload(sv.signon);
        assert(clients[0].message.cursize == 0);
        assert(clients[1].message.cursize == 0);
        break;
    }
    assert(clients[2].message.cursize == 0);
    for (int i = 0; i < sv.num_signon_buffers; ++i)
        free(sv.signon_buffers[i]);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-multicast-init-") as tmp:
    path = Path(tmp)
    (path / "multicast.c").write_text(source)
    subprocess.run([
        *shlex.split(os.environ.get("CC", "cc")), "-std=c99", "-O1", "-g",
        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
        str(path / "multicast.c"), "-o", str(path / "multicast"),
    ], check=True)
    failures = []
    for scenario, name in enumerate((
        "late-join retention", "single delivery", "extension filtering",
        "buffer rollover", "loading-time init",
    )):
        result = subprocess.run([str(path / "multicast"), str(scenario)],
                                capture_output=True, text=True)
        print(f"{name}: {'PASS' if result.returncode == 0 else 'FAIL'}")
        if result.returncode:
            failures.append(name)
            print(result.stderr.strip())
    if failures:
        raise SystemExit(f"{len(failures)} multicast init regression(s)")
