"""Exercise the real chunk scheduler, parser and server file reads with a fake wire.

Run with python3 Misc/stress/test_chunked_download.py (requires cc and SDL2).
The deterministic transfer timings model 72 Hz peers, delay and packet loss;
they are not measurements of a live connection. --baseline DIR can compare
saved before-cl_main.c, before-host_cmd.c and before-sv_main.c sources.
"""

from pathlib import Path
import argparse
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, declaration):
    start = source.index(declaration)
    while ";" in source[start:source.index("\n", start)]:
        start = source.index(declaration, start + len(declaration))
    return source[start:source.index("\n}", start) + 2] + "\n"


PRELUDE = r'''
#include "quakedef.h"
#include <assert.h>
#include <errno.h>

client_static_t cls;
double realtime;
sizebuf_t net_message;
qboolean msg_badread;
static int readpos, finished, packets, requests_sent;
static sizebuf_t captured;
static byte capture_data[1024];
static int seek_count;
static int incoming_sequence;
struct qsocket_s { int unused; }; /* opaque to these routines */
static struct qsocket_s test_socket;
static qboolean can_send_reliable = true;
int NET_QSocketGetSequenceIn(const struct qsocket_s *sock) { assert(sock && !cls.demoplayback); return incoming_sequence; }
int NET_QSocketGetSequenceOut(const struct qsocket_s *sock) { (void)sock; return 17; }
qboolean NET_CanSendMessage(struct qsocket_s *sock) { assert(sock); return can_send_reliable; }
client_t *host_client;
server_static_t svs;
cvar_t sv_downloadrate;
cmd_source_t cmd_source = src_client;
static char chunk_argument[32];
static int command_argc = 2;
static char filename_argument[MAX_QPATH];
int Cmd_Argc(void) { return command_argc; }
const char *Cmd_Argv(int arg) { return arg == 1 ? chunk_argument : arg == 2 ? filename_argument : "nextdl"; }

static int counted_seek(FILE *file, long offset, int whence) {
    seek_count++;
    return fseek(file, offset, whence);
}
#undef fseek
#define fseek counted_seek
void *Z_Malloc(int size) { void *p = calloc(1, size); assert(p); return p; }
void Z_Free(void *p) { free(p); }
void Host_Error(const char *fmt, ...) { (void)fmt; abort(); }
void Con_Printf(const char *fmt, ...) { (void)fmt; }
void Con_SafePrintf(const char *fmt, ...) { (void)fmt; }
void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
void Con_Warning(const char *fmt, ...) { (void)fmt; }
void SZ_Write(sizebuf_t *buf, const void *data, int size) {
    assert(size >= 0 && size <= buf->maxsize - buf->cursize);
    memcpy(buf->data + buf->cursize, data, size); buf->cursize += size;
}
void MSG_WriteByte(sizebuf_t *buf, int value) { byte b = value; SZ_Write(buf, &b, 1); }
void MSG_WriteLong(sizebuf_t *buf, int value) {
    unsigned int n = value;
    for (int i = 0; i < 4; i++, n >>= 8) MSG_WriteByte(buf, n);
}
void MSG_WriteShort(sizebuf_t *buf, int value) {
    MSG_WriteByte(buf, value); MSG_WriteByte(buf, (unsigned int)value >> 8);
}
void MSG_WriteString(sizebuf_t *buf, const char *s) { SZ_Write(buf, s, strlen(s) + 1); }
int q_snprintf(char *s, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt); int n = vsnprintf(s, size, fmt, args);
    va_end(args); return n;
}
char *va(const char *fmt, ...) {
    static char text[1024]; va_list args;
    va_start(args, fmt); vsnprintf(text, sizeof(text), fmt, args); va_end(args);
    return text;
}
byte *MSG_ReadData(unsigned int size) {
    if (size > (unsigned int)(net_message.cursize - readpos)) {
        msg_badread = true; return NULL;
    }
    byte *p = net_message.data + readpos; readpos += size; return p;
}
int MSG_ReadLong(void) {
    byte *p = MSG_ReadData(4);
    return p ? (int)((unsigned int)p[0] | (unsigned int)p[1] << 8 |
        (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24) : -1;
}
const char *MSG_ReadString(void) {
    const char *s = (char *)net_message.data + readpos;
    while (readpos < net_message.cursize)
        if (!net_message.data[readpos++]) return s;
    msg_badread = true; return "";
}
int NET_SendUnreliableMessage(struct qsocket_s *sock, sizebuf_t *buf) {
    (void)sock; assert(!captured.cursize && buf->cursize <= 1024);
    SZ_Write(&captured, buf->data, buf->cursize); packets++; return 1;
}
static void DL_FreeBlocks(void) {
    while (cls.download.dlblocks) {
        dlblock_t *b = cls.download.dlblocks;
        cls.download.dlblocks = b->next; Z_Free(b);
    }
}
static void DL_ClearChunkedState(qboolean remove_temp) {
    (void)remove_temp; DL_FreeBlocks();
    if (cls.download.file) fclose(cls.download.file);
    cls.download.file = NULL;
}
static void CL_OptionalDownloadCache_RecordCurrentServerTransient(void) { abort(); }
static void CL_OptionalDownloadCache_RecordServerTransient(const char *s) { (void)s; }
static void CL_OptionalDownloadCache_RecordServerMissing(const char *s) { (void)s; }
static qboolean CL_DownloadNameIsLoc(const char *s) { (void)s; return false; }
static qboolean CL_AsyncDownload_IsActive(void) { return false; }
static void DL_AbortChunked(qboolean tell) { (void)tell; abort(); }
void COM_CreatePath(char *path) { (void)path; }
static void DL_FinishChunked(void) { finished = 1; }
static qboolean CL_DownloadProgress_Update(double received, double total) {
    cls.download.percent = total ? 100 * received / total : 0; return false;
}
static void Host_CloseDownload(client_t *client) { (void)client; abort(); }
static void Host_FailChunkedDownload(client_t *client) { (void)client; abort(); }
'''

