"""Exercise actual shader teardown with forced OpenGL program-ID reuse.

No display or driver is needed. Compile production ownership helpers and teardown
with a fake GL delete boundary, then recreate lazy menu programs before gameplay.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
misc = (ROOT / 'Quake/gl_rmisc.c').read_text()
alias = (ROOT / 'Quake/r_alias.c').read_text()
world = (ROOT / 'Quake/r_world.c').read_text()

def function(text, declaration):
    start = text.index(declaration + '\n{')
    return text[start:text.index('\n}', start) + 2] + '\n'

if 'void GLAlias_DeleteShaders (void)' in alias:
    alias_delete = function(alias, 'void GLAlias_DeleteShaders (void)')
else:
    start = alias.index('\tfor (i = 0; i < ALIAS_GLSL_MODES; i++)')
    end = alias.index('\n\tif (!gl_glsl_alias_able)', start)
    alias_delete = 'void GLAlias_DeleteShaders (void)\n{\nint i;\n' + alias[start:end] + '\n}\n'
world_delete = function(world, 'static void GLWorld_DeleteShaderPrograms (void)')
uniforms = sorted(set(re.findall(r'^\t(\w+) =', world_delete, re.M)) -
    {'r_world_program', 'r_world_instanced_program'})
water_fields = sorted(set(re.findall(r'r_water\[i\]\.(\w+)', world_delete)) - {'program'})

source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int GLuint;
#define countof(a) (sizeof(a)/sizeof((a)[0]))
#define ALIAS_GLSL_MODES 2
static int gl_glsl_able = 1, gl_num_programs, deleted;
static int gl_vbo_able = 1, gl_bmodel_vbo = 1, gl_glsl_water_able = 1;
static GLuint gl_programs[32];
static int alive[32];
static struct { GLuint program; int uniform; } r_alias_glsl[ALIAS_GLSL_MODES], r_alias_inst_glsl;
static GLuint r_world_program, r_world_instanced_program;
static void GL_DeleteProgramFunc(GLuint id) { assert(id < 32 && alive[id]); alive[id] = 0; deleted++; }
static void R_ItemTimersShutdownGL(void) {}
static void PolyBlend_DeleteVignetteTexture(void) {}
'''
source += '\n'.join('static int ' + name + ';' for name in uniforms)
source += '\nstatic struct { GLuint program; ' + ''.join('int ' + name + ';' for name in water_fields) + '} r_water[2];\n'
source += function(misc, 'void GL_DeleteProgramTracked (GLuint *program)')
source += alias_delete + world_delete
source += function(world, 'void GLWorld_DeleteShaders (void)') if 'void GLWorld_DeleteShaders (void)' in world else 'void GLWorld_DeleteShaders(void) { GLWorld_DeleteShaderPrograms(); }\n'
source += function(misc, 'void R_DeleteShaders (void)')
if 'static qboolean RSceneCache_CanDraw(void)' in world:
    source += '#define qboolean int\n' + function(world, 'static qboolean RSceneCache_CanDraw(void)')
else:
    queue = function(world, 'static qboolean RSceneCache_Queue(byte *vis)')
    admission = re.search(r'\tif \((.*?)\)\n\t\{', queue).group(1)
    source += 'static int RSceneCache_CanDraw(void) { return !(' + admission + '); }\n'
source += r'''
static void track(GLuint id) { assert(!alive[id]); alive[id] = 1; gl_programs[gl_num_programs++] = id; }
int main(void) {
    for (int cycle = 0; cycle < 8; cycle++) {
        deleted = 0;
        r_alias_glsl[0].program = 1; r_alias_glsl[1].program = 2; r_alias_inst_glsl.program = 3;
        r_world_program = 4; r_world_instanced_program = 5; r_grass_program = 6;
        r_water[0].program = 7; r_water[1].program = 8;
        for (GLuint id = 1; id <= 8; id++) track(id);
        R_DeleteShaders();
        assert(deleted == 8 && gl_num_programs == 0);
        assert(!r_alias_glsl[0].program && !r_alias_glsl[1].program && !r_alias_inst_glsl.program);
        assert(!r_world_program && !r_world_instanced_program && !r_grass_program);
        assert(!r_water[0].program && !r_water[1].program);
        /* Lazy gamma/menu shaders reuse former alias/world IDs before map loading. */
        track(1); track(4);
        GLAlias_DeleteShaders(); GLWorld_DeleteShaderPrograms();
        assert(alive[1] && alive[4] && gl_num_programs == 2 && deleted == 8);
        R_DeleteShaders();
        assert(deleted == 10 && gl_num_programs == 0);
    }
    gl_glsl_able = 0;
    r_alias_glsl[0].program = 1; r_world_program = 4;
    R_DeleteShaders();
    assert(!r_alias_glsl[0].program && !r_world_program);
    /* VBOs alone cannot admit the shader-only scene cache on fallback drivers. */
    r_world_program = 4;
    assert(!RSceneCache_CanDraw());
    gl_glsl_able = 1;
    assert(RSceneCache_CanDraw());
    gl_glsl_water_able = 0;
    assert(!RSceneCache_CanDraw());
    gl_glsl_water_able = 1; r_world_program = 0;
    assert(!RSceneCache_CanDraw());
    r_world_program = 4; gl_bmodel_vbo = 0;
    assert(!RSceneCache_CanDraw());
    gl_bmodel_vbo = 1; gl_vbo_able = 0;
    assert(!RSceneCache_CanDraw());
    puts("startup shaders: PASS (ownership, reused IDs, restart, unsupported GLSL, partial shader failure)");
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-startup-shaders-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(source)
    binary = path / 'test'
    compiler = os.environ.get('CC', 'cc')
    if Path(compiler).name.lower() in ('cl', 'cl.exe'):
        binary = binary.with_suffix('.exe')
        command = [compiler, '/nologo', '/std:c11', '/W3', '/fsanitize=address', '/Zi',
            '/Fe:' + str(binary), '/Fo:' + str(path / 'test.obj'), '/Fd:' + str(path / 'test.pdb'),
            str(path / 'test.c')]
    else:
        command = [compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-function', '-fsanitize=address,undefined', '-g',
            str(path / 'test.c'), '-o', str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)
