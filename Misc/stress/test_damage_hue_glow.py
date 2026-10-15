"""Check real viewmodel shell submissions: damage rims must not grow halos.

Run with CC, or from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
renderer = (ROOT / "Quake/r_alias.c").read_text()


def function(declaration):
    start = renderer.index(declaration)
    return renderer[start:renderer.index("\n}", start) + 2] + "\n"


source = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c, m) do { if (!(c)) { puts("FAIL: " m); exit(1); } } while (0)
#define CLAMP(lo, x, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define false 0
#define true 1
#define GL_FALSE 0
#define GL_VIEWMODEL_STENCIL_BIT() (1u << 7)
enum { GL_ENABLE_BIT=1, GL_COLOR_BUFFER_BIT=2, GL_DEPTH_BUFFER_BIT=4,
    GL_STENCIL_BUFFER_BIT=8, GL_STENCIL_TEST, GL_NOTEQUAL, GL_KEEP,
    GL_REPLACE, GL_BLEND, GL_SRC_ALPHA, GL_ONE, GL_LEQUAL,
    GL_TRIANGLES, GL_UNSIGNED_SHORT, IT_QUAD=1<<20, IT_INVULNERABILITY=1<<19 };
typedef int qboolean, GLint;
typedef unsigned int GLuint;
typedef unsigned char byte;
typedef struct { float value; const char *string; } cvar_t;
typedef struct { int numindexes, eboofs; } aliashdr_t;
typedef struct { const byte *meshindexesvboptr; } qmodel_t;
typedef struct { qmodel_t *model; } entity_t;
typedef struct { int unused; } lerpdata_t;
typedef struct { int unused; } plcolour_t;
typedef struct {
    int isOutlinePassLoc, outlineWidthLoc, outlineColorLoc,
        shellModeLoc, shellTimeLoc, shellWaveParamsLoc;
} aliasglsl_t;
static struct { entity_t viewent; int items; double time, faceanimtime; } cl;
static cvar_t r_coloredpowerupglow, gl_powerupshells, gl_powerupshells_alpha,
    gl_powerupshells_glow, gl_laserpoint, cl_damagehue, cl_damagehuecolor, chase_active;
static int gl_stencilbits = 8, md5, preview_damage, preview_powerup;
static int count, pushed, popped, pass;
static float width, color[4];
static struct { int pass; float width, color[4]; } draws[16];
static int M_LivePreview_UsePowerupShells(void) { return preview_powerup; }
static int M_LivePreview_UseDamageTint(void) { return preview_damage; }
static float R_CalculateAliasModelRadius(aliashdr_t *h, entity_t *e,
    lerpdata_t *l, qboolean *skeletal) {
    (void)h; (void)e; (void)l; *skeletal=md5; return 15;
}
static plcolour_t CL_PLColours_Parse(const char *s) {
    plcolour_t result={0}; CHECK(!strcmp(s,"0xeb580e"), "damage tint input"); return result;
}
static byte *CL_PLColours_ToRGB(plcolour_t *c) {
    static byte orange[]={235,88,14}; (void)c; return orange;
}
static void glPushAttrib(int mask) { (void)mask; pushed++; }
static void glPopAttrib(void) { popped++; }
static void glEnable(int cap) { (void)cap; }
static void glStencilFunc(int func, int ref, int mask) { (void)func; (void)ref; (void)mask; }
static void glStencilOp(int a, int b, int c) { (void)a; (void)b; (void)c; }
static void glStencilMask(int mask) { (void)mask; }
static void glBlendFunc(int src, int dst) { (void)src; (void)dst; }
static void glDepthMask(int mask) { (void)mask; }
static void glDepthFunc(int func) { (void)func; }
static void GL_Uniform1iFunc(int location, int value) { if (location==1) pass=value; }
static void GL_Uniform1fFunc(int location, float value) { if (location==2) width=value; }
static void GL_Uniform4fFunc(int location, float r, float g, float b, float a) {
    if (location==3) { color[0]=r; color[1]=g; color[2]=b; color[3]=a; }
}
static void glDrawElements(int mode, int indices, int type, const void *offset) {
    CHECK(mode==GL_TRIANGLES && indices==30 && type==GL_UNSIGNED_SHORT && !offset,
        "shell must submit the viewmodel geometry");
    CHECK(count<16, "too many shell layers");
    draws[count].pass=pass; draws[count].width=width;
    memcpy(draws[count++].color,color,sizeof(color));
}
'''
source += function("static void R_DrawPowerupShellGlow(")
source += function("void R_DrawViewmodelShell(")
source += r'''
static aliasglsl_t shader={1,2,3,4,5,6};
static aliashdr_t header={30,0};
static qmodel_t model={0};
static lerpdata_t lerp;

static void Draw(int items, int damage, float glow) {
    cl.items=items; cl.faceanimtime=damage ? 2 : 0;
    gl_powerupshells_glow.value=glow;
    count=pushed=popped=0; pass=0;
    R_DrawViewmodelShell(&shader,&header,&lerp,&cl.viewent);
    CHECK(pushed==popped && pass==0, "shell must restore state and uniforms");
}

int main(void) {
    cl.time=1; cl.viewent.model=&model;
    r_coloredpowerupglow.value=gl_powerupshells.value=cl_damagehue.value=1;
    gl_powerupshells_alpha.value=.3f; cl_damagehuecolor.string="0xeb580e";
    for (md5=0; md5<2; md5++) {
        for (int level=0; level<3; level++) {
            float glow=level*.5f;
            Draw(0,1,glow);
            CHECK(count==1 && draws[0].pass==2,
                "damage without powerups must draw only its original thin rim, without a halo");
            CHECK(fabsf(draws[0].color[0]-235/255.f)<.0001f &&
                fabsf(draws[0].color[1]-88/255.f)<.0001f,
                "damage must retain its configured orange tint");
            Draw(IT_QUAD|IT_INVULNERABILITY,1,glow);
            CHECK(count==1, "damage overriding a powerup must not get an orange outer halo");
            for (int item=IT_INVULNERABILITY; item<=IT_QUAD; item*=2) {
                Draw(item,0,glow);
                CHECK(level ? count>1 : count==1, "powerup must retain its core and optional outer halo");
                CHECK(draws[0].color[item==IT_QUAD ? 2 : 0]==1,
                    "powerup shell must retain its own color");
                for (int i=1; i<count; i++)
                    CHECK(draws[i].pass==3 && draws[i].width>draws[0].width,
                        "powerup halo must extend outside the core rim");
            }
        }
        Draw(0,0,1); CHECK(count==0, "no damage or powerup means no shell");
        preview_damage=1;
        Draw(0,0,1); CHECK(count==1, "damage menu preview must not get a halo");
        preview_damage=0; preview_powerup=1;
        Draw(0,0,1); CHECK(count>1, "powerup menu preview must retain its halo");
        preview_powerup=0; cl_damagehue.value=0;
        Draw(IT_QUAD,1,1); CHECK(count>1, "disabled damage hue must not suppress powerup glow");
        cl_damagehue.value=1;
    }
    puts("PASS: damage keeps its tint and thin rim without a halo; morph/MD5 powerups and menu previews retain their glow");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-damage-glow-") as directory:
    work = Path(directory)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        command = [*cc, "/nologo", "/std:c11", "/W3", "/O2", "test.c", f"/Fe:{binary}"]
    else:
        command = [*cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-fsanitize=address,undefined",
                   "test.c", "-o", str(binary), "-lm"]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
