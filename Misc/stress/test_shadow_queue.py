"""Exercise the production shadow queue: footprint cones, waves and flush order.

The stencil lets the first shadow drawn over a pixel win, so batching may only
reorder shadows that can never share a pixel. This compiles the real footprint
cone, wave assignment, flush and make-room code from Quake/r_alias.c with
recording stubs and checks that:

* every point of a model's shadow, pushed through the engine's own
  transform chain, lies inside its cone (alias yaw/pitched, brush yaw/rotated);
* any two shadows whose cones meet land in successive waves in queue order;
* the flush draws wave by wave, keeps every such pair in order, never draws a
  batched shadow individually, and closes the brush state before alias draws;
* a full queue is flushed before more shadows are added.

Compile with CC, or run from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
renderer = (ROOT / "Quake/r_alias.c").read_text()


def function(declaration):
    start = renderer.index(declaration)
    return renderer[start:renderer.index("\n}", start) + 2]


defines = "\n".join(re.findall(r"^#define SHADOW_(?:SKEW_X|SKEW_Y|VSCALE|HEIGHT)\b.*$", renderer, re.M))
entry_type = renderer[renderer.index("enum { SHADOWQ_ALIAS"):renderer.index("#define MAX_SHADOWQ")]

source = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define false 0
#define true 1
#define M_PI 3.14159265358979323846
#define ENTSCALE_DEFAULT 16
#define DEG2RAD(a) ((a) * (M_PI / 180.0))
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
#define VectorSubtract(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define VectorScale(a,s,c) ((c)[0]=(a)[0]*(s),(c)[1]=(a)[1]*(s),(c)[2]=(a)[2]*(s))
#define VectorLength(a) sqrt(DotProduct(a,a))
#define q_min(a,b) ((a) < (b) ? (a) : (b))
#define q_max(a,b) ((a) > (b) ? (a) : (b))
#define CHECK(cond, message) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", message, __LINE__); exit(1); } } while (0)
typedef int qboolean;
typedef float vec3_t[3];
typedef struct { vec3_t mins, maxs, rmins, rmaxs; } qmodel_t;
typedef struct { vec3_t origin, angles; struct { int scale; } netstate; qmodel_t *model; } entity_t;
typedef struct { vec3_t origin, angles; } lerpdata_t;
typedef struct { entity_t *ent; lerpdata_t lerp; float lheight; } alias_shadow_rec_t;
typedef struct { entity_t *e; float lheight, alpha; } brushshadow_rec_t;
static struct { vec3_t vieworg; } r_refdef;
static entity_t *currententity;
'''
source += defines + "\n" + entry_type + r'''
#define MAX_SHADOWQ 64
static shadowq_entry_t r_shadowq[MAX_SHADOWQ];
static qboolean r_shadowq_batched[MAX_SHADOWQ];
static int r_num_shadowq, r_num_alias_shadows, r_num_brush_shadows;
static qboolean r_shadowq_instancing = true;
/* recording stubs for the flush */
static int log_entry[256], log_level[256], log_kind[256], nlog, brush_open, flushes, upload_ok = 1;
static int pack_batch[MAX_SHADOWQ];
static void GL_BrushShadowEndState(void) { CHECK(brush_open, "brush state closed twice"); brush_open = 0; }
static void R_DrawShadowQueueEntry(const shadowq_entry_t *q, qboolean *brushstate) {
    if (q->kind == SHADOWQ_BRUSH) { *brushstate = true; brush_open = 1; }
    else { if (*brushstate) GL_BrushShadowEndState(); *brushstate = false;
           CHECK(!brush_open, "alias shadow drawn with the brush state still open"); }
    CHECK(!r_shadowq_batched[q - r_shadowq], "batched shadow also drawn one by one");
    log_entry[nlog] = (int)(q - r_shadowq); log_level[nlog] = q->level; log_kind[nlog++] = q->kind;
}
static int R_PackShadowQueueRuns(int n, int maxlevel, int *ndata) {
    int i, runs = 0; (void)maxlevel; *ndata = 0;
    for (i = 0; i < n; ++i) if (pack_batch[i]) { r_shadowq_batched[i] = true; ++*ndata; runs = 1; }
    return runs;
}
static int R_AliasInst_Upload(int ndata) { (void)ndata; return upload_ok; }
static void R_DrawShadowQueueRuns(int level, int nruns) {
    int i;
    if (!nruns) return;
    for (i = 0; i < r_num_shadowq; ++i)
        if (r_shadowq_batched[i] && r_shadowq[i].level == level) {
            log_entry[nlog] = i; log_level[nlog] = level; log_kind[nlog++] = -1;
        }
}
'''
for decl in ("static float R_ShadowModelRadius (",
             "static void R_ShadowConeFromSphere (",
             "static void R_ShadowConeFromBox (",
             "static int R_ShadowAssignLevels (",
             "static void R_AliasShadowCone (",
             "static void R_BrushShadowCone ("):
    source += function(decl) + "\n"
source += "static void R_FlushShadowQueue (void);\n"
source += function("static void R_ShadowQueueMakeRoom (void)") + "\n"
flush = function("static void R_FlushShadowQueue (void)\n{")
source += flush + "\n"
source += r'''
static double frand(double lo, double hi) { return lo + (hi - lo) * (rand() / (double)RAND_MAX); }
static void rotate(const double m[3][3], const double v[3], double out[3]) {
    int i; for (i = 0; i < 3; ++i) out[i] = m[i][0]*v[0] + m[i][1]*v[1] + m[i][2]*v[2];
}
/* glRotatef (yaw,0,0,1) * glRotatef (-pitch,0,1,0) * glRotatef (roll,1,0,0) */
static void gl_rotation(const float *angles, double m[3][3]) {
    double a = DEG2RAD(angles[1]), b = DEG2RAD(-angles[0]), g = DEG2RAD(angles[2]);
    double z[3][3] = {{cos(a), -sin(a), 0}, {sin(a), cos(a), 0}, {0, 0, 1}};
    double y[3][3] = {{cos(b), 0, sin(b)}, {0, 1, 0}, {-sin(b), 0, cos(b)}};
    double x[3][3] = {{1, 0, 0}, {0, cos(g), -sin(g)}, {0, sin(g), cos(g)}};
    double t[3][3]; int i, j, k;
    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) { t[i][j] = 0; for (k = 0; k < 3; ++k) t[i][j] += z[i][k]*y[k][j]; }
    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) { m[i][j] = 0; for (k = 0; k < 3; ++k) m[i][j] += t[i][k]*x[k][j]; }
}
static void check_inside(const shadowq_entry_t *q, const double p[3], const char *what) {
    double d[3] = {p[0] - r_refdef.vieworg[0], p[1] - r_refdef.vieworg[1], p[2] - r_refdef.vieworg[2]};
    double len = sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]), c;
    if (q->angle >= M_PI || len < 1e-3) return;
    c = (d[0]*q->dir[0] + d[1]*q->dir[1] + d[2]*q->dir[2]) / len;
    if (c > 1) c = 1;
    if (acos(c) > q->angle + 1e-5) { printf("FAIL: %s point outside its cone (%g > %g)\n", what, acos(c), q->angle); exit(1); }
}
static void random_model(qmodel_t *m) {
    int k;
    for (k = 0; k < 3; ++k) {
        m->mins[k] = (float)frand(-80, 0); m->maxs[k] = (float)frand(0, 80);
        if (rand() & 1) { m->mins[k] += 300; m->maxs[k] += 300; }   /* off-origin like inline bmodels */
        m->rmins[k] = m->mins[k]; m->rmaxs[k] = m->maxs[k];
    }
}
static void footprint_containment(void) {
    int t, s, k;
    for (t = 0; t < 4000; ++t) {
        qmodel_t m; entity_t e; shadowq_entry_t q; double rot[3][3];
        int brush = t & 1, tilted = (t >> 1) & 1;
        float lh = (float)frand(-60, 400);
        random_model(&m);
        memset(&e, 0, sizeof(e)); e.model = &m; e.netstate.scale = ENTSCALE_DEFAULT;
        for (k = 0; k < 3; ++k) { e.origin[k] = (float)frand(-2000, 2000); r_refdef.vieworg[k] = (float)frand(-2000, 2000); }
        e.angles[1] = (float)frand(-360, 360);
        if (tilted) { e.angles[0] = (float)frand(-90, 90); e.angles[2] = (float)frand(-90, 90); }
        gl_rotation(e.angles, rot);
        if (brush) {
            brushshadow_rec_t rec = {&e, lh, 0.5f};
            R_BrushShadowCone(&q, &rec);
        } else {
            alias_shadow_rec_t rec;
            rec.ent = &e; rec.lheight = lh;
            memcpy(rec.lerp.origin, e.origin, sizeof(vec3_t)); memcpy(rec.lerp.angles, e.angles, sizeof(vec3_t));
            R_AliasShadowCone(&q, &rec);
        }
        for (s = 0; s < 64; ++s) {
            double v[3], w[3], p[3];
            for (k = 0; k < 3; ++k)
                v[k] = s < 8 ? ((s >> k) & 1 ? m.maxs[k] : m.mins[k]) : frand(m.mins[k], m.maxs[k]);
            if (brush) {   /* T(o) R T(0,0,-lh) S T(0,0,lh) v */
                double sq[3] = {v[0] + SHADOW_SKEW_X*(v[2] + lh), v[1] + SHADOW_SKEW_Y*(v[2] + lh), SHADOW_HEIGHT - lh};
                rotate(rot, sq, w);
                for (k = 0; k < 3; ++k) p[k] = e.origin[k] + w[k];
            } else {       /* T(o) T(0,0,-lh) S T(0,0,lh) R v */
                double zref = e.origin[2] - lh;
                rotate(rot, v, w);
                for (k = 0; k < 3; ++k) w[k] += e.origin[k];
                p[0] = w[0] + SHADOW_SKEW_X*(w[2] - zref);
                p[1] = w[1] + SHADOW_SKEW_Y*(w[2] - zref);
                p[2] = zref + SHADOW_HEIGHT;
            }
            check_inside(&q, p, brush ? "brush" : "alias");
        }
    }
    /* a scaled brush model is never trusted to a footprint */
    {
        qmodel_t m; entity_t e; shadowq_entry_t q; brushshadow_rec_t rec;
        random_model(&m); memset(&e, 0, sizeof(e)); e.model = &m; e.netstate.scale = 32;
        rec.e = &e; rec.lheight = 10; rec.alpha = 0.5f;
        R_BrushShadowCone(&q, &rec);
        CHECK(q.angle >= M_PI, "scaled brush shadow must overlap everything");
    }
}
static int overlap(const shadowq_entry_t *a, const shadowq_entry_t *b) {
    float sum = a->angle + b->angle;
    return sum >= (float)M_PI || DotProduct(a->dir, b->dir) > cos(sum);
}
static void random_entry(shadowq_entry_t *q) {
    double l;
    q->dir[0] = (float)frand(-1, 1); q->dir[1] = (float)frand(-1, 1); q->dir[2] = (float)frand(-1, 1);
    l = VectorLength(q->dir); VectorScale(q->dir, (float)(1.0 / l), q->dir);
    q->angle = (rand() % 40 == 0) ? (float)M_PI : (float)frand(0.001, 0.3);
    q->kind = rand() % 3;
}
static void waves(void) {
    int t, i, j, n;
    for (t = 0; t < 500; ++t) {
        n = 2 + rand() % (MAX_SHADOWQ - 2);
        for (i = 0; i < n; ++i) random_entry(&r_shadowq[i]);
        R_ShadowAssignLevels(r_shadowq, n);
        for (i = 0; i < n; ++i)
            for (j = i + 1; j < n; ++j)
                if (overlap(&r_shadowq[i], &r_shadowq[j]))
                    CHECK(r_shadowq[i].level < r_shadowq[j].level, "overlapping shadows must keep queue order across waves");
    }
    /* one giant shadow only splits the queue into before and after */
    n = 5;
    for (i = 0; i < n; ++i) { r_shadowq[i].dir[0] = (float)cos(i); r_shadowq[i].dir[1] = (float)sin(i); r_shadowq[i].dir[2] = 0; r_shadowq[i].angle = 0.01f; }
    r_shadowq[2].angle = (float)M_PI;
    CHECK(R_ShadowAssignLevels(r_shadowq, n) == 2 && r_shadowq[0].level == 0 && r_shadowq[1].level == 0 &&
          r_shadowq[2].level == 1 && r_shadowq[3].level == 2 && r_shadowq[4].level == 2,
          "a giant shadow must only split the queue");
}
static void flush_order(void) {
    int t, i, j, a, b;
    for (t = 0; t < 500; ++t) {
        int n = 2 + rand() % (MAX_SHADOWQ - 2), pos[MAX_SHADOWQ], seen[MAX_SHADOWQ];
        upload_ok = (t % 7) != 0;
        for (i = 0; i < n; ++i) {
            random_entry(&r_shadowq[i]);
            pack_batch[i] = r_shadowq[i].kind == SHADOWQ_ALIAS_INST && (rand() & 1);
        }
        r_num_shadowq = n; nlog = 0; brush_open = 0;
        R_FlushShadowQueue();
        CHECK(!brush_open, "flush left the brush state open");
        CHECK(!r_num_shadowq && !r_num_alias_shadows && !r_num_brush_shadows, "flush must empty the queue");
        CHECK(nlog == n, "every queued shadow must be drawn exactly once");
        memset(seen, 0, sizeof(seen));
        for (i = 0; i < nlog; ++i) {
            CHECK(!seen[log_entry[i]], "shadow drawn twice"); seen[log_entry[i]] = 1; pos[log_entry[i]] = i;
            if (i) CHECK(log_level[i] >= log_level[i - 1], "waves must be drawn in order");
            if (!upload_ok) CHECK(log_kind[i] != -1, "failed upload must not draw instanced");
        }
        for (a = 0; a < n; ++a)
            for (b = a + 1; b < n; ++b)
                if (overlap(&r_shadowq[a], &r_shadowq[b]))
                    CHECK(pos[a] < pos[b], "overlapping shadows drawn out of order");
        (void)j;
    }
}
static void R_FlushShadowQueue_count(void) { flushes++; }
int main(void) {
    srand(12345);
    footprint_containment();
    waves();
    flush_order();
    /* a full queue flushes before anything else is added */
    r_num_shadowq = MAX_SHADOWQ; r_num_alias_shadows = 3; r_num_brush_shadows = 3;
    for (int i = 0; i < MAX_SHADOWQ; ++i) { random_entry(&r_shadowq[i]); pack_batch[i] = 0; }
    nlog = 0; R_ShadowQueueMakeRoom();
    CHECK(nlog == MAX_SHADOWQ && !r_num_shadowq, "a full queue must be drawn before it takes more");
    r_num_shadowq = 1; r_num_alias_shadows = MAX_SHADOWQ; nlog = 0;
    random_entry(&r_shadowq[0]); R_ShadowQueueMakeRoom();
    CHECK(nlog == 1 && !r_num_alias_shadows, "a full alias record array must flush too");
    r_num_shadowq = 2; r_num_alias_shadows = 1; r_num_brush_shadows = 1; nlog = 0; R_ShadowQueueMakeRoom();
    CHECK(nlog == 0 && r_num_shadowq == 2, "a queue with room must not flush");
    (void)R_FlushShadowQueue_count;
    puts("PASS: shadow footprint cones contain every shadow point, waves keep overlapping pairs ordered, flush order/batching/state, full-queue flush");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-shadowq-") as tmp:
    work = Path(tmp)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        command = [*cc, "/nologo", "/W3", "/O2", "test.c", f"/Fe:{binary}"]
    else:
        command = [*cc, "-std=c99", "-Wall", "-Wextra", "-O2", "test.c", "-o", str(binary), "-lm"]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
