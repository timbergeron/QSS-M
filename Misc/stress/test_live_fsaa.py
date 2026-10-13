#!/usr/bin/env python3
"""Exercise production live-MSAA functions in a real compatibility GL context.

No Quake data required. Run with SDL_VIDEODRIVER=offscreen for headless Mesa,
or under xvfb-run. Requires a C compiler, SDL3 headers, and OpenGL.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
main = (ROOT / "Quake/gl_rmain.c").read_text()
video = (ROOT / "Quake/gl_vidsdl.c").read_text()
screen = (ROOT / "Quake/gl_screen.c").read_text()


def function(source, declaration):
    start = source.index(declaration + "\n{")
    return source[start:source.index("\n}", start) + 2] + "\n"


source = r'''
#include "quakedef.h"
#include <assert.h>
cvar_t vid_fsaa, vid_fxaa, vid_gamma = {.value = 1}, vid_contrast = {.value = 1};
cvar_t r_clearcolor;
qboolean gl_fbo_able = true, gl_msaa_able = true, gl_texture_NPOT = true;
qboolean gl_glsl_gamma_able, gl_mtexable, scr_disabled_for_loading;
int gl_max_texture_image_units, gl_stencilbits, glwidth = 64, glheight = 64, glx, gly;
unsigned int d_8to24table[256];
static int warnings, status_checks, allocations, fail_above, fail_all, allocated_request;
static int fail_resolve, fail_texture, unprotected_warnings, max_samples_override;
static int softemu_mode, warning_redraw, in_warning_redraw;
static int safe_messages;
static char last_message[1024];
static qboolean vid_initialized = true;
static qboolean vid_locked;
void Con_Printf(const char *format, ...) {}
void Con_DPrintf(const char *format, ...) {}
void Con_SafePrintf(const char *format, ...) {
    va_list args;
    va_start(args,format); vsnprintf(last_message,sizeof(last_message),format,args); va_end(args);
    safe_messages++;
}
#define q_vsnprintf vsnprintf
void Con_Warning(const char *format, ...) {
    warnings++;
    if (fail_resolve && !scr_disabled_for_loading) unprotected_warnings++;
    /* Con_Printf redraws the console during signon, with a recursion guard. */
    if (warning_redraw && !scr_disabled_for_loading && !in_warning_redraw) {
        in_warning_redraw=1;
        GLSLGamma_BeginFrame();
        glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
        GLSLGamma_GammaCorrect();
        in_warning_redraw=0;
    }
}
void Sys_Error(const char *format, ...) { abort(); }
void GL_ClearBindings(void) {}
void GL_DisableMultitexture(void) {}
void GL_SelectTexture(GLenum target) {}
void VID_UpdateFSAA(int samples) {}
static int GLSLGamma_SoftEmuMode(void) { return softemu_mode; }
static qboolean GLSLGamma_EnsureSoftEmuLUT(void) { return false; }
static qboolean GLSLGamma_UpdateSoftEmuPalette(float g, float c) { return false; }
int TexMgr_Pad(int size) { return size; }
static void VID_FSAA_f(cvar_t *var);
void Cvar_SetValueQuick(cvar_t *var, float value) {
    if (var->value == value) return;
    var->value = value;
    if (var == &vid_fsaa) VID_FSAA_f(var);
}
void Cvar_SetQuick(cvar_t *var, const char *value) {
    Cvar_SetValueQuick(var, strtof(value, NULL));
}

PFNGLGENFRAMEBUFFERSPROC GL_GenFramebuffersFunc;
PFNGLBINDFRAMEBUFFERPROC GL_BindFramebufferFunc;
PFNGLFRAMEBUFFERTEXTURE2DPROC GL_FramebufferTexture2DFunc;
PFNGLDELETEFRAMEBUFFERSPROC GL_DeleteFramebuffersFunc;
PFNGLGENRENDERBUFFERSPROC GL_GenRenderbuffersFunc;
PFNGLBINDRENDERBUFFERPROC GL_BindRenderbufferFunc;
PFNGLRENDERBUFFERSTORAGEPROC GL_RenderbufferStorageFunc;
PFNGLFRAMEBUFFERRENDERBUFFERPROC GL_FramebufferRenderbufferFunc;
PFNGLDELETERENDERBUFFERSPROC GL_DeleteRenderbuffersFunc;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC GL_RenderbufferStorageMultisampleFunc;
static PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC real_storage;
static void GLAPIENTRY storage(GLenum target, GLsizei samples, GLenum format, GLsizei width, GLsizei height) {
    allocated_request=samples;
    real_storage(target,samples,format,width,height);
}
PFNGLGETRENDERBUFFERPARAMETERIVPROC GL_GetRenderbufferParameterivFunc;
PFNGLBLITFRAMEBUFFERPROC GL_BlitFramebufferFunc;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC real_check;
static PFNGLGENFRAMEBUFFERSPROC real_gen;
static GLenum GLAPIENTRY check(GLenum target) {
    GLint samples;
    status_checks++;
    glGetIntegerv(GL_SAMPLES, &samples);
    if (!samples && fail_resolve) return GL_FRAMEBUFFER_UNSUPPORTED;
    if (samples > 1 && (fail_all || (fail_above && allocated_request > fail_above)))
        return GL_FRAMEBUFFER_UNSUPPORTED;
    return real_check(target);
}
static void GLAPIENTRY gen(GLsizei count, GLuint *ids) {
    allocations += count;
    real_gen(count, ids);
}
PFNGLCHECKFRAMEBUFFERSTATUSPROC GL_CheckFramebufferStatusFunc = check;
QS_PFNGLUSEPROGRAMPROC GL_UseProgramFunc;
QS_PFNGLUNIFORM1FPROC GL_Uniform1fFunc;
QS_PFNGLUNIFORM1IPROC GL_Uniform1iFunc;
static void test_teximage(GLenum target, GLint level, GLint format, GLsizei width, GLsizei height,
                          GLint border, GLenum pixels, GLenum type, const void *data) {
    /* An allocation error must not publish the old storage with new dimensions. */
    glTexImage2D(target,level,fail_texture ? -1 : format,width,height,border,pixels,type,data);
}
#define glTexImage2D test_teximage
static void test_getinteger(GLenum name, GLint *value) {
    if (name == GL_MAX_SAMPLES && max_samples_override) *value=max_samples_override;
    else glGetIntegerv(name,value);
}
#define glGetIntegerv test_getinteger
'''

# Include the actual resource state/helpers, plus the existing gamma state.
start = main.index("static GLuint r_gamma_texture;")
end = main.index("static unsigned int GLSLGamma_SoftEmuPaletteHash")
source += main[start:end]
source += r'''
static void GLSLGamma_CreateShaders(void) {
    PFNGLCREATESHADERPROC create = (void *)SDL_GL_GetProcAddress("glCreateShader");
    PFNGLSHADERSOURCEPROC setsource = (void *)SDL_GL_GetProcAddress("glShaderSource");
    PFNGLCOMPILESHADERPROC compile = (void *)SDL_GL_GetProcAddress("glCompileShader");
    PFNGLCREATEPROGRAMPROC program = (void *)SDL_GL_GetProcAddress("glCreateProgram");
    PFNGLATTACHSHADERPROC attach = (void *)SDL_GL_GetProcAddress("glAttachShader");
    PFNGLLINKPROGRAMPROC link = (void *)SDL_GL_GetProcAddress("glLinkProgram");
    PFNGLGETUNIFORMLOCATIONPROC uniform = (void *)SDL_GL_GetProcAddress("glGetUniformLocation");
    PFNGLDELETESHADERPROC del = (void *)SDL_GL_GetProcAddress("glDeleteShader");
    const char *vs = "#version 120\nvoid main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}";
    const char *fs = "#version 120\nuniform sampler2D tex; uniform float Gamma, Contrast; void main(){gl_FragColor=vec4(pow(texture2D(tex,gl_TexCoord[0].xy).rgb*Contrast,vec3(Gamma)),1.);}";
    GLuint vert = create(GL_VERTEX_SHADER), frag = create(GL_FRAGMENT_SHADER);
    setsource(vert,1,&vs,NULL); compile(vert);
    setsource(frag,1,&fs,NULL); compile(frag);
    r_gamma_program = program(); attach(r_gamma_program,vert); attach(r_gamma_program,frag); link(r_gamma_program);
    del(vert); del(frag);
    gammaLoc=uniform(r_gamma_program,"Gamma"); contrastLoc=uniform(r_gamma_program,"Contrast");
    textureLoc=uniform(r_gamma_program,"tex");
    softEmuModeLoc=softEmuLUTLoc=softEmuPaletteLoc=-1;
}
'''

for declaration in (
    "static void GLSLGamma_Warning (const char *format, ...)",
    "static qboolean GLSLGamma_NeedsPostPass (void)",
    "static qboolean GLSLGamma_CreateTexture (int width, int height)",
    "static qboolean GLSLGamma_CreateFBO (int width, int height)",
    "void GLSLGamma_BeginFrame (void)",
    "GLuint GLSLGamma_SceneFBO (void)",
    "void GLSLGamma_GammaCorrect (void)",
):
    source += function(main, declaration)
source += function(video, "static void VID_FSAA_f (cvar_t *var)")
source += function(screen, "void FXAA_VidFxaaChanged(cvar_t *v)")

source += r'''
static void no_error(void) { GLenum e=glGetError(); if(e) fprintf(stderr,"GL error: 0x%x\n",e); assert(e==GL_NO_ERROR); }
static void request(float value) { Cvar_SetValueQuick(&vid_fsaa,value); }
static void begin(void) { GLSLGamma_BeginFrame(); }
static void end(void) { GLSLGamma_GammaCorrect(); no_error(); }
static void triangle(void) {
    glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST); glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_DITHER);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor3f(1,1,1); glBegin(GL_TRIANGLES);
    glVertex2f(-.8f,-.7f); glVertex2f(.85f,-.45f); glVertex2f(-.35f,.9f); glEnd();
}
static int edges(void) {
    unsigned char pixels[64*64*4]; int partial=0;
    GL_ReadScenePixels(0,0,glwidth,glheight,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(int i=0;i<glwidth*glheight;i++) if(pixels[i*4]>0 && pixels[i*4]<255) partial++;
    no_error(); return partial;
}
int main(int argc, char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0);
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24); SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);
    SDL_Window *window=SDL_CreateWindow("live FSAA test",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!window) fprintf(stderr,"%s\n",SDL_GetError()); assert(window);
    SDL_GLContext context=SDL_GL_CreateContext(window);
    if(!context) fprintf(stderr,"%s\n",SDL_GetError()); assert(context);
    /* SDL's EGL context probe queries GL_MAJOR_VERSION even on GL 2.1. */
    while(glGetError()!=GL_NO_ERROR) {}
#define LOAD(name) GL_##name##Func=(void *)SDL_GL_GetProcAddress("gl" #name); assert(GL_##name##Func)
    LOAD(GenFramebuffers); LOAD(BindFramebuffer); LOAD(FramebufferTexture2D); LOAD(DeleteFramebuffers);
    LOAD(GenRenderbuffers); LOAD(BindRenderbuffer); LOAD(RenderbufferStorage); LOAD(FramebufferRenderbuffer);
    LOAD(DeleteRenderbuffers); LOAD(RenderbufferStorageMultisample); LOAD(GetRenderbufferParameteriv);
    LOAD(BlitFramebuffer); LOAD(UseProgram); LOAD(Uniform1f); LOAD(Uniform1i);
    real_check=(void *)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    real_gen=GL_GenFramebuffersFunc; GL_GenFramebuffersFunc=gen;
    real_storage=GL_RenderbufferStorageMultisampleFunc; GL_RenderbufferStorageMultisampleFunc=storage;
    glGetIntegerv(GL_STENCIL_BITS,&gl_stencilbits);
    int window_stencil=gl_stencilbits;
    printf("GL: %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));

    max_samples_override=8;
    request(0); assert(GLSLGamma_NextMSAA(1)==2 && GLSLGamma_NextMSAA(-1)==8);
    request(2); assert(GLSLGamma_NextMSAA(1)==4 && GLSLGamma_NextMSAA(-1)==0);
    request(3); assert(GLSLGamma_NextMSAA(1)==4 && GLSLGamma_NextMSAA(-1)==2);
    request(6); assert(GLSLGamma_NextMSAA(1)==8 && GLSLGamma_NextMSAA(-1)==4);
    request(8); assert(GLSLGamma_NextMSAA(1)==0 && GLSLGamma_NextMSAA(-1)==4);
    request(16); assert(GLSLGamma_NextMSAA(1)==0 && GLSLGamma_NextMSAA(-1)==8);
    max_samples_override=1; assert(GLSLGamma_NextMSAA(1)==0 && GLSLGamma_NextMSAA(-1)==0);
    gl_msaa_able=false; max_samples_override=0;
    assert(GLSLGamma_NextMSAA(1)==0 && GLSLGamma_NextMSAA(-1)==0);
    gl_msaa_able=true;
    puts("PASS: menu choices respect sample limits and cycle either way from custom requests");

    /* The resolved triangle must have fractional coverage only with MSAA. */
    request(0); begin(); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); triangle(); end();
    assert(edges()==0);
    int levels[]={2,4,6,8,16,2,4,0};
    for(unsigned int i=0;i<sizeof(levels)/sizeof(*levels);i++) {
        request(levels[i]); begin(); triangle();
        assert((levels[i]>0)==(GLSLGamma_MSAASamples()>1));
        end(); assert(gl_stencilbits==window_stencil);
        if(levels[i]) assert(edges()>20);
    }
    puts("PASS: live sample changes alter pixel coverage and restore the window target");

    request(3); begin(); end();
    int messages=safe_messages, resize_warnings=warnings;
    for(int i=0;i<10;i++) { glwidth=63-i; begin(); end(); }
    assert(messages==safe_messages && resize_warnings==warnings);
    glwidth=64;
    puts("PASS: rounded sample diagnostics do not repeat during resize");

    request(4); begin(); GLuint active=GLSLGamma_SceneFBO(); assert(active);
    int count=allocations, checks=status_checks, warned;
    end();
    for(int i=0;i<100;i++) { begin(); assert(GLSLGamma_SceneFBO()==active); end(); }
    assert(allocations==count && status_checks==checks);
    begin(); request(0); assert(GLSLGamma_SceneFBO()==active); triangle(); end();
    begin(); assert(!GLSLGamma_MSAASamples() && !GLSLGamma_SceneFBO()); end();
    puts("PASS: changes are deferred until the next frame; stable frames allocate nothing");

    request(4); begin(); active=GLSLGamma_SceneFBO();
    GLuint copy; glGenTextures(1,&copy); glBindTexture(GL_TEXTURE_2D,copy);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST); glScissor(0,0,1,1);
    GL_CopySceneToTexture(0,0,64,64);
    unsigned char data[64*64*4]; glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    assert(data[100*4]==255 && data[100*4+1]==0 && glIsEnabled(GL_SCISSOR_TEST));
    GLint read,draw; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    assert((GLuint)read==active && (GLuint)draw==active);
    glDisable(GL_SCISSOR_TEST); glClearColor(0,1,0,1); glClear(GL_COLOR_BUFFER_BIT);
    GL_CopySceneToTexture(0,0,64,64); glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    assert(data[100*4]==0 && data[100*4+1]==255);
    GL_ReadScenePixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,data);
    assert(data[100*4]==0 && data[100*4+1]==255); no_error();

    /* A nested pass can have different read/draw targets. Resolve only when
       reading the multisample scene, preserving the independent draw target. */
    GL_BindFramebufferFunc(GL_DRAW_FRAMEBUFFER,0);
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    GL_CopySceneToTexture(0,0,64,64);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    assert(data[100*4]==0 && data[100*4+1]==255 && data[100*4+2]==0);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    assert((GLuint)read==active && draw==0);
    GL_BindFramebufferFunc(GL_READ_FRAMEBUFFER,0);
    GL_BindFramebufferFunc(GL_DRAW_FRAMEBUFFER,active);
    GL_ReadScenePixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,data);
    assert(data[100*4]==0 && data[100*4+1]==0 && data[100*4+2]==255);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    assert(read==0 && (GLuint)draw==active);

    /* Object names are unsigned: a valid FBO with the high bit set must not
       collide with the read helper's "no resolve needed" sentinel. */
    GLuint low_fbo=r_scene_msaa.fbo, high_fbo=0x80000001u;
    GL_BindFramebufferFunc(GL_FRAMEBUFFER,high_fbo);
    GL_FramebufferRenderbufferFunc(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,r_scene_msaa.color);
    GL_FramebufferRenderbufferFunc(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,r_scene_msaa.depth);
    GL_FramebufferRenderbufferFunc(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,r_scene_msaa.depth);
    assert(real_check(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE); no_error();
    r_scene_msaa.fbo=high_fbo;
    GL_ReadScenePixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    assert((GLuint)read==high_fbo && (GLuint)draw==high_fbo);
    GL_CopySceneToTexture(0,0,64,64);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    assert((GLuint)read==high_fbo && (GLuint)draw==high_fbo);
    r_scene_msaa.fbo=low_fbo; GL_BindFramebufferFunc(GL_FRAMEBUFFER,low_fbo);
    GL_DeleteFramebuffersFunc(1,&high_fbo);
    no_error(); end(); glDeleteTextures(1,&copy);
    puts("PASS: copies and screenshots use fresh resolves and preserve scissor/read/draw state");

    glwidth=37; glheight=29; begin();
    GL_BindRenderbufferFunc(GL_RENDERBUFFER,r_scene_msaa.color);
    GLint width,height; GL_GetRenderbufferParameterivFunc(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&width);
    GL_GetRenderbufferParameterivFunc(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&height);
    assert(width==37 && height==29); end();
    begin(); begin(); assert(gl_stencilbits==8); end(); assert(gl_stencilbits==window_stencil);
    puts("PASS: resize and aborted-frame recovery preserve valid attachments and stencil state");

    /* Turning gamma on inside a frame can resize the copy texture while an
       inactive scene FBO still references its previous, smaller depth buffer. */
    request(0); begin(); end();
    glwidth=glheight=64; begin(); assert(!GLSLGamma_SceneFBO());
    gl_glsl_gamma_able=true; vid_gamma.value=.7f; end();
    begin(); assert(GLSLGamma_SceneFBO());
    GL_BindRenderbufferFunc(GL_RENDERBUFFER,r_gamma_depth_renderbuffer);
    GL_GetRenderbufferParameterivFunc(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&width);
    GL_GetRenderbufferParameterivFunc(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&height);
    assert(width==glwidth && height==glheight);
    glClearColor(1,1,1,1); glClear(GL_COLOR_BUFFER_BIT); end();
    unsigned char pixel[4]; GL_ReadScenePixels(50,50,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[0]==255); no_error();
    vid_gamma.value=1; gl_glsl_gamma_able=false; request(4);
    puts("PASS: mid-frame gamma enable after resizing refreshes all scene attachments");

    gl_glsl_gamma_able=true; vid_gamma.value=.7f;
    begin(); glClearColor(.5f,.5f,.5f,1); glClear(GL_COLOR_BUFFER_BIT); end();
    GL_ReadScenePixels(12,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[0]>150 && pixel[0]<165); no_error();
    vid_gamma.value=1; gl_glsl_gamma_able=false;
    begin(); glClearColor(.5f,.5f,.5f,1); glClear(GL_COLOR_BUFFER_BIT); end();
    GL_ReadScenePixels(12,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[0]>=127 && pixel[0]<=128); no_error();
    puts("PASS: shader gamma and fixed-function presentation both consume resolved color");

    /* An unsupported-softemu warning must not redraw and overwrite the scene
       between resolving its color and presenting that resolved texture. */
    gl_glsl_gamma_able=argc>1;
    softemu_mode=1; warning_redraw=1; warned=warnings;
    begin(); glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT); end();
    GL_ReadScenePixels(12,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[0]==255 && pixel[1]==0 && pixel[2]==0);
    assert(warnings==warned+1 && !scr_disabled_for_loading);
    softemu_mode=warning_redraw=0; gl_glsl_gamma_able=false;
    puts("PASS: unsupported postprocessing warnings cannot redraw over a resolved frame");

    /* A failed resize can leave an old texture that still makes an FBO complete. */
    gl_glsl_gamma_able=true; vid_gamma.value=.7f;
    fail_texture=1; glwidth=35; glheight=27; begin();
    assert(!GLSLGamma_MSAASamples() && !GLSLGamma_SceneFBO());
    glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT); end();
    GL_ReadScenePixels(12,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel); assert(pixel[0]==255);
    count=allocations; warned=warnings;
    for(int i=0;i<100;i++) { begin(); end(); }
    assert(count==allocations && warned==warnings);
    for(int i=0;i<5;i++) { glwidth=36+i; begin(); end(); }
    assert(warned==warnings);
    fail_texture=0; GLSLGamma_MSAAChanged(); begin(); assert(GLSLGamma_MSAASamples()>1); end();
    gl_glsl_gamma_able=false; vid_gamma.value=1;
    GLSLGamma_DeleteTexture(); request(0); begin(); end();
    gl_glsl_gamma_able=true; vid_gamma.value=.7f; fail_resolve=1;
    begin(); assert(!GLSLGamma_SceneFBO() && !unprotected_warnings); end();
    warned=warnings;
    for(int i=0;i<5;i++) { glwidth=42+i; begin(); end(); }
    assert(warned==warnings && !unprotected_warnings);
    fail_resolve=0; GLSLGamma_MSAAChanged(); begin(); assert(GLSLGamma_SceneFBO()); end();
    vid_gamma.value=1; gl_glsl_gamma_able=false; request(4);
    GLSLGamma_DeleteTexture(); max_samples_override=1; begin();
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    assert(!GLSLGamma_MSAASamples() && !draw); end();
    max_samples_override=0; GLSLGamma_MSAAChanged(); begin(); end();
    puts("PASS: resolve allocation failures preserve window rendering without recursive warnings or retries");

    fail_above=2; GLSLGamma_MSAAChanged(); begin(); assert(GLSLGamma_MSAASamples()>=2 && allocated_request==2); end();
    request(3); begin(); assert(GLSLGamma_MSAASamples()>=2 && allocated_request==2); end();
    fail_all=1; GLSLGamma_MSAAChanged(); begin(); assert(!GLSLGamma_MSAASamples()); end();
    checks=status_checks; warned=warnings;
    for(int i=0;i<100;i++) { begin(); end(); }
    assert(checks==status_checks && warned==warnings);
    for(int i=0;i<5;i++) { glwidth=48+i; begin(); end(); }
    assert(warned==warnings);
    fail_all=fail_above=0; gl_msaa_able=false; GLSLGamma_MSAAChanged(); begin(); assert(!GLSLGamma_MSAASamples()); end();
    gl_msaa_able=true; GLSLGamma_MSAAChanged(); begin(); assert(GLSLGamma_MSAASamples()>1); end();
    puts("PASS: failed allocations fall back, do not retry every frame, and recover on change");

    request(NAN); assert(vid_fsaa.value==0); request(INFINITY); assert(vid_fsaa.value==0);
    request(-4); assert(vid_fsaa.value==0); request(1); assert(vid_fsaa.value==0);
    request(1e30f); assert(vid_fsaa.value==16); request(3.5f); assert(vid_fsaa.value==3);
    messages=safe_messages;
    vid_fxaa.value=2; request(4); assert(vid_fxaa.value==0);
    assert(safe_messages==messages+1 && strstr(last_message,"disabling FXAA"));
    vid_fxaa.value=2; FXAA_VidFxaaChanged(&vid_fxaa); assert(vid_fsaa.value==0);
    assert(safe_messages==messages+2 && strstr(last_message,"disabling MSAA"));
    request(4); assert(vid_fxaa.value==0);
    begin(); GLuint old_fbo=r_scene_msaa.fbo, old_color=r_scene_msaa.color, old_depth=r_scene_msaa.depth;
    end(); GLSLGamma_DeleteTexture();
    PFNGLISFRAMEBUFFERPROC is_fbo=(void *)SDL_GL_GetProcAddress("glIsFramebuffer");
    PFNGLISRENDERBUFFERPROC is_rb=(void *)SDL_GL_GetProcAddress("glIsRenderbuffer");
    assert(!is_fbo(old_fbo) && !is_rb(old_color) && !is_rb(old_depth));
    no_error(); puts("PASS: input validation, FXAA selection, and context-resource cleanup");
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-live-fsaa-") as work:
    work = Path(work)
    (work / "test.c").write_text(source)
    cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True))
    libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl3"], text=True))
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-I", str(ROOT / "Quake"),
                    *cflags, str(work / "test.c"), *libs, "-lGL", "-lm", "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
    subprocess.run([str(work / "test"), "nomtex"], check=True)
