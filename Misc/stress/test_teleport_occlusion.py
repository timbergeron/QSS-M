"""Exercise production teleporter-view culling: screen bounds, queries, skipping.

r_telestyle 2/3 render a subview per teleporter plane. This compiles the real
R_TeleportScreenBounds, slot allocation, query read-back, skip decision and
per-face query allocation from Quake/gl_warp.c with stub GL functions, and
checks that:

* faces entirely behind the eye or projected off screen are dropped, faces
  crossing the eye plane keep the whole target, visible faces keep their rect;
* a plane is only skipped when its newest finished query saw no pixels, it has
  recent images for the current style, and its periodic refresh is not due;
  unknown, in-flight, overflowing, stale or disabled cases always render;
* per-face queries reset per view, overflow conservatively, and slots are
  reused by plane with least-recently-seen eviction.

Compile with CC, or run from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
warp = (ROOT / "Quake/gl_warp.c").read_text()


def function(declaration):
    start = warp.index(declaration)
    return warp[start:warp.index("\n}", start) + 2]


defines = "\n".join(re.findall(r"^#define (?:MAX_TELEPORT_PLANES|TELEPORT_\w+)\b.*$", warp, re.M))
plane_type = warp[warp.index("typedef struct\n{\n\tvec3_t normal, center;"):warp.index("} teleport_plane_t;") + len("} teleport_plane_t;")]

source = r'''
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define false 0
#define true 1
#define GL_SAMPLES_PASSED 0x8914
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#define CLAMP(lo, x, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define q_min(a,b) ((a) < (b) ? (a) : (b))
#define q_max(a,b) ((a) > (b) ? (a) : (b))
#define CHECK(cond, message) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", message, __LINE__); exit(1); } } while (0)
typedef int qboolean;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef float vec3_t[3];
typedef float vec4_t[4];
typedef float mat4_t[16];
typedef struct { int contents; } mleaf_t;
typedef struct { int numverts; float verts[8][7]; } glpoly_t;
typedef struct { glpoly_t *polys; } msurface_t;
#define VectorCopy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
static void Matrix4_Transform4(const float *m, const float *v, float *out) {
    int i; for (i = 0; i < 4; ++i) out[i] = m[i]*v[0] + m[4+i]*v[1] + m[8+i]*v[2] + m[12+i]*v[3];
}
/* stub GL queries */
static int next_id = 1, avail_of[256], samples_of[256], gen_fail;
static void GL_GenQueriesFunc(GLsizei n, GLuint *ids) { int i; for (i = 0; i < n; ++i) ids[i] = gen_fail ? 0 : (GLuint)next_id++; }
static void GL_GetQueryObjectuivFunc(GLuint id, GLenum pname, GLuint *out) {
    *out = pname == GL_QUERY_RESULT_AVAILABLE ? (GLuint)avail_of[id] : (GLuint)samples_of[id];
}
static qboolean gl_occlusion_able = true;
static struct { float value; } r_teleocclude = {1};
'''
source += defines + "\n" + plane_type + r'''
static teleport_plane_t teleport_planes[MAX_TELEPORT_PLANES];
static int teleport_numplanes;
static unsigned int teleport_frame = 1;
static mat4_t teleport_screenmatrix;
'''
for decl in ("static qboolean R_TeleportScreenBounds (",
             "static void R_TeleportResetPlane (",
             "static int R_TeleportAllocPlane (",
             "static void R_TeleportResolveQueries (",
             "static qboolean R_TeleportPlaneHidden (",
             "static GLuint R_TeleportFaceQuery ("):
    source += function(decl) + "\n"
source += r'''
/* perspective looking down +x from the origin: clip = (y, z, -, x) */
static void setup_view(void) {
    memset(teleport_screenmatrix, 0, sizeof(teleport_screenmatrix));
    teleport_screenmatrix[4] = 1;   /* column-major: clip x = world y */
    teleport_screenmatrix[9] = 1;   /* clip y = world z */
    teleport_screenmatrix[3] = 1;   /* clip w = world x */
}
static void quad(msurface_t *s, glpoly_t *p, float x0, float x1, float y0, float y1, float z0, float z1) {
    float v[4][3] = {{x0,y0,z0},{x1,y1,z0},{x1,y1,z1},{x0,y0,z1}}; int i;
    memset(p, 0, sizeof(*p)); p->numverts = 4;
    for (i = 0; i < 4; ++i) VectorCopy(v[i], p->verts[i]);
    s->polys = p;
}
static void bounds(void) {
    msurface_t s; glpoly_t p; float mn[2], mx[2];
    setup_view();
    quad(&s, &p, 100, 100, -10, 10, -10, 10);
    CHECK(R_TeleportScreenBounds(&s, NULL, mn, mx), "centred face must be kept");
    CHECK(fabsf(mn[0] - 0.45f) < 1e-4 && fabsf(mx[0] - 0.55f) < 1e-4, "screen rect of a visible face");
    quad(&s, &p, -100, -100, -10, 10, -10, 10);
    CHECK(!R_TeleportScreenBounds(&s, NULL, mn, mx), "face entirely behind the eye must be dropped");
    quad(&s, &p, 100, 100, 300, 400, -10, 10);
    CHECK(!R_TeleportScreenBounds(&s, NULL, mn, mx), "face projected right of the screen must be dropped");
    quad(&s, &p, 100, 100, -10, 10, -400, -300);
    CHECK(!R_TeleportScreenBounds(&s, NULL, mn, mx), "face projected below the screen must be dropped");
    quad(&s, &p, 100, 100, 50, 300, -10, 10);
    CHECK(R_TeleportScreenBounds(&s, NULL, mn, mx) && mx[0] == 1.0f, "face straddling the edge is kept and clamped");
    quad(&s, &p, -50, 50, -10, 10, -10, 10);
    CHECK(R_TeleportScreenBounds(&s, NULL, mn, mx) && mn[0] == 0 && mx[0] == 1 && mn[1] == 0 && mx[1] == 1,
          "face crossing the eye plane must keep the whole target");
}
static teleport_plane_t *fresh_plane(void) {
    teleport_plane_t *p = &teleport_planes[0];
    memset(teleport_planes, 0, sizeof(teleport_planes));
    teleport_numplanes = 1;
    return p;
}
static void issue(teleport_plane_t *p, int faces, int samples, int available) {
    int i;
    for (i = 0; i < faces; ++i) {
        GLuint q = R_TeleportFaceQuery(p);
        if (q) { samples_of[q] = samples; avail_of[q] = available; }
    }
}
static void hidden_decisions(void) {
    teleport_plane_t *p;
    int style = 2;
    /* never rendered: must render */
    p = fresh_plane(); teleport_frame = 10;
    CHECK(!R_TeleportPlaneHidden(p, style), "a plane without images must render");
    /* rendered at 10, drawn at 10 with zero pixels, resolved at 11 -> hidden */
    p->renderframe = 10; p->renderstyle = style;
    issue(p, 2, 0, 1);
    teleport_frame = 11; R_TeleportResolveQueries();
    CHECK(p->resolvedframe == 10 && p->visibleframe < 10, "zero-pixel query must resolve as hidden");
    CHECK(R_TeleportPlaneHidden(p, style), "hidden plane with fresh images must skip");
    CHECK(!R_TeleportPlaneHidden(p, 3), "images for another style must not be reused");
    r_teleocclude.value = 0;
    CHECK(!R_TeleportPlaneHidden(p, style), "r_teleocclude 0 must always render"); r_teleocclude.value = 1;
    gl_occlusion_able = false;
    CHECK(!R_TeleportPlaneHidden(p, style), "no query support must always render"); gl_occlusion_able = true;
    teleport_frame = 10 + TELEPORT_HIDDEN_REFRESH;
    CHECK(!R_TeleportPlaneHidden(p, style), "periodic refresh must re-render hidden planes");
    /* a resolved query that saw pixels makes it visible again */
    teleport_frame = 12; issue(p, 1, 5, 1);
    teleport_frame = 13; R_TeleportResolveQueries();
    CHECK(p->visibleframe == 12 && !R_TeleportPlaneHidden(p, style), "visible query must render");
    /* in-flight results are not trusted */
    p = fresh_plane(); teleport_frame = 20; p->renderframe = 20; p->renderstyle = style;
    issue(p, 1, 0, 0);
    teleport_frame = 21; R_TeleportResolveQueries();
    CHECK(!p->resolvedframe && !R_TeleportPlaneHidden(p, style), "unavailable query must not skip");
    /* one of several faces unavailable: wait for all */
    p = fresh_plane(); teleport_frame = 30; p->renderframe = 30; p->renderstyle = style;
    issue(p, 1, 0, 1); issue(p, 1, 0, 0);
    teleport_frame = 31; R_TeleportResolveQueries();
    CHECK(!p->resolvedframe, "partially available frame must stay unresolved");
    /* any face with pixels: visible */
    p = fresh_plane(); teleport_frame = 40; p->renderframe = 40; p->renderstyle = style;
    issue(p, 1, 0, 1); issue(p, 1, 7, 1);
    teleport_frame = 41; R_TeleportResolveQueries();
    CHECK(p->visibleframe == 40 && !R_TeleportPlaneHidden(p, style), "a visible face makes the plane visible");
    /* too many faces: the unqueried ones count as visible */
    p = fresh_plane(); teleport_frame = 50; p->renderframe = 50; p->renderstyle = style;
    issue(p, TELEPORT_FACE_QUERIES + 1, 0, 1);
    CHECK(p->queryoverflow[50 % TELEPORT_QUERY_RING], "extra faces must mark overflow");
    teleport_frame = 51; R_TeleportResolveQueries();
    CHECK(p->visibleframe == 50 && !R_TeleportPlaneHidden(p, style), "overflow must count as visible");
    /* query objects that cannot be created count as visible */
    p = fresh_plane(); teleport_frame = 60; p->renderframe = 60; p->renderstyle = style;
    gen_fail = 1; issue(p, 1, 0, 1); gen_fail = 0;
    teleport_frame = 61; R_TeleportResolveQueries();
    CHECK(!R_TeleportPlaneHidden(p, style), "failed query creation must not skip");
    /* old zero result is not trusted */
    p = fresh_plane(); teleport_frame = 70; p->renderframe = 70; p->renderstyle = style;
    issue(p, 1, 0, 1); teleport_frame = 71; R_TeleportResolveQueries();
    p->renderframe = 70 + TELEPORT_QUERY_RING + 1;
    teleport_frame = 70 + TELEPORT_QUERY_RING + 2;
    CHECK(!R_TeleportPlaneHidden(p, style), "a stale zero result must not skip");
    /* current-view queries are never read back early */
    p = fresh_plane(); teleport_frame = 80; issue(p, 1, 0, 1); R_TeleportResolveQueries();
    CHECK(!p->resolvedframe, "the current view's queries must not be read");
}
static void slots(void) {
    int i, idx;
    memset(teleport_planes, 0, sizeof(teleport_planes)); teleport_numplanes = 0; teleport_frame = 100;
    for (i = 0; i < MAX_TELEPORT_PLANES; ++i) {
        idx = R_TeleportAllocPlane(); CHECK(idx == i, "free slots allocate in order");
        teleport_planes[idx].seenframe = 90 + i; teleport_planes[idx].image[0] = 1000 + i;
    }
    teleport_planes[5].seenframe = 50;
    idx = R_TeleportAllocPlane();
    CHECK(idx == 5, "full table must evict the least recently seen plane");
    for (i = 0; i < MAX_TELEPORT_PLANES; ++i) teleport_planes[i].seenframe = teleport_frame;
    CHECK(R_TeleportAllocPlane() < 0, "planes collected this view must never be evicted");
    teleport_planes[3].renderframe = 7; teleport_planes[3].query[1][2] = 55;
    R_TeleportResetPlane(&teleport_planes[3]);
    CHECK(teleport_planes[3].image[0] == 1003 && teleport_planes[3].query[1][2] == 55 &&
          !teleport_planes[3].renderframe && !teleport_planes[3].seenframe,
          "reset must forget the plane but keep its GL objects");
}
int main(void) {
    bounds();
    hidden_decisions();
    slots();
    puts("PASS: teleporter screen bounds, query read-back, skip decisions, face queries and slot reuse");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-teleocc-") as tmp:
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
