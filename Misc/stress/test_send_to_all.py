"""Exercise production reliable broadcast with fake transport/time boundaries.

Catches busy polling, early success without ACK, timeout extension, and waiting
out the whole timeout after the receive pump drops a connection.
"""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2];text=(root/'Quake/net_main.c').read_text()
a=text.index('int NET_SendToAll (');body=text[a:text.index('\n}',a)+2]
source=r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t Uint64;
typedef int qboolean;
#define true 1
#define false 0
#define MAX_SCOREBOARD 4
#define IS_LOOP_DRIVER(d) ((d)==0)
#define q_min(a,b) ((a)<(b)?(a):(b))
typedef struct {int driver,disconnected,sent,acked,fail;} qsocket_t;
typedef struct {int active;qsocket_t *netconnection;} client_t;
typedef struct {int unused;} sizebuf_t;
static qsocket_t socks[MAX_SCOREBOARD];static client_t clients[MAX_SCOREBOARD],*host_client;
static struct {int maxclients;client_t *clients;} svs={4,clients};
static double now,net_time,ack_delay,drop_at;static int pumps,sleeps,sends;
static double Sys_DoubleTime(void){now+=.000001;return now;}
static void SetNetTime(void){net_time=Sys_DoubleTime();}
static void SDL_DelayNS(Uint64 ns){assert(ns<=1000000);now+=ns/1e9;sleeps++;}
static qboolean NET_CanSendMessage(qsocket_t *s){SetNetTime();return s && !s->disconnected && (!s->sent||s->acked);}
static int NET_SendMessage(qsocket_t *s,sizebuf_t *data){(void)data;sends++;s->sent=1;return s->fail?-1:1;}
static void NET_GetServerMessages(void *cb){
 (void)cb;pumps++;now+=.000001;
 for(int i=0;i<MAX_SCOREBOARD;i++){
  if(socks[i].sent && ack_delay>=0 && now>=ack_delay)socks[i].acked=1;
 }
 if(drop_at>=0 && now>=drop_at){clients[0].netconnection=NULL;clients[0].active=0;}
}
'''+body+r'''
static void reset(int n){
 memset(socks,0,sizeof(socks));memset(clients,0,sizeof(clients));now=net_time=0;pumps=sleeps=sends=0;ack_delay=.02;drop_at=-1;
 for(int i=0;i<n;i++){socks[i].driver=1;clients[i].active=1;clients[i].netconnection=&socks[i];}
}
int main(void){sizebuf_t data={0};int result;
 reset(2);result=NET_SendToAll(&data,.05);assert(result==0 && sends==2 && now>=.02 && now<.023);
 assert(sleeps>0 && pumps<100); // fail if we busy-poll instead of yielding
 reset(2);ack_delay=-1;result=NET_SendToAll(&data,.05);assert(result==2 && sends==2 && now>=.05 && now<.052 && pumps<100);
 reset(2);drop_at=.005;result=NET_SendToAll(&data,.2);assert(result==1 && now<.025); // other client ACKs normally
 reset(1);socks[0].fail=1;result=NET_SendToAll(&data,5);assert(result==1 && now<.001 && sleeps==0);
 reset(1);socks[0].disconnected=1;result=NET_SendToAll(&data,5);assert(result==1 && now<.001);
 reset(0);result=NET_SendToAll(&data,5);assert(result==0 && pumps==0 && sleeps==0);
 reset(1);socks[0].driver=0;result=NET_SendToAll(&data,5);assert(result==0 && sends==1 && sleeps==0);
 reset(1);ack_delay=-1;result=NET_SendToAll(&data,0);assert(result==1 && sends==1 && sleeps==0 && now<.001);
 reset(1);ack_delay=-1;result=NET_SendToAll(&data,.0001);assert(result==1 && now<.0002);
 puts("reliable broadcast: PASS (ACK, timeout, disconnect, failure, loopback)");
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-broadcast-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(source)
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O2','-fsanitize=address,undefined',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
