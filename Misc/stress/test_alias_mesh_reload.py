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
#define malloc tracked_malloc
#define free tracked_free
'''
source += function('static size_t GLMesh_CheckedSize (')
source += function('static void GLMesh_AddCheckedSize (')
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
    trivertx_t vertices[3];
    unsigned short indices[3];
};
int main(void) {
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
    gl_vbo_able = true;
    GLMesh_LoadVertexBuffer(&model, &f.hdr);
    assert(live == 0 && !model.meshvboptr && !model.meshindexesvboptr);
    assert(model.meshvbo && model.meshindexesvbo && uploads == 2 && deletes == 2);
    gl_vbo_able = false;
    GLMesh_LoadVertexBuffer(&model, &f.hdr);
    assert(live == 2 && model.meshvboptr && model.meshindexesvboptr);
    tracked_free(model.meshvboptr); tracked_free(model.meshindexesvboptr);
    assert(live == 0);
    puts("PASS: 1000 real mesh rebuilds preserve bytes, keep two CPU buffers, and free them on VBO transitions");
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
