"""Actual preparation and commit keep exact item colors off the commit thread."""
import os
from pathlib import Path
import shlex, subprocess, sys, tempfile
ROOT=Path(__file__).resolve().parents[2]
text=(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'Quake/gl_texmgr.c').read_text(encoding='utf-8')
def function(signature):
 start=text.index(signature);opening=text.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]+'\n'
color=text[text.index('static int TexMgr_ItemColorBucket'):text.index('\nqboolean TexMgr_GetItemColor')]
color=color.replace('static qboolean TexMgr_SampleItemColor (','static qboolean TexMgr_SampleItemColor_impl (',1)
split=color.index('/* Sample while RGBA')
color=color[:split]+r'''
static qboolean TexMgr_SampleItemColor(const byte *p,size_t w,size_t h,vec3_t c) {
    if(worker_phase)worker_samples++;else commit_samples++;
    return TexMgr_SampleItemColor_impl(p,w,h,c);
}
'''+color[split:]
start=text.index('typedef enum texprep_status_e')
types=text[start:text.index('\ndouble texmgr_load_time;',start)]
helpers=''.join(function(s) for s in ('static qboolean TexPrep_CheckedBytes','static qboolean TexPrep_CheckedAdd','static void TexPrep_FreeResult'))
if 'static void TexPrep_PrepareItemColor (' in text:
 helpers+=function('static void TexPrep_PrepareItemColor (')