CHECKS = r'''
#undef fseek
static client_t server;
static byte reliable_data[1024];
static byte expected[2 * 1024 * 1024 + 37];

static void setup(unsigned int size) {
    DL_ClearChunkedState(false);
    if (server.download.file) fclose(server.download.file);
    memset(&cls, 0, sizeof(cls)); memset(&server, 0, sizeof(server));
    memset(&captured, 0, sizeof(captured));
    captured.data = capture_data; captured.maxsize = sizeof(capture_data);
    cls.message.data = reliable_data; cls.message.maxsize = sizeof(reliable_data);
    cls.state = ca_connected; cls.download.active = cls.download.chunked = true;
    cls.netcon = &test_socket; can_send_reliable = true;
    cls.download.size = size; cls.download.file = tmpfile(); assert(cls.download.file);
    cls.download.dlblocks = DL_NewBlock(0, size, DLB_MISSING);
    server.download.file = tmpfile(); assert(server.download.file);
    server.download.chunked = true; server.download.size = size;
    /* A nonzero archive offset must survive both sequential and random reads. */
    server.download.startpos = 19;
    assert(fwrite("archive-file-prefix", 1, 19, server.download.file) == 19);
    for (unsigned int i = 0; i < size; i++) expected[i] = (i * 13 + i / 1024) % 251;
    assert(fwrite(expected, 1, size, server.download.file) == size);
    assert(!fseek(server.download.file, 19, SEEK_SET));
    realtime = 1; finished = packets = requests_sent = seek_count = 0;
    DL_InitChunkedPacing();
    server.active = true; server.download.chunkcredit_time = realtime;
    svs.clients = &server; svs.maxclients = 1; sv_downloadrate.value = 1024;
}

static int queued_requests(sizebuf_t *buf, unsigned int *chunks) {
    int count = 0;
    for (int pos = 0; pos < buf->cursize;) {
        assert(buf->data[pos++] == clc_stringcmd);
        char *command = (char *)buf->data + pos;
        assert(memchr(command, 0, buf->cursize - pos));
        assert(sscanf(command, "nextdl %u", &chunks[count]) == 1);
        count++; assert(count <= 64); pos += strlen(command) + 1;
    }
    buf->cursize = 0; requests_sent += count; return count;
}

static void receive_chunk(unsigned int chunk, qboolean duplicate) {
    byte packet[DL_CHUNK_PACKET_SIZE];
    sizebuf_t buf = {0}; buf.data = packet; buf.maxsize = sizeof(packet);
    assert(!server.download.chunkqueue_count);
    server.download.chunkqueue_head = 0; server.download.chunkqueue_count = 1;
    server.download.chunkqueue[0] = chunk;
    assert(Host_AppendDownloadData(&server, &buf));
    assert(buf.cursize == sizeof(packet) && packet[0] == svc_download);
    net_message = buf; readpos = 1; msg_badread = false;
    CL_Download_Chunked(); assert(!msg_badread);
    if (duplicate) {
        long before = ftell(cls.download.file);
        unsigned int completed = cls.download.completedbytes;
        readpos = 1; CL_Download_Chunked();
        assert(ftell(cls.download.file) == before);
        assert(cls.download.completedbytes == completed);
    }
}

static void verify_file(void) {
    assert(finished && !cls.download.dlblocks);
    assert(cls.download.completedbytes == cls.download.size);
    assert(!fseek(cls.download.file, 0, SEEK_END));
    assert(ftell(cls.download.file) == (long)cls.download.size);
    rewind(cls.download.file);
    for (unsigned int i = 0; i < cls.download.size; i++)
        assert(fgetc(cls.download.file) == expected[i]);
}

static void check_scheduler(void) {
    unsigned int chunks[64];
    setup(sizeof(expected));
    /* Reliable traffic must remain untouched even when that buffer is full. */
    cls.message.cursize = cls.message.maxsize;
    memset(reliable_data, 0x5a, sizeof(reliable_data));
    DLC_RequestDownloadChunks();
    assert(queued_requests(&captured, chunks) == 8);
    for (int i = 0; i < 8; i++) assert(chunks[i] == (unsigned int)i);
    for (int frame = 0; frame < 100; frame++) {
        realtime += 0.001;
        DLC_RequestDownloadChunks();
        assert(!captured.cursize && packets == 1);
    }
    assert(cls.message.cursize == cls.message.maxsize);
    for (unsigned int i = 0; i < sizeof(reliable_data); i++) assert(reliable_data[i] == 0x5a);
    /* No response at all: first retry after one second, then back off. */
    realtime = 2.01; DLC_RequestDownloadChunks();
    assert(queued_requests(&captured, chunks) == 6 && chunks[0] == 0);
    assert(cls.download.chunkwindow == 6);
    assert(cls.download.dlblocks->requests == 2);
    DL_RecordChunkRTT(0); assert(!cls.download.chunkrtt);
    assert(cls.download.chunkwindow == 6); /* retransmissions cannot grow it */
    /* A valid sample shortens loss recovery; retransmits cannot change it. */
    setup(3 * DLBLOCKSIZE + 37);
    DLC_RequestDownloadChunks(); assert(queued_requests(&captured, chunks) == 4);
    realtime = 1.05; receive_chunk(0, true);
    assert(fabs(cls.download.chunkrtt - 0.05) < 0.00001);
    realtime = 1.24; DLC_RequestDownloadChunks(); assert(!captured.cursize);
    realtime = 1.26; DLC_RequestDownloadChunks();
    assert(queued_requests(&captured, chunks) == 3);
    realtime = 1.35; receive_chunk(2, true);
    assert(fabs(cls.download.chunkrtt - 0.05) < 0.00001);
    realtime = 1.75; DLC_RequestDownloadChunks(); assert(!captured.cursize);
    realtime = 1.77; DLC_RequestDownloadChunks();
    assert(queued_requests(&captured, chunks) == 2);
    receive_chunk(3, false); receive_chunk(1, true); verify_file();
    /* Sequential transfers retain stdio buffering on both ends. */
    setup(3 * DLBLOCKSIZE + 37); DLC_RequestDownloadChunks();
    queued_requests(&captured, chunks); realtime += 0.05;
    for (unsigned int i = 0; i < 4; i++) receive_chunk(i, true);
    assert(seek_count == 0); verify_file();
    /* A truncated wire chunk cannot credit bytes or write to the file. */
    setup(1024); byte short_chunk[4] = {0};
    net_message.data = short_chunk; net_message.cursize = 4; readpos = 0;
    msg_badread = false; CL_Download_Chunked();
    assert(msg_badread && cls.download.completedbytes == 0 && ftell(cls.download.file) == 0);
    /* Exercise real server queue capacity, deduplication and ring wraparound. */
    setup(sizeof(expected)); host_client = &server;
    for (unsigned int i = 0; i < DL_MAX_CHUNK_QUEUE + 8; i++) {
        snprintf(chunk_argument, sizeof(chunk_argument), "%u", i);
        Host_NextDownload_f(); Host_NextDownload_f();
    }
    assert(server.download.chunkqueue_count == DL_MAX_CHUNK_QUEUE);
    for (int i = 0; i < 17; i++) Host_PopDownloadChunk(&server);
    for (unsigned int i = 0; i < 17; i++) {
        snprintf(chunk_argument, sizeof(chunk_argument), "%u", DL_MAX_CHUNK_QUEUE + i);
        Host_NextDownload_f();
    }
    for (unsigned int i = 0; i < DL_MAX_CHUNK_QUEUE; i++) {
        assert(server.download.chunkqueue[server.download.chunkqueue_head] == i + 17);
        Host_PopDownloadChunk(&server);
    }
    strcpy(chunk_argument, "9999999999999999999999"); Host_NextDownload_f();
    strcpy(chunk_argument, "3junk"); Host_NextDownload_f();
    assert(!server.download.chunkqueue_count);
    puts("PASS: bounded batches/window, reliable isolation, retry timing/backoff, RTT ambiguity, duplicates, reorder, partial tail, buffered I/O, truncation");
    puts("PASS: server queue capacity, deduplication, ring wraparound and invalid requests");
}

typedef struct { int due; unsigned int chunk; } event_t;
static event_t outbound[65536], inbound[65536];

static double transfer(int latency_ms, int loss_interval, int server_budget, qboolean request_loss) {
    int outhead = 0, outtail = 0, inhead = 0, intail = 0, wire_chunks = 0;
    int delay = (latency_ms * 72 + 1999) / 2000;
    int next_reliable = 0, request_packets = 0, frame;
    unsigned int ready[DL_MAX_CHUNK_QUEUE];
    unsigned int readyhead = 0, readycount = 0;
    unsigned int capacity = server_budget == 8 ? 64 : DL_MAX_CHUNK_QUEUE;
    setup(sizeof(expected));
    for (frame = 0; frame < 72 * 180 && !finished; frame++) {
        realtime = 1 + frame / 72.0;
        int allowance = Host_ChunkDownloadAllowance(&server);
        /* Model requests arriving after one-way delay, then the server tick. */
        while (outhead < outtail && outbound[outhead].due <= frame) {
            unsigned int chunk = outbound[outhead++].chunk;
            if (readycount < capacity) ready[(readyhead + readycount++) % capacity] = chunk;
        }
        for (int sent = 0; readycount && sent < server_budget && allowance >= DL_CHUNK_PACKET_SIZE; sent++) {
            unsigned int chunk = ready[readyhead];
            readyhead = (readyhead + 1) % capacity; readycount--; wire_chunks++;
            allowance -= DL_CHUNK_PACKET_SIZE;
            server.download.chunkcredit = q_max(0.0, server.download.chunkcredit - DL_CHUNK_PACKET_SIZE);
            if (loss_interval && wire_chunks % loss_interval == 0) continue;
            assert(intail < (int)countof(inbound));
            inbound[intail++] = (event_t){frame + delay, chunk};
        }
        while (inhead < intail && inbound[inhead].due <= frame) {
            event_t e = inbound[inhead++]; receive_chunk(e.chunk, false);
        }
        if (finished) break;
        DLC_RequestDownloadChunks();
        unsigned int chunks[64]; int count = 0;
        if (BEFORE) {
            if (frame >= next_reliable && cls.message.cursize) {
                count = queued_requests(&cls.message, chunks);
                next_reliable = frame + 2 * delay + 1;
            }
        } else count = queued_requests(&captured, chunks);
        if (count) request_packets++;
        /* Drop entire request batches too; the scheduler must recover. */
        if (!BEFORE && request_loss && loss_interval && request_packets % (loss_interval + 3) == 0 && count) continue;
        for (int i = 0; i < count; i++) {
            assert(outtail < (int)countof(outbound));
            outbound[outtail++] = (event_t){frame + delay, chunks[i]};
        }
    }
    verify_file();
    double seconds = frame / 72.0;
    printf("%s: RTT %d ms, loss interval %d, server %d/tick: %.2f s, %.0f KiB/s (%d requests)\n",
        BEFORE ? "before" : "after", latency_ms, loss_interval, server_budget,
        seconds, cls.download.size / 1024.0 / seconds, requests_sent);
    return seconds;
}

static void check_rate_limit(void) {
    setup(1024);
    client_t *clients = calloc(4, sizeof(*clients)); assert(clients);
    unsigned int bytes[4] = {0};
    for (int i = 0; i < 4; i++) {
        clients[i].active = true; clients[i].download.file = server.download.file;
        clients[i].download.chunked = true; clients[i].download.chunkcredit_time = realtime;
    }
    svs.clients = clients; svs.maxclients = 4; sv_downloadrate.value = 256;
    for (int tick = 1; tick <= 1000; tick++) {
        realtime = 1 + tick / 100.0;
        for (int i = 0; i < 4; i++) {
            int allowance = Host_ChunkDownloadAllowance(&clients[i]);
            assert(allowance >= 0 && allowance <= 32 * DL_CHUNK_PACKET_SIZE);
            clients[i].download.chunkcredit -= allowance;
            bytes[i] += allowance;
        }
    }
    assert(bytes[0] * 4 <= 256 * 1024 * 10);
    assert(bytes[0] * 4 > 255 * 1024 * 10);
    for (int i = 1; i < 4; i++) assert(bytes[i] == bytes[0]);
    /* A long stall cannot accumulate an unbounded burst. */
    realtime += 100;
    assert(Host_ChunkDownloadAllowance(&clients[0]) <= 7 * DL_CHUNK_PACKET_SIZE);
    /* Changing rates and player counts cannot spend the old larger balance. */
    sv_downloadrate.value = 1;
    assert(Host_ChunkDownloadAllowance(&clients[0]) <= DL_CHUNK_PACKET_SIZE);
    sv_downloadrate.value = 0;
    realtime += 10;
    assert(Host_ChunkDownloadAllowance(&clients[0]) == 32 * DL_CHUNK_PACKET_SIZE);
    assert(clients[0].download.chunkcredit_time == realtime);
    sv_downloadrate.value = -1;
    realtime += 10;
    assert(Host_ChunkDownloadAllowance(&clients[0]) == 32 * DL_CHUNK_PACKET_SIZE);
    assert(clients[0].download.chunkcredit_time == realtime);
    sv_downloadrate.value = 0.5;
    assert(Host_ChunkDownloadAllowance(&clients[0]) <= DL_CHUNK_PACKET_SIZE);
    sv_downloadrate.value = NAN;
    assert(Host_ChunkDownloadAllowance(&clients[0]) <= DL_CHUNK_PACKET_SIZE);
    free(clients); svs.clients = &server; svs.maxclients = 1;
    puts("PASS: shared bandwidth cap, equal service, bounded stalls and rate changes");
}

static void check_sequence_marker(void) {
    byte message[512];
    setup(2 * DLBLOCKSIZE);
    server.message.data = message; server.message.maxsize = sizeof(message);
    Host_SendChunkedDownloadStart(&server, cls.download.size, "sound/probe.wav");
    assert(message[0] == svc_stufftext);
    assert(!strcmp((char *)message + 1, "//cl_downloadsequence 17 \"sound/probe.wav\"\n"));
    assert(message[2 + strlen((char *)message + 1)] == svc_download);
    FILE *file = cls.download.file; cls.download.file = NULL;
    command_argc = 3; strcpy(cls.download.current, "sound/probe.wav");
    strcpy(filename_argument, "sound/wrong.wav"); strcpy(chunk_argument, "17");
    CL_Download_Sequence_f(); assert(!cls.download.chunksequence_valid);
    strcpy(filename_argument, cls.download.current); strcpy(chunk_argument, "bad");
    CL_Download_Sequence_f(); assert(!cls.download.chunksequence_valid);
    strcpy(chunk_argument, "17"); CL_Download_Sequence_f();
    assert(cls.download.chunksequence_valid && cls.download.chunksequence == 17);
    cls.download.file = file;
    strcpy(chunk_argument, "25"); CL_Download_Sequence_f();
    assert(cls.download.chunksequence == 17); /* cannot move the marker mid-file */
    command_argc = 2;
    incoming_sequence = 16; receive_chunk(0, false);
    assert(!cls.download.completedbytes && ftell(file) == 0);
    incoming_sequence = 17; receive_chunk(0, false); receive_chunk(1, false);
    verify_file();
    /* The file floor compares sequence distance modulo 2^32. This does not
     * claim to fix the transport's own handling of sequence wraparound. */
    setup(2 * DLBLOCKSIZE); cls.download.chunksequence_valid = true;
    cls.download.chunksequence = 0;
    incoming_sequence = -1; receive_chunk(0, false);
    assert(!cls.download.completedbytes);
    cls.download.chunksequence = UINT_MAX;
    incoming_sequence = -2; receive_chunk(0, false);
    assert(!cls.download.completedbytes);
    incoming_sequence = 0; receive_chunk(0, false); receive_chunk(1, false);
    verify_file();
    /* Demos and a missing connection must never dereference the socket. */
    setup(2 * DLBLOCKSIZE); cls.download.chunksequence_valid = true;
    cls.download.chunksequence = 17; incoming_sequence = 16;
    cls.netcon = NULL; DLC_RequestDownloadChunks(); assert(!captured.cursize);
    receive_chunk(0, false);
    cls.netcon = &test_socket; cls.demoplayback = true;
    DLC_RequestDownloadChunks(); assert(!captured.cursize);
    receive_chunk(1, false); verify_file();
    puts("PASS: actual server marker, pending filename validation and rejection of old-file replies");
}

static void check_pacing_and_fallback(void) {
    unsigned int chunks[64];
    setup(sizeof(expected)); DLC_RequestDownloadChunks(); queued_requests(&captured, chunks);
    realtime += 0.05; receive_chunk(0, false);
    double rtt = cls.download.chunkrtt, var = cls.download.chunkrttvar;
    DL_InitChunkedPacing();
    assert(cls.download.chunkrtt == rtt && cls.download.chunkrttvar == var);
    assert(cls.download.chunkrttmin == rtt && cls.download.chunkwindow == 8);
    /* A standing queue stops growth; draining it permits growth again. */
    cls.download.chunkrtt = 0.2;
    realtime = 1.2; DL_RecordChunkRTT(DLBLOCKSIZE);
    assert(cls.download.chunkwindow == 8);
    cls.download.chunkrtt = 0.05;
    realtime = 1.05; DL_RecordChunkRTT(2 * DLBLOCKSIZE);
    assert(cls.download.chunkwindow == 9);
    /* A 5 FPS frame earns 200 ms of credit, still bounded to 32 requests. */
    setup(sizeof(expected)); cls.download.chunkrtt = 0.2; cls.download.chunkcredit = 0;
    realtime += 0.201; DLC_RequestDownloadChunks();
    assert(queued_requests(&captured, chunks) == 8);
    setup(sizeof(expected)); cls.download.chunkwindow = DL_MAX_CHUNK_QUEUE;
    cls.download.chunkcredit = 0; realtime += 20; DLC_RequestDownloadChunks();
    assert(queued_requests(&captured, chunks) == 32);
    /* Time spent loading before the first request is not a download stall. */
    realtime += 0.01; DLC_RequestDownloadChunks();
    assert(!cls.download.chunkreliable && cls.download.chunkprogress_time == 21);
    queued_requests(&captured, chunks);
    /* No credited data for five seconds selects the reliable stream. */
    setup(sizeof(expected)); DLC_RequestDownloadChunks(); queued_requests(&captured, chunks);
    realtime = 5.99; DLC_RequestDownloadChunks(); queued_requests(&captured, chunks);
    assert(!cls.download.chunkreliable);
    realtime = 6.01; can_send_reliable = false; DLC_RequestDownloadChunks();
    assert(cls.download.chunkreliable && !cls.message.cursize && !captured.cursize);
    can_send_reliable = true; cls.message.cursize = cls.message.maxsize;
    DLC_RequestDownloadChunks(); assert(cls.download.dlblocks->state == DLB_MISSING);
    cls.message.cursize = 0; MSG_WriteByte(&cls.message, clc_nop);
    DLC_RequestDownloadChunks(); /* signon keepalives cannot starve requests */
    assert(cls.message.data[0] == clc_nop);
    memmove(cls.message.data, cls.message.data + 1, --cls.message.cursize);
    assert(queued_requests(&cls.message, chunks) == 8);
    for (int i = 0; i < 8; i++) assert(chunks[i] == (unsigned int)i);
    assert(!captured.cursize);
    DLC_RequestDownloadChunks(); assert(!cls.message.cursize); /* window is full */
    realtime += 0.05; receive_chunk(0, false);
    assert(cls.download.chunkprogress_time == realtime && !cls.download.chunkunreliable_ok);
    DL_InitChunkedPacing(); assert(cls.download.chunkreliable);
    setup(sizeof(expected)); assert(!cls.download.chunkreliable && !cls.download.chunkrtt);
    /* Credited progress resets the stall timer; duplicate data does not. */
    DLC_RequestDownloadChunks(); queued_requests(&captured, chunks);
    realtime = 5.9; receive_chunk(0, false);
    realtime = 6.1; receive_chunk(0, true); DLC_RequestDownloadChunks();
    assert(!cls.download.chunkreliable && cls.download.chunkprogress_time == 5.9);
    queued_requests(&captured, chunks);
    /* Once unreliable requests have worked, a later outage cannot latch the
     * fallback, including at the start of a new file on the same connection. */
    assert(cls.download.chunkunreliable_ok);
    realtime = 12; DLC_RequestDownloadChunks();
    assert(!cls.download.chunkreliable); queued_requests(&captured, chunks);
    DL_FreeBlocks(); cls.download.dlblocks = DL_NewBlock(0, cls.download.size, DLB_MISSING);
    DL_InitChunkedPacing(); assert(cls.download.chunkunreliable_ok);
    DLC_RequestDownloadChunks(); queued_requests(&captured, chunks);
    realtime += 6; DLC_RequestDownloadChunks();
    assert(!cls.download.chunkreliable); queued_requests(&captured, chunks);
    setup(sizeof(expected)); assert(!cls.download.chunkunreliable_ok);
    puts("PASS: connection RTT, low FPS pacing, reliable batches/backpressure and outage discrimination");
}

static void check_rtt_minimum_aging(void) {
    double minimums[2];
    for (int pass = 0; pass < 2; pass++) {
        setup(sizeof(expected));
        cls.download.chunkrtt = 0.2; cls.download.chunkrttmin = 0.01;
        cls.download.chunkrttmin_time = realtime;
        dlblock_t *b = cls.download.dlblocks;
        b->state = DLB_PENDING; b->requests = 1;
        int samples = pass ? 1000 : 10;
        for (int i = 1; i <= samples; i++) {
            realtime = 1 + 10.0 * i / samples;
            b->requesttime = realtime - 0.2;
            DL_RecordChunkRTT(0);
            if (realtime <= 2) assert(cls.download.chunkwindow == 8);
        }
        /* A sustained higher baseline eventually permits growth, regardless
         * of how many ACKs arrived during the ten-second interval. */
        assert(cls.download.chunkwindow > 8);
        minimums[pass] = cls.download.chunkrttmin;
        assert(minimums[pass] > 0.1 && minimums[pass] < 0.2);
        /* An ambiguous retransmit must not age or replace the estimate. */
        b->requests = 2; realtime += 20; b->requesttime = realtime - 0.4;
        DL_RecordChunkRTT(0); assert(cls.download.chunkrttmin == minimums[pass]);
        b->requests = 1;
        /* File changes preserve the clock: time between files ages the floor. */
        DL_InitChunkedPacing();
        DL_RecordChunkRTT(0); assert(cls.download.chunkwindow > 8);
        assert(cls.download.chunkrttmin > minimums[pass]);
        /* A fresh low sample immediately lowers the floor again. */
        realtime += 0.05; b->requesttime = realtime - 0.05;
        DL_RecordChunkRTT(0); assert(fabs(cls.download.chunkrttmin - 0.05) < 0.00001);
    }
    assert(fabs(minimums[0] - minimums[1]) < 0.00001);
    puts("PASS: RTT minimum ages across path/file changes independently of ACK frequency");
}

int main(void) {
    if (!BEFORE) check_scheduler();
    if (!BEFORE) check_rate_limit();
    if (!BEFORE) check_sequence_marker();
    if (!BEFORE) check_pacing_and_fallback();
    if (!BEFORE) check_rtt_minimum_aging();
    transfer(0, 0, SERVER_BUDGET, false);
    transfer(50, 0, SERVER_BUDGET, false);
    transfer(100, 0, SERVER_BUDGET, false);
    transfer(200, 0, SERVER_BUDGET, false);
    transfer(100, 17, SERVER_BUDGET, true);
    transfer(500, 0, SERVER_BUDGET, false);
    if (!BEFORE) transfer(100, 17, 8, true); /* older QSS-M server */
    double slow = transfer(100, 17, 1, false);
    assert(slow < 36.0); /* regression: the first fixed-window patch needed 45.5 s */
    DL_ClearChunkedState(false); fclose(server.download.file);
    return 0;
}
'''


