#!/usr/bin/env python3
"""Check teleporter shader failure handling and CPU brush transforms.

Uses production C functions. Requires SDL2/OpenGL and an X11 display:
    xvfb-run -a python3 Misc/stress/test_teleport_gl.py
"""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
warp = (ROOT / "Quake/gl_warp.c").read_text()
math = (ROOT / "Quake/mathlib.c").read_text()
main = (ROOT / "Quake/gl_rmain.c").read_text()
misc = (ROOT / "Quake/gl_rmisc.c").read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


source = r'''
#include "quakedef.h"
#include <assert.h>
static int fail_at, lookups, creates, uses, uniforms;
cvar_t gl_zfix, r_telestyle;
client_state_t cl;
client_static_t cls;
qboolean gl_glsl_water_able, gl_fbo_able, gl_texture_NPOT;
qboolean scr_disabled_for_loading;
void Con_Warning(const char *fmt, ...) {}
GLuint GL_CreateProgram(const GLchar *vs, const GLchar *fs, int n, const glsl_attrib_binding_t *bindings) {
    creates++;
    return fail_at == -2 ? 0 : 42;
}
static GLint GLAPIENTRY uniform_location(GLuint program, const GLchar *name) {
    assert(program == 42);
    return lookups++ == fail_at ? -1 : lookups;
}
static void GLAPIENTRY use_program(GLuint program) {
    assert(program == 42 || program == 0);
    uses++;
}
static void GLAPIENTRY uniform_i(GLint location, GLint value) {
    assert(location >= 0);
    uniforms++;
}
QS_PFNGLGETUNIFORMLOCATIONPROC GL_GetUniformLocationFunc = uniform_location;
QS_PFNGLUSEPROGRAMPROC GL_UseProgramFunc = use_program;
QS_PFNGLUNIFORM1IPROC GL_Uniform1iFunc = uniform_i;
refdef_t r_refdef;
vec3_t r_origin, vpn, vright, vup;
mleaf_t *r_viewleaf;
entity_t *currententity, **cl_visedicts;
int cl_numvisedicts;
qboolean r_teleport_view, r_teleport_reflection, skyroom_drawn;
byte *r_teleport_pvs;
float r_fovx, r_fovy;
static int aborted;
void R_SetFrustum(float x, float y) {}
void R_SetupGL(void) { glViewport(0,0,r_refdef.vrect.width,r_refdef.vrect.height); }
void RSceneCache_AbortTeleport(void) { aborted++; }
void GL_ClearBindings(void) {}
void GL_SelectTexture(GLenum unit) { assert(unit==GL_TEXTURE0); }
PFNGLBINDFRAMEBUFFERPROC GL_BindFramebufferFunc;
PFNGLBINDRENDERBUFFERPROC GL_BindRenderbufferFunc;
PFNGLDELETEFRAMEBUFFERSPROC GL_DeleteFramebuffersFunc;
PFNGLDELETERENDERBUFFERSPROC GL_DeleteRenderbuffersFunc;
PFNGLGENFRAMEBUFFERSPROC GL_GenFramebuffersFunc;
PFNGLGENRENDERBUFFERSPROC GL_GenRenderbuffersFunc;
PFNGLRENDERBUFFERSTORAGEPROC GL_RenderbufferStorageFunc;
PFNGLFRAMEBUFFERTEXTURE2DPROC GL_FramebufferTexture2DFunc;
PFNGLFRAMEBUFFERRENDERBUFFERPROC GL_FramebufferRenderbufferFunc;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC real_check_framebuffer;
static int framebuffer_checks;
static qboolean fail_framebuffer;
static GLenum GLAPIENTRY check_framebuffer(GLenum target) {
    framebuffer_checks++;
    return fail_framebuffer ? GL_FRAMEBUFFER_UNSUPPORTED : real_check_framebuffer(target);
}
PFNGLCHECKFRAMEBUFFERSTATUSPROC GL_CheckFramebufferStatusFunc=check_framebuffer;

'''
source += warp[warp.index("#define MAX_TELEPORT_PLANES"):warp.index("extern float r_fovx")]
source += function(misc, "GLint GL_GetUniformLocation (")
source += function(warp, "qboolean R_TeleportActive (")
source += function(warp, "static qboolean R_TeleportCreateShader (")
source += function(warp, "void R_TeleportCreateShaders (")
source += function(warp, "static void R_TeleportDeleteTargets (")
source += function(warp, "void R_TeleportShutdownGL (")
source += function(warp, "void R_TeleportStyleChanged (")
source += function(math, "void AngleVectors (")
source += function(main, "void R_RotateForEntity (")
source += function(warp, "static void R_TeleportEntityMatrix (")
source += function(warp, "static void R_TeleportScissorBounds (")
source += function(warp, "static void R_TeleportScissor (")
source += function(warp, "static qboolean R_TeleportTarget (")
source += warp[warp.index("static struct\n", warp.index("/* This snapshot")):warp.index("void R_TeleportPrepare (")]

