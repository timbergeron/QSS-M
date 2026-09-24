"""Exercise production translucent brush runs: eligibility, queue order, texture lists.

Maps such as immortal.bsp and Peril's start.bsp hold hundreds of translucent
brush entities: liquid-only func_walls, and see-through walls of plain
lightmapped textures. The alpha pass queues consecutive ones of one kind and
draws them as a run with the program set up once. This compiles the real
queue, eligibility, texture-list, chaining and flush code from
Quake/r_brush.c with recording stubs and checks that:

* only translucent, effect-free brush models are queued, and only while
  collecting: plain-liquid models (no teleporter or sky) as liquid runs with a
  normal GLSL render mode available, others as solid runs when
  R_SolidRunModel accepts them;
* a change of kind or a full queue flushes what is queued first, so the
  back-to-front order is kept across kinds;
* the flush draws queued entities in exactly the queued order, sends a run of
  one through R_DrawBrushModel, skips culled entities, starts the run once and
  only when something draws, ends it exactly once with the matching kind, and
  uploads lightmaps on texture unit 0 before each liquid entity's surfaces;
* solid runs name each model's texture list for the chain walks only while
  that model draws, and fall back to every texture without one; entities with
  the same transform share one pushed matrix, and the run's open batch is
  flushed before every matrix pop, which always matches a push;
* texture lists are ascending and unique, are built once per model load (a new
  model generation rebuilds them), fall back to the full chain walk when a
  surface's texture does not match its index or there are too many, and the
  list chaining clears only those textures' chains, leaves the lightmap poly
  lists alone and chains without linking them.

Compile with CC, or run from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
brush = (ROOT / "Quake/r_brush.c").read_text()


def function(declaration):
    start = brush.index(declaration)
    return brush[start:brush.index("\n}", start) + 2]


defines = "\n".join(re.findall(r"^#define (?:MAX_LIQUID_RUN)\b.*$", brush, re.M)) + "\n#define MAX_MODEL_CHAIN_TEXTURES 64"
statics = brush[brush.index("enum { RUN_LIQUID, RUN_SOLID };"):
                brush.index("static qboolean R_LiquidOnlyModel")]

source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define false 0
#define true 1
#define SURF_DRAWTURB 0x10
#define SURF_DRAWSKY 0x4
#define SURF_DRAWTELE 0x1000
#define mod_brush 0
#define mod_alias 2
#define ENTALPHA_DEFAULT 0
#define ENTALPHA_DECODE(a) (((a)==ENTALPHA_DEFAULT)?1.0f:((float)(a)-1)/(254))
#define GL_TEXTURE0 0x84C0
#define CHECK(cond, message) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", message, __LINE__); exit(1); } } while (0)
typedef int qboolean;
typedef enum { chain_world, chain_model } texchain_t;
typedef struct texture_s { struct msurface_s *texturechains[2]; } texture_t;
typedef struct { texture_t *texture; int materialidx; } mtexinfo_t;
typedef struct msurface_s { int flags; mtexinfo_t *texinfo; int lightmaptexturenum; } msurface_t;
typedef struct { int type, firstmodelsurface, nummodelsurfaces, numtextures; msurface_t *surfaces; texture_t **textures;
    int chaintextures_modgen, chainnumtextures; unsigned short chaintextures[64]; } qmodel_t;
typedef struct { qmodel_t *model; int effects; unsigned char alpha; int culled, id, solid, mkey; } entity_t;
static int mod_generation;
static struct { void *polys; } lightmaps[8];
static int lightmap_count = 8;
/* recording stubs */
static int calls[16384], ncalls, active_unit, run_open, run_kind, run_begins, run_ends, available = 1;
static int drawn_ids[4096], ndrawn, drawn_lists[4096], full_chains, list_chains;
static qmodel_t *chain_textures_model; static int chain_textures_count;
enum { C_SINGLE = 1, C_BEGIN, C_UPLOAD, C_DRAW, C_POP, C_RUNBEGIN, C_RUNEND, C_SRUNBEGIN, C_SDRAW, C_SRUNEND, C_PUSH, C_FLUSH };
static int matrix_depth, pushed_key = -1, flushed_since_draw = 1, lmchain_arg = -1;
#define REC(x) (calls[ncalls++] = (x))
static qboolean R_LiquidRunAvailable(void) { return available; }
static qboolean R_SolidRunModel(entity_t *e) { return available && e->solid; }
static void R_DrawBrushModel(entity_t *e) { REC(C_SINGLE); drawn_ids[ndrawn++] = e->id; }
static qboolean R_SetupBrushModel(entity_t *e) { if (e->culled) return false; REC(C_BEGIN); return true; }
static void R_PushBrushModelMatrix(entity_t *e) { CHECK(matrix_depth == 0, "matrix pushed twice"); matrix_depth++; pushed_key = e->mkey; REC(C_PUSH); }
static qboolean R_BeginBrushModel(entity_t *e) { if (!R_SetupBrushModel(e)) return false; R_PushBrushModelMatrix(e); return true; }
static qboolean R_BrushModelSameMatrix(entity_t *a, entity_t *b) { return a->mkey == b->mkey; }
static void R_SolidRunFlush(void) { flushed_since_draw = 1; REC(C_FLUSH); }
static qboolean R_ChainBrushModel(qmodel_t *m) { (void)m; full_chains++; return false; }
static qboolean R_ChainVisibleSurfaces(qmodel_t *m, qboolean lm) { (void)m; lmchain_arg = lm; list_chains++; return true; }
static void R_SetChainTextures(qmodel_t *m, const unsigned short *t, int n) { (void)t; chain_textures_model = m; chain_textures_count = n; }
static void R_LiquidRunBegin(float a) { (void)a; CHECK(!run_open, "run begun twice"); run_open = 1; run_kind = 0; run_begins++; REC(C_RUNBEGIN); }
static void R_LiquidRunEnd(void) { CHECK(run_open && run_kind == 0, "liquid run ended without a liquid begin"); run_open = 0; run_ends++; REC(C_RUNEND); }
static void R_SolidRunBegin(float a) { (void)a; CHECK(!run_open, "run begun twice"); run_open = 1; run_kind = 1; run_begins++; REC(C_SRUNBEGIN); }
static void R_SolidRunEnd(void) { CHECK(run_open && run_kind == 1, "solid run ended without a solid begin"); CHECK(!chain_textures_model, "texture list left set after a run"); run_open = 0; run_ends++; REC(C_SRUNEND); }
static void GL_SelectTexture(int unit) { active_unit = unit; }
static void R_UploadLightmaps(void) { CHECK(active_unit == GL_TEXTURE0, "lightmaps must upload with unit 0 selected"); REC(C_UPLOAD); }
static void R_LiquidRunDrawModel(qmodel_t *m, entity_t *e, texchain_t c, const unsigned short *t, int n) {
    (void)m; (void)c; CHECK(run_open && run_kind == 0, "liquid draw outside a liquid run"); REC(C_DRAW);
    drawn_lists[ndrawn] = t != NULL; drawn_ids[ndrawn++] = e->id; (void)n;
}
static void R_SolidRunDrawModel(qmodel_t *m, entity_t *e, texchain_t c) {
    (void)c; CHECK(run_open && run_kind == 1, "solid draw outside a solid run"); REC(C_SDRAW);
    CHECK(matrix_depth == 1 && pushed_key == e->mkey, "solid entity must draw under its own transform"); flushed_since_draw = 0;
    CHECK(!chain_textures_model || chain_textures_model == m, "texture list must name the drawing model");
    drawn_lists[ndrawn] = chain_textures_model == m; drawn_ids[ndrawn++] = e->id;
}
static void glPopMatrix(void) {
    CHECK(!chain_textures_model, "texture list must be cleared before the matrix pops");
    CHECK(matrix_depth == 1, "pop without a push"); matrix_depth--;
    if (run_open && run_kind == 1) CHECK(flushed_since_draw, "the open batch must be flushed before the matrix pops");
    REC(C_POP);
}
'''
source += defines + "\n" + statics + "\n"
for decl in ("static int R_BrushModelTextures (",
             "static qboolean R_ChainModelTextures (",
             "static qboolean R_LiquidOnlyModel (",
             "void R_BeginLiquidBrushes (void)",
             "void R_FlushLiquidBrushes (void)",
             "qboolean R_QueueLiquidBrush (entity_t *e)",
             "void R_EndLiquidBrushes (void)"):
    source += function(decl) + "\n"
source += r'''
static texture_t tex[8];
static texture_t *textab[8];
static mtexinfo_t ti[8];
static msurface_t surfs[64];
static qmodel_t liquid, mixed, tele, sky, empty, bad, wide, wall, badwall, turn;
static entity_t ents[2048];
static void model(qmodel_t *m, int first, int n, const int *texidx, int flags) {
    int i;
    m->type = mod_brush; m->firstmodelsurface = first; m->nummodelsurfaces = n; m->surfaces = surfs;
    m->numtextures = 8; m->textures = textab;
    for (i = 0; i < n; i++) {
        surfs[first + i].flags = flags; surfs[first + i].texinfo = &ti[texidx[i]]; surfs[first + i].lightmaptexturenum = i % 8;
    }
}
static entity_t *ent(int id, qmodel_t *m, int alpha) {
    entity_t *e = &ents[id]; memset(e, 0, sizeof(*e)); e->id = id; e->model = m; e->alpha = (unsigned char)alpha; return e;
}
static entity_t *solid(int id, qmodel_t *m, int alpha) { entity_t *e = ent(id, m, alpha); e->solid = 1; return e; }
static void reset(void) { ncalls = ndrawn = run_begins = run_ends = full_chains = list_chains = 0; active_unit = -1; }
int main(void) {
    int i; const unsigned short *idx;
    const int t_liq[] = {5, 2, 5, 2, 7}, t_mix[] = {1, 1}, t_one[] = {3}, t_wall[] = {6, 1, 6};
    for (i = 0; i < 8; i++) { textab[i] = &tex[i]; ti[i].texture = &tex[i]; ti[i].materialidx = i; }
    model(&liquid, 0, 5, t_liq, SURF_DRAWTURB);
    model(&mixed, 5, 2, t_mix, SURF_DRAWTURB); surfs[6].flags = 0;          /* one solid surface */
    model(&tele, 7, 1, t_one, SURF_DRAWTURB | SURF_DRAWTELE);
    model(&sky, 8, 1, t_one, SURF_DRAWTURB | SURF_DRAWSKY);
    model(&empty, 9, 0, t_one, SURF_DRAWTURB);
    model(&wall, 20, 3, t_wall, 0);

    /* texture lists: ascending, unique */
    CHECK(R_BrushModelTextures(&liquid, &idx) == 3 && idx[0] == 2 && idx[1] == 5 && idx[2] == 7,
          "texture list must be the sorted unique indices");
    /* a surface whose texture is not textures[materialidx] */
    model(&bad, 10, 1, t_one, SURF_DRAWTURB); { static mtexinfo_t odd; odd.texture = &tex[4]; odd.materialidx = 3; surfs[10].texinfo = &odd; }
    CHECK(R_BrushModelTextures(&bad, &idx) == -1, "mismatched texture index must fall back");
    { /* more textures than the list holds */
        static texture_t many[MAX_MODEL_CHAIN_TEXTURES + 2]; static texture_t *manytab[MAX_MODEL_CHAIN_TEXTURES + 2];
        static mtexinfo_t manyti[MAX_MODEL_CHAIN_TEXTURES + 2]; static msurface_t manys[MAX_MODEL_CHAIN_TEXTURES + 2];
        for (i = 0; i < MAX_MODEL_CHAIN_TEXTURES + 2; i++) { manytab[i] = &many[i]; manyti[i].texture = &many[i]; manyti[i].materialidx = i; manys[i].flags = SURF_DRAWTURB; manys[i].texinfo = &manyti[i]; }
        wide.type = mod_brush; wide.surfaces = manys; wide.firstmodelsurface = 0; wide.nummodelsurfaces = MAX_MODEL_CHAIN_TEXTURES + 1;
        wide.numtextures = MAX_MODEL_CHAIN_TEXTURES + 2; wide.textures = manytab;
        CHECK(R_BrushModelTextures(&wide, &idx) == -1, "too many textures must fall back");
    }
    { /* lists are built once per model generation */
        const int t_turn[] = {4};
        model(&turn, 40, 1, t_turn, 0);
        CHECK(R_BrushModelTextures(&turn, &idx) == 1 && idx[0] == 4, "first build");
        surfs[40].texinfo = &ti[6];
        CHECK(R_BrushModelTextures(&turn, &idx) == 1 && idx[0] == 4, "the list is reused until the model generation changes");
        mod_generation++;
        CHECK(R_BrushModelTextures(&turn, &idx) == 1 && idx[0] == 6, "a new model generation rebuilds the list");
    }
    { /* list chaining clears only the listed chains, leaves lightmap poly lists, chains without linking */
        static msurface_t marker;
        int n = R_BrushModelTextures(&wall, &idx);
        for (i = 0; i < 8; i++) { tex[i].texturechains[chain_model] = &marker; tex[i].texturechains[chain_world] = &marker; lightmaps[i].polys = &marker; }
        list_chains = 0;
        CHECK(n == 2 && R_ChainModelTextures(&wall, idx, n) && list_chains == 1, "list chaining chains the model once");
        CHECK(lmchain_arg == 0, "list chaining does not link lightmap polys");
        for (i = 0; i < 8; i++) {
            CHECK((tex[i].texturechains[chain_model] == NULL) == (i == 1 || i == 6), "only the listed textures' chains are cleared");
            CHECK(tex[i].texturechains[chain_world] == &marker, "world chains are left alone");
            CHECK(lightmaps[i].polys == &marker, "lightmap poly lists are left alone");
        }
    }

    /* eligibility */
    CHECK(!R_QueueLiquidBrush(ent(1, &liquid, 100)), "nothing queues outside the alpha pass");
    R_BeginLiquidBrushes();
    CHECK(!R_QueueLiquidBrush(ent(2, &liquid, 0)), "opaque (default alpha) entities are not queued");
    CHECK(!R_QueueLiquidBrush(ent(3, &liquid, 255)), "fully opaque alpha is not queued");
    { entity_t *e = ent(4, &liquid, 100); e->effects = 1; CHECK(!R_QueueLiquidBrush(e), "entities with effects are not queued"); }
    { entity_t *e = solid(4, &wall, 100); e->effects = 1; CHECK(!R_QueueLiquidBrush(e), "solid entities with effects are not queued"); }
    CHECK(!R_QueueLiquidBrush(ent(5, &mixed, 100)), "models with a non-liquid surface are not queued as liquid");
    CHECK(!R_QueueLiquidBrush(ent(6, &tele, 100)), "teleporter surfaces are not queued");
    CHECK(!R_QueueLiquidBrush(ent(7, &sky, 100)), "sky surfaces are not queued");
    CHECK(!R_QueueLiquidBrush(ent(8, &empty, 100)), "surfaceless models are not queued");
    CHECK(!R_QueueLiquidBrush(solid(8, &wall, 255)), "opaque solid entities are not queued");
    { entity_t *e = ent(9, &liquid, 100); e->model->type = mod_alias; CHECK(!R_QueueLiquidBrush(e), "only brush models"); liquid.type = mod_brush; }
    available = 0; CHECK(!R_QueueLiquidBrush(ent(10, &liquid, 100)), "debug render modes are not queued");
    CHECK(!R_QueueLiquidBrush(solid(10, &wall, 100)), "debug render modes are not queued as solid"); available = 1;

    /* a run of one takes the ordinary path, of either kind */
    reset(); CHECK(R_QueueLiquidBrush(ent(20, &liquid, 100)), "eligible entity queues"); R_FlushLiquidBrushes();
    CHECK(ncalls == 1 && calls[0] == C_SINGLE && drawn_ids[0] == 20 && !run_begins, "a single queued entity uses R_DrawBrushModel");
    reset(); CHECK(R_QueueLiquidBrush(solid(21, &wall, 100)), "eligible solid entity queues"); R_FlushLiquidBrushes();
    CHECK(ncalls == 1 && calls[0] == C_SINGLE && drawn_ids[0] == 21 && !run_begins, "a single solid entity uses R_DrawBrushModel");

    /* order, culling, run bracketing, per-entity upload before draw */
    reset();
    for (i = 0; i < 6; i++) { entity_t *e = ent(30 + i, i == 4 ? &bad : &liquid, 50 + i); e->culled = (i == 0 || i == 3); bad.type = mod_brush;
        if (i == 4) { static msurface_t okbad; okbad = surfs[10]; okbad.flags = SURF_DRAWTURB; surfs[10] = okbad; }
        CHECK(R_QueueLiquidBrush(e), "run member queues"); }
    R_FlushLiquidBrushes();
    CHECK(run_begins == 1 && run_ends == 1 && !run_open, "a run begins and ends exactly once");
    CHECK(ndrawn == 4 && drawn_ids[0] == 31 && drawn_ids[1] == 32 && drawn_ids[2] == 34 && drawn_ids[3] == 35,
          "run must draw unculled entities in queue order");
    CHECK(drawn_lists[0] && drawn_lists[1] && !drawn_lists[2] && drawn_lists[3], "mismatched model walks all textures");
    CHECK(list_chains == 3 && full_chains == 1, "texture-list chaining with full fallback");
    for (i = 0; i < ncalls; i++)
        if (calls[i] == C_DRAW) CHECK(calls[i - 1] == C_UPLOAD && calls[i + 1] == C_POP, "each entity uploads, draws, pops");
    CHECK(calls[ncalls - 1] == C_RUNEND, "run ends after the last entity");

    /* solid runs: lists named per model, full walk without one, bracketed by the solid kind */
    reset();
    {	/* a solid model whose surface texture does not match its index */
        static mtexinfo_t odd; odd.texture = &tex[4]; odd.materialidx = 3;
        model(&badwall, 30, 1, t_one, 0); surfs[30].texinfo = &odd;
    }
    CHECK(R_QueueLiquidBrush(solid(50, &wall, 60)) && R_QueueLiquidBrush(solid(51, &badwall, 70)) && R_QueueLiquidBrush(solid(52, &wall, 80)), "solid run members queue");
    R_FlushLiquidBrushes();
    CHECK(run_begins == 1 && run_ends == 1 && !run_open, "a solid run begins and ends exactly once");
    CHECK(ndrawn == 3 && drawn_ids[0] == 50 && drawn_ids[1] == 51 && drawn_ids[2] == 52, "solid run draws in queue order");
    CHECK(drawn_lists[0] && !drawn_lists[1] && drawn_lists[2], "listed models name their textures, the others walk all");
    CHECK(calls[ncalls - 1] == C_SRUNEND, "solid run ends after the last entity");

    /* shared transforms: one push per change, a flush before every pop, culled entities in between */
    reset();
    {
        const int keys[7] = {1, 1, 1, 2, 2, 1, 1};
        int pushes = 0, pops = 0;
        for (i = 0; i < 7; i++) { entity_t *e = solid(70 + i, &wall, 90); e->mkey = keys[i]; e->culled = (i == 2 || i == 5); CHECK(R_QueueLiquidBrush(e), "shared-matrix members queue"); }
        R_FlushLiquidBrushes();
        for (i = 0; i < ncalls; i++) { pushes += calls[i] == C_PUSH; pops += calls[i] == C_POP; }
        CHECK(ndrawn == 5 && drawn_ids[0] == 70 && drawn_ids[1] == 71 && drawn_ids[2] == 73 && drawn_ids[3] == 74 && drawn_ids[4] == 76, "shared-matrix run draws in order");
        CHECK(pushes == 3 && pops == 3 && matrix_depth == 0, "one push per transform change, balanced by pops");
        for (i = 1; i < ncalls; i++) if (calls[i] == C_POP) CHECK(calls[i - 1] == C_FLUSH, "every pop follows a flush");
        CHECK(calls[ncalls - 1] == C_SRUNEND, "the run ends after the last pop");
    }

    /* a change of kind flushes what is queued, keeping the order */
    reset();
    CHECK(R_QueueLiquidBrush(ent(60, &liquid, 90)) && R_QueueLiquidBrush(ent(61, &liquid, 90)), "liquid queues");
    CHECK(R_QueueLiquidBrush(solid(62, &wall, 90)), "solid queues after liquid");
    CHECK(ndrawn == 2 && drawn_ids[0] == 60 && drawn_ids[1] == 61 && run_ends == 1, "switching kind flushes the liquid run first");
    CHECK(R_QueueLiquidBrush(solid(63, &wall, 90)), "second solid queues");
    CHECK(R_QueueLiquidBrush(ent(64, &liquid, 90)), "liquid queues after solid");
    CHECK(ndrawn == 4 && drawn_ids[2] == 62 && drawn_ids[3] == 63 && run_ends == 2, "switching back flushes the solid run");
    R_FlushLiquidBrushes();
    CHECK(ndrawn == 5 && drawn_ids[4] == 64, "the last liquid draws alone");

    /* all culled: no run at all */
    reset(); ents[40].culled = 1; R_QueueLiquidBrush(ent(40, &liquid, 90)); ents[40].culled = 1;
    R_QueueLiquidBrush(ent(41, &liquid, 90)); ents[41].culled = 1;
    R_FlushLiquidBrushes();
    CHECK(!run_begins && !run_ends && !ndrawn, "a fully culled run draws nothing and opens no run");

    /* a full queue flushes before taking more */
    reset();
    for (i = 0; i < MAX_LIQUID_RUN + 1; i++) CHECK(R_QueueLiquidBrush(ent(100 + (i % 1800), &liquid, 80)), "queue accepts");
    CHECK(ndrawn == MAX_LIQUID_RUN && run_begins == 1, "the full queue must be drawn before it takes more");
    R_EndLiquidBrushes();
    CHECK(ndrawn == MAX_LIQUID_RUN + 1, "end flushes the remainder");
    CHECK(!R_QueueLiquidBrush(ent(5, &liquid, 80)), "nothing queues after the alpha pass ends");
    puts("PASS: liquid and solid run eligibility, kind switches, queue order, culling, run bracketing, lightmap uploads, texture lists, list chaining and full-queue flush");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-liquid-") as tmp:
    work = Path(tmp)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        command = [*cc, "/nologo", "/W3", "/O2", "test.c", f"/Fe:{binary}"]
    else:
        command = [*cc, "-std=c99", "-Wall", "-Wextra", "-O2", "test.c", "-o", str(binary)]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
