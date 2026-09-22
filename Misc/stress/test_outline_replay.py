"""Exercise production deferred outline dispatch and GL pass orchestration.

Single-surface models must prepare once for their stencil/ring/scrub passes.
Multi-surface models must retain whole-model stencil barriers, otherwise one
surface can erase or expose another surface's outline. Compile with CC, or
run from a Visual Studio developer shell on Windows.
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


types = renderer[renderer.index("typedef enum alias_outline_phase_e"):
                 renderer.index("static qboolean R_QueueDeferredAliasOutline")]
draw = function("static void GL_DrawAliasFrame_GLSL (")
passes = draw[draw.index("alias_outline_phase_t saved_phase"):
              draw.index("\n\t}\n\n// clean up")]
if "--mutation-check" in sys.argv:
    # Deliberately lose scrub in the extracted code, never edit the renderer.
    passes = passes.replace("last_phase = ALIAS_OUTLINE_PHASE_SCRUB;",
                            "last_phase = ALIAS_OUTLINE_PHASE_RING;")
source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_EDICTS 16
#define false 0
#define true 1
#define GL_ENABLE_BIT 1
#define GL_STENCIL_BUFFER_BIT 2
#define GL_SCISSOR_TEST 3
#define GL_COLOR_BUFFER_BIT 4
#define GL_DEPTH_BUFFER_BIT 8
#define GL_POLYGON_BIT 16
#define GL_DEPTH_TEST 20
#define GL_STENCIL_TEST 21
#define GL_LEQUAL 22
#define GL_ALWAYS 23
#define GL_KEEP 24
#define GL_REPLACE 25
#define GL_TRIANGLES 26
#define GL_UNSIGNED_SHORT 27
#define GL_FALSE 0
#define CHECK(cond, message) do { if (!(cond)) { puts("FAIL: " message); exit(1); } } while (0)
typedef int qboolean;
typedef struct { int nextsurface, numtris, numindexes, eboofs; } aliashdr_t;
typedef struct { aliashdr_t header; const char *meshindexesvboptr; } qmodel_t;
typedef struct { qmodel_t *model; int alias_outline_order; } entity_t;
typedef struct { int outlineWidthLoc, isOutlinePassLoc, shellModeLoc, useShellTexLoc; } aliasglsl_t;
typedef struct { int unused; } lerpdata_t;
typedef struct { int depthwrite, colorwrite, stencilwrite, depthfunc, stencilref; } glstate_t;
static entity_t *currententity;
static entity_t *cl_visedicts[4];
static int cl_numvisedicts;
static int gl_stencilbits = 8, clears, saved, setups, phase_count;
static int phases[32], surface_index, skip_ring, rs_aliaspolys, rs_aliaspasses;
static int uniforms[4];
static float width;
static glstate_t state = {1, 1, 127, 99, 77}, stack[8];
static void glPushAttrib(int bits) { (void)bits; stack[saved++] = state; }
static void glPopAttrib(void) { state = stack[--saved]; }
static void glEnable(int cap) { (void)cap; }
static void glDisable(int cap) { (void)cap; }
static void glClearStencil(int value) { CHECK(value == 0, "wrong clear value"); }
static void glStencilMask(int mask) { state.stencilwrite = mask; }
static void glClear(int bits) { (void)bits; CHECK(state.stencilwrite == 1, "clear overwrites other stencil bits"); clears++; }
static void glDepthMask(int value) { state.depthwrite = value; }
static void glDepthFunc(int value) { state.depthfunc = value; }
static void glColorMask(int a, int b, int c, int d) {
    CHECK(a == b && a == c && a == d, "unexpected color mask"); state.colorwrite = a;
}
static void glStencilFunc(int func, int ref, int mask) {
    CHECK(func == GL_ALWAYS && mask == 1, "wrong stencil function/mask"); state.stencilref = ref;
}
static void glStencilOp(int a, int b, int c) {
    CHECK(a == GL_KEEP && b == GL_KEEP && c == GL_REPLACE, "wrong stencil operations");
}
static void GL_Uniform1fFunc(int location, float value) { CHECK(location == 0, "unexpected float uniform"); width = value; }
static void GL_Uniform1iFunc(int location, int value) { uniforms[location] = value; }
static void *Mod_Extradata(qmodel_t *model) { return &model->header; }
'''
source += types
source += r'''
/* Record the actual production pass loop's GL draw calls and draw-time state. */
static void glDrawElements(int mode, int count, int type, const void *offset) {
    CHECK(mode == GL_TRIANGLES && count == 30 && type == GL_UNSIGNED_SHORT && offset == NULL,
          "geometry submission changed");
    CHECK(!state.depthwrite && state.depthfunc == GL_LEQUAL, "outline pass modifies depth");
    if (r_alias_outline_phase != ALIAS_OUTLINE_PHASE_RING) {
        CHECK(!state.colorwrite && state.stencilwrite == 1, "mask/scrub modifies color or other stencil bits");
        CHECK(state.stencilref == (r_alias_outline_phase == ALIAS_OUTLINE_PHASE_MASK ? 1 : 0),
              "mask/scrub writes wrong stencil value");
        CHECK(width == 0 && uniforms[1] == 0 && uniforms[2] == 0 && uniforms[3] == 0,
              "mask/scrub inherited ring or shell shader state");
    } else {
        CHECK(width == 4 && uniforms[1] == 1, "ring shader state lost");
    }
    phases[phase_count++] = r_alias_outline_phase * 10 + surface_index;
}
static void R_DrawAliasModelOutline(aliasglsl_t *glsl, aliashdr_t *hdr, lerpdata_t *lerp, entity_t *e) {
    (void)lerp; (void)e;
    if (skip_ring) return; /* Pixel/bounds fade can reject the ring. */
    GL_Uniform1fFunc(glsl->outlineWidthLoc, 4);
    GL_Uniform1iFunc(glsl->isOutlinePassLoc, 1);
    glDrawElements(GL_TRIANGLES, hdr->numindexes, GL_UNSIGNED_SHORT, NULL);
    state.depthwrite = 1; state.stencilwrite = 255; /* Real ring cleanup. */
}
static void draw_outline_passes(aliasglsl_t *glsl, aliashdr_t *paliashdr, lerpdata_t lerpdata, entity_t *e) {
'''
source += passes + "\n}\n"
source += r'''
/* Replace only the unrelated model setup; execute the real GL orchestration. */
static void R_DrawAliasModel(entity_t *entity) {
    aliasglsl_t shader = {0, 1, 2, 3};
    lerpdata_t lerp = {0};
    int phase = r_alias_outline_phase;
    setups++;
    for (surface_index = 0; surface_index <= !!entity->model->header.nextsurface; surface_index++) {
        rs_aliaspolys += entity->model->header.numtris;
        draw_outline_passes(&shader, &entity->model->header, lerp, entity);
        rs_aliaspasses += entity->model->header.numtris;
        CHECK(r_alias_outline_phase == phase, "pass loop did not restore replay phase");
    }
}
'''
source += function("static int R_CompareDeferredAliasOutlines(")
source += function("void R_BeginDeferredAliasOutlines(void)")
source += function("void R_DrawDeferredAliasOutlines(void)")
source += r'''
int main(void) {
    qmodel_t single = {{0, 10, 30, 0}, NULL}, multi = {{128, 10, 30, 0}, NULL};
    entity_t a = {&single}, b = {&multi};
    glstate_t original = state;
    cl_visedicts[0] = &a; cl_visedicts[1] = &b; cl_visedicts[2] = &a;
    cl_numvisedicts = 3; r_alias_outline_order_changed = 1;
    R_BeginDeferredAliasOutlines();
    CHECK(a.alias_outline_order == 0 && b.alias_outline_order == 1 &&
          !r_alias_outline_order_changed, "collection must reset order and stamp first visible occurrence");
    r_deferred_alias_outlines[0] = &a;
    r_num_deferred_alias_outlines = 1;
    R_DrawDeferredAliasOutlines();
    CHECK(setups == 1, "single-surface outline repeats full model setup");
    CHECK(phase_count == 3 && phases[0] == 10 && phases[1] == 20 && phases[2] == 30,
          "single-surface stencil/ring/scrub order changed");
    CHECK(rs_aliaspolys == 30 && rs_aliaspasses == 30, "single-surface triangle counters changed");
    CHECK(!memcmp(&state, &original, sizeof(state)), "pass loop leaks GL state");
    setups = phase_count = 0;
    r_deferred_alias_outlines[0] = &b;
    r_num_deferred_alias_outlines = 1;
    R_DrawDeferredAliasOutlines();
    CHECK(setups == 3 && phase_count == 6, "multi-surface outline lost whole-model passes");
    CHECK(phases[0] == 10 && phases[1] == 11 && phases[2] == 20 && phases[3] == 21 &&
          phases[4] == 30 && phases[5] == 31, "multi-surface stencil barriers changed");
    CHECK(!r_num_deferred_alias_outlines && !r_alias_outline_collecting &&
          r_alias_outline_phase == ALIAS_OUTLINE_PHASE_NORMAL && !saved,
          "outline replay did not restore state");
    R_DrawDeferredAliasOutlines();
    CHECK(clears == 3 && setups == 3, "empty queue must clear stale stencil without model work");
    setups = phase_count = 0;
    r_deferred_alias_outlines[0] = &a;
    r_deferred_alias_outlines[1] = &b;
    r_deferred_alias_outlines[2] = &a;
    r_num_deferred_alias_outlines = 3;
    R_DrawDeferredAliasOutlines();
    {
        const int expected[] = {10,20,30, 10,11,20,21,30,31, 10,20,30};
        CHECK(setups == 5 && phase_count == 12 && !memcmp(phases, expected, sizeof(expected)),
              "mixed queue lost per-entity/per-surface stencil ordering");
    }
    setups = phase_count = 0;
    r_deferred_alias_outlines[0] = &b;
    r_deferred_alias_outlines[1] = &a;
    r_num_deferred_alias_outlines = 2;
    r_alias_outline_order_changed = 1;
    R_DrawDeferredAliasOutlines();
    {
        const int expected[] = {10,20,30, 10,11,20,21,30,31};
        CHECK(phase_count == 9 && !memcmp(phases, expected, sizeof(expected)),
              "late instanced outlines must replay in original visible order");
    }
    setups = phase_count = 0; skip_ring = 1;
    r_deferred_alias_outlines[0] = &a;
    r_num_deferred_alias_outlines = 1;
    R_DrawDeferredAliasOutlines();
    CHECK(setups == 1 && phase_count == 2 && phases[0] == 10 && phases[1] == 30,
          "faded ring must still scrub its mask");
    CHECK(!memcmp(&state, &original, sizeof(state)), "faded ring leaks GL state");
    puts("PASS: setup reuse, real GL pass order/state/uniforms/counters, multi-surface and mixed queues, faded rings");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-outline-") as tmp:
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
