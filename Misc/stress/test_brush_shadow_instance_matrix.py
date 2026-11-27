#!/usr/bin/env python3
"""The instanced brush shadow must land exactly where the fixed-function one does.

Compiles the real GL_BrushShadowInstanceMatrix (r_alias.c) and compares it with
the fixed-function chain GL_DrawBrushShadowPrepared builds:
R_RotateForEntity * T(0,0,-lheight) * r_brush_shadowmatrix * T(0,0,lheight),
with r_brush_shadowmatrix taken verbatim from r_brush.c. Covers yaw, pitch,
roll, every entity-scale origin mode and shadow heights on both sides of the
plane. --mutation-check must FAIL: it swaps in the alias Y skew (0).
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
alias = (ROOT / "Quake/r_alias.c").read_text()
brush = (ROOT / "Quake/r_brush.c").read_text()
glquake = (ROOT / "Quake/glquake.h").read_text()
mathlib = (ROOT / "Quake/mathlib.c").read_text()


def function(text, declaration):
    start = text.index(declaration)
    return text[start:text.index("\n}", start) + 2]


defines = "\n".join(re.findall(r"^#define BRUSH_SHADOW_\w+.*$", glquake, re.M))
fixed_matrix = brush[brush.index("static const float r_brush_shadowmatrix[16]"):]
fixed_matrix = fixed_matrix[:fixed_matrix.index("};") + 2]
instance = function(alias, "static void GL_BrushShadowInstanceMatrix (")
if "--mutation-check" in sys.argv:
    assert "sy = BRUSH_SHADOW_SKEW_Y;" in instance
    instance = instance.replace("sy = BRUSH_SHADOW_SKEW_Y;", "sy = 0.0f;")

source = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef float vec3_t[3];
#define PITCH 0
#define YAW 1
#define ROLL 2
#define ENTSCALE_DEFAULT 16
#define ENTSCALE_DECODE(a) ((float)(a) / ENTSCALE_DEFAULT)
#define VectorCopy(a, b) ((b)[0]=(a)[0], (b)[1]=(a)[1], (b)[2]=(a)[2])
typedef struct { vec3_t mins, maxs; } qmodel_t;
typedef struct { qmodel_t *model; vec3_t origin, angles; struct { int scale, drawflags; } netstate; } entity_t;
typedef struct { entity_t *e; float lheight, alpha; } brushshadow_rec_t;
'''
source += defines + "\n" + fixed_matrix + "\n"
source += function(mathlib, "void AngleVectors (") + "\n"
source += instance + "\n"
source += r'''
/* column-major 4x4, like glMultMatrixf */
static void mul(float *m, const float *r) {
    float t[16]; int c, k, row;
    for (c = 0; c < 4; c++) for (row = 0; row < 4; row++) {
        t[c*4 + row] = 0; for (k = 0; k < 4; k++) t[c*4 + row] += m[k*4 + row] * r[c*4 + k];
    }
    memcpy(m, t, sizeof(t));
}
static void translate(float *m, float x, float y, float z) {
    float t[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1}; mul(m, t);
}
static void rotate(float *m, float deg, float x, float y, float z) {
    double a = deg * M_PI / 180.0, c = cos(a), s = sin(a);
    float t[16] = {
        (float)(x*x*(1-c)+c),   (float)(y*x*(1-c)+z*s), (float)(x*z*(1-c)-y*s), 0,
        (float)(x*y*(1-c)-z*s), (float)(y*y*(1-c)+c),   (float)(y*z*(1-c)+x*s), 0,
        (float)(x*z*(1-c)+y*s), (float)(y*z*(1-c)-x*s), (float)(z*z*(1-c)+c),   0,
        0, 0, 0, 1};
    mul(m, t);
}
static void scale(float *m, float s) { float t[16] = {s,0,0,0, 0,s,0,0, 0,0,s,0, 0,0,0,1}; mul(m, t); }
/* GL_DrawBrushShadowPrepared: R_RotateForEntity (no pitch flip) then the shadow chain */
static void fixed_function(const brushshadow_rec_t *rec, float *m) {
    const entity_t *e = rec->e;
    float id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    memcpy(m, id, sizeof(id));
    translate(m, e->origin[0], e->origin[1], e->origin[2]);
    rotate(m, e->angles[1], 0, 0, 1);
    rotate(m, -e->angles[0], 0, 1, 0);
    rotate(m, e->angles[2], 1, 0, 0);
    if (e->netstate.scale != ENTSCALE_DEFAULT) {
        float s = ENTSCALE_DECODE(e->netstate.scale);
        switch ((e->netstate.drawflags >> 5) & 3) {
        case 0: translate(m, 0, 0, (e->model->mins[2] + e->model->maxs[2]) / 2 * (1 - s)); break;
        case 1: translate(m, 0, 0, e->model->mins[2] * (1 - s)); break;
        case 2: translate(m, 0, 0, e->model->maxs[2] * (1 - s)); break;
        }
        scale(m, s);
    }
    translate(m, 0, 0, -rec->lheight);
    mul(m, r_brush_shadowmatrix);
    translate(m, 0, 0, rec->lheight);
}
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; }
int main(void) {
    int t, k, worst_at = -1;
    double worst = 0;
    srand(1);
    for (t = 0; t < 20000; t++) {
        qmodel_t model; entity_t e; brushshadow_rec_t rec; float want[16], got[16];
        memset(&e, 0, sizeof(e)); e.model = &model;
        for (k = 0; k < 3; k++) { model.mins[k] = frand(-64, 0); model.maxs[k] = frand(0, 64); e.origin[k] = frand(-2000, 2000); }
        if (t % 4) e.angles[1] = frand(-360, 360);
        if (t % 5 == 1) { e.angles[0] = frand(-90, 90); e.angles[2] = frand(-90, 90); }
        e.netstate.scale = t % 3 == 0 ? (int)frand(4, 64) : ENTSCALE_DEFAULT;
        e.netstate.drawflags = (t % 4) << 5;
        rec.e = &e; rec.lheight = frand(-60, 400); rec.alpha = 0.5f;
        fixed_function(&rec, want);
        GL_BrushShadowInstanceMatrix(&rec, got);
        for (k = 0; k < 16; k++) {
            double err = fabs(want[k] - got[k]) / (1.0 + fabs(want[k]));
            if (err > worst) { worst = err; worst_at = t; }
        }
    }
    if (worst > 1e-4) { printf("FAIL: instanced brush shadow matrix differs from the fixed-function chain (rel err %g, case %d)\n", worst, worst_at); return 1; }
    puts("PASS: instanced brush shadow matrix matches the fixed-function chain (yaw/pitch/roll, entity scales, shadow heights)");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-bshadow-matrix-") as tmp:
    work = Path(tmp)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        command = [*cc, "/nologo", "/W3", "/O2", "/D_USE_MATH_DEFINES", "test.c", f"/Fe:{binary}"]
    else:
        command = [*cc, "-std=gnu99", "-Wall", "-Wextra", "-O2", "test.c", "-o", str(binary), "-lm"]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