def run(directory, baseline=False):
    prefix = "before-" if baseline else ""
    client = (directory / f"{prefix}cl_main.c").read_text()
    host = (directory / f"{prefix}host_cmd.c").read_text()
    server = (directory / f"{prefix}sv_main.c").read_text()
    budget = re.search(r"MAX_CHUNKED_DOWNLOAD_PACKETS = (\d+)", server)[1]
    source = f"#define BEFORE {int(baseline)}\n#define SERVER_BUDGET {budget}\n" + PRELUDE
    declarations = ["static qboolean DL_SendDownloadCommand(", "static qboolean DL_SendNextDownloadChunk("] if baseline else ["static qboolean DL_WriteNextDownloadChunk("]
    source += "static void DL_InitChunkedPacing(void) {}\n" if baseline else function(client, "static void DL_InitChunkedPacing(")
    declarations += ["static dlblock_t *DL_NewBlock(", "static qboolean DL_ChunkExpected(",
                     "static unsigned int DL_MarkBlockReceived(", "static void DLC_RequestDownloadChunks("]
    for declaration in declarations:
        source += function(client, declaration)
    source += "static void DL_RecordChunkRTT(unsigned int offset) {(void)offset;}\n" if baseline else function(client, "static void DL_RecordChunkRTT(")
    source += function(client, "void CL_Download_Chunked(")
    source += function(host, "static void Host_PopDownloadChunk(")
    source += function(host, "qboolean Host_AppendDownloadData(")
    source += function(host, "static qboolean Host_DownloadChunkQueued(")
    source += function(host, "static void Host_NextDownload_f(")
    source += function(host, "static void Host_SendChunkedDownloadStart(")
    if baseline:
        source += "static void CL_Download_Sequence_f(void) {}\n"
    else:
        source += function(client, "static qboolean DL_ParseUnsigned(")
        source += function(client, "static void CL_Download_Sequence_f(")
    source += "int Host_ChunkDownloadAllowance(client_t *client) {(void)client; return 32 * DL_CHUNK_PACKET_SIZE;}\n" if baseline else function(host, "int Host_ChunkDownloadAllowance(")
    source += CHECKS
    flags = shlex.split(subprocess.check_output(["sdl2-config", "--cflags"], text=True))
    with tempfile.TemporaryDirectory(prefix="qssm-chunk-test-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(source)
        subprocess.run([os.environ.get("CC", "cc"), "-O1", "-g", "-std=gnu11", "-DUSE_SDL2",
                        "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I", str(ROOT / "Quake"), str(path / "test.c"),
                        str(ROOT / "Quake/strlcpy.c"), "-lm",
                        "-o", str(path / "test"), *flags], check=True)
        subprocess.run([str(path / "test")], check=True, timeout=60,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    if args.baseline:
        run(args.baseline, baseline=True)
    run(ROOT / "Quake")
