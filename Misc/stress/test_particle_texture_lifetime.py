"""Particle textures must survive UI refreshes when both use the same name.

Compile the real particle texture lookup/upload phase and texture-manager
lookup/purge functions. Controlled image/GL boundaries simulate texture-record
reuse during HUD changes without clearing live particle state.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
manager = (ROOT / 'Quake/gl_texmgr.c').read_text()
particles = (ROOT / 'Quake/r_part_fte.c').read_text()


def function(source, name):
    match = re.search(r'(?:static )?(?:void|gltexture_t \*)\s*' + name + r'\s*\([^;]*?\)\s*\{', source)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


# Keep the real successful-image resource phase. Procedural fallback particles
# do not borrow named UI textures and are outside this regression.
load = function(particles, 'P_LoadTexture')
load = load[:load.index('\n\tif (!ptype->looks.texture)\n\t{\n\t\t//the specified')] + '\n}\n'
lookup = ''
if 'TexMgr_FindTextureWithFlags (' in manager:
    lookup += function(manager, 'TexMgr_FindTextureWithFlags') + '\n'
lookup += function(manager, 'TexMgr_FindTexture')
source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef int qboolean;
typedef unsigned char byte;
typedef struct {int unused;} qmodel_t;
typedef unsigned long src_offset_t;
enum srcformat {SRC_INDEXED, SRC_LIGHTMAP, SRC_RGBA, SRC_EXTERNAL};
#define true 1
#define false 0
#define MAX_QPATH 64
#define TEXPREF_UI 0x80000u
#define TEXPREF_PREMULTIPLY 0x1000u
#define TEXPREF_NEAREST 4u
#define TEXPREF_LINEAR 2u
#define TEXPREF_NOPICMIP 0x80u
#define TEXPREF_ALPHA 8u
#define CHECK(c,m) do {if (!(c)) {puts("FAIL: " m);exit(1);}} while (0)
typedef struct gltexture_s {
 struct gltexture_s *next; qmodel_t *owner; char name[64]; unsigned flags; int alive, pixel;
} gltexture_t;
typedef struct {gltexture_t *texture;int premul, nearest;} looks_t;
typedef struct {int nummodels; struct {qmodel_t *model;} models[1]; char texname[64]; looks_t looks;} part_type_t;
static gltexture_t records[4], *active_gltextures;
static int decodes, allocations;
static char *va(const char *format, ...) {
 static char value[128];va_list ap;va_start(ap,format);vsnprintf(value,sizeof(value),format,ap);va_end(ap);return value;
}
static void q_snprintf(char *out,size_t size,const char *format,...) {
 va_list ap;va_start(ap,format);vsnprintf(out,size,format,ap);va_end(ap);
}
static int Hunk_LowMark(void) {return 0;}
static void Hunk_FreeToLowMark(int mark) {(void)mark;}
static byte *Image_LoadImage(const char *name,int *width,int *height,enum srcformat *format,qboolean *malloced) {
 byte *pixels=malloc(4);CHECK(pixels&&strstr(name,"shared"),"expected shared image");
 memcpy(pixels,"\x14\x64\xc8\xff",4);
 *width=*height=1;*format=SRC_RGBA;*malloced=true;decodes++;return pixels;
}
static gltexture_t *new_texture(const char *name,unsigned flags,int pixel) {
 for(int i=0;i<4;i++) if(!records[i].alive) {
  gltexture_t *texture=&records[i];memset(texture,0,sizeof(*texture));texture->alive=true;
  texture->flags=flags;texture->pixel=pixel;snprintf(texture->name,sizeof(texture->name),"%s",name);
  texture->next=active_gltextures;active_gltextures=texture;allocations++;return texture;
 }
 CHECK(false,"texture records leaked");return NULL;
}
static gltexture_t *TexMgr_LoadImage(qmodel_t *owner,const char *name,int width,int height,
 enum srcformat format,byte *data,const char *file,src_offset_t offset,unsigned flags) {
 (void)file;(void)offset;CHECK(!owner&&width==1&&height==1&&format==SRC_RGBA&&data,"particle upload unchanged");
 CHECK(!(flags&TEXPREF_UI),"particle allocation must outlive UI cache");return new_texture(name,flags,data[0]);
}
static void TexMgr_FreeTexture(gltexture_t *texture) {
 gltexture_t **link=&active_gltextures;CHECK(texture&&texture->alive,"free a live texture");
 while(*link!=texture) {CHECK(*link,"texture is registered");link=&(*link)->next;}
 *link=texture->next;texture->alive=false;
}
'''
source += lookup + '\n' + function(manager, 'TexMgr_FreeTextures') + '\n' + load
source += r'''
int main(void) {
 part_type_t first={0},second={0};strcpy(first.texname,"shared");strcpy(second.texname,"shared");
 gltexture_t *ui=new_texture("shared",TEXPREF_UI,90);
 P_LoadTexture(&first,true);
 CHECK(first.looks.texture!=ui,"particle borrowed a UI record that a HUD refresh frees");
 gltexture_t *particle=first.looks.texture;
 CHECK(particle&&particle->alive&&particle->pixel==20&&decodes==1,"particle owns the expected image");
 /* A newer UI lookup must not hide an older, reusable particle texture. */
 TexMgr_FreeTextures(TEXPREF_UI,TEXPREF_UI);ui=new_texture("shared",TEXPREF_UI,91);
 CHECK(TexMgr_FindTexture(NULL,"shared")==ui,"ordinary lookup still returns newest matching record");
 P_LoadTexture(&second,true);CHECK(second.looks.texture==particle&&decodes==1,"same-name particle types reuse their surviving image");
 first.nummodels=1;first.models[0].model=(qmodel_t*)1;
 for(int i=0;i<1000;i++) {
  TexMgr_FreeTextures(TEXPREF_UI,TEXPREF_UI);
  ui=new_texture("shared",TEXPREF_UI,100+i);
  P_LoadTexture(&first,false);P_LoadTexture(&second,true);
  CHECK(first.looks.texture==particle&&second.looks.texture==particle&&particle->pixel==20,"live particles survive HUD record reuse");
  CHECK(!first.models[0].model,"map resource pass still invalidates particle model pointers");
 }
 CHECK(decodes==1&&allocations==1003,"HUD changes do not decode particle images again");
 TexMgr_FreeTextures(0,0);CHECK(!active_gltextures,"all records released at shutdown");
 puts("PASS: shared particle/UI names, HUD record reuse, particle sharing, stable pixels, no repeated decoding");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-particle-lifetime-') as temporary:
    work = Path(temporary)
    (work / 'test.c').write_text(source)
    compiler = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    command = ([*compiler, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}']
               if Path(compiler[0]).stem.lower() == 'cl' else
               [*compiler, '-std=c99', '-Wall', '-Wextra', '-O2', '-fsanitize=address,undefined', 'test.c', '-o', str(binary)])
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], cwd=work, check=True)
