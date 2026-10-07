"""Real texture-record/commit functions: pooled names, ordering and legacy path."""
import os
from pathlib import Path
import shlex, subprocess, sys, tempfile
ROOT=Path(__file__).resolve().parents[2]
text=(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'Quake/gl_texmgr.c').read_text(encoding='utf-8')
start=text.index('static gltexture_t *TexMgr_AllocTexture (') if 'static gltexture_t *TexMgr_AllocTexture (' in text else text.index('gltexture_t *TexMgr_NewTexture (')
records=text[start:text.index('\nstatic void GL_DeleteTexture',start)]
start=text.index('static gltexture_t *TexPrep_Commit (')
commits=text[start:text.index('\nvoid TexMgr_LoadImageBatch',start)]
if 'static void TexPrep_CommitBatch (' not in commits:
    commits+=r'''
static void TexPrep_CommitBatch(texprep_jobstate_t *states,texmgr_loadjob_t *jobs,size_t count) {
    size_t i;for(i=0;i<count;i++)*jobs[i].destination=TexPrep_Commit(&states[i]);
}
'''
source=r'''
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int GLuint;
typedef int GLint;typedef int GLsizei;
typedef float vec3_t[3];
typedef int qboolean;
#define true 1
#define false 0
#define countof(x) (sizeof(x)/sizeof((x)[0]))
#define q_min(a,b) ((a)<(b)?(a):(b))
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define GL_TEXTURE_2D 1
#define GL_RGBA 2
#define GL_UNSIGNED_BYTE 3
#define TEXPREF_ALPHA 1
#define CHECK(c,m) do {if(!(c)){puts("FAIL: " m);exit(1);}}while(0)
typedef struct {int type;} color_t;
typedef struct gltexture_s {
 struct gltexture_s *next;GLuint texnum;void *owner;char name[80],source_file[80];
 int source_width,source_height,source_format,source_offset,source_crc,width,height,flags;
 color_t shirt,pants;int itemcolor_valid,retained,grass_stored;vec3_t itemcolor;
} gltexture_t;
typedef struct {void *owner;const char *name,*source_file;int width,height,format,source_offset,flags;gltexture_t **destination;} texmgr_loadjob_t;
typedef struct {unsigned int width,height;size_t offset;} texprep_level_t;
typedef struct {unsigned int width,height,effective_flags,num_levels;unsigned char *pixels;texprep_level_t levels[2];qboolean itemcolor_valid;vec3_t itemcolor;} texprep_result_t;
typedef struct {texmgr_loadjob_t *job;texprep_result_t result;unsigned int source_crc;} texprep_jobstate_t;
static gltexture_t *free_gltextures,*active_gltextures;static int numgltextures;
static const int gl_alpha_format=7,gl_solid_format=8;
static GLuint next_name=1,bound;static int gen_calls,generated,uploads,filters;static GLuint upload_names[1024];
static void Sys_Error(const char *s,...) {puts(s);exit(2);}
static size_t q_strlcpy(char *d,const char *s,size_t n) {size_t z=strlen(s),k=z<n-1?z:n-1;memcpy(d,s,k);d[k]=0;return z;}
static void glGenTextures(GLsizei n,GLuint *p) {int i;CHECK(n>0&&n<=1024,"bounded GLsizei generation");gen_calls++;generated+=n;for(i=0;i<n;i++)p[i]=next_name++;}
static void GL_Bind(gltexture_t *t) {bound=t->texnum;}
static void glTexImage2D(int target,int level,int fmt,int w,int h,int border,int external,int type,const void *p) {
 CHECK(target==1&&external==2&&type==3&&!border,"upload API unchanged");
 CHECK((fmt==7||fmt==8)&&w==2&&h==2&&level==0&&p,"mip metadata unchanged");
 upload_names[uploads++]=bound;
}
static void TexMgr_VerifyLevel(const char *n,int f,int l,int w,int h,const void *p) {(void)n;(void)f;(void)l;(void)w;(void)h;(void)p;}
static void TexMgr_CacheItemColor(gltexture_t *t,const unsigned char *p) {CHECK(p,"base pixels valid");t->itemcolor_valid=t->owner!=NULL;}
static void TexMgr_SetFilterModes(gltexture_t *t) {CHECK(bound==t->texnum,"filter order follows upload");filters++;}
'''+records+commits+r'''
int main(void) {
 texmgr_loadjob_t jobs[130];texprep_jobstate_t states[130];gltexture_t *out[130]={0};unsigned char pixels[16]={0};
 int i,previous;
 memset(jobs,0,sizeof(jobs));memset(states,0,sizeof(states));
 for(i=0;i<130;i++){
  jobs[i].owner=(void*)1;jobs[i].name="fixture";jobs[i].source_file="map.bsp";jobs[i].width=jobs[i].height=2;jobs[i].format=9;jobs[i].source_offset=100+i;jobs[i].destination=&out[i];
  states[i].job=&jobs[i];states[i].source_crc=200+i;states[i].result.width=states[i].result.height=2;states[i].result.pixels=pixels;states[i].result.num_levels=1;
  states[i].result.levels[0].width=states[i].result.levels[0].height=2;states[i].result.effective_flags=i%2;
  states[i].result.itemcolor_valid=true;
 }
 TexPrep_CommitBatch(states,jobs,130);
 CHECK(gen_calls==1,"one bulk name generation serves a whole level");
 CHECK(numgltextures==130&&uploads==130&&filters==130,"all textures committed once");
 for(i=0;i<130;i++){
  int j;
  CHECK(out[i]&&out[i]->texnum&&upload_names[i]==out[i]->texnum,"job/name/upload order");
  for(j=0;j<i;j++) CHECK(out[j]->texnum!=out[i]->texnum,"names are unique");
  CHECK(out[i]->source_crc==200+i&&out[i]->source_offset==100+i,"source metadata retained");
  CHECK(!out[i]->retained&&!out[i]->grass_stored&&out[i]->itemcolor_valid,"record state reset");
 }
 previous=gen_calls;TexPrep_CommitBatch(states,jobs,0);CHECK(gen_calls==previous,"empty batch has no GL work");
 {gltexture_t *legacy=TexMgr_NewTexture();CHECK(gen_calls==previous&&legacy->texnum&&numgltextures==131,"legacy caller draws from the pool");}
 for(i=131;i<generated;i++) TexMgr_NewTexture();
 CHECK(gen_calls==1&&texmgr_numnames==0,"pool drains before refilling");
 {gltexture_t *next=TexMgr_NewTexture();CHECK(gen_calls==2&&next->texnum==(GLuint)generated,"empty pool refills in bulk");}
 texmgr_numnames=0;{gltexture_t *fresh=TexMgr_NewTexture();CHECK(gen_calls==3,"reset pool (new context) generates fresh names");(void)fresh;}
 puts("PASS: pooled texture names, unique names, job/upload/filter order, metadata, zero count, legacy allocation, refill and reset");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-name-batch-') as tmp:
 work=Path(tmp);(work/'test.c').write_text(source)
 cc=shlex.split(os.environ.get('CC','cl' if os.name=='nt' else 'cc'));binary=work/('test.exe' if os.name=='nt' else 'test')
 args=[*cc,'/nologo','/W3','/O2','test.c',f'/Fe:{binary}'] if Path(cc[0]).stem.lower()=='cl' else [*cc,'-std=c99','-Wall','-Wextra','-O2','-fsanitize=address,undefined','test.c','-o',str(binary)]
 subprocess.run(args,cwd=work,check=True);subprocess.run([str(binary)],cwd=work,check=True)
