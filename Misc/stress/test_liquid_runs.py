"""Exercise production liquid-brush runs: eligibility, queue order, texture lists.

Maps such as immortal.bsp hold hundreds of translucent liquid-only brush
entities. The alpha pass queues consecutive ones and draws them as a run with
the liquid program set up once. This compiles the real queue, eligibility,
texture-list and flush code from Quake/r_brush.c with recording stubs and
checks that:

* only translucent, effect-free brush models whose every surface is plain
  liquid (no teleporter or sky) are queued, and only while collecting with a
  normal GLSL render mode available;
* the flush draws queued entities in exactly the queued order, sends a run of
  one through R_DrawBrushModel, skips culled entities, starts the run once and
  only when something draws, ends it exactly once, and uploads lightmaps on
  texture unit 0 before each entity's surfaces;
* texture lists are ascending and unique, fall back to the full chain walk when
  a surface's texture does not match its index or there are too many, and a
  full queue is flushed before it takes more.

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


defines = "\n".join(re.findall(r"^#define (?:MAX_LIQUID_RUN|MAX_LIQUID_TEXTURES)\b.*$", brush, re.M))
statics = brush[brush.index("static entity_t *r_liquid_queue[MAX_LIQUID_RUN];"):
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
typedef struct { int type, firstmodelsurface, nummodelsurfaces, numtextures; msurface_t *surfaces; texture_t **textures; } qmodel_t;
typedef struct { qmodel_t *model; int effects; unsigned char alpha; int culled, id; } entity_t;
static struct { void *polys; } lightmaps[8];
static int lightmap_count = 8;
/* recording stubs */
static int calls[4096], ncalls, active_unit, run_open, run_begins, run_ends, available = 1;
static int drawn_ids[4096], ndrawn, drawn_lists[4096], full_chains, list_chains;
enum { C_SINGLE = 1, C_BEGIN, C_UPLOAD, C_DRAW, C_POP, C_RUNBEGIN, C_RUNEND };
#define REC(x) (calls[ncalls++] = (x))
static qboolean R_LiquidRunAvailable(void) { return available; }
static void R_DrawBrushModel(entity_t *e) { REC(C_SINGLE); drawn_ids[ndrawn++] = e->id; }
static qboolean R_BeginBrushModel(entity_t *e) { if (e->culled) return false; REC(C_BEGIN); return true; }
static qboolean R_ChainBrushModel(qmodel_t *m) { (void)m; full_chains++; return false; }
static void R_ChainLiquidModel(qmodel_t *m, const int *idx, int n) { (void)m; (void)idx; (void)n; list_chains++; }
static void R_LiquidRunBegin(float a) { (void)a; CHECK(!run_open, "run begun twice"); run_open = 1; run_begins++; REC(C_RUNBEGIN); }
static void R_LiquidRunEnd(void) { CHECK(run_open, "run ended without a begin"); run_open = 0; run_ends++; REC(C_RUNEND); }
static void GL_SelectTexture(int unit) { active_unit = unit; }
static void R_UploadLightmaps(void) { CHECK(active_unit == GL_TEXTURE0, "lightmaps must upload with unit 0 selected"); REC(C_UPLOAD); }
static void R_LiquidRunDrawModel(qmodel_t *m, entity_t *e, texchain_t c, const int *t, int n) {
    (void)m; (void)c; CHECK(run_open, "run draw outside a run"); REC(C_DRAW);
    drawn_lists[ndrawn] = t != NULL; drawn_ids[ndrawn++] = e->id; (void)n;
}
static void glPopMatrix(void) { REC(C_POP); }
'''
source += defines + "\n" + statics + "\n"
for decl in ("static qboolean R_LiquidOnlyModel (",
             "static int R_LiquidModelTextures (",
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
static qmodel_t liquid, mixed, tele, sky, empty, bad, wide;
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
static void reset(void) { ncalls = ndrawn = run_begins = run_ends = full_chains = list_chains = 0; active_unit = -1; }
int main(void) {
    int i, idx[MAX_LIQUID_TEXTURES];
    const int t_liq[] = {5, 2, 5, 2, 7}, t_mix[] = {1, 1}, t_one[] = {3};
    for (i = 0; i < 8; i++) { textab[i] = &tex[i]; ti[i].texture = &tex[i]; ti[i].materialidx = i; }
    model(&liquid, 0, 5, t_liq, SURF_DRAWTURB);
    model(&mixed, 5, 2, t_mix, SURF_DRAWTURB); surfs[6].flags = 0;          /* one solid surface */
    model(&tele, 7, 1, t_one, SURF_DRAWTURB | SURF_DRAWTELE);
    model(&sky, 8, 1, t_one, SURF_DRAWTURB | SURF_DRAWSKY);
    model(&empty, 9, 0, t_one, SURF_DRAWTURB);

    /* texture lists: ascending, unique */
    CHECK(R_LiquidModelTextures(&liquid, idx) == 3 && idx[0] == 2 && idx[1] == 5 && idx[2] == 7,
          "texture list must be the sorted unique indices");
    /* a surface whose texture is not textures[materialidx] */
    model(&bad, 10, 1, t_one, SURF_DRAWTURB); { static mtexinfo_t odd; odd.texture = &tex[4]; odd.materialidx = 3; surfs[10].texinfo = &odd; }
    CHECK(R_LiquidModelTextures(&bad, idx) == -1, "mismatched texture index must fall back");
    { /* more textures than the list holds */
        static texture_t many[MAX_LIQUID_TEXTURES + 2]; static texture_t *manytab[MAX_LIQUID_TEXTURES + 2];
        static mtexinfo_t manyti[MAX_LIQUID_TEXTURES + 2]; static msurface_t manys[MAX_LIQUID_TEXTURES + 2];
        for (i = 0; i < MAX_LIQUID_TEXTURES + 2; i++) { manytab[i] = &many[i]; manyti[i].texture = &many[i]; manyti[i].materialidx = i; manys[i].flags = SURF_DRAWTURB; manys[i].texinfo = &manyti[i]; }
        wide.type = mod_brush; wide.surfaces = manys; wide.firstmodelsurface = 0; wide.nummodelsurfaces = MAX_LIQUID_TEXTURES + 1;
        wide.numtextures = MAX_LIQUID_TEXTURES + 2; wide.textures = manytab;
        CHECK(R_LiquidModelTextures(&wide, idx) == -1, "too many textures must fall back");
    }

    /* eligibility */
    CHECK(!R_QueueLiquidBrush(ent(1, &liquid, 100)), "nothing queues outside the alpha pass");
    R_BeginLiquidBrushes();
    CHECK(!R_QueueLiquidBrush(ent(2, &liquid, 0)), "opaque (default alpha) entities are not queued");
    CHECK(!R_QueueLiquidBrush(ent(3, &liquid, 255)), "fully opaque alpha is not queued");
    { entity_t *e = ent(4, &liquid, 100); e->effects = 1; CHECK(!R_QueueLiquidBrush(e), "entities with effects are not queued"); }
    CHECK(!R_QueueLiquidBrush(ent(5, &mixed, 100)), "models with a non-liquid surface are not queued");
    CHECK(!R_QueueLiquidBrush(ent(6, &tele, 100)), "teleporter surfaces are not queued");
    CHECK(!R_QueueLiquidBrush(ent(7, &sky, 100)), "sky surfaces are not queued");
    CHECK(!R_QueueLiquidBrush(ent(8, &empty, 100)), "surfaceless models are not queued");
    { entity_t *e = ent(9, &liquid, 100); e->model->type = mod_alias; CHECK(!R_QueueLiquidBrush(e), "only brush models"); liquid.type = mod_brush; }
    available = 0; CHECK(!R_QueueLiquidBrush(ent(10, &liquid, 100)), "debug render modes are not queued"); available = 1;

    /* a run of one takes the ordinary path */
    reset(); CHECK(R_QueueLiquidBrush(ent(20, &liquid, 100)), "eligible entity queues"); R_FlushLiquidBrushes();
    CHECK(ncalls == 1 && calls[0] == C_SINGLE && drawn_ids[0] == 20 && !run_begins, "a single queued entity uses R_DrawBrushModel");

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
    puts("PASS: liquid run eligibility, queue order, culling, run bracketing, lightmap uploads, texture lists and full-queue flush");
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
