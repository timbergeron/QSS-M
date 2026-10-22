"""Exercise production deferred outline dispatch and GL pass orchestration.

Single-surface models must prepare once for their stencil/ring(/scrub) passes.
Multi-surface models must retain whole-model stencil barriers, otherwise one
surface can erase or expose another surface's outline. With enough stencil
bits every model masks with its own value (no scrub pass, cleared once at the
end) through the lean replay, which never re-enters R_DrawAliasModel; with
fewer, or an MF_HOLEY skin queued, the legacy mask/ring(/scrub) replay remains.
Compile with CC, or run from a Visual Studio developer shell on Windows.
--mutation-check and --mutation-check-lean must FAIL: they break the extracted
legacy and lean code respectively, never the renderer.
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


outline = function("void R_DrawAliasModelOutline(")
ring_stencil = outline[outline.index("GLuint outline_stencil_mask"):
                       outline.index(";", outline.index("GLint outline_stencil_ref")) + 1]
assert "glStencilFunc(GL_NOTEQUAL, outline_stencil_ref, (GLint)outline_stencil_mask)" in outline, \
    "ring must test NOTEQUAL against the derived stencil ref/mask"
types = renderer[renderer.index("typedef enum alias_outline_phase_e"):
                 renderer.index("static qboolean R_QueueDeferredAliasOutline")]
draw = function("static void GL_DrawAliasFrame_GLSL (")
passes = draw[draw.index("alias_outline_phase_t saved_phase"):
              draw.index("\n\t}\n\n// clean up")]
if "--mutation-check" in sys.argv:
    # Deliberately lose scrub in the extracted code, never edit the renderer.
    passes = passes.replace(
        "last_phase = r_alias_outline_unique ? ALIAS_OUTLINE_PHASE_RING : ALIAS_OUTLINE_PHASE_SCRUB;",
        "last_phase = ALIAS_OUTLINE_PHASE_RING;")
lean = function("static void R_DrawDeferredAliasOutlineLean (")
if "--mutation-check-lean" in sys.argv:
    # Deliberately mask with the previous ring's culling.
    cull_restore = ("\t\t\tif (cull)\n\t\t\t\tglEnable (GL_CULL_FACE);\n"
                    "\t\t\telse\n\t\t\t\tglDisable (GL_CULL_FACE);\n")
    assert cull_restore in lean
    lean = lean.replace(cull_restore, "")
source = r'''
#include <stdio.h>
#include <stdint.h>
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
#define GL_TRUE 1
#define GL_CULL_FACE 30
#define GL_COLOR_WRITEMASK 38
#define GL_TEXTURE0 31
#define GL_TEXTURE_ENV 32
#define GL_TEXTURE_ENV_MODE 33
#define GL_PERSPECTIVE_CORRECTION_HINT 34
#define GL_NICEST 35
#define GL_FLAT 36
#define GL_BLEND 37
#define PV_IQM 5
#define ENTALPHA_DECODE(a) ((a) == 0 ? 1.0f : (float)((a) - 1) / 254)
#define texCoordsAttrIndex 4
#define pose1VertexAttrIndex 0
#define pose1NormalAttrIndex 1
#define pose2VertexAttrIndex 2
#define pose2NormalAttrIndex 3
#define vertColoursAttrIndex 5
enum { ALIAS_GLSL_BASIC, ALIAS_GLSL_SKELETAL };
typedef unsigned char byte;
typedef unsigned char GLboolean;
#define CHECK(cond, message) do { if (!(cond)) { puts("FAIL: " message); exit(1); } } while (0)
#define GL_VIEWMODEL_STENCIL_BIT()  (gl_stencilbits > 1 ? (1u << (gl_stencilbits - 1)) : 0u)
typedef int qboolean;
typedef unsigned int GLuint;
typedef int GLint;
typedef struct { int nextsurface, numtris, numindexes, eboofs, poseverttype, numbones, md5, id; } aliashdr_t;
/* A second surface sits far enough away for the real nextsurface walk. */
typedef struct { aliashdr_t header; char pad[256]; aliashdr_t second; const char *meshindexesvboptr; } qmodel_t;
typedef struct { qmodel_t *model; int alias_outline_order; unsigned char alpha; } entity_t;
typedef struct { int outlineWidthLoc, isOutlinePassLoc, shellModeLoc, useShellTexLoc,
                 useAlphaTestLoc, blendLoc, bonesLoc, md5NormalsLoc, program, maxbones; } aliasglsl_t;
typedef struct { float mat[12]; } bonepose_t;
typedef struct { int pose1, pose2; float blend, origin[3], angles[3]; bonepose_t *bonestate; } lerpdata_t;
typedef struct { int depthwrite, colorwrite, stencilwrite, depthfunc, stencilref, cull; } glstate_t;
static entity_t *currententity;
static entity_t *cl_visedicts[4];
static int cl_numvisedicts;
static int gl_stencilbits = 8, clears, saved, setups, phase_count;
static int phases[64], refs[64], surface_index, skip_ring, rs_aliaspolys, rs_aliaspasses;
static int clear_masks[16], clear_at[16], expect_mask = 1;
static struct { float value; } gl_laserpoint;
static int uniforms[8];
static float width;
/* colorwrite holds the RGBA write mask as bits 1/2/4/8. */
static glstate_t state = {1, 15, 127, 99, 77, 1}, stack[8];
static int ring_colormask = 15;
static int ambient_cull = 1;
static void glPushAttrib(int bits) { (void)bits; stack[saved++] = state; }
static void glPopAttrib(void) { state = stack[--saved]; }
static void glEnable(int cap) { if (cap == GL_CULL_FACE) state.cull = 1; }
static void glDisable(int cap) { if (cap == GL_CULL_FACE) state.cull = 0; }
static void glClearStencil(int value) { CHECK(value == 0, "wrong clear value"); }
static void glStencilMask(GLuint mask) { state.stencilwrite = (int)mask; }
static void glClear(int bits) { (void)bits; clear_at[clears] = phase_count; clear_masks[clears++] = state.stencilwrite; }
static void glDepthMask(int value) { state.depthwrite = value; }
static void glDepthFunc(int value) { state.depthfunc = value; }
static void glColorMask(int a, int b, int c, int d) {
    state.colorwrite = (a != 0) | (b != 0) << 1 | (c != 0) << 2 | (d != 0) << 3;
}
static void glStencilFunc(int func, int ref, int mask) {
    CHECK(func == GL_ALWAYS && mask == expect_mask, "wrong stencil function/mask"); state.stencilref = ref;
}
static void glStencilOp(int a, int b, int c) {
    CHECK(a == GL_KEEP && b == GL_KEEP && c == GL_REPLACE, "wrong stencil operations");
}
static void GL_Uniform1fFunc(int location, float value) { CHECK(location == 0, "unexpected float uniform"); width = value; }
static void GL_Uniform1iFunc(int location, int value) { uniforms[location] = value; }
static void *Mod_Extradata(qmodel_t *model) { return &model->header; }
'''
source += types
source = source.replace("RING_STENCIL", ring_stencil)
source += r'''
/* Record the actual production pass loop's GL draw calls and draw-time state. */
static void glDrawElements(int mode, int count, int type, const void *offset) {
    CHECK(mode == GL_TRIANGLES && count == 30 && type == GL_UNSIGNED_SHORT && offset == NULL,
          "geometry submission changed");
    CHECK(!state.depthwrite && state.depthfunc == GL_LEQUAL, "outline pass modifies depth");
    if (r_alias_outline_phase != ALIAS_OUTLINE_PHASE_RING) {
        CHECK(!state.colorwrite && state.stencilwrite == expect_mask, "mask/scrub modifies color or other stencil bits");
        CHECK(state.stencilref == (r_alias_outline_phase == ALIAS_OUTLINE_PHASE_MASK ? (int)r_alias_outline_ref : 0),
              "mask/scrub writes wrong stencil value");
        CHECK(width == 0 && uniforms[1] == 0 && uniforms[2] == 0 && uniforms[3] == 0,
              "mask/scrub inherited ring or shell shader state");
        CHECK(state.cull == ambient_cull, "mask/scrub must cull like the fill it masks");
    } else {
        CHECK(width == 4 && uniforms[1] == 1, "ring shader state lost");
        CHECK(state.colorwrite == ring_colormask, "ring ignores the caller's colour mask");
    }
    refs[phase_count] = (int)r_alias_outline_ref;
    phases[phase_count++] = r_alias_outline_phase * 10 + surface_index;
}
static void R_DrawAliasModelOutline(aliasglsl_t *glsl, aliashdr_t *hdr, lerpdata_t *lerp, entity_t *e) {
    (void)lerp; (void)e;
    (void)0;
    {
    RING_STENCIL
    (void)hdr;
    if (skip_ring) return; /* Pixel/bounds fade can reject the ring. */
    /* The production ring passes these to glStencilFunc(GL_NOTEQUAL, ...). */
    CHECK((int)outline_stencil_mask == expect_mask, "ring tests the wrong stencil bits");
    CHECK(outline_stencil_ref == (GLint)r_alias_outline_ref, "ring tests against a different value than its mask wrote");
    }
    /* Real ring setup: depth writes off, front faces culled. */
    state.depthwrite = 0; state.depthfunc = GL_LEQUAL; state.cull = 1;
    GL_Uniform1fFunc(glsl->outlineWidthLoc, 4);
    GL_Uniform1iFunc(glsl->isOutlinePassLoc, 1);
    glDrawElements(GL_TRIANGLES, hdr->numindexes, GL_UNSIGNED_SHORT, NULL);
    state.depthwrite = 1; state.stencilwrite = 255; state.cull = 0; /* Real ring cleanup. */
}
static void draw_outline_passes(aliasglsl_t *glsl, aliashdr_t *paliashdr, lerpdata_t lerpdata, entity_t *e) {
'''
source += passes + "\n}\n"
source = source.replace("RING_STENCIL", ring_stencil)
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
source += function("static void R_ClearAliasOutlineStencil(GLuint mask)")
source += function("static int R_CompareDeferredAliasOutlines(")
source += function("void R_BeginDeferredAliasOutlines(void)")
source += r'''
/* Lean replay: replace only pose/transform math; execute its real GL orchestration. */
static int lean_setups, program_binds, current_program, attrib_mask, matrix_depth;
static int r_drawflat_cheatsafe, r_lightmap_cheatsafe;
static float entalpha;
static aliasglsl_t r_alias_glsl[2];
static void R_SetupAliasFrame(aliashdr_t *hdr, entity_t *e, lerpdata_t *l) {
    (void)hdr; (void)e; memset(l, 0, sizeof(*l)); lean_setups++;
}
static void R_SetupEntityTransform(entity_t *e, lerpdata_t *l) { (void)e; (void)l; }
static int R_CullModelForEntityTransform(entity_t *e, float *o, float *a) { (void)e; (void)o; (void)a; return 0; }
static void glGetBooleanv(int pname, GLboolean *v) {
    CHECK(pname == GL_COLOR_WRITEMASK, "unexpected boolean query");
    for (int i = 0; i < 4; i++) v[i] = (GLboolean)((state.colorwrite >> i) & 1);
}
static GLboolean glIsEnabled(int cap) { CHECK(cap == GL_CULL_FACE, "unexpected query"); return (GLboolean)state.cull; }
static void GL_UseProgramFunc(int program) { current_program = program; if (program) program_binds++; }
static void GL_EnableVertexAttribArrayFunc(int i) { attrib_mask |= 1 << i; }
static void GL_DisableVertexAttribArrayFunc(int i) { attrib_mask &= ~(1 << i); }
static void GL_Uniform4fvFunc(int location, int count, const float *v) { (void)location; (void)count; (void)v; }
static void GL_SelectTexture(int unit) { (void)unit; }
static void glPushMatrix(void) { matrix_depth++; }
static void glPopMatrix(void) { CHECK(--matrix_depth >= 0, "matrix stack underflow"); }
static void R_RotateForEntity(float *o, float *a, entity_t *e) { (void)o; (void)a; (void)e; }
static void R_ApplyAliasModelScale(const qmodel_t *m, const aliashdr_t *h, float s) { (void)m; (void)h; (void)s; }
static void GL_BindAliasPose(aliashdr_t *hdr, const lerpdata_t *l) {
    (void)l; CHECK(attrib_mask == 0x1F, "pose bound without its attribute arrays"); surface_index = hdr->id;
}
static void glTexEnvf(int a, int b, int c) { (void)a; (void)b; (void)c; }
static void glHint(int a, int b) { (void)a; (void)b; }
static void glShadeModel(int a) { (void)a; }
static void glColor3f(float r, float g, float b) { (void)r; (void)g; (void)b; }
'''
source += lean + "\n"
source += function("void R_DrawDeferredAliasOutlines(void)")
source += r'''
static void reset(void) { setups = phase_count = clears = lean_setups = program_binds = 0; }
static void make_model(qmodel_t *m, int surfaces) {
    memset(m, 0, sizeof(*m));
    m->header.numtris = m->second.numtris = 10;
    m->header.numindexes = m->second.numindexes = 30;
    m->second.id = 1;
    if (surfaces > 1)
        m->header.nextsurface = (int)((char *)&m->second - (char *)&m->header);
}
static void check_restored(const glstate_t *original) {
    CHECK(!memcmp(&state, original, sizeof(state)), "pass loop leaks GL state");
    CHECK(!attrib_mask && !current_program && !matrix_depth,
          "lean replay leaks attribute arrays, program or matrix");
    CHECK(!r_num_deferred_alias_outlines && !r_alias_outline_collecting &&
          r_alias_outline_phase == ALIAS_OUTLINE_PHASE_NORMAL && !saved &&
          r_alias_outline_ref == 1 && r_alias_outline_refmask == 1 && !r_alias_outline_unique,
          "outline replay did not restore state");
}
int main(void) {
    static qmodel_t single, multi;
    entity_t a = {&single}, b = {&multi};
    glstate_t original = state;
    int i;
    make_model(&single, 1);
    make_model(&multi, 2);
    for (i = 0; i < 2; i++) {
        aliasglsl_t g = {0, 1, 2, 3, 4, -1, -1, -1, 7, 0};
        r_alias_glsl[i] = g;
    }
    cl_visedicts[0] = &a; cl_visedicts[1] = &b; cl_visedicts[2] = &a;
    cl_numvisedicts = 3; r_alias_outline_order_changed = 1;
    R_BeginDeferredAliasOutlines();
    CHECK(a.alias_outline_order == 0 && b.alias_outline_order == 1 &&
          !r_alias_outline_order_changed, "collection must reset order and stamp first visible occurrence");

    /* ---- legacy replay: too few stencil bits for per-model values ---- */
    gl_stencilbits = 1; expect_mask = 1;
    reset();
    r_deferred_alias_outlines[0] = &a;
    r_num_deferred_alias_outlines = 1;
    R_DrawDeferredAliasOutlines();
    CHECK(setups == 1, "single-surface outline repeats full model setup");
    CHECK(phase_count == 3 && phases[0] == 10 && phases[1] == 20 && phases[2] == 30,
          "single-surface stencil/ring/scrub order changed");
    CHECK(rs_aliaspolys == 30 && rs_aliaspasses == 30, "single-surface triangle counters changed");
    CHECK(clears == 1 && clear_masks[0] == 1, "legacy replay must only clear bit 0x01");
    check_restored(&original);
    reset();
    r_deferred_alias_outlines[0] = &b;
    r_num_deferred_alias_outlines = 1;
    R_DrawDeferredAliasOutlines();
    CHECK(setups == 3 && phase_count == 6, "multi-surface outline lost whole-model passes");
    CHECK(phases[0] == 10 && phases[1] == 11 && phases[2] == 20 && phases[3] == 21 &&
          phases[4] == 30 && phases[5] == 31, "multi-surface stencil barriers changed");
    check_restored(&original);
    reset();
    R_DrawDeferredAliasOutlines();
    CHECK(clears == 1 && clear_masks[0] == 1 && setups == 0, "empty queue must clear stale stencil without model work");
    reset();
    r_deferred_alias_outlines[0] = &a;
    r_deferred_alias_outlines[1] = &b;
    r_deferred_alias_outlines[2] = &a;
    r_num_deferred_alias_outlines = 3;
    R_DrawDeferredAliasOutlines();
    {
        const int expected[] = {10,20,30, 10,11,20,21,30,31, 10,20,30};
        CHECK(setups == 5 && phase_count == 12 && !memcmp(phases, expected, sizeof(expected)),
              "mixed queue lost per-entity/per-surface stencil ordering");
        for (i = 0; i < phase_count; i++)
            CHECK(refs[i] == 1, "legacy replay must always mask with 1");
    }
    reset();
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
    reset(); skip_ring = 1;
    r_deferred_alias_outlines[0] = &a;
    r_num_deferred_alias_outlines = 1;
    R_DrawDeferredAliasOutlines();
    CHECK(setups == 1 && phase_count == 2 && phases[0] == 10 && phases[1] == 30,
          "faded ring must still scrub its mask");
    check_restored(&original);
    skip_ring = 0;

    /* ---- per-model values: 8 stencil bits, viewmodel bit 0x80 excluded ---- */
    gl_stencilbits = 8; expect_mask = 0x7F;
    r_alias_outline_order_changed = 0;
    reset();
    r_deferred_alias_outlines[0] = &a;
    r_deferred_alias_outlines[1] = &b;
    r_deferred_alias_outlines[2] = &a;
    r_num_deferred_alias_outlines = 3;
    R_DrawDeferredAliasOutlines();
    {
        const int expected[] = {10,20, 10,11,20,21, 10,20};
        const int expected_refs[] = {1,1, 2,2,2,2, 3,3};
        CHECK(setups == 0 && lean_setups == 3 && program_binds == 1,
              "per-model replay must take the lean path: one pose setup per model, one program bind");
        CHECK(phase_count == 8 && !memcmp(phases, expected, sizeof(expected)),
              "per-model replay must drop scrub but keep mask/ring order and multi-surface barriers");
        CHECK(!memcmp(refs, expected_refs, sizeof(expected_refs)),
              "each model must mask and ring with its own stencil value");
        CHECK(clears == 2 && clear_masks[0] == 0x7F && clear_masks[1] == 0x7F,
              "per-model replay must clear its bits before and after, never the viewmodel bit");
    }
    check_restored(&original);
    reset(); skip_ring = 1;
    r_deferred_alias_outlines[0] = &a;
    r_num_deferred_alias_outlines = 1;
    R_DrawDeferredAliasOutlines();
    CHECK(phase_count == 1 && phases[0] == 10 && clears == 2, "faded ring still masks; the final clear removes it");
    check_restored(&original);
    skip_ring = 0;
    reset();
    R_DrawDeferredAliasOutlines();
    CHECK(clears == 1 && clear_masks[0] == 1, "empty queue keeps the narrow 0x01 clear");

    /* An MF_HOLEY skin needs its texture in the mask: the whole queue keeps the full path. */
    reset();
    r_alias_outline_holey = 1;
    r_deferred_alias_outlines[0] = &a;
    r_deferred_alias_outlines[1] = &b;
    r_num_deferred_alias_outlines = 2;
    R_DrawDeferredAliasOutlines();
    {
        const int expected[] = {10,20, 10,11,20,21};
        CHECK(lean_setups == 0 && setups == 3 && phase_count == 6 && !memcmp(phases, expected, sizeof(expected)),
              "a holey queue must replay through R_DrawAliasModel");
    }
    check_restored(&original);
    r_alias_outline_holey = 0;

    /* The mask culls like the fill even when culling is off and every ring turned it on. */
    {
        glstate_t uncull;
        state.cull = 0; ambient_cull = 0; uncull = state;
        reset();
        r_deferred_alias_outlines[0] = &b;
        r_deferred_alias_outlines[1] = &a;
        r_num_deferred_alias_outlines = 2;
        R_DrawDeferredAliasOutlines();
        CHECK(lean_setups == 2 && phase_count == 6, "unculled lean replay lost passes");
        check_restored(&uncull);
        state.cull = 1; ambient_cull = 1;
    }

    /* A stereo eye's colour mask survives the ring: the red eye stays red. */
    {
        glstate_t eye;
        state.colorwrite = ring_colormask = 1 | 8; eye = state;
        reset();
        r_deferred_alias_outlines[0] = &a;
        r_deferred_alias_outlines[1] = &b;
        r_num_deferred_alias_outlines = 2;
        R_DrawDeferredAliasOutlines();
        CHECK(lean_setups == 2 && phase_count == 6, "anaglyph lean replay lost passes");
        check_restored(&eye);
        state.colorwrite = ring_colormask = 15;
    }

    /* ---- running out of values: 5 bits -> 0x0F usable, 15 values ---- */
    gl_stencilbits = 5; expect_mask = 0x0F;
    reset();
    for (i = 0; i < 16; i++)
        r_deferred_alias_outlines[i] = &a;
    r_num_deferred_alias_outlines = 16;
    R_DrawDeferredAliasOutlines();
    CHECK(phase_count == 32 && refs[0] == 1 && refs[28] == 15 && refs[30] == 1,
          "values must count up and restart after a wrap");
    CHECK(clears == 3 && clear_masks[0] == 0x0F && clear_masks[1] == 0x0F && clear_masks[2] == 0x0F,
          "a wrap must clear the value bits before reusing a value");
    CHECK(clear_at[0] == 0 && clear_at[1] == 30 && clear_at[2] == 32,
          "wrap clear must come after the last use of value 15 and before value 1 is reused");
    check_restored(&original);

    puts("PASS: setup reuse, real GL pass order/state/uniforms/counters, legacy and per-model stencil values, lean replay state/culling/colour mask/holey fallback, wraps, multi-surface and mixed queues, faded rings");
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
