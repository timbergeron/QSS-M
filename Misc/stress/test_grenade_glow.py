#!/usr/bin/env python3
"""Exercise production fuse eligibility, VM restoration and pulse progression."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(path, declaration):
    source = (ROOT / path).read_text()
    start = source.index(declaration + '\n{')
    return source[start:source.index('\n}', start) + 2] + '\n'


source = r'''
#define _USE_MATH_DEFINES
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ca_connected 1
#define CLAMP(a,x,b) fminf(b,fmaxf(a,x))
typedef struct {float value;} cvar_t;
typedef struct {int free;struct {int classname,owner,model,modelindex,touch;float nextthink;} v;} edict_t;
typedef struct {edict_t edicts[8];int num_edicts;} qcvm_t;
typedef struct {int fromrl;} qmodel_t;
typedef struct {struct {int modelindex;} netstate;qmodel_t *model;} entity_t;
static cvar_t scr_showgrenadecounter={3};
static struct {int active;qcvm_t qcvm;} sv;
static struct {int state,demoplayback;} cls={ca_connected,0};
static struct {entity_t *entities;int num_entities,viewentity,intermission;double time;} cl;
static int qeintermission,crxintermission,switches;
static qcvm_t *qcvm;
static const float GRENADE_EXPLOSION_TIME=2.5f;
static const char *strings[]={"","grenade","progs/grenade.mdl","rocket"};
static const char *PR_GetString(int n) {assert(qcvm==&sv.qcvm);return strings[n];}
static edict_t *EDICT_NUM(int n) {assert(qcvm==&sv.qcvm);assert(n>=0 && n<qcvm->num_edicts);return &qcvm->edicts[n];}
#define EDICT_TO_PROG(e) ((int)((e)-qcvm->edicts))
static void PR_SwitchQCVM(qcvm_t *next) {assert(!next || !qcvm);qcvm=next;switches++;}
#define q_max(a,b) fmaxf(a,b)
#define q_min(a,b) fminf(a,b)
typedef struct {float surface[4],halo[4],halo_width;} grenade_glow_t;
'''
source += function('Quake/gl_screen.c', 'float SCR_GrenadeGlowTimeRemaining(int entnum)')
source += function('Quake/r_alias.c', 'static float R_GrenadeGlowTimeRemaining(const entity_t *e)')
source += function('Quake/r_alias.c', 'static void R_GrenadeGlowColor(float remaining, grenade_glow_t *glow)')
source += function('Quake/r_alias.c', 'static float R_GrenadeGlowHaloWidth(float width, float distance, float fov_x, float view_width)')
source += r'''
static float luma(const float c[4]) {return 0.2126f*c[0]+0.7152f*c[1]+0.0722f*c[2];}
int main(void) {
 entity_t entities[8]={0},temp={0};qcvm_t csqc={0};grenade_glow_t a,b;
 sv.active=1;sv.qcvm.num_edicts=8;cl.entities=entities;cl.num_entities=8;cl.viewentity=1;cl.time=10;
 edict_t *g=&sv.qcvm.edicts[2];
 g->v.classname=1;g->v.owner=1;g->v.model=2;g->v.modelindex=4;g->v.touch=1;g->v.nextthink=12;
 entities[2].netstate.modelindex=4;
 /* cl_r2g swaps only the rendered model. A real grenade and a disguised
  * rocket may share that model (and its fromrl flag), but never a fuse.
  */
 qmodel_t grenade_model={0};
 entities[2].model=entities[3].model=&grenade_model;
 entities[3].netstate.modelindex=5;
 sv.qcvm.edicts[3]=*g;
 sv.qcvm.edicts[3].v.classname=3;
 sv.qcvm.edicts[3].v.modelindex=5;
 for(int r2g=0;r2g<=1;r2g++) {
  grenade_model.fromrl=r2g;
  assert(R_GrenadeGlowTimeRemaining(&entities[2])==2);
  assert(R_GrenadeGlowTimeRemaining(&entities[3])<0);
 }
 assert(SCR_GrenadeGlowTimeRemaining(2)==2 && !qcvm);
 qcvm=&csqc;assert(R_GrenadeGlowTimeRemaining(&entities[2])==2 && qcvm==&csqc);
 qcvm=&sv.qcvm;int before=switches;assert(SCR_GrenadeGlowTimeRemaining(2)==2 && qcvm==&sv.qcvm && switches==before);
 qcvm=NULL;
 assert(R_GrenadeGlowTimeRemaining(&temp)<0);
 assert(R_GrenadeGlowTimeRemaining((entity_t *)((char *)entities+1))<0);
 assert(SCR_GrenadeGlowTimeRemaining(-1)<0 && SCR_GrenadeGlowTimeRemaining(8)<0);
 assert(SCR_GrenadeGlowTimeRemaining(0)<0);
 for(int mode=0;mode<3;mode++) {scr_showgrenadecounter.value=mode;assert(SCR_GrenadeGlowTimeRemaining(2)<0);}
 scr_showgrenadecounter.value=3;
 sv.active=0;assert(SCR_GrenadeGlowTimeRemaining(2)<0);sv.active=1;
 cls.demoplayback=1;assert(SCR_GrenadeGlowTimeRemaining(2)<0);cls.demoplayback=0;
 cls.state=0;assert(SCR_GrenadeGlowTimeRemaining(2)<0);cls.state=ca_connected;
 cl.intermission=1;assert(SCR_GrenadeGlowTimeRemaining(2)<0);cl.intermission=0;
 cl.viewentity=0;assert(SCR_GrenadeGlowTimeRemaining(2)<0);cl.viewentity=1;
 g->free=1;assert(SCR_GrenadeGlowTimeRemaining(2)<0);g->free=0;
 g->v.owner=3;assert(SCR_GrenadeGlowTimeRemaining(2)<0);g->v.owner=1;
 g->v.classname=3;assert(SCR_GrenadeGlowTimeRemaining(2)<0);g->v.classname=1;
 g->v.model=0;assert(SCR_GrenadeGlowTimeRemaining(2)<0);g->v.model=2;
 g->v.modelindex=5;assert(SCR_GrenadeGlowTimeRemaining(2)<0);g->v.modelindex=4;
 g->v.nextthink=12.5f;assert(SCR_GrenadeGlowTimeRemaining(2)<0);
 g->v.nextthink=9;assert(SCR_GrenadeGlowTimeRemaining(2)<0);
 g->v.nextthink=NAN;assert(SCR_GrenadeGlowTimeRemaining(2)<0);
 g->v.nextthink=12;g->v.touch=0;assert(SCR_GrenadeGlowTimeRemaining(2)==0);
 g->v.touch=1;
 /* Identical times must reproduce identical colors (pause and multiple views). */
 R_GrenadeGlowColor(1,&a);R_GrenadeGlowColor(1,&b);assert(!memcmp(&a,&b,sizeof a));
 R_GrenadeGlowColor(2.5,&a);R_GrenadeGlowColor(0,&b);
 /* Detonation: the halo reddens while the surface reads hotter, not darker. */
 assert(b.halo[1]<a.halo[1] && b.halo[3]>a.halo[3] && b.halo[3]<0.75f);
 assert(luma(b.surface)*b.surface[3]>luma(a.surface)*a.surface[3]*3);
 assert(luma(b.halo)*b.halo[3]>luma(a.halo)*a.halo[3]);
 assert(b.halo_width>a.halo_width);
 /* Count peaks per second: fuse-driven pulses should visibly accelerate. */
 int early=0,late=0,final_half=0;float prev=0,cur=0,lowest=1;
 for(int i=0;i<=2500;i++) {
  R_GrenadeGlowColor(2.5f-i/1000.0f,&a);
  if(i>1 && cur>prev && cur>a.surface[3]) {
   if(i<1000)early++;
   if(i>1500)late++;
   if(i>2000)final_half++;
  }
  /* Surface rgb overdrives past 1 by design; its mix amount must not. */
  for(int k=0;k<4;k++)assert(isfinite(a.surface[k]) && a.surface[k]>=0 && a.surface[k]<=2 &&
   isfinite(a.halo[k]) && a.halo[k]>=0 && a.halo[k]<=1);
  assert(a.surface[3]<=1);
  /* The think can fire early, so the whole final window must stay at the crest. */
  if(i>=2430 && a.surface[3]<lowest)lowest=a.surface[3];
  prev=cur;cur=a.surface[3];
 }
 assert(early>=1 && early<=2 && late>=6 && late<=9);
 assert(final_half>=4 && final_half<=5);
 assert(lowest>0.62f);
 /* Close up the pulse sets the width; far away a pixel floor holds, then a cap. */
 assert(R_GrenadeGlowHaloWidth(2,50,90,1920)==2);
 float far=R_GrenadeGlowHaloWidth(2,1000,90,1920);
 assert(far>3 && far<3.2f);
 assert(R_GrenadeGlowHaloWidth(2,20000,90,1920)==8);
 printf("PASS: grenade ownership, fuse validity, VM restoration, entity identity and accelerating pulses (%d in final 500 ms)\n",final_half);
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-grenade-glow-') as tmp:
    work = Path(tmp)
    (work / 'test.c').write_text(source)
    cc = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / 'test'
    if Path(cc[0]).stem.lower() == 'cl':
        binary = binary.with_suffix('.exe')
        command = [*cc, '/nologo', '/std:c11', '/W3', '/fsanitize=address',
                   str(work / 'test.c'), '/Fe:' + str(binary)]
    else:
        command = [*cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', str(work / 'test.c'), '-lm', '-o', str(binary)]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
