"""Exercise real picture draws from four-byte-aligned cache records with UBSan."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'Quake/gl_draw.c').read_text()

def function(name):
    match = re.search(r'void ' + name + r'\s*\([^;]*?\)\s*\{', text)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]

start = text.index('typedef struct\n{\n\tgltexture_t *gltexture;')
metadata = text[start:text.index('} glpic_t;', start) + len('} glpic_t;')]
wad = (ROOT / 'Quake/wad.h').read_text()
start = wad.index('typedef struct\n{\n\tunsigned int\twidth, height;')
picture = wad[start:wad.index('} qpic_t;', start) + len('} qpic_t;')]
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
typedef struct {int id;} gltexture_t;
''' + picture + '\n' + metadata + r'''
#define GL_QUADS 7
static int scrap_dirty=1,uploads,begins,ends,texcount,vertcount;
static float texcoords[4][2],vertices[4][2],gl_menu_alpha=1;
static gltexture_t *bound;
static qpic_t *draw_backtile;
static void Scrap_Upload(void) {uploads++;scrap_dirty=0;}
static void GL_Bind(gltexture_t *texture) {bound=texture;}
static void glBegin(int mode) {assert(mode==GL_QUADS);begins++;}
static void glEnd(void) {ends++;}
static void glTexCoord2f(float s,float t) {assert(texcount<4);texcoords[texcount][0]=s;texcoords[texcount++][1]=t;}
static void glVertex2f(float x,float y) {assert(vertcount<4);vertices[vertcount][0]=x;vertices[vertcount++][1]=y;}
static void glColor4f(float r,float g,float b,float a) {assert(r==1&&g==1&&b==1&&a==1);}
''' + '\n'.join(function(name) for name in ['Draw_Pic', 'Draw_ScaledPic', 'Draw_SubPic_QW', 'Draw_TileClear']) + r'''
static void reset(void) {begins=ends=texcount=vertcount=0;bound=NULL;}
static void check(gltexture_t *texture,float width,float height,float s1,float t1,float s2,float t2) {
 assert(bound==texture&&begins==1&&ends==1&&texcount==4&&vertcount==4);
 float uv[4][2]={{s1,t1},{s2,t1},{s2,t2},{s1,t2}};
 float xy[4][2]={{5,7},{5+width,7},{5+width,7+height},{5,7+height}};
 assert(!memcmp(texcoords,uv,sizeof(uv))&&!memcmp(vertices,xy,sizeof(xy)));
}
int main(void) {
 struct {qpic_t pic;byte padding[32];} *cache=calloc(2,sizeof(*cache));
 assert(cache);gltexture_t texture={42};glpic_t gl={&texture,0.125f,0.25f,0.75f,0.875f};
 assert(sizeof(gl)==sizeof(void*)+4*sizeof(float));
 int unaligned=0;
 for(int i=0;i<2;i++) {
  qpic_t *pic=&cache[i].pic;pic->width=24;pic->height=16;memcpy(pic->data,&gl,sizeof(gl));
  if((uintptr_t)pic->data%sizeof(void*)) unaligned++;
  reset();Draw_Pic(5,7,pic);check(&texture,24,16,gl.sl,gl.tl,gl.sh,gl.th);
  reset();Draw_ScaledPic(5,7,pic,1.5f);check(&texture,36,24,gl.sl,gl.tl,gl.sh,gl.th);
  reset();Draw_SubPic_QW(5,7,pic,2,3,8,6);
  check(&texture,8,6,gl.sl+2*(gl.sh-gl.sl)/24,gl.tl+3*(gl.th-gl.tl)/16,
                   gl.sl+10*(gl.sh-gl.sl)/24,gl.tl+9*(gl.th-gl.tl)/16);
  reset();draw_backtile=pic;Draw_TileClear(5,7,24,16);check(&texture,24,16,5/64.0f,7/64.0f,29/64.0f,23/64.0f);
 }
 if(sizeof(void*)==8) assert(unaligned==1);
 reset();Draw_Pic(5,7,NULL);assert(!bound&&!begins&&!texcount&&!vertcount);
 assert(uploads==1);free(cache);
 puts("PASS: aligned and four-byte cache records, actual normal/scaled/cropped/tiled draws, exact UVs and vertices");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-draw-alignment-') as temporary:
    work = Path(temporary)
    (work / 'test.c').write_text(source)
    compiler = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    command = ([*compiler, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}']
               if Path(compiler[0]).stem.lower() == 'cl' else
               [*compiler, '-std=c99', '-Wall', '-Wextra', '-O2', '-fsanitize=address,undefined',
                'test.c', '-o', str(binary)])
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], cwd=work, check=True,
                   env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
