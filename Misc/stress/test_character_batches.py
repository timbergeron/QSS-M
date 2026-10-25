"""Compare production console/menu glyph batches with individual glyph draws.

The GL double checks legal begin/end boundaries and records ordered geometry,
UVs, colors, blend/alpha/texture state, and final state. No window is required.
Set CC/CFLAGS to exercise another compiler, sanitizers, or unsigned char.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
draw = (ROOT / "Quake/gl_draw.c").read_text()
header = (ROOT / "Quake/draw.h").read_text()
menu = (ROOT / "Quake/menu.c").read_text()
console = (ROOT / "Quake/console.c").read_text()


def function(source, name):
    match = re.search(r"(?m)^(?:static )?(?:void|float|int)\s+" + name + r"\s*\(", source)
    assert match, name
    return source[match.start():source.index("\n}", match.start()) + 2]


def reference(source):
    # Keep the production layout/selection logic, submit each glyph separately.
    source = source.replace("Draw_BatchCharacter", "ReferenceCharacter")
    return source.replace("Draw_EndCharacterBatch", "ReferenceEnd")


colour = header[header.index("typedef union"):header.index("} plcolour_t;") + len("} plcolour_t;")]
batch = header[header.index("typedef struct\n{\n\tqboolean active, rgba;"):header.index("} drawcharbatch_t;") + len("} drawcharbatch_t;")]
limits = []
for filename, name in [('quakedef.h', 'MAX_QPATH'), ('keys.h', 'MAX_CHAT_SIZE_EX'),
                       ('console.h', 'QW_MAPTITLE_SIZE')]:
    match = re.search(r'(?m)^#define\s+' + name + r'\s+(\d+)',
                      (ROOT / 'Quake' / filename).read_text())
    assert match, name
    limits.append(f'#define {name} {match.group(1)}')

source = r'''
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
typedef enum {false, true} qboolean;
''' + colour + '\n' + batch + '\n' + '\n'.join(limits) + r'''
#define GL_QUADS 7
#define GL_BLEND 1
#define GL_ALPHA_TEST 2
#define GL_TEXTURE_ENV 3
#define GL_TEXTURE_ENV_MODE 4
#define GL_MODULATE 5
#define GL_REPLACE 6
#define q_min(a,b) ((a)<(b)?(a):(b))
#define q_max(a,b) ((a)>(b)?(a):(b))
#define MENU_ONESCROLL_SPEED 24.0
#define MENU_SCROLL_SEPARATOR_LEN 3
#define MENU_SCROLL_SEPARATOR_DOT 133
static int max_word_length=12;
static qboolean char_hexen2;
static int *char_texture, texture_token;
static unsigned d_8to24table[256];
static float gl_menu_alpha=1;
typedef struct { float x,y,s,t,rgba[4]; int blend,alpha,env; } vertex_t;
typedef struct {
 vertex_t v[4096]; int count,blend,alpha,env,texture; float rgba[4];
 int begins,calls;
} trace_t;
static trace_t traces[2], *tr;
static int inside,bound,depth,cases;
static float tx,stack[8],uv[2];
static void reset(int n,float menu_alpha) {
 assert(!inside&&!depth);
 tr=&traces[n];memset(tr,0,sizeof(*tr));gl_menu_alpha=menu_alpha;
 tr->blend=menu_alpha<1;tr->alpha=menu_alpha>=1;
 tr->env=menu_alpha<1?GL_MODULATE:GL_REPLACE;
 tr->rgba[0]=tr->rgba[1]=tr->rgba[2]=1;tr->rgba[3]=menu_alpha;
 bound=0;tx=uv[0]=uv[1]=0;
}
static void GL_Bind(int *t) {
 assert(!inside&&t==char_texture);tr->texture=1;
 if(!bound){bound=1;tr->calls++;}
}
static void glBegin(int mode){assert(!inside&&mode==GL_QUADS);inside=1;tr->begins++;tr->calls++;}
static void glEnd(void){assert(inside);inside=0;tr->calls++;}
static void glTexCoord2f(float s,float t){assert(inside);uv[0]=s;uv[1]=t;tr->calls++;}
static void glVertex2f(float x,float y) {
 assert(inside&&tr->count<4096);vertex_t *v=&tr->v[tr->count++];
 v->x=x+tx;v->y=y;v->s=uv[0];v->t=uv[1];memcpy(v->rgba,tr->rgba,sizeof(v->rgba));
 v->blend=tr->blend;v->alpha=tr->alpha;v->env=tr->env;tr->calls++;
}
static void glEnable(int state){assert(!inside);if(state==GL_BLEND)tr->blend=1;else {assert(state==GL_ALPHA_TEST);tr->alpha=1;}tr->calls++;}
static void glDisable(int state){assert(!inside);if(state==GL_BLEND)tr->blend=0;else {assert(state==GL_ALPHA_TEST);tr->alpha=0;}tr->calls++;}
static void glColor4f(float r,float g,float b,float a){tr->rgba[0]=r;tr->rgba[1]=g;tr->rgba[2]=b;tr->rgba[3]=a;tr->calls++;}
static void glTexEnvf(int a,int b,int c){assert(!inside&&a==GL_TEXTURE_ENV&&b==GL_TEXTURE_ENV_MODE);tr->env=c;tr->calls++;}
static void glPushMatrix(void){assert(!inside&&depth<8);stack[depth++]=tx;tr->calls++;}
static void glTranslatef(float x,float y,float z){assert(!inside&&y==0&&z==0);tx+=x;tr->calls++;}
static void glPopMatrix(void){assert(!inside&&depth>0);tx=stack[--depth];tr->calls++;}
static const char *q_strcasestr(const char *s,const char *needle) {
 size_t n=strlen(needle);
 for(;*s;s++){size_t i;for(i=0;i<n&&s[i]&&tolower((unsigned char)s[i])==tolower((unsigned char)needle[i]);i++);if(i==n)return s;}
 return n?NULL:s;
}
static size_t q_strlcpy(char *d,const char *s,size_t n){size_t len=strlen(s);if(n){size_t k=len<n-1?len:n-1;memcpy(d,s,k);d[k]=0;}return len;}
static int q_snprintf(char *dst,size_t size,const char *fmt,...) {
 va_list ap;va_start(ap,fmt);int result=vsnprintf(dst,size,fmt,ap);va_end(ap);return result;
}
#define q_tolower(c) tolower((unsigned char)(c))
#define SLIDER_RANGE 10
#define CONTROLLER_DIM_ALPHA 0.5f
static plcolour_t CL_PLColours_Parse(const char *s){plcolour_t c={0};(void)s;c.type=2;c.rgb[0]=c.rgb[1]=c.rgb[2]=255;return c;}
'''

source += '\n'.join(function(draw, name) for name in [
    'Draw_CharacterQuad', 'Draw_Character', 'Draw_CharactersRGBA',
    'Draw_CharacterRGBA', 'Draw_String', 'Draw_StringMasked',
    'Draw_BeginCharacterBatch', 'Draw_BeginCharacterBatchRGBA',
    'Draw_BatchCharacter', 'Draw_EndCharacterBatch'])
source += r'''
static void ReferenceCharacter(drawcharbatch_t *batch,int x,int y,int n) {
 if(batch->rgba)Draw_CharacterRGBA(x,y,n,batch->colour,batch->alpha);
 else Draw_Character(x,y,n);
}
static void ReferenceEnd(drawcharbatch_t *batch){(void)batch;}
static void M_Print(int x,int y,const char *s){Draw_StringMasked(x,y,s);}
static void M_PrintWhite(int x,int y,const char *s){Draw_String(x,y,s);}
static void M_PrintRGBA(int x,int y,const char *s,plcolour_t c,float a,qboolean mask) {
 drawcharbatch_t batch;Draw_BeginCharacterBatchRGBA(&batch,c,a);
 for(;*s;s++,x+=8)Draw_BatchCharacter(&batch,x,y,(unsigned char)*s+(mask?128:0));
 Draw_EndCharacterBatch(&batch);
}
'''
source += '\n'.join(function(menu, name) for name in [
    'M_OneScrollPixelOffset', 'M_ScrollPixelOffset', 'M_ScrollSeparatorChar',
    'M_ModMenu_TitleVisibleLength'])
names = ['M_PrintScrollOnceMasked', 'M_PrintHighlight', 'M_PrintScroll',
         'M_PrintScroll2', 'M_PrintHighlightScroll2', 'M_PrintHighlightScroll',
         'M_DownloadMaps_DrawInstalledMap', 'M_DownloadMaps_DrawTitle',
         'M_DownloadMods_PrintRGBAScroll', 'M_Print2', 'M_DrawQuakeBar',
         'M_DrawEllipsisBar', 'M_ModMenu_PrintTitle', 'M_ModMenu_PrintModeHeader',
         'M_DrawSlider', 'M_Controller_DrawSliderMaybeDim']
for name in names:
    original = function(menu, name)
    source += '\n' + original
    source += '\n' + reference(re.sub(r'\b' + name + r'\b', name + '_reference', original))

# Retain the actual console's interleaved underline/glyph emission sequence.
start = console.index('\t\tfor (x = 0; x < con_linewidth; x++) // woods #conselection')
loop = console[start:console.index('\n\t}', start)]
source += r'''
typedef struct {int col;} conofs_t;
typedef struct {int id;} conlink_t;
static conlink_t links[2],*con_hotlink=&links[0];
static conlink_t *Con_GetLinkAtOfs(conofs_t *o){return o->col%7<3?&links[0]:o->col%7==4?&links[1]:NULL;}
'''
for ref in (False, True):
    source += ('\nstatic void ConsoleRow' + ('_reference' if ref else '') +
               '(const char *text,int con_linewidth,int y,int links_enabled) {\n'
               'int x;conofs_t ofs;drawcharbatch_t batch={0};\n' +
               (reference(loop) if ref else loop) + '\n' +
               ('ReferenceEnd' if ref else 'Draw_EndCharacterBatch') + '(&batch);\n}\n')
    # Multiple rows share a batch in the real console; underline quads can
    # overlap the next row. Keep that ordering and close before later draws.
    source += ('\nstatic void ConsoleRows' + ('_reference' if ref else '') +
               '(const char *text,int con_linewidth,int y,int links_enabled) {\n'
               'int x;conofs_t ofs;drawcharbatch_t batch={0};\n'
               'for(int row=0;row<3;row++,y+=8){\n' +
               (reference(loop) if ref else loop) + '\n}\n' +
               ('ReferenceEnd' if ref else 'Draw_EndCharacterBatch') + '(&batch);\n'
               'glPushMatrix();glTranslatef(.5f,0,0);glPopMatrix();\n}\n')

source += r'''
static void compare(void) {
 trace_t *a=&traces[0],*b=&traces[1];
 assert(!inside&&!depth&&tx==0);assert(a->count==b->count);
 assert(!memcmp(a->v,b->v,a->count*sizeof(vertex_t)));
 assert(a->blend==b->blend&&a->alpha==b->alpha&&a->env==b->env&&a->texture==b->texture);
 assert(!memcmp(a->rgba,b->rgba,sizeof(a->rgba)));cases++;
}
#define CHECK_CALL(name,...) do {reset(0,ma);name##_reference(__VA_ARGS__);reset(1,ma);name(__VA_ARGS__);compare();} while(0)
int main(void) {
 char_texture=&texture_token;
 for(int i=0;i<256;i++)d_8to24table[i]=0xff000000u|(unsigned)i*0x010101u;
 char all[256],text[97],spaces[81];for(int i=0;i<256;i++)all[i]=(char)i;
 for(int i=0;i<96;i++)text[i]=(char)(33+i%90);text[96]=0;
 memset(spaces,32,80);spaces[80]=0;
 for(int font=0;font<2;font++)for(int fade=0;fade<3;fade++) {
  char_hexen2=font;float ma=fade==0?1:fade==1?.35f:0;
  for(int links_on=0;links_on<2;links_on++) {
   CHECK_CALL(ConsoleRow,all,256,-9,links_on);
   if(links_on){assert(tr->v[0].y==-7&&tr->v[4].y==-9);}
  }
  CHECK_CALL(ConsoleRow,spaces,80,0,0);assert(tr->calls==0);
  CHECK_CALL(ConsoleRows,all,128,-9,1);assert(traces[1].begins==1);
  CHECK_CALL(M_Print2,12,-4,"0123456789");
  CHECK_CALL(M_DrawQuakeBar,12,-4,20);
  CHECK_CALL(M_DrawEllipsisBar,12,-4,20);
  CHECK_CALL(M_ModMenu_PrintTitle,-4,"Title ^g0123^g normal");
  CHECK_CALL(M_ModMenu_PrintModeHeader,-4,"A mode header");
  char long_name[129];memset(long_name,'A',128);long_name[128]=0;
  CHECK_CALL(M_PrintScroll2,15,-4,128,long_name,"description",.137,true);
  char stored_name[MAX_QPATH];memset(stored_name,'A',sizeof(stored_name)-1);stored_name[sizeof(stored_name)-1]=0;
  reset(0,ma);M_PrintScroll2(15,-4,128,stored_name,"description",.137,true);
  reset(1,ma);M_PrintScroll2(15,-4,128,long_name,"description",.137,true);compare();
  char long_highlight[MAX_CHAT_SIZE_EX+257];
  memset(long_highlight,'A',sizeof(long_highlight)-1);long_highlight[sizeof(long_highlight)-1]=0;
  CHECK_CALL(M_PrintHighlightScroll,15,-4,128,long_highlight,"A",(MAX_CHAT_SIZE_EX+32)*8/30.0);
  char stored_highlight[MAX_CHAT_SIZE_EX];memset(stored_highlight,'A',sizeof(stored_highlight)-1);stored_highlight[sizeof(stored_highlight)-1]=0;
  reset(0,ma);M_PrintHighlightScroll(15,-4,128,stored_highlight,"A",(MAX_CHAT_SIZE_EX+32)*8/30.0);
  reset(1,ma);M_PrintHighlightScroll(15,-4,128,long_highlight,"A",(MAX_CHAT_SIZE_EX+32)*8/30.0);compare();
  for(int range=-1;range<=2;range++) {
   CHECK_CALL(M_DrawSlider,12,-4,(float)range,0.5f,"%.1f");
   CHECK_CALL(M_Controller_DrawSliderMaybeDim,12,-4,(float)range,0.5f,"%.1f",false);
  }
  for(int len=0;len<97;len+=16) {
   char saved=text[len];text[len]=0;
   for(int w=0;w<=128;w+=16)for(int tick=0;tick<5;tick++)for(int mask=0;mask<2;mask++) {
    double time=tick*.137;
    CHECK_CALL(M_PrintScrollOnceMasked,15,-4,w,text,time,mask*128);
    CHECK_CALL(M_PrintScroll,15,-4,w,text,time,mask);
    CHECK_CALL(M_PrintHighlightScroll,15,-4,w,text,"ABC",time);
    CHECK_CALL(M_PrintScroll2,15,-4,w,text,"description",time,mask);
    CHECK_CALL(M_PrintHighlightScroll2,15,-4,w,text,"description","ABC",time);
    CHECK_CALL(M_DownloadMods_PrintRGBAScroll,15,-4,w,text,time,CL_PLColours_Parse("white"),.5f,mask);
    CHECK_CALL(M_DownloadMaps_DrawInstalledMap,15,-4,w,text,time);
   }
   for(int match=0;match<3;match++) {
    const char *search=match==0?"":match==1?"ABC":"~missing~";
    CHECK_CALL(M_PrintHighlight,15,-4,text,search,(int)strlen(search));
    for(int installed=0;installed<2;installed++)for(int w=8;w<=256;w+=31)
     CHECK_CALL(M_DownloadMaps_DrawTitle,15,-4,w,text,"an ABC description",search,.137,installed);
   }
   text[len]=saved;
  }
  for(int color=0;color<2;color++)for(int len=0;len<=256;len++) {
   plcolour_t c={0};c.type=color?2:0;c.basic=7;c.rgb[0]=32;c.rgb[1]=128;c.rgb[2]=240;
   reset(0,ma);for(int i=0;i<len;i++)Draw_CharacterRGBA(8+i*8,-3,all[i],c,.75f);
   reset(1,ma);drawcharbatch_t batch;Draw_BeginCharacterBatchRGBA(&batch,c,.75f);
   for(int i=0;i<len;i++)Draw_BatchCharacter(&batch,8+i*8,-3,all[i]);
   Draw_EndCharacterBatch(&batch);compare();
  }
 }
 float ma=1;char_hexen2=0;memset(text,'A',80);text[80]=0;
 CHECK_CALL(ConsoleRow,text,80,0,0);
 assert(traces[0].begins==80&&traces[1].begins==1);
 assert(traces[0].calls==801&&traces[1].calls==643);
 plcolour_t white=CL_PLColours_Parse("white");
 reset(0,1);for(int i=0;i<80;i++)Draw_CharacterRGBA(i*8,0,text[i],white,.75f);
 reset(1,1);drawcharbatch_t batch;Draw_BeginCharacterBatchRGBA(&batch,white,.75f);
 for(int i=0;i<80;i++)Draw_BatchCharacter(&batch,i*8,0,text[i]);Draw_EndCharacterBatch(&batch);compare();
 assert(traces[0].calls==1441&&traces[1].calls==651);
 reset(0,1);reset(1,1);Draw_BeginCharacterBatchRGBA(&batch,white,.5f);
 for(int i=0;i<80;i++)Draw_BatchCharacter(&batch,i*8,0,32);Draw_EndCharacterBatch(&batch);compare();assert(!tr->calls);
 printf("PASS: %d geometry/state comparisons; glyph banks, zero glyphs, overlapping underlines, fonts, fades, scrolling, long-text bounds, opacity splits, and empty batches; GL calls 801 -> 643 plain, 1441 -> 651 RGBA.\n",cases);
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-character-batches-") as tmp:
    path = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    path.write_text(source)
    command = shlex.split(os.environ.get("CC", "cc"))
    command += ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Wno-unused-function',
                '-Wno-misleading-indentation']
    command += shlex.split(os.environ.get("CFLAGS", ""))
    subprocess.run(command + ['-UNDEBUG', str(path), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