source += r'''
int main(void) {
    // A missing uniform at any position must disable the path before binding
    // a program or sending sampler uniforms, with no retries on later frames.
    for (fail_at=-2; fail_at<11; fail_at++) {
        if (fail_at==-1) continue;
        teleport_program=0; teleport_failed=false;
        lookups=creates=uses=uniforms=0;
        for (int frame=0; frame<128; frame++) assert(!R_TeleportCreateShader());
        assert(creates==1 && !uses && !uniforms && !teleport_program && teleport_failed);
    }
    teleport_program=0; teleport_failed=false;
    lookups=creates=uses=uniforms=0; fail_at=-1;
    for (int frame=0; frame<128; frame++) assert(R_TeleportCreateShader());
    assert(creates==1 && uses==2 && uniforms==4 && teleport_program==42);

    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_Window *window=SDL_CreateWindow("teleporter matrix test",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext context=SDL_GL_CreateContext(window);
    assert(context);
    qmodel_t model={0}; entity_t ent={0}; ent.model=&model;
    model.mins[2]=-13; model.maxs[2]=71;
    // Warmup is independent of visibility. Disconnected restarts must wait
    // for map setup, even if cl.worldmodel still references the previous map.
    R_TeleportShutdownGL();
    creates=uses=uniforms=lookups=0; fail_at=-1;
    cl.worldmodel=&model; model.hasteletextures=true;
    r_telestyle.value=3;
    gl_glsl_water_able=gl_fbo_able=gl_texture_NPOT=true;
    cls.state=ca_disconnected;
    R_TeleportCreateShaders(); assert(!creates && !teleport_program);
    cls.state=ca_connected;
    r_telestyle.value=1; R_TeleportCreateShaders(); assert(!creates);
    r_telestyle.value=3; model.hasteletextures=false;
    R_TeleportCreateShaders(); assert(!creates);
    model.hasteletextures=true;
    R_TeleportCreateShaders(); // R_NewMap, before any plane is collected
    assert(creates==1 && teleport_program && !teleport_numplanes && !scr_disabled_for_loading);
    R_TeleportCreateShaders(); assert(creates==1); // another map reuses the program
    R_TeleportShutdownGL();
    scr_disabled_for_loading=true;
    R_TeleportCreateShaders(); // connected vid_restart, with screen updates blocked
    assert(creates==2 && teleport_program && scr_disabled_for_loading);
    scr_disabled_for_loading=false;
    R_TeleportShutdownGL(); cls.state=ca_disconnected;
    R_TeleportCreateShaders(); assert(creates==2 && !teleport_program);
    cls.state=ca_connected; R_TeleportCreateShaders(); assert(creates==3);
    R_TeleportShutdownGL();
    cls.signon=SIGNONS;
    R_TeleportStyleChanged(&r_telestyle); // enabling mid-game before the portal is visible
    assert(creates==4 && teleport_program && !teleport_numplanes);
    R_TeleportShutdownGL();
    fail_at=lookups; // next lookup fails; warmup must keep the existing failure latch
    R_TeleportCreateShaders(); assert(creates==5 && teleport_failed && !teleport_program);
    R_TeleportCreateShaders(); R_TeleportStyleChanged(&r_telestyle);
    assert(creates==5 && !scr_disabled_for_loading);
    R_TeleportShutdownGL(); fail_at=-1;
    R_TeleportCreateShaders(); assert(creates==6 && teleport_program && !teleport_failed);
    cls.signon=0;

    int tested=0;
    for (int nudge=0; nudge<2; nudge++) for (int isstatic=0; isstatic<2; isstatic++)
    for (int pivot=0; pivot<4; pivot++) for (int scale=8; scale<=48; scale+=8)
    for (int pitch=-80; pitch<=80; pitch+=40) for (int yaw=-150; yaw<=150; yaw+=60)
    for (int roll=-45; roll<=45; roll+=45) {
        mat4_t expected, actual; vec3_t angles, origin;
        ent.angles[0]=pitch; ent.angles[1]=yaw; ent.angles[2]=roll;
        ent.origin[0]=103; ent.origin[1]=-51; ent.origin[2]=91;
        gl_zfix.value=nudge; ent.is_static=isstatic;
        VectorCopy(ent.origin,origin);
        if (nudge && !isstatic) for(int i=0;i<3;i++) origin[i]-=DIST_EPSILON;
        ent.netstate.scale=scale; ent.netstate.drawflags=pivot<<5;
        VectorCopy(ent.angles,angles); angles[0]=-angles[0];
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        R_RotateForEntity(origin,angles,&ent);
        glGetFloatv(GL_MODELVIEW_MATRIX,expected);
        R_TeleportEntityMatrix(&ent,actual);
        for(int i=0;i<16;i++) assert(fabsf(expected[i]-actual[i])<0.0001f);
        tested++;
    }
    // Simulate an interrupted subview with altered GL state and entity list.
    GL_BindFramebufferFunc=SDL_GL_GetProcAddress("glBindFramebuffer");
    GL_BindRenderbufferFunc=SDL_GL_GetProcAddress("glBindRenderbuffer");
    assert(GL_BindFramebufferFunc && GL_BindRenderbufferFunc);
    GL_GenFramebuffersFunc=SDL_GL_GetProcAddress("glGenFramebuffers");
    GL_GenRenderbuffersFunc=SDL_GL_GetProcAddress("glGenRenderbuffers");
    GL_DeleteFramebuffersFunc=SDL_GL_GetProcAddress("glDeleteFramebuffers");
    GL_DeleteRenderbuffersFunc=SDL_GL_GetProcAddress("glDeleteRenderbuffers");
    GL_RenderbufferStorageFunc=SDL_GL_GetProcAddress("glRenderbufferStorage");
    GL_FramebufferTexture2DFunc=SDL_GL_GetProcAddress("glFramebufferTexture2D");
    GL_FramebufferRenderbufferFunc=SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
    real_check_framebuffer=SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    // Validate newly allocated attachments, reuse them without driver status
    // queries, and validate again after size/context teardown.
    for (int cycle=0;cycle<2;cycle++) {
        teleport_width=teleport_height=64+cycle*32;
        for (int frame=0;frame<8;frame++) for (int p=0;p<3;p++) for (int pass=0;pass<2;pass++) {
            teleport_planes[p].screenmaxs[0]=teleport_planes[p].screenmaxs[1]=1;
            assert(R_TeleportTarget(&teleport_planes[p],pass));
            assert(real_check_framebuffer(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        }
        assert(framebuffer_checks==(cycle+1)*6);
        R_TeleportDeleteTargets();
    }
    teleport_width=teleport_height=64; fail_framebuffer=true;
    assert(!R_TeleportTarget(teleport_planes,0) && teleport_failed);
    R_TeleportDeleteTargets(); fail_framebuffer=teleport_failed=false;
    assert(glGetError()==GL_NO_ERROR);
    entity_t *saved[]={&ent}, *visible[]={NULL};
    teleport_savedents=saved; cl_visedicts=visible;
    teleport_restore.savedcount=1; teleport_restore.savedentity=&ent;
    teleport_restore.savedfront=GL_CW;
    teleport_restore.savedscissor[0]=4; teleport_restore.savedscissor[1]=7;
    teleport_restore.savedscissor[2]=19; teleport_restore.savedscissor[3]=23;
    teleport_restore.scissor=GL_TRUE;
    teleport_restore.depthmask=GL_FALSE;
    teleport_restore.stencilmask=0x37;
    teleport_restore.stenciltest=GL_FALSE;
    teleport_restore.colormask[0]=GL_TRUE;
    teleport_restore.colormask[2]=GL_TRUE;
    teleport_restore.clearcolor[1]=.5f;
    teleport_restore.skyroom=true;
    teleport_restore.savedref.vrect.width=64; teleport_restore.savedref.vrect.height=64;
    teleport_restore.active=true; r_teleport_view=r_teleport_reflection=true;
    r_teleport_pvs=(void*)1; teleport_numplanes=1;
    glFrontFace(GL_CCW); glDisable(GL_SCISSOR_TEST); glEnable(GL_STENCIL_TEST);
    glDepthMask(GL_TRUE); glStencilMask(0xff); glColorMask(1,1,1,1);
    R_TeleportAbort(); R_TeleportAbort();
    assert(aborted==1 && !teleport_restore.active && !r_teleport_view && !r_teleport_reflection);
    assert(!r_teleport_pvs && !teleport_numplanes && skyroom_drawn);
    assert(cl_numvisedicts==1 && cl_visedicts[0]==&ent && currententity==&ent);
    GLint integer[4]; GLboolean boolean[4]; GLfloat color[4];
    glGetIntegerv(GL_FRONT_FACE,integer); assert(integer[0]==GL_CW);
    glGetIntegerv(GL_SCISSOR_BOX,integer); assert(!memcmp(integer,teleport_restore.savedscissor,sizeof(integer)));
    assert(glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_STENCIL_TEST));
    glGetIntegerv(GL_STENCIL_WRITEMASK,integer); assert(integer[0]==0x37);
    glGetBooleanv(GL_DEPTH_WRITEMASK,boolean); assert(!boolean[0]);
    glGetBooleanv(GL_COLOR_WRITEMASK,boolean); assert(boolean[0] && !boolean[1] && boolean[2] && !boolean[3]);
    glGetFloatv(GL_COLOR_CLEAR_VALUE,color); assert(color[1]==.5f);
    glGetIntegerv(GL_VIEWPORT,integer); assert(integer[2]==64 && integer[3]==64);
    // Style changes release only the targets the new style no longer needs.
    GLuint targets[2];
    glGenTextures(2,targets);
    glBindTexture(GL_TEXTURE_2D,targets[0]); glBindTexture(GL_TEXTURE_2D,targets[1]);
    memcpy(teleport_planes[0].image,targets,sizeof(targets));
    cvar_t style={0}; style.value=2;
    R_TeleportStyleChanged(&style);
    assert(glIsTexture(targets[0]) && !glIsTexture(targets[1]));
    style.value=1; R_TeleportStyleChanged(&style);
    assert(!glIsTexture(targets[0]) && !teleport_width && !teleport_height);
    assert(glGetError()==GL_NO_ERROR);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    printf("PASS: shader warmup/restart lifecycle and failure latch; %d CPU brush transforms match OpenGL; subview abort restores state; style changes release targets\n",tested);
    puts("PASS: framebuffer validation only on allocation, attachment reuse, resize/restart teardown, and failure fallback");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-tele-gl-") as directory:
    work = Path(directory)
    (work / "test.c").write_text(source)
    cflags = shlex.split(subprocess.check_output(["sdl2-config", "--cflags"], text=True))
    libs = shlex.split(subprocess.check_output(["sdl2-config", "--libs"], text=True))
    subprocess.run(["cc", "-std=gnu11", "-O2", "-DUSE_SDL2", *cflags,
                    "-I", str(ROOT / "Quake"), str(work / "test.c"), *libs, "-lGL", "-lm",
                    "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
