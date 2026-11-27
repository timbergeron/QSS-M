"""Exercise production loopback framing and reliable delivery under load."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
loop = (ROOT / "Quake/net_loop.c").read_text()
net = (ROOT / "Quake/net.h").read_text()
limit = next(line for line in net.splitlines() if line.startswith("#define NET_MAXMESSAGE"))

source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
''' + limit + r'''
typedef unsigned char byte;
typedef int qboolean;
typedef struct {
    byte *data;
    int cursize;
} sizebuf_t;
typedef struct qsocket_s {
    int receiveMessageLength, sendMessageLength;
    qboolean canSend;
    unsigned int unreliableSendSequence, unreliableReceiveSequence;
    struct qsocket_s *driverdata;
    byte receiveMessage[NET_MAXMESSAGE], sendMessage[NET_MAXMESSAGE];
} qsocket_t;
#define true 1
#define false 0
#define Q_memcpy memcpy
static byte message_data[NET_MAXMESSAGE];
static sizebuf_t net_message = {message_data, 0};
static qsocket_t *loop_server;
static void SZ_Clear(sizebuf_t *buf) { buf->cursize = 0; }
static void SZ_Write(sizebuf_t *buf, const byte *data, int length) {
    assert(length >= 0 && buf->cursize + length <= NET_MAXMESSAGE);
    memcpy(buf->data + buf->cursize, data, length);
    buf->cursize += length;
}
''' + loop[loop.index("static int IntAlign("):loop.index("qboolean Loop_CanSendUnreliableMessage")] + r'''
static void disconnect(qsocket_t *sock) {
    assert(sock == loop_server);
    loop_server = NULL;
}

int main(void) {
    static qsocket_t sender, receiver;
    static byte snapshot[NET_MAXMESSAGE], reliable_data[NET_MAXMESSAGE];
    sizebuf_t datagram = {snapshot, 64984};
    sizebuf_t reliable = {reliable_data, 1394};
    sizebuf_t small = {snapshot, 1};
    sender.driverdata = &receiver;
    receiver.driverdata = &sender;
    sender.canSend = true;
    memset(snapshot, 0x45, sizeof(snapshot));
    memset(reliable_data, 0x79, sizeof(reliable_data));

    /* Payloads measured on sm212_naitelveni after the FTE snapshot fix. */
    assert(Loop_SendUnreliableMessage(&sender, &datagram) == 1);
    assert(Loop_SendMessage(&sender, &reliable) == 1);
    assert(!Loop_CanSendMessage(&sender));
    assert(Loop_SendMessage(&sender, &small) == 0);
    assert(Loop_SendUnreliableMessage(&sender, &small) == 0);
    assert(Loop_GetMessage(&receiver) == 2 && net_message.cursize == datagram.cursize);
    assert(!memcmp(net_message.data, snapshot, net_message.cursize));
    assert(!Loop_CanSendMessage(&sender));
    assert(Loop_GetMessage(&receiver) == 1 && net_message.cursize == reliable.cursize);
    assert(!memcmp(net_message.data, reliable_data, net_message.cursize));
    assert(Loop_CanSendMessage(&sender) && Loop_GetMessage(&receiver) == 0);

    /* Check both sides of the aligned unreliable queue limit. */
    datagram.cursize = (NET_MAXMESSAGE & ~3) - 7;
    assert(Loop_SendUnreliableMessage(&sender, &datagram) == 0);
    datagram.cursize--;
    assert(Loop_SendUnreliableMessage(&sender, &datagram) == 1);
    reliable.cursize = 0;
    assert(Loop_SendMessage(&sender, &reliable) == 1);
    assert(Loop_GetMessage(&receiver) == 2 && net_message.cursize == datagram.cursize);
    assert(Loop_GetMessage(&receiver) == 1 && net_message.cursize == 0);

    /* A full-size reliable packet fits the separate buffer. Typical packets
       still use the shared queue and keep their order with later datagrams. */
    reliable.cursize = NET_MAXMESSAGE;
    assert(Loop_SendMessage(&sender, &reliable) == 1);
    assert(Loop_GetMessage(&receiver) == 1 && net_message.cursize == NET_MAXMESSAGE);
    assert(!memcmp(net_message.data, reliable_data, net_message.cursize));
    assert(Loop_SendMessage(&sender, &small) == 1);
    sender.unreliableSendSequence = 0xffffffffu;
    assert(Loop_SendUnreliableMessage(&sender, &small) == 1);
    assert(Loop_GetMessage(&receiver) == 1 && net_message.cursize == 1);
    assert(Loop_GetMessage(&receiver) == 2 && receiver.unreliableReceiveSequence == 0);

    loop_server = &receiver;
    assert(Loop_SendUnreliableMessage(&sender, &small) == 1);
    Loop_GetAnyMessages(disconnect);
    assert(!loop_server);
    puts("loopback queue: PASS");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-loopback-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(
        compiler + ["-std=c11", "-Wall", "-fsanitize=address,undefined",
                    str(path / "test.c"), "-o", str(path / "test")],
        check=True,
    )
    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    subprocess.run([str(path / "test")], check=True, env=env)
