"""Exercise real brush-cache eligibility and generation handling with C stubs."""
from pathlib import Path
import os, re, shlex, subprocess, tempfile

renderer=(Path(__file__).resolve().parents[2]/'Quake/r_world.c').read_text()
def function(name):
    m=re.search(r'(?:static )?(?:qboolean|bmodel_drawcache_t \*)\s*'+name+r'\s*\([^;]*?\)\s*\{',renderer)
    assert m,name
    end=m.end();depth=1
    while depth:
        depth+=(renderer[end]=='{')-(renderer[end]=='}');end+=1
    return renderer[m.start():end]

source=r'''
#include <stdio.h>
#include <stdlib.h>
typedef int qboolean;
#define true 1
#define false 0
#define mod_brush 1
#define ENTALPHA_DECODE(a) (a)
#define GL_RGB10_A2 1
#define SURF_DRAWTURB 16
#define SURF_DRAWTILED 32
#define SURF_DRAWSKY 4
#define SURF_NOTEXTURE 256
typedef struct {float value;} cvar_t;
typedef struct {int grass;} texture_t;
typedef struct {int unused;} gltexture_t;
typedef struct {int materialidx;} texinfo_t;
typedef struct {int numedges,flags,lightmaptexturenum;texinfo_t *texinfo;} msurface_t;
typedef struct {int texture;} batch_t;
typedef struct {int valid,unsupported,has_cached_dlight,numbatches,lightmap_count;unsigned generation;batch_t batches[1];} bmodel_drawcache_t;
typedef struct qmodel_s {int type,needload,nummodelsurfaces,submodelidx,numtextures;struct qmodel_s *submodelof;texture_t **textures;void *bmodel_drawcache;} qmodel_t;
typedef struct {qmodel_t *model;float alpha;int effects;} entity_t;
static struct {qmodel_t *worldmodel;} cl;
static cvar_t gl_bmodel_instancing={1},r_bmodelcache={1},gl_cull={1},gl_overbright={1};
static int gl_bmodel_instancing_able=1,r_world_instanced_program=1,gl_bmodel_vbo=1;
static int gl_vbo_able=1,GL_GenBuffersFunc=1,GL_BufferDataFunc=1,GL_DeleteBuffersFunc=1;
static int r_world_program=1,r_drawflat_cheatsafe,r_fullbright_cheatsafe,r_lightmap_cheatsafe,gl_lightmap_format;
static int lightmap_count=1,lightmaps_latecached,dlights,grass,blades,builds,cleans;
static unsigned gl_bmodel_vbo_generation=5;
static unsigned char skipbits[1],*skipsubmodels=skipbits;
static bmodel_drawcache_t cache={1,0,0,0,1,5};
static void R_BModelDrawCache_Cleanup(qmodel_t *m){m->bmodel_drawcache=NULL;cleans++;}
static bmodel_drawcache_t *R_BModelDrawCache_Build(qmodel_t *m){cache.generation=gl_bmodel_vbo_generation;cache.lightmap_count=lightmap_count;m->bmodel_drawcache=&cache;builds++;return &cache;}
static qboolean R_BModelDrawCache_HasActiveDlights(const entity_t *e){return dlights;}
static qboolean R_GrassEntityAllowsGrass(entity_t *e){return grass;}
static qboolean R_ModelHasActiveGrassBlades(qmodel_t *m){return blades;}
static qboolean R_TextureUsesSurfaceGrass(texture_t *t){return t->grass;}
#define CHECK(c,msg) do{if(!(c)){puts("FAIL: " msg);return 1;}}while(0)
'''
source+=function('R_BModelDrawCache_SurfaceSupported')
source+=function('R_BModelDrawCache_Get')
source+=function('R_CanInstanceBrushEntity')
# Exercise the real individual-draw gates up to GL submission; GL drawing is
# checked by the live image comparison, not simulated here.
draw=function('R_DrawBModelDrawCache')
source+=draw[:draw.index('\n\tif (R_BModelDrawCache_LightstylesChanged')]+'\n (void)i; (void)t; (void)animt; return true;\n}\n'
source+=r'''
int main(void){
 qmodel_t world={0},external={0},inline_model={0};entity_t e={0};bmodel_drawcache_t *out=NULL;
 texture_t texture={0};texture_t *textures[]={&texture};texinfo_t info={0};msurface_t surf={4,0,0,&info};
 cl.worldmodel=&world;external.type=mod_brush;external.submodelof=&external;external.nummodelsurfaces=4;external.numtextures=1;external.textures=textures;external.bmodel_drawcache=&cache;
 inline_model=external;inline_model.submodelof=&world;e.model=&external;e.alpha=1;
 CHECK(R_CanInstanceBrushEntity(&e,&out)&&out==&cache,"opaque external BSP must be eligible for batching");
 CHECK(R_DrawBModelDrawCache(&external,&e),"opaque external BSP must use cached individual drawing");
 skipbits[0]=1;
 CHECK(R_CanInstanceBrushEntity(&e,&out),"world scene-cache skip bits must not hide external BSPs");
 e.model=&inline_model;CHECK(!R_CanInstanceBrushEntity(&e,&out)&&out==NULL,"inline scene-cache skip must remain active");
 e.model=&external;skipbits[0]=0;e.alpha=.5f;
 CHECK(!R_CanInstanceBrushEntity(&e,&out)&&!R_DrawBModelDrawCache(&external,&e),"transparency must retain ordered drawing");
 e.alpha=1;e.effects=1;CHECK(!R_CanInstanceBrushEntity(&e,&out)&&!R_DrawBModelDrawCache(&external,&e),"special effects must fall back");e.effects=0;
 external.needload=1;CHECK(!R_CanInstanceBrushEntity(&e,&out),"unloaded models must fall back");external.needload=0;
 lightmaps_latecached=1;CHECK(!R_CanInstanceBrushEntity(&e,&out)&&!R_DrawBModelDrawCache(&external,&e),"late model uploads must run before caching");lightmaps_latecached=0;
 dlights=1;CHECK(!R_CanInstanceBrushEntity(&e,&out),"active dynamic light fallback");dlights=0;
 cache.has_cached_dlight=1;CHECK(!R_CanInstanceBrushEntity(&e,&out),"stale dynamic light fallback");cache.has_cached_dlight=0;
 cache.unsupported=1;cache.valid=0;CHECK(!R_CanInstanceBrushEntity(&e,&out)&&!R_DrawBModelDrawCache(&external,&e),"unsupported surface fallback");cache.unsupported=0;cache.valid=1;
 grass=blades=1;CHECK(!R_CanInstanceBrushEntity(&e,&out),"grass blades must retain their renderer");blades=0;cache.numbatches=1;texture.grass=1;
 CHECK(!R_CanInstanceBrushEntity(&e,&out),"shader grass tint fallback");grass=0;texture.grass=0;
 gl_bmodel_instancing.value=0;CHECK(!R_CanInstanceBrushEntity(&e,&out)&&R_DrawBModelDrawCache(&external,&e),"disabled instancing must retain cached individual draws");gl_bmodel_instancing.value=1;
 r_bmodelcache.value=0;CHECK(!R_CanInstanceBrushEntity(&e,&out)&&!R_DrawBModelDrawCache(&external,&e),"disabled cache fallback");r_bmodelcache.value=1;
 gl_bmodel_vbo_generation++;CHECK(R_CanInstanceBrushEntity(&e,&out)&&builds==1&&cleans==1,"video restart must invalidate external indices");
 lightmap_count++;CHECK(R_DrawBModelDrawCache(&external,&e)&&builds==2&&cleans==2,"changed lightmap layout must rebuild cache");
 CHECK(R_BModelDrawCache_SurfaceSupported(&external,&surf),"ordinary lightmapped surface");
 surf.flags=SURF_DRAWTURB;CHECK(!R_BModelDrawCache_SurfaceSupported(&external,&surf),"water must retain special path");
 surf.flags=SURF_DRAWSKY;CHECK(!R_BModelDrawCache_SurfaceSupported(&external,&surf),"sky must retain special path");
 surf.flags=SURF_NOTEXTURE;CHECK(!R_BModelDrawCache_SurfaceSupported(&external,&surf),"missing texture fallback");
 puts("PASS: external brush cache/instancing eligibility, fallbacks, skip bits and buffer generations");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-brush-cache-') as tmp:
    work=Path(tmp);(work/'test.c').write_text(source)
    cc=shlex.split(os.environ.get('CC','cl' if os.name=='nt' else 'cc'))
    binary=work/('test.exe' if os.name=='nt' else 'test')
    args=[*cc,'/nologo','/W3','/O2','test.c',f'/Fe:{binary}'] if Path(cc[0]).stem.lower()=='cl' else [*cc,'-std=c99','-Wall','-O2','test.c','-o',str(binary)]
    subprocess.run(args,cwd=work,check=True)
    subprocess.run([str(binary)],check=True)
