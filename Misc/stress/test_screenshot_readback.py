"""Test production screenshot readbacks against a real offscreen OpenGL context.

Requires cc, SDL3 via pkg-config, and Mesa's offscreen driver. Exercises pixel
identity, frame ownership, nonblocking polling, bounded queues, GL state,
driver failures, and draining before context destruction. No game data needed.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "Quake/gl_screen.c").read_text()


def function(declaration):
    start = source.index(declaration + "\n{")
    return source[start:source.index("\n}", start) + 2] + "\n"


def between(start, end):
    pos = source.index(start)
    return source[pos:source.index(end, pos) + len(end)] + "\n"


code = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
typedef unsigned char byte;
typedef int qboolean;
#define MAX_OSPATH 4096
#define MAX_QPATH 64
#define SCR_SCREENSHOT_MAX_OUTSTANDING 2
#define SCR_IMAGE_QUEUE_MAX_BYTES ((size_t)256 * 1024 * 1024)
'''
code += '#include "' + str(ROOT / "Quake/platform.h") + '"\n'
code += between("typedef enum\n{\n\tSCR_SCREENSHOT_PNG", "} scr_screenshot_format_t;")
code += between("typedef struct scr_screenshot_job_s", "} scr_screenshot_job_t;")
code += between("typedef struct\n{\n\tscr_screenshot_job_t *job;", "static size_t scr_screenshot_readback_bytes;")
code += r'''
static PFNGLFENCESYNCPROC GL_FenceSyncFunc;
static PFNGLCLIENTWAITSYNCPROC GL_ClientWaitSyncFunc;
static PFNGLDELETESYNCPROC GL_DeleteSyncFunc;
static PFNGLGENBUFFERSARBPROC GL_GenBuffersFunc;
static PFNGLBINDBUFFERARBPROC GL_BindBufferFunc;
static PFNGLBUFFERDATAARBPROC GL_BufferDataFunc;
static PFNGLDELETEBUFFERSARBPROC GL_DeleteBuffersFunc;
static PFNGLMAPBUFFERARBPROC GL_MapBufferFunc;
static PFNGLUNMAPBUFFERARBPROC GL_UnmapBufferFunc;
static int scr_screenshot_outstanding, scr_screenshot_shutdown;
static size_t scr_screenshot_outstanding_bytes;
static void *scr_screenshot_thread = (void *)1;
static int glx, gly, errors;
static scr_screenshot_job_t *completed[2];
static int count;
static char last_error[MAX_OSPATH + 128];
static void Con_SafePrintf(const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, args);
    va_end(args); ++errors;
}
static qboolean SCR_ScreenshotQueue(scr_screenshot_job_t *job) {
    assert(count < 2); completed[count++] = job; return true;
}
static void SCR_ImageJobProcess(scr_screenshot_job_t *job) { (void)job; abort(); }
static void SCR_ImageJobFinish(scr_screenshot_job_t *job, qboolean publish) {
    (void)job; (void)publish; abort();
}
static qboolean GLSLGamma_BeginSceneRead(GLint *old_fbo) { (void)old_fbo; return false; }
static PFNGLBINDFRAMEBUFFERPROC GL_BindFramebufferFunc;
'''
rmain = (ROOT / "Quake/gl_rmain.c").read_text()
pos = rmain.index("void GL_ReadScenePixels (")
code += rmain[pos:rmain.index("\n}", pos) + 2] + "\n"
code += r'''
#define USE_GAMMA_RAMPS 1
static void *draw_context = (void *)1;
static qboolean gammaworks = true, gl_glsl_gamma_able;
static struct { float value; } vid_gamma = { .7f }, vid_contrast = { 1.2f };
static unsigned short vid_gamma_red[256], vid_gamma_green[256], vid_gamma_blue[256];
'''
video = (ROOT / "Quake/gl_vidsdl.c").read_text()
pos = video.index("qboolean VID_Gamma_GetCaptureTable (")
code += video[pos:video.index("\n}", pos) + 2] + "\n"
for name in ("static qboolean SCR_ScreenshotRoom (size_t pixel_bytes)",
             "static void SCR_ScreenshotCollectReadbacks (qboolean wait)",
             "void SCR_ScreenshotShutdownGL (void)",
             "static qboolean SCR_ScreenshotReadback (scr_screenshot_job_t *job)"):
    code += function(name)
code += r'''
static void load(void) {
#define LOADGL(name, type) GL_##name##Func = (type)SDL_GL_GetProcAddress("gl" #name); assert(GL_##name##Func)
    LOADGL(FenceSync, PFNGLFENCESYNCPROC);
    LOADGL(ClientWaitSync, PFNGLCLIENTWAITSYNCPROC);
    LOADGL(DeleteSync, PFNGLDELETESYNCPROC);
    LOADGL(GenBuffers, PFNGLGENBUFFERSARBPROC);
    LOADGL(BindBuffer, PFNGLBINDBUFFERARBPROC);
    LOADGL(BufferData, PFNGLBUFFERDATAARBPROC);
    LOADGL(DeleteBuffers, PFNGLDELETEBUFFERSARBPROC);
    LOADGL(MapBuffer, PFNGLMAPBUFFERARBPROC);
    LOADGL(UnmapBuffer, PFNGLUNMAPBUFFERARBPROC);
}
static scr_screenshot_job_t *job(void) {
    scr_screenshot_job_t *j = calloc(1, sizeof(*j)); assert(j);
    j->width = 127; j->height = 93; j->pixel_bytes = 127 * 93 * 3;
    return j;
}
static void clear(float r, float g, float b) {
    glClearColor(r, g, b, 1); glClear(GL_COLOR_BUFFER_BIT);
}
static void reset(void) {
    for (int i = 0; i < count; ++i) { free(completed[i]->pixels); free(completed[i]); }
    count = 0;
}
static GLenum APIENTRY pending(GLsync fence, GLbitfield flags, GLuint64 timeout) {
    (void)fence; assert(flags == 0 && timeout == 0); return GL_TIMEOUT_EXPIRED;
}
static GLsync APIENTRY no_fence(GLenum condition, GLbitfield flags) {
    (void)condition; (void)flags; return NULL;
}
static void APIENTRY bad_allocation(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    (void)target; (void)size; (void)data; (void)usage;
    glEnable(0xffffffff); /* deterministically fail the allocation check */
}
static void *APIENTRY no_map(GLenum target, GLenum access) { (void)target; (void)access; return NULL; }
int main(void) {
    byte gamma[3][256];
    for (int i = 0; i < 256; ++i) {
        vid_gamma_red[i] = (unsigned short)(i << 8);
        vid_gamma_green[i] = (unsigned short)((255 - i) << 8);
        vid_gamma_blue[i] = (unsigned short)((i ^ 0x37) << 8);
    }
    assert(VID_Gamma_GetCaptureTable(gamma));
    memset(vid_gamma_red, 0, sizeof(vid_gamma_red)); /* snapshots survive later changes */
    for (int i = 0; i < 256; ++i)
        assert(gamma[0][i] == i && gamma[1][i] == 255-i && gamma[2][i] == (i ^ 0x37));
    gl_glsl_gamma_able = true; assert(!VID_Gamma_GetCaptureTable(gamma)); gl_glsl_gamma_able = false;
    gammaworks = false; assert(!VID_Gamma_GetCaptureTable(gamma)); gammaworks = true;
    vid_gamma.value = vid_contrast.value = 1; assert(!VID_Gamma_GetCaptureTable(gamma));
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window = SDL_CreateWindow("Screenshot readback", 127, 93, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext context = SDL_GL_CreateContext(window); assert(context);
    load();
    printf("OpenGL: %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    glViewport(0, 0, 127, 93);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 127, 93); clear(.2f, .4f, .8f);
    glScissor(17, 23, 57, 41); clear(.9f, .1f, .3f);
    glDisable(GL_SCISSOR_TEST);
    byte expected[127 * 93 * 3];
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 127, 93, GL_RGB, GL_UNSIGNED_BYTE, expected);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    GLuint other; GL_GenBuffersFunc(1, &other); GL_BindBufferFunc(GL_PIXEL_PACK_BUFFER, other);
    scr_screenshot_job_t *a = job(); assert(SCR_ScreenshotReadback(a));
    GLint state; glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state); assert((GLuint)state == other);
    glGetIntegerv(GL_PACK_ALIGNMENT, &state); assert(state == 8);
    clear(0, 1, 0); /* a must still contain the previous frame */
    scr_screenshot_job_t *b = job(); assert(SCR_ScreenshotReadback(b));
    assert(!SCR_ScreenshotRoom(1));
    PFNGLCLIENTWAITSYNCPROC wait = GL_ClientWaitSyncFunc;
    GL_ClientWaitSyncFunc = pending;
    SCR_ScreenshotCollectReadbacks(false);
    assert(count == 0 && scr_screenshot_readback_count == 2 && !a->pixels && !b->pixels);
    GL_ClientWaitSyncFunc = wait;
    SCR_ScreenshotShutdownGL();
    assert(count == 2 && completed[0] == a && completed[1] == b);
    assert(!memcmp(a->pixels, expected, sizeof(expected)));
    for (size_t i = 0; i < b->pixel_bytes; i += 3)
        assert(b->pixels[i] == 0 && b->pixels[i+1] == 255 && b->pixels[i+2] == 0);
    assert(!scr_screenshot_readback_count && !scr_screenshot_readback_bytes);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state); assert((GLuint)state == other);
    assert(SCR_ScreenshotRoom(SIZE_MAX)); /* one oversized image alone */
    scr_screenshot_outstanding = 1; scr_screenshot_outstanding_bytes = SCR_IMAGE_QUEUE_MAX_BYTES;
    assert(!SCR_ScreenshotRoom(1) && !SCR_ScreenshotRoom(SIZE_MAX));
    scr_screenshot_outstanding = 0; scr_screenshot_outstanding_bytes = 0;
    reset();
    a = job();
    snprintf(a->name, sizeof(a->name), "screenshots/readback-test.png");
    PFNGLFENCESYNCPROC fence = GL_FenceSyncFunc;
    GL_FenceSyncFunc = NULL; assert(!SCR_ScreenshotReadback(a));
    GL_FenceSyncFunc = no_fence; assert(!SCR_ScreenshotReadback(a));
    GL_FenceSyncFunc = fence;
    PFNGLBUFFERDATAARBPROC allocate = GL_BufferDataFunc;
    GL_BufferDataFunc = bad_allocation; assert(!SCR_ScreenshotReadback(a));
    GL_BufferDataFunc = allocate;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state); assert((GLuint)state == other);
    glGetIntegerv(GL_PACK_ALIGNMENT, &state); assert(state == 8);
    assert(!scr_screenshot_readback_count && !scr_screenshot_readback_bytes);
    assert(SCR_ScreenshotReadback(a));
    PFNGLMAPBUFFERARBPROC map = GL_MapBufferFunc; GL_MapBufferFunc = no_map;
    SCR_ScreenshotShutdownGL(); GL_MapBufferFunc = map;
    assert(errors == 1 && !count && !scr_screenshot_readback_count && !scr_screenshot_readback_bytes);
    assert(strstr(last_error, "Couldn't capture screenshots/readback-test.png"));
    /* A failed transfer must leave the next capture usable. */
    assert(SCR_ScreenshotReadback(job())); SCR_ScreenshotShutdownGL(); assert(count == 1); reset();
    assert(glGetError() == GL_NO_ERROR);
    GL_BindBufferFunc(GL_PIXEL_PACK_BUFFER, 0); GL_DeleteBuffersFunc(1, &other);
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    puts("PASS: screenshot pixels, frame ownership, polling, limits, GL state, failures, shutdown");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-screenshot-") as tmp:
    tmp = Path(tmp)
    (tmp / "test.c").write_text(code)
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
    subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
                    "-fsanitize=address,undefined", str(tmp / "test.c"), "-o", str(tmp / "test"),
                    *flags, "-lGL"], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", ASAN_OPTIONS="detect_leaks=0")
    subprocess.run([str(tmp / "test")], env=env, check=True)
