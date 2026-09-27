"""Run the production dedicated loop with deterministic clock/signal boundaries.

Catches fixed sleeps crossing tick deadlines, polling with connected clients,
extra sleeping after over-budget work, idle-only polling, and long-tick shutdown.
"""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Quake/main_sdl.c').read_text();a=s.index('\tif (isDedicated)',s.index('oldtime = Sys_DoubleTime();'));b=s.index('\n\telse\n\twhile (1)',a)
loop=s[a:b]
source=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include <sys/time.h>
#define true 1
#define false 0
#define CLAMP(a,b,c) ((b)<(a)?(a):(b)>(c)?(c):(b))
#define q_min(a,b) ((a)<(b)?(a):(b))
#define PRESPAWN_DONE 0
#define _WIN32 1
typedef int qboolean;
typedef uint64_t Uint64;
static struct {float value;} sys_ticrate;
static struct {int active;} sv={1};
typedef struct {int active;void *netconnection;int sendsignon;} client_t;
static client_t clients[16];
static struct {int maxclients;client_t *clients;} svs={16,clients};
static int isDedicated=1,ticks,waits,quit;
static double now,work,oversleep,quit_at,maxsleep,lastgap;
static jmp_buf done;
static double Sys_DoubleTime(void){return now;}
static int Sys_HasDedicatedQuitRequest(void){return quit_at>0 && now>=quit_at;}
static void Sys_Quit(void){quit=1;longjmp(done,1);}
static void delay(double seconds){waits++;if(seconds>maxsleep)maxsleep=seconds;now+=seconds+oversleep;}
static void SDL_Delay(unsigned ms){delay(ms/1000.0);}
static int coarse_slop;
static void SDL_DelayNS(Uint64 ns){delay(ns/1e9);if(coarse_slop)now+=.001;}
static void SDL_DelayPrecise(Uint64 ns){assert(ns<=2000001);delay(ns/1e9);}
static void Host_Frame(double dt){lastgap=dt;now+=work;if(++ticks==4)longjmp(done,1);}
static void run(float rate,int active,double framework,double overshoot,double stop){
 double time,oldtime=0,newtime;now=0;work=framework;oversleep=overshoot;quit_at=stop;ticks=waits=quit=0;maxsleep=lastgap=0;
 sys_ticrate.value=rate;memset(clients,0,sizeof(clients));clients[0].active=active;
 if(!setjmp(done)) {
'''+loop+r'''
 }
}
int main(void){
 int populated_waits;
 run(.05f,0,.0002,0,0);
 assert(ticks==4 && fabs(lastgap-.05)<.000001); // no 8 ms overshoot
 run(.05f,1,.0002,0,0);assert(ticks==4 && waits<=8); // bulk waits
 populated_waits=waits;
 /* Empty servers must use the same deadline wait as populated servers. */
 run(.05f,0,.0002,0,0);
 assert(ticks==4 && waits==populated_waits && fabs(lastgap-.05)<.000001);
 run(.05f,1,.07,0,0);assert(waits<=2 && lastgap>=.069999999); // work already passed deadline
 run(.2f,1,0,0,.051);assert(quit && now<=.101 && ticks==0 && maxsleep<=.050000001);
 run(.001f,1,0,.0003,0);assert(ticks==4 && waits==4 && lastgap<.001301);
 run(0,1,.0002,0,0);assert(waits==0 && ticks==4);
 run(-1,1,.0002,0,0);assert(waits==0 && ticks==4);
 run(.001f,0,0,0,0);assert(lastgap<.001001);
 run(.05f,0,0,0,0);assert(waits<=8);
 coarse_slop=1;run(.05f,1,0,0,0);assert(lastgap<.05001);
 puts("dedicated wait: PASS (deadlines, population, work, oversleep, shutdown)");
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-dedicated-wait-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(source)
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O2','-fsanitize=undefined',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