prepare=function('static void TexPrep_PrepareOne (')
commit=function('static gltexture_t *TexPrep_Commit (')
call='TexPrep_Commit (&state, &texture);' if 'const texprep_jobstate_t *state, gltexture_t *glt)' in commit else 'TexPrep_Commit (&state);'
source=r'''
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <float.h>
typedef unsigned char byte;typedef int qboolean;typedef float vec3_t[3];typedef int GLsizei,GLint;
#define false 0
#define true 1
#define q_min(a,b) ((a)<(b)?(a):(b))
#define q_max(a,b) ((a)>(b)?(a):(b))
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define TEXPREF_ALPHA 1
#define TEXPREF_CONCHARS 2
#define TEXPREF_MIPMAP 4
#define SRC_INDEXED 0
#define SRC_RGBA 1
#define GL_TEXTURE_2D 1
#define GL_RGBA 2
#define GL_UNSIGNED_BYTE 3
#define TEXPREP_MAX_MIPS 32
#define CHECK(c,m) do {if(!(c)){puts("FAIL: " m);exit(1);}}while(0)
typedef struct {int type;} color_t;
typedef struct gltexture_s {
 void *owner;char name[80],source_file[80];int source_width,source_height,source_format,source_offset,source_crc,width,height,flags;
 color_t shirt,pants;int itemcolor_valid;vec3_t itemcolor;
} gltexture_t;
typedef struct {void *owner;const char *name,*source_file;unsigned int width,height,format,source_offset,flags;const byte *data;gltexture_t **destination;} texmgr_loadjob_t;
static gltexture_t texture;
static int worker_phase,worker_samples,commit_samples;
static const int gl_alpha_format=7,gl_solid_format=8;
static double Sys_DoubleTime(void){return 0;}
static int CRC_Block(const byte *p,int n){(void)p;(void)n;return 0;}
static void TexMgr_8to32Into(const byte *p,size_t n,const unsigned int *pal,unsigned int *out){(void)p;(void)n;(void)pal;(void)out;CHECK(0,"unexpected indexed branch");}
static void TexMgr_AlphaEdgeFix(byte *p,int w,int h){(void)p;(void)w;(void)h;CHECK(0,"unexpected alpha-edge branch");}
static void TexMgr_ResampleTextureInto(const unsigned *p,int w,int h,int a,unsigned *o){(void)p;(void)w;(void)h;(void)a;(void)o;CHECK(0,"unexpected resample branch");}
static unsigned *TexMgr_MipMapW(unsigned *p,int w,int h){(void)w;(void)h;CHECK(0,"unexpected picmip");return p;}
static unsigned *TexMgr_MipMapH(unsigned *p,int w,int h){(void)w;(void)h;CHECK(0,"unexpected picmip");return p;}
static int TexMgr_IsPowerOfTwo(int n){return n>0&&!(n&(n-1));}
static void TexMgr_GenerateMipLevelWithParticipants(const unsigned *s,unsigned *d,int w,int h,int nw,int nh,int parts){(void)s;(void)d;(void)w;(void)h;(void)nw;(void)nh;(void)parts;CHECK(0,"unexpected extra mip");}
static gltexture_t *TexMgr_NewTexture(void){memset(&texture,0,sizeof(texture));return &texture;}
static size_t q_strlcpy(char *d,const char *s,size_t n){size_t z=strlen(s),k=z<n-1?z:n-1;memcpy(d,s,k);d[k]=0;return z;}
static void GL_Bind(gltexture_t *t){(void)t;}
static void TexMgr_VerifyLevel(const char *n,int f,int l,int w,int h,const void *p){(void)n;(void)f;(void)l;(void)w;(void)h;(void)p;}
static void glTexImage2D(int t,int l,int f,int w,int h,int b,int e,int type,const void *p){(void)t;(void)l;(void)f;(void)w;(void)h;(void)b;(void)e;(void)type;CHECK(p,"upload base present");}
static void TexMgr_SetFilterModes(gltexture_t *t){(void)t;}
'''+color+types+helpers+prepare+commit+r'''
static void fixture(unsigned int width,unsigned int height,int owner,int transparent) {
 texmgr_loadjob_t job={0};texprep_jobstate_t state={0};size_t n=(size_t)width*height*4,i;
 byte *pixels=(byte*)malloc(n);vec3_t expected={0};int valid;unsigned int seed=42;
 CHECK(pixels,"fixture allocation");
 for(i=0;i<n;i++){seed=seed*1664525u+1013904223u;pixels[i]=(byte)(seed>>24);if(i%4==3&&transparent)pixels[i]=0;}
 job.owner=owner?(void*)1:NULL;job.name="fixture";job.source_file="fixture.png";job.width=width;job.height=height;job.format=SRC_RGBA;job.data=pixels;
 state.job=&job;state.work_width=state.target_width=width;state.work_height=state.target_height=height;state.source_size=n;state.mip_participants=1;
 worker_samples=commit_samples=0;worker_phase=1;TexPrep_PrepareOne(&state);worker_phase=0;
 CHECK(state.result.status==TEXPREP_OK,"RGBA preparation succeeds");
 CHECK(state.result.levels[0].size==n&&!memcmp(state.result.pixels,pixels,n),"pixels remain byte-identical");
 valid=owner&&TexMgr_SampleItemColor_impl(state.result.pixels,width,height,expected);
 memset(&texture,0,sizeof(texture));
 '''+call+r'''
 CHECK(worker_samples==(owner?1:0)&&commit_samples==0,"sample item colors in preparation, never during GL commit");
 CHECK(texture.itemcolor_valid==valid,"valid color state retained");
 if(valid)CHECK(!memcmp(texture.itemcolor,expected,sizeof(expected)),"bit-identical selected item RGB");
 TexPrep_FreeResult(&state.result);free(pixels);
}
int main(void) {
 fixture(1,1,1,0);fixture(64,64,1,0);fixture(129,17,1,0);fixture(16,513,1,0);
 fixture(128,64,1,1);fixture(16,16,0,0);
 puts("PASS: real preparation/commit preserve pixels and exact item colors for square, wide, tall, transparent and ownerless textures; commit does no sampling");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-prep-colors-') as tmp:
 work=Path(tmp);(work/'test.c').write_text(source)
 cc=shlex.split(os.environ.get('CC','cl' if os.name=='nt' else 'cc'));binary=work/('test.exe' if os.name=='nt' else 'test')
 args=[*cc,'/nologo','/W3','/O2','test.c',f'/Fe:{binary}'] if Path(cc[0]).stem.lower()=='cl' else [*cc,'-std=c99','-Wall','-Wextra','-O2','-fsanitize=address,undefined','test.c','-o',str(binary)]
 subprocess.run(args,cwd=work,check=True);subprocess.run([str(binary)],cwd=work,check=True)
