#!/usr/bin/env python3
"""Compile the real alias eligibility/preparation gates with recording stubs."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

renderer = (Path(__file__).resolve().parents[2] / 'Quake/r_alias.c').read_text()

def function(name):
    match = re.search(r'(?:static )?(?:qboolean|int) ' + name + r'\s*\([^;]*?\)\s*\{', renderer)
    assert match, name
    start = end = match.end()
    depth = 1
    while depth:
        depth += (renderer[end] == '{') - (renderer[end] == '}')
        end += 1
    return renderer[match.start():end]

source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define countof(a) (sizeof(a)/sizeof((a)[0]))
#define mod_alias 1
#define EFLAGS_VIEWMODEL 1
#define EFLAGS_COLOURMAPPED 2
#define EF_NOSHADOW 1
#define ENTSCALE_DEFAULT 16
#define PV_QUAKE1 1
#define ALIAS_GLSL_BASIC 0
#define ALIAS_OUTLINE_PHASE_NORMAL 0
#define ALIAS_INST_PREP_FALLBACK 0
#define ALIAS_INST_PREP_CULLED 1
#define ALIAS_INST_PREP_OK 2
#define ENTALPHA_DECODE(a) (a)
#define VectorCopy(a,b) memcpy(b,a,sizeof(float)*3)
typedef struct {float value;} cvar_t;
typedef struct skintextures_s {void *base,*luma,*lower,*upper;} skintextures_t;
typedef struct {int program;} aliasglsl_t;
typedef struct {int pose1,pose2;} lerpdata_t;
typedef struct {int poseverttype,nextsurface,numbones,numindexes,numverts_vbo,nummorphposes,numskins;float scale[3],scale_origin[3];skintextures_t textures[1][4];} aliashdr_t;
typedef struct {int type,needload,meshvbo,meshindexesvbo;char *name;aliashdr_t *hdr;} qmodel_t;
typedef struct {qmodel_t *model;int eflags,effects,skinnum,alias_outline_order;float alpha;struct {int colormap,scale,tagentity,glowmod[3];} netstate;} entity_t;
typedef struct {entity_t *ent;qmodel_t *model;lerpdata_t lerp;float scale[3],scale_origin[3];skintextures_t tex;int skin;} alias_inst_rec_t;
static cvar_t gl_alias_instancing={1},r_outline={5},gl_fullbrights={1};
static aliasglsl_t r_alias_inst_glsl={1},r_alias_glsl[1]={{1}};
static int gl_bmodel_instancing_able=1,r_drawflat_cheatsafe,r_fullbright_cheatsafe,r_lightmap_cheatsafe;
static int r_alias_outline_collecting=1,gl_stencilbits=8,r_alias_outline_phase;
static entity_t *r_deferred_alias_outlines[2];
static int r_num_deferred_alias_outlines,outline_calls,culled;
static qboolean r_alias_outline_order_changed;
static qboolean R_QueueDeferredAliasOutline(entity_t *e);
static float entalpha;
static struct {entity_t viewent;double time;} cl;
static void *Mod_Extradata(qmodel_t *m) {return m->hdr;}
static void R_SetupAliasFrame(aliashdr_t *h,entity_t *e,lerpdata_t *l) {l->pose1=l->pose2=0;}
static void R_SetupEntityTransform(entity_t *e,lerpdata_t *l) {}
/* Only the culling outcome matters to this preparation test. */
#define R_CullModelForEntityTransform(e,o,a) (culled)
static void R_DrawAliasModelOutline(aliasglsl_t *g,aliashdr_t *h,lerpdata_t *l,entity_t *e) {
 if(!r_alias_outline_collecting || !gl_stencilbits || r_alias_outline_phase || entalpha!=1 || r_num_deferred_alias_outlines==countof(r_deferred_alias_outlines)) {puts("FAIL: unsafe outline queue call");exit(1);}
 R_QueueDeferredAliasOutline(e);outline_calls++;
}
#define CHECK(c,msg) do {if(!(c)) {puts("FAIL: " msg);return 1;}} while(0)
'''
source += '\n'.join(function(n) for n in ('R_QueueDeferredAliasOutline', 'R_CompareDeferredAliasOutlines', 'R_AliasInst_SpecialModel', 'R_AliasInst_Eligible', 'R_AliasInst_Prepare'))
source += r'''
int main(void) {
 aliashdr_t h={0};qmodel_t m={mod_alias,0,1,1,"progs/knight.mdl",&h};
 entity_t e={0},special={0};alias_inst_rec_t rec;
 e.model=&m;e.alpha=1;e.netstate.scale=ENTSCALE_DEFAULT;
 e.netstate.glowmod[0]=e.netstate.glowmod[1]=e.netstate.glowmod[2]=32;
 h.poseverttype=PV_QUAKE1;h.numindexes=6;h.numverts_vbo=4;h.nummorphposes=1;h.numskins=1;
 CHECK(R_AliasInst_Eligible(&e),"deferred opaque fill should be eligible with outlines enabled");
 CHECK(R_AliasInst_Prepare(&e,&rec)==ALIAS_INST_PREP_OK,"compatible model should prepare");
 CHECK(outline_calls==1 && r_deferred_alias_outlines[0]==&e,"instanced fill must retain its deferred outline");
 r_num_deferred_alias_outlines=countof(r_deferred_alias_outlines);
 CHECK(R_AliasInst_Prepare(&e,&rec)==ALIAS_INST_PREP_FALLBACK,"full outline queue must fall back before drawing");
 CHECK(outline_calls==1,"full queue must not call outline renderer");
 r_num_deferred_alias_outlines=0;
 culled=1;
 CHECK(R_AliasInst_Prepare(&e,&rec)==ALIAS_INST_PREP_CULLED && outline_calls==1,"culled model must not queue");
 culled=0;h.nextsurface=1;
 CHECK(R_AliasInst_Prepare(&e,&rec)==ALIAS_INST_PREP_FALLBACK && outline_calls==1,"multi-surface fallback");
 h.nextsurface=0;h.textures[0][0].lower=&e;
 CHECK(R_AliasInst_Prepare(&e,&rec)==ALIAS_INST_PREP_FALLBACK && outline_calls==1,"colormapped skin fallback");
 h.textures[0][0].lower=NULL;
 r_alias_outline_collecting=0;CHECK(!R_AliasInst_Eligible(&e),"immediate outlines must stay on normal path");
 r_alias_outline_collecting=1;gl_stencilbits=0;CHECK(!R_AliasInst_Eligible(&e),"no-stencil fallback");
 gl_stencilbits=8;r_alias_outline_phase=1;CHECK(!R_AliasInst_Eligible(&e),"outline replay must not instance");
 r_alias_outline_phase=0;e.alpha=.5f;CHECK(!R_AliasInst_Eligible(&e),"translucent fallback");
 e.alpha=1;m.name="progs/player.mdl";CHECK(!R_AliasInst_Eligible(&e),"player/xray fallback");
 m.name="progs/knight.mdl";r_outline.value=0;r_alias_outline_collecting=0;gl_stencilbits=0;
 CHECK(R_AliasInst_Eligible(&e) && R_AliasInst_Prepare(&e,&rec)==ALIAS_INST_PREP_OK && outline_calls==1,"outlines off must preserve existing batching without queuing");
 special.alias_outline_order=8;e.alias_outline_order=2;r_num_deferred_alias_outlines=0;
 CHECK(R_QueueDeferredAliasOutline(&special) && R_QueueDeferredAliasOutline(&e),"queue entities in batch completion order");
 CHECK(r_alias_outline_order_changed,"late batch collection must request ordering before replay");
 qsort(r_deferred_alias_outlines,r_num_deferred_alias_outlines,sizeof(r_deferred_alias_outlines[0]),R_CompareDeferredAliasOutlines);
 CHECK(r_deferred_alias_outlines[0]==&e && r_deferred_alias_outlines[1]==&special,"colored and black outlines must replay in original visible order");
 CHECK(R_QueueDeferredAliasOutline(&e) && r_num_deferred_alias_outlines==2,"duplicate model fallback must not replay an outline twice");
 puts("PASS: outline-aware instancing eligibility, queueing and fallbacks");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-outline-instance-') as tmp:
    work = Path(tmp)
    (work/'test.c').write_text(source)
    cc = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work/('test.exe' if os.name == 'nt' else 'test')
    args = [*cc, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}'] if Path(cc[0]).stem.lower() == 'cl' else [*cc, '-std=c99', '-Wall', '-O2', 'test.c', '-o', str(binary)]
    subprocess.run(args,cwd=work,check=True)
    subprocess.run([str(binary)],check=True)
