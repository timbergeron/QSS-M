#!/usr/bin/env python3
"""Check production framebuffer capability loading without requiring a GPU.

Models a GL loader that returns non-null addresses even for unsupported entry
points, as GLX permits. Requires a C compiler and SDL3 headers.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
video = (ROOT / "Quake/gl_vidsdl.c").read_text()
declaration = "static void GL_CheckFramebufferExtensions (void)"
start = video.index(declaration + "\n{")
loader = video[start:video.index("\n}", start) + 2]

source = r'''
#include "quakedef.h"
#include <assert.h>
qboolean gl_fbo_able, gl_msaa_able, gl_texture_NPOT = true;
int gl_version_major;
const char *gl_extensions;
client_static_t cls;
void Con_Printf(const char *format, ...) {}
void Con_Warning(const char *format, ...) {}
static int core_lookups, ext_lookups;
static const char *missing;
static void core_entry(void) {}
static void ext_entry(void) {}
static SDL_FunctionPointer get_proc(const char *name) {
    int ext = strstr(name, "EXT") != NULL;
    if (ext) ext_lookups++; else core_lookups++;
    if (missing && !strcmp(missing,name)) return NULL;
    return ext ? ext_entry : core_entry;
}
#define SDL_GL_GetProcAddress get_proc
'''
start = video.index("PFNGLGENFRAMEBUFFERSPROC GL_GenFramebuffersFunc")
end = video.index("\n\n", start)
source += video[start:end] + "\n"
start = video.index("static qboolean GL_ParseExtensionList (")
source += video[start:video.index("\n}", start) + 2] + "\n" + loader
source += r'''
static void load(int version, const char *extensions) {
    gl_version_major=version; gl_extensions=extensions;
    core_lookups=ext_lookups=0; GL_CheckFramebufferExtensions();
}
int main(void) {
    const char *ext="GL_EXT_framebuffer_object GL_EXT_framebuffer_multisample "
        "GL_EXT_framebuffer_blit GL_EXT_packed_depth_stencil";
    load(2,ext);
    assert(gl_fbo_able && gl_msaa_able && !core_lookups && ext_lookups==13);
    assert((SDL_FunctionPointer)GL_BlitFramebufferFunc==ext_entry);
    load(3,""); assert(gl_fbo_able && gl_msaa_able && core_lookups==13 && !ext_lookups);
    load(2,"GL_ARB_framebuffer_object");
    assert(gl_fbo_able && gl_msaa_able && core_lookups==13 && !ext_lookups);
    load(2,"GL_EXT_framebuffer_object");
    assert(gl_fbo_able && !gl_msaa_able && !GL_BlitFramebufferFunc);
    load(2,"GL_EXT_framebuffer_object GL_EXT_framebuffer_multisample GL_EXT_packed_depth_stencil");
    assert(gl_fbo_able && !gl_msaa_able && !GL_RenderbufferStorageMultisampleFunc);
    gl_texture_NPOT=false; load(2,ext); assert(gl_fbo_able && !gl_msaa_able);
    gl_texture_NPOT=true; missing="glBlitFramebufferEXT";
    load(2,ext); assert(gl_fbo_able && !gl_msaa_able && !core_lookups);
    missing="glBindFramebuffer"; load(3,""); assert(!gl_fbo_able && !gl_msaa_able);
    missing=NULL; load(3,""); assert(gl_msaa_able);
    load(1,"");
    assert(!gl_fbo_able && !gl_msaa_able && !core_lookups && !ext_lookups);
    assert(!GL_BindFramebufferFunc && !GL_BlitFramebufferFunc && !GL_GetRenderbufferParameterivFunc);
    puts("PASS: core/ARB and EXT-only entry points, missing capabilities/functions, and context resets");
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-fsaa-loader-") as work:
    work = Path(work)
    (work / "test.c").write_text(source)
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True))
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-I", str(ROOT / "Quake"),
                    *flags, str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
