#!/usr/bin/env python3
"""Run the real instanced deferred-outline replay against a recording GL.

R_DrawDeferredAliasOutlinesInstancedRun groups plain MDL outlines into waves by
view-cone overlap and gives each wave one stencil value. This drives the
production wave assignment, sorting, instance packing and stencil sequencing
over randomized scenes and checks the result against the sequential replay's
contract, using exact (unpadded) sphere overlap computed here:

- every eligible, unculled entity is masked once and ringed once;
- in each wave every mask precedes every ring, and rings test their own value;
- two entities whose true view spheres overlap never share a value, and the
  later one (in visible-list order) masks only after the earlier one's ring;
- an instanced draw only groups one model and one pose pair;
- values count up across runs (start > 0), and a wrap clears before reuse;
- the run stops at the first ineligible entity and restores GL state.

--mutation-check must FAIL: it collapses every wave to level 0 in the
extracted code, never in the renderer.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
renderer = (ROOT / "Quake/r_alias.c").read_text()


def function(declaration):
    start = renderer.index(declaration)
    return renderer[start:renderer.index("\n}", start) + 2]


radius = function("static float R_ShadowModelRadius (")
cone = function("static void R_ShadowConeFromSphere (")
levels = function("static int R_ShadowAssignLevels (")
records_end = renderer.index("static int R_CompareOutlineInst (")
records = renderer[renderer.rindex("typedef struct", 0, renderer.index("} outline_inst_rec_t;")):records_end]
compare = function("static int R_CompareOutlineInst (")
eligible = function("static qboolean R_CanInstanceDeferredAliasOutline (")
run = function("static int R_DrawDeferredAliasOutlinesInstancedRun (int start")
clear = function("static void R_ClearAliasOutlineStencil(GLuint mask)")
if "--mutation-check" in sys.argv:
    assert "maxlevel = R_ShadowAssignLevels (r_shadowq, m);" in run
    run = run.replace("maxlevel = R_ShadowAssignLevels (r_shadowq, m);",
                      "maxlevel = R_ShadowAssignLevels (r_shadowq, m); "
                      "for (i = 0; i < m; ++i) r_shadowq[i].level = 0; maxlevel = 0;")

source = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(cond, message) do { if (!(cond)) { printf("FAIL: %s (seed %d)\n", message, seed); exit(1); } } while (0)
typedef int qboolean;
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned char GLboolean;
typedef float vec3_t[3];
#define true 1
#define false 0
#define GL_FALSE 0
#define GL_TRUE 1
enum { GL_ENABLE_BIT = 1, GL_COLOR_BUFFER_BIT = 2, GL_DEPTH_BUFFER_BIT = 4, GL_STENCIL_BUFFER_BIT = 8,
       GL_POLYGON_BIT = 16 };
enum { GL_DEPTH_TEST = 100, GL_STENCIL_TEST, GL_CULL_FACE, GL_BLEND, GL_SCISSOR_TEST, GL_LEQUAL,
       GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_BACK, GL_FRONT, GL_ALWAYS, GL_NOTEQUAL, GL_KEEP,
       GL_REPLACE, GL_TRIANGLES, GL_UNSIGNED_SHORT, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
       GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST, GL_FLAT };
#define MAX_ALIAS_INSTANCES 1024
#define MAX_SHADOWQ 1024
#define mod_alias 2
#define PV_QUAKE1 0
#define MF_HOLEY 0x4000
#define MOD_NOOUTLINE 0x8000
#define ENTSCALE_DEFAULT 16
#define CSQC_RENDER_AXIS 1
#define CSQC_RENDER_DEPTHHACK 2
#define CSQC_RENDER_MIRRORED 4
#define ENTALPHA_DECODE(a) ((a) == 0 ? 1.0f : (float)((a) - 1) / 254)
#define DEG2RAD(a) ((a) * (M_PI / 180.0))
#define CLAMP(lo, x, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define q_max(a, b) ((a) > (b) ? (a) : (b))
#define q_min(a, b) ((a) < (b) ? (a) : (b))
#define DotProduct(a, b) ((a)[0]*(b)[0] + (a)[1]*(b)[1] + (a)[2]*(b)[2])
#define VectorSubtract(a, b, c) ((c)[0]=(a)[0]-(b)[0], (c)[1]=(a)[1]-(b)[1], (c)[2]=(a)[2]-(b)[2])
#define VectorCopy(a, b) ((b)[0]=(a)[0], (b)[1]=(a)[1], (b)[2]=(a)[2])
#define VectorScale(a, s, c) ((c)[0]=(a)[0]*(s), (c)[1]=(a)[1]*(s), (c)[2]=(a)[2]*(s))
#define VectorLength(a) sqrtf(DotProduct(a, a))
typedef struct { float value; } cvar_t;
typedef struct { int nextsurface, numtris, numindexes, eboofs, poseverttype, numbones; vec3_t scale, scale_origin; } aliashdr_t;
typedef struct qmodel_s { int type, meshvbo, meshindexesvbo, flags; char name[64];
                          vec3_t mins, maxs, rmins, rmaxs; const char *meshindexesvboptr; aliashdr_t hdr; } qmodel_t;
typedef struct { int scale; } entity_state_t;
typedef struct { qmodel_t *model; entity_state_t netstate; int csqc_renderflags, skinnum, culled, pose, id;
                 unsigned char alpha; vec3_t origin; } entity_t;
typedef struct { int pose1, pose2; float blend; vec3_t origin, angles; } lerpdata_t;
typedef struct { GLuint program; GLint isOutlinePassLoc; } aliasglsl_t;
typedef struct { float matrix[16]; float light[4]; float shade_blend[4]; } alias_inst_data_t;
typedef struct { entity_t *ent; qmodel_t *model; lerpdata_t lerp; vec3_t scale; vec3_t scale_origin; } alias_inst_rec_t;
typedef struct { int kind, index; vec3_t dir; float angle; int level; } shadowq_entry_t;

static int seed;
static cvar_t gl_alias_instancing = {1}, r_outline = {1}, r_outline_minpixels = {0}, r_telestyle_detail = {2};
static qboolean gl_bmodel_instancing_able = true, r_teleport_view, r_drawflat_cheatsafe, r_lightmap_cheatsafe;
static struct { struct { qmodel_t *model; } viewent; } cl;
static struct { float fov_y; struct { int height; } vrect; vec3_t vieworg; } r_refdef = {90, {720}, {0, 0, 0}};
static vec3_t vpn = {1, 0, 0};
static aliasglsl_t r_alias_inst_glsl = {7, 3};
static entity_t *currententity, *r_deferred_alias_outlines[MAX_ALIAS_INSTANCES];
static shadowq_entry_t r_shadowq[MAX_SHADOWQ];
static int r_shadowq_sorted[MAX_SHADOWQ];
static alias_inst_data_t r_alias_inst_data[MAX_ALIAS_INSTANCES];
static int rs_aliaspolys, rs_aliaspasses;
static GLuint r_alias_outline_ref = 1, r_alias_outline_refmask = 0x7F;
static qboolean r_alias_outline_unique = true;

/* ---- recording GL ---- */
typedef struct { int kind, phase, func, ref, mask, ent; } event_t;   /* kind 0 = draw, 1 = clear */
static event_t events[8192];
static int nevents, cur_phase, stencil_func, stencil_ref, stencil_mask, attr_depth, attribs, program_bound;
static int bound_first = -1, bound_pose1, bound_pose2, upload_ok = 1, uploaded, lean_ends;
static qmodel_t *bound_model;
static entity_t *scene[MAX_ALIAS_INSTANCES];
static int nscene;
static void glPushAttrib(int bits) { (void)bits; attr_depth++; }
static void glPopAttrib(void) { attr_depth--; }
static void glEnable(int c) { (void)c; }
static void glDisable(int c) { (void)c; }
static void glDepthFunc(int f) { (void)f; }
static void glDepthMask(int m) { (void)m; }
static void glBlendFunc(int a, int b) { (void)a; (void)b; }
static void glCullFace(int f) { (void)f; }
static void glColorMask(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; }
static void glStencilFunc(int f, int r, int m) { stencil_func = f; stencil_ref = r; stencil_mask = m; }
static void glStencilOp(int a, int b, int c) { (void)a; (void)b; (void)c; }
static int stencil_write;
static void glStencilMask(GLuint m) { stencil_write = (int)m; }
static void glClearStencil(int v) { (void)v; }
static void glClear(int bits) { (void)bits; events[nevents++] = (event_t){1, 0, 0, 0, stencil_write, -1}; }
static void glTexEnvf(int a, int b, int c) { (void)a; (void)b; (void)c; }
static void glHint(int a, int b) { (void)a; (void)b; }
static void glShadeModel(int m) { (void)m; }
static void glColor3f(float r, float g, float b) { (void)r; (void)g; (void)b; }
static void GL_UseProgramFunc(GLuint p) { program_bound = (int)p; }
static void GL_Uniform1iFunc(GLint loc, int v) { if (loc == 3) cur_phase = v; }
static void *Mod_Extradata(qmodel_t *m) { return &m->hdr; }
static void R_SetupAliasFrame(aliashdr_t *h, entity_t *e, lerpdata_t *l) {
    (void)h; memset(l, 0, sizeof(*l)); l->pose1 = l->pose2 = e->pose;
}
static void R_SetupEntityTransform(entity_t *e, lerpdata_t *l) { VectorCopy(e->origin, l->origin); }
static int R_CullModelForEntityTransform(entity_t *e, float *o, float *a) { (void)o; (void)a; return e->culled; }
static float R_CalculateAliasModelOutlineWidth(aliashdr_t *h, entity_t *e, lerpdata_t *l, qboolean x, qboolean xo) {
    (void)h; (void)e; (void)l; (void)x; (void)xo; return 2.0f;
}
static qboolean R_IsOutlineBoundsFadeEnabled(void) { return false; }
static float R_GetEntityBoundsFadeFactor(const entity_t *e) { (void)e; return 1.0f; }
static void R_AliasInst_BuildMatrix(const alias_inst_rec_t *rec, float *m) {
    memset(m, 0, 16 * sizeof(float)); VectorCopy(rec->lerp.origin, m);   /* identifies the entity */
}
static GLuint R_AliasInst_Upload(int n) { uploaded = n; return (GLuint)upload_ok; }
static void R_AliasInst_BeginAttribs(void) { attribs = 1; }
static void R_AliasInst_BindAttribs(qmodel_t *m, aliashdr_t *h, const lerpdata_t *l, int first, GLuint *last) {
    (void)h; (void)last; bound_model = m; bound_first = first; bound_pose1 = l->pose1; bound_pose2 = l->pose2;
}
static void R_AliasInst_Unbind(void) { attribs = 0; bound_first = -1; }
static void R_EndDeferredAliasOutlineLean(void) { lean_ends++; }
static int entity_of(const alias_inst_data_t *d) {
    for (int i = 0; i < nscene; i++)
        if (scene[i]->origin[0] == d->matrix[0] && scene[i]->origin[1] == d->matrix[1] &&
            scene[i]->origin[2] == d->matrix[2])
            return i;
    return -1;
}
static void GL_DrawElementsInstancedFunc(int mode, int count, int type, const void *p, int inst) {
    (void)mode; (void)count; (void)type; (void)p;
    CHECK(attribs && bound_first >= 0 && program_bound == 7, "instanced draw without its program/attributes");
    for (int k = 0; k < inst; k++) {
        int e = entity_of(&r_alias_inst_data[bound_first + k]);
        CHECK(e >= 0, "instance data lost its entity");
        CHECK(scene[e]->model == bound_model && scene[e]->pose == bound_pose1 && scene[e]->pose == bound_pose2,
              "one instanced draw mixed models or poses");
        events[nevents++] = (event_t){0, cur_phase, stencil_func, stencil_ref, stencil_mask, e};
    }
}
'''
source += radius + "\n" + cone + "\n" + levels + "\n" + records + compare + "\n" + eligible + "\n" + clear + "\n" + run + "\n"
source += r'''
static qmodel_t models[3];
static entity_t ents[MAX_ALIAS_INSTANCES];
static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static int true_overlap(int i, int j) {
    /* exact view spheres: model radius plus the outline expansion (width 2 * scale 1) */
    float ri = radius_of(scene[i]), rj = radius_of(scene[j]);
    vec3_t di, dj;
    float li, lj, ai, aj, c;
    VectorSubtract(scene[i]->origin, r_refdef.vieworg, di);
    VectorSubtract(scene[j]->origin, r_refdef.vieworg, dj);
    li = VectorLength(di); lj = VectorLength(dj);
    if (li <= ri || lj <= rj) return 1;
    ai = asinf(ri / li); aj = asinf(rj / lj);
    if (ai + aj >= (float)M_PI) return 1;
    c = DotProduct(di, dj) / (li * lj);
    return acosf(CLAMP(-1.0f, c, 1.0f)) < ai + aj;
}
'''
source = source.replace("static int true_overlap(", r'''static float radius_of(const entity_t *e) {
    float r2 = 0;
    for (int k = 0; k < 3; k++) {
        float v = q_max(fabsf(e->model->mins[k]), fabsf(e->model->maxs[k]));
        r2 += v * v;
    }
    return sqrtf(r2) + 2.0f;
}
static int true_overlap(''')
source += r'''
static void make_model(qmodel_t *m, const char *name, float r) {
    memset(m, 0, sizeof(*m));
    m->type = mod_alias; m->meshvbo = m->meshindexesvbo = 1;
    strcpy(m->name, name);
    for (int k = 0; k < 3; k++) { m->mins[k] = m->rmins[k] = -r; m->maxs[k] = m->rmaxs[k] = r; }
    m->hdr.numtris = 10; m->hdr.numindexes = 30;
    m->hdr.scale[0] = m->hdr.scale[1] = m->hdr.scale[2] = 1;
}
/* Validate one run's event log against the sequential replay's contract. */
static void validate(int start_ref, int n, int expect_clear_ok) {
    int mask_at[MAX_ALIAS_INSTANCES], ring_at[MAX_ALIAS_INSTANCES], mref[MAX_ALIAS_INSTANCES], rref[MAX_ALIAS_INSTANCES];
    int i, j, last_ref = start_ref, clears = 0;
    for (i = 0; i < n; i++) mask_at[i] = ring_at[i] = -1;
    for (i = 0; i < nevents; i++) {
        const event_t *ev = &events[i];
        if (ev->kind == 1) {
            CHECK(ev->mask == (int)r_alias_outline_refmask, "a wrap must clear exactly the value bits");
            clears++; last_ref = 0; continue;
        }
        if (ev->phase == 2) {
            CHECK(ev->func == GL_ALWAYS && ev->mask == (int)r_alias_outline_refmask, "mask must stamp its value");
            CHECK(mask_at[ev->ent] < 0, "entity masked twice");
            CHECK(ev->ref >= last_ref || last_ref == 0, "values must never go backwards without a clear");
            CHECK(ev->ref >= 1 && ev->ref <= (int)r_alias_outline_refmask, "value out of range");
            last_ref = ev->ref; mask_at[ev->ent] = i; mref[ev->ent] = ev->ref;
        } else {
            CHECK(ev->phase == 1, "draw outside a mask or ring pass");
            CHECK(ev->func == GL_NOTEQUAL && mask_at[ev->ent] >= 0 && ev->ref == mref[ev->ent],
                  "ring must follow its own mask and test its own value");
            CHECK(ring_at[ev->ent] < 0, "entity ringed twice");
            ring_at[ev->ent] = i; rref[ev->ent] = ev->ref;
        }
    }
    CHECK(expect_clear_ok || !clears, "unexpected wrap clear");
    for (i = 0; i < n; i++) {
        if (scene[i]->culled) { CHECK(mask_at[i] < 0 && ring_at[i] < 0, "culled entity drawn"); continue; }
        CHECK(mask_at[i] >= 0 && ring_at[i] >= 0, "eligible entity lost a pass");
    }
    for (i = 0; i < n; i++) for (j = i + 1; j < n; j++) {
        int k, cleared_between = 0;
        if (scene[i]->culled || scene[j]->culled) continue;
        /* Same value, both alive at once: their silhouettes must not touch. */
        for (k = q_min(mask_at[i], mask_at[j]); k < q_max(ring_at[i], ring_at[j]); k++)
            if (events[k].kind == 1) cleared_between = 1;
        if (mref[i] == mref[j] && !cleared_between)
            CHECK(!true_overlap(i, j), "overlapping entities share a stencil value");
        if (true_overlap(i, j))
            CHECK(mask_at[j] > ring_at[i], "a later overlapping entity masked before the earlier ring");
    }
    (void)rref;
}
static void reset_gl(void) {
    nevents = 0; cur_phase = 0; attr_depth = 0; attribs = 0; program_bound = 0; uploaded = 0; lean_ends = 0;
}
int main(void) {
    make_model(&models[0], "progs/armor.mdl", 16);
    make_model(&models[1], "progs/backpack.mdl", 8);
    make_model(&models[2], "progs/holey.mdl", 8); models[2].flags = MF_HOLEY;
    for (seed = 1; seed <= 300; seed++) {
        int n = 2 + seed % 120, holey_at = (seed % 7 == 0) ? n / 2 : -1, consumed, lean = seed & 1;
        GLuint program = lean ? 5 : 0;
        qboolean lean_bound = lean;
        GLboolean colormask[4] = {1, 1, 1, 1};
        int start = seed % 3 == 0 ? 1 + seed % 5 : 0;
        srand((unsigned)seed);
        r_alias_outline_refmask = seed % 5 == 0 ? 0x0F : 0x7F;
        r_alias_outline_ref = start ? 1 + (unsigned)(seed % (int)r_alias_outline_refmask) : 1;
        nscene = n;
        for (int i = 0; i < n; i++) {
            entity_t *e = &ents[i];
            memset(e, 0, sizeof(*e));
            e->model = &models[rand() % 2];
            if (i == holey_at) e->model = &models[2];
            e->netstate.scale = ENTSCALE_DEFAULT;
            e->pose = rand() % 3;
            e->culled = rand() % 11 == 0;
            /* a crowded cone ahead of the eye, unique x so instances map back */
            e->origin[0] = 60.0f + 400.0f * frand() + i * 0.001f;
            e->origin[1] = (frand() - 0.5f) * 300.0f;
            e->origin[2] = (frand() - 0.5f) * 200.0f;
            scene[i] = r_deferred_alias_outlines[start + i] = e;
        }
        reset_gl();
        {
            GLuint first_ref = r_alias_outline_ref;
            consumed = R_DrawDeferredAliasOutlinesInstancedRun(start, n, GL_TRUE, colormask, &lean_bound, &program);
            CHECK(consumed == (holey_at >= 0 ? holey_at : n), "run must stop exactly at the first ineligible entity");
            if (holey_at >= 0) nscene = consumed;
            if (holey_at == 0) continue;
            validate(start ? (int)first_ref + 1 : (int)first_ref, nscene, r_alias_outline_refmask == 0x0F || start);
            if (!start) {
                int first_mask = -1;
                for (int i = 0; i < nevents; i++) if (events[i].kind == 0) { first_mask = events[i].ref; break; }
                CHECK(first_mask < 0 || first_mask == (int)first_ref, "a run at the queue head must use the current value");
            }
        }
        CHECK(!attr_depth && !attribs && cur_phase == 0, "run leaked attribs, attributes or the outline-pass uniform");
        CHECK(!lean_bound && !program && lean_ends == lean, "run must close an open lean replay first");
    }
    /* No instance buffer: nothing drawn, and the caller replays these entities itself. */
    seed = 0; upload_ok = 0; reset_gl();
    {
        GLuint program = 0; qboolean lean_bound = false; GLboolean colormask[4] = {1, 1, 1, 1};
        nscene = 4;
        for (int i = 0; i < 4; i++) {
            memset(&ents[i], 0, sizeof(ents[i]));
            ents[i].model = &models[0]; ents[i].netstate.scale = ENTSCALE_DEFAULT;
            ents[i].origin[0] = 100.0f + 50.0f * i;
            scene[i] = r_deferred_alias_outlines[i] = &ents[i];
        }
        CHECK(R_DrawDeferredAliasOutlinesInstancedRun(0, 4, GL_TRUE, colormask, &lean_bound, &program) == 0 && !nevents,
              "a failed upload must fall back without drawing");
    }
    puts("PASS: instanced outline waves keep overlapping entities ordered and apart, share values only when disjoint, group by model/pose, count/wrap values, stop at ineligible entities, restore state");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-outline-waves-") as tmp:
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
