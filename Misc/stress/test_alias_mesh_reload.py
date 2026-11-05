"""Check real alias mesh rebuild ownership, bytes, and VBO/fallback transitions."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
mesh = (ROOT / 'Quake/gl_mesh.c').read_text()


def function(declaration):
    start = mesh.index(declaration)
    return mesh[start:mesh.index('\n}', start) + 2] + '\n'


source = r'''
#include "quakedef.h"
#include <assert.h>
#undef main
qboolean isDedicated, gl_vbo_able;
const float r_avertexnormals[162][3] = {{0, 0, 1}};
static void *allocations[8];
static int live, deletes, uploads;
static void *tracked_malloc(size_t n) {
    void *p = malloc(n); assert(p);
    for (int i = 0; i < 8; i++) if (!allocations[i]) {
        allocations[i] = p; live++; return p;
    }
    abort();
}
static void tracked_free(void *p) {
    if (!p) return;
    for (int i = 0; i < 8; i++) if (allocations[i] == p) {
        allocations[i] = NULL; live--; free(p); return;
    }
    abort();
}
static void mock_delete(GLsizei n, const GLuint *ids) { (void)ids; deletes += n; }
static void mock_bind(GLenum target, GLuint id) { (void)target; (void)id; }
static void mock_upload(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    (void)target; (void)usage; assert(size > 0 && data); uploads++;
}
GLuint GL_GenBufferName(void) { static GLuint name; return ++name; }
void GL_ClearBufferBindings(void) {}
int TexMgr_PadConditional(int n) { return n; }
void Sys_Error(const char *fmt, ...) { (void)fmt; abort(); }
stvert_t stverts[MAXALIASVERTS];
mtriangle_t *triangles;
trivertx_t *poseverts_mdl[MAXALIASFRAMES];
static union { max_align_t align; byte bytes[8192]; } hunk;
static int hunk_used;
int Hunk_LowMark(void) { return hunk_used; }
void Hunk_FreeToLowMark(int mark) { hunk_used = mark; }
void *Hunk_Alloc(int bytes) {
    void *p = hunk.bytes + hunk_used;
    hunk_used += (bytes + 15) & ~15;
    assert((size_t)hunk_used <= sizeof(hunk.bytes)); memset(p, 0, bytes); return p;
}
void *Hunk_AllocNoFill(int bytes) { return Hunk_Alloc(bytes); }
#define malloc tracked_malloc
#define free tracked_free
'''
source += function('static size_t GLMesh_CheckedSize (')
source += function('static void GLMesh_AddCheckedSize (')
source += function('static int GLMesh_CheckedHunkSize (')
source += function('void GL_MakeAliasModelDisplayLists (')
source += (function('void GLMesh_LoadVertexBuffer (')
           .replace('GL_DeleteBuffersFunc', 'mock_delete')
           .replace('GL_BindBufferFunc', 'mock_bind')
           .replace('GL_BufferDataFunc', 'mock_upload'))
source += r'''
#undef malloc
#undef free
struct fixture {
    aliashdr_t hdr;
    aliasmesh_t desc[3];
    trivertx_t vertices[6];
    unsigned short indices[3];
};
static void check_seam_mesh(void) {
    aliashdr_t *h = Hunk_Alloc(sizeof(*h));
    h->poseverttype = PV_QUAKEFORGE;
    h->numverts = 3; h->numtris = 2; h->nummorphposes = 1;
    h->skinwidth = 4; h->skinheight = 1;
    mtriangle_t faces[2] = {0}; triangles = faces;
    faces[0].facesfront = 1;
    for (int f=0; f<2; f++) for (int v=0; v<3; v++) faces[f].vertindex[v] = v;
    stverts[0].onseam = 1; stverts[0].s = INT_MAX;
    trivertx_t *pose = calloc(6, sizeof(*pose)); poseverts_mdl[0] = pose;
    for (int v=0; v<6; v++) for (int axis=0; axis<3; axis++) pose[v].v[axis] = 10*v + axis;
    GL_MakeAliasModelDisplayLists(NULL, h);
    assert(h->numverts_vbo == 4);
    aliasmesh_t *desc = (aliasmesh_t *)((byte *)h + h->meshdesc);
    trivertx_t *out = (trivertx_t *)((byte *)h + h->vertexes);
    assert(desc[3].st[0] >= (double)INT_MAX);
    for (int v=0; v<4; v++) {
        assert(!memcmp(&out[v], &pose[desc[v].vertindex], sizeof(*pose)));
        assert(!memcmp(&out[v+4], &pose[desc[v].vertindex+3], sizeof(*pose)));
    }
    qmodel_t model = {0};
    GLMesh_LoadVertexBuffer(&model, h);
    meshxyz_mdl16_t *xyz = (meshxyz_mdl16_t *)model.meshvboptr;
    for (int v=0; v<4; v++) for (int a=0; a<3; a++)
        assert(xyz[v].xyz[a] == ((pose[desc[v].vertindex].v[a]<<8) | pose[desc[v].vertindex+3].v[a]));
    tracked_free(model.meshvboptr); tracked_free(model.meshindexesvboptr); free(pose);
}
int main(void) {
    check_seam_mesh();
    struct fixture f = {0};
    qmodel_t model = {0};
    f.hdr.poseverttype = PV_QUAKE1;
    f.hdr.nummorphposes = 1; f.hdr.numverts_vbo = 3; f.hdr.numindexes = 3;
    f.hdr.skinwidth = f.hdr.skinheight = 16;
    f.hdr.meshdesc = (byte *)f.desc - (byte *)&f.hdr;
    f.hdr.vertexes = (byte *)f.vertices - (byte *)&f.hdr;
    f.hdr.indexes = (byte *)f.indices - (byte *)&f.hdr;
    for (int i = 0; i < 3; i++) {
        f.vertices[i].v[0] = 10 + i;
        f.desc[i].st[0] = i; f.desc[i].st[1] = i + 2;
        f.indices[i] = i;
    }
    for (int i = 0; i < 1000; i++) {
        GLMesh_LoadVertexBuffer(&model, &f.hdr);
        assert(live == 2 && model.meshvboptr && model.meshindexesvboptr);
        assert(model.meshvboptr[0] == 10);
        assert(!memcmp(model.meshindexesvboptr, f.indices, sizeof(f.indices)));
    }
    // Both MDL formats must map invalid normal bytes to normal zero.
    f.vertices[1].lightnormalindex = 162;
    f.vertices[2].lightnormalindex = 255;
    for (int format = 0; format < 2; format++) {
        f.hdr.poseverttype = format ? PV_QUAKEFORGE : PV_QUAKE1;
        GLMesh_LoadVertexBuffer(&model, &f.hdr);
        for (int v = 0; v < 3; v++) {
            if (format)
                assert(((meshxyz_mdl16_t *)model.meshvboptr)[v].normal[2] == 127);
            else
                assert(((meshxyz_mdl_t *)model.meshvboptr)[v].normal[2] == 127);
        }
    }
    f.hdr.poseverttype = PV_QUAKE1;
    gl_vbo_able = true;
    GLMesh_LoadVertexBuffer(&model, &f.hdr);
    assert(live == 0 && !model.meshvboptr && !model.meshindexesvboptr);
    assert(model.meshvbo && model.meshindexesvbo && uploads == 2 && deletes == 2);
    gl_vbo_able = false;
    GLMesh_LoadVertexBuffer(&model, &f.hdr);
    assert(live == 2 && model.meshvboptr && model.meshindexesvboptr);
    tracked_free(model.meshvboptr); tracked_free(model.meshindexesvboptr);
    assert(live == 0);
    puts("PASS: 1000 real mesh rebuilds preserve bytes, keep two CPU buffers, and free them on VBO transitions; QuakeForge seam duplication, XYZ precision, extreme UVs and invalid normals");
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-mesh-reload-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-Wall', '-Wextra', '-I', str(ROOT / 'Quake'), str(work / 'test.c'),
                    '-o', str(work / 'test'), *flags, '-lm'], check=True)
    subprocess.run([str(work / 'test')], check=True)
