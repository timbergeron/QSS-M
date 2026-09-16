"""Exercise the shared SDL3 clipboard layer (Quake/clipboard_sdl.c).

Run with python3 Misc/stress/test_sdl3_clipboard.py. Requires cc and SDL3 via
pkg-config; uses ASan/UBSan and SDL's dummy video driver, so no display,
window or game assets are needed.

Two binaries are built from the production sources:

* fake: clipboard_sdl.c against an SDL clipboard core that reproduces SDL
  3.2.12's SDL_SetClipboardData, including the callback state it keeps when a
  backend fails and a stale Cocoa-style provider that calls back after cleanup.
* real: clipboard_sdl.c, the image.c PNG encoder, the gl_screen.c image job
  steps and pl_linux.c's file-list reader, linked with the installed SDL3.
  Exported PNG/BMP data is decoded with stb_image and compared per pixel.

Real-desktop interchange (pasting into other applications) is not covered.
"""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
QUAKE = ROOT / "Quake"


def read(name):
    return (QUAKE / name).read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


def between(source, start_text, end_text):
    start = source.index(start_text)
    return source[start:source.index(end_text, start) + len(end_text)] + "\n"


# --- source guards ------------------------------------------------------------

for path in sorted(QUAKE.glob("*.[chm]")):
    text = path.read_text(errors="replace")
    if path.name != "clipboard_sdl.c":
        assert "SDL_SetClipboardText" not in text, (
            f"{path.name}: engine text copies must use Clipboard_SetText so "
            "they supersede pending image copies")
    assert "Sys_Image_BGRA_To_Clipboard" not in text, path.name

in_sdl = read("in_sdl.c")
assert re.search(r"case SDL_EVENT_CLIPBOARD_UPDATE:\s*Clipboard_HandleEvent \(&event\);", in_sdl)
host = read("host.c")
shutdown = host[host.index("void Host_Shutdown(void)"):]
assert shutdown.index("SCR_Shutdown();") < shutdown.index("Clipboard_Shutdown();") < shutdown.index("VID_Shutdown();")
for makefile in ("Makefile", "Makefile.w32", "Makefile.w64"):
    assert "clipboard_sdl.o" in read(makefile), makefile
for project in ("quakespasm.vcxproj", "quakespasm.vcxproj.filters"):
    assert "clipboard_sdl.c" in (ROOT / "Windows/VisualStudio" / project).read_text(), project
pbxproj = (ROOT / "macOS/QuakeSpasm.xcodeproj/project.pbxproj").read_text()
sources_phase = between(pbxproj, "/* Begin PBXSourcesBuildPhase section */", "/* End PBXSourcesBuildPhase section */")
assert "clipboard_sdl.c in Sources" in sources_phase

# --- shared stub header ---------------------------------------------------------

STUB_HEADER = r'''
#ifndef QUAKEDEF_STUB_H
#define QUAKEDEF_STUB_H
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <SDL3/SDL.h>
typedef unsigned char byte;
typedef int qboolean;
#define MAX_QPATH 64
#define MAX_OSPATH 4096
#define Q_MAXINT ((int)0x7fffffff)
typedef struct { float value; } cvar_t;
extern cvar_t developer;
void *Z_Malloc (int size);
void Z_Free (void *ptr);
size_t q_strlcpy (char *dst, const char *src, size_t size);
size_t q_strlcat (char *dst, const char *src, size_t size);
int q_strncasecmp (const char *s1, const char *s2, size_t n);
void Con_Printf (const char *fmt, ...);
void Con_SafePrintf (const char *fmt, ...);
void Con_DPrintf (const char *fmt, ...);
void Con_LinkPrintf (const char *link, const char *fmt, ...);
#include "@QUAKE@/platform.h"
#include "@QUAKE@/image.h"
#endif
'''.replace("@QUAKE@", str(QUAKE))

COMMON_STUBS = r'''
#include "quakedef.h"
cvar_t developer;
static int z_outstanding;
void *Z_Malloc (int size) {
    assert(size > 0);
    void *p = calloc(1, (size_t)size);
    assert(p);
    ++z_outstanding;
    return p;
}
void Z_Free (void *ptr) {
    if (ptr) --z_outstanding;
    free(ptr);
}
size_t q_strlcpy (char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) {
        size_t c = n >= size ? size - 1 : n;
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return n;
}
size_t q_strlcat (char *dst, const char *src, size_t size) {
    size_t d = strnlen(dst, size);
    if (d == size) return size + strlen(src);
    return d + q_strlcpy(dst + d, src, size - d);
}
int q_strncasecmp (const char *s1, const char *s2, size_t n) { return strncasecmp(s1, s2, n); }
static char console[65536];
static void console_append (const char *fmt, va_list ap) {
    size_t used = strlen(console);
    vsnprintf(console + used, sizeof(console) - used, fmt, ap);
}
void Con_Printf (const char *fmt, ...) { va_list ap; va_start(ap, fmt); console_append(fmt, ap); va_end(ap); }
void Con_SafePrintf (const char *fmt, ...) { va_list ap; va_start(ap, fmt); console_append(fmt, ap); va_end(ap); }
void Con_DPrintf (const char *fmt, ...) { va_list ap; va_start(ap, fmt); console_append(fmt, ap); va_end(ap); }
void Con_LinkPrintf (const char *link, const char *fmt, ...) {
    (void)link;
    va_list ap; va_start(ap, fmt); console_append(fmt, ap); va_end(ap);
}
static int released_images, made_images;
static void release_counted (void *data) { ++released_images; free(data); }
static clipboard_image_t make_image (size_t size, int marker) {
    clipboard_image_t image;
    memset(&image, 0, sizeof(image));
    image.format = CLIPBOARD_IMAGE_PNG;
    image.data = (byte *)malloc(size);
    assert(image.data);
    memset(image.data, marker, size);
    image.size = size;
    image.release = release_counted;
    ++made_images;
    return image;
}
'''

# --- fake SDL clipboard core (SDL 3.2.12 semantics) ---------------------------

FAKE_TEST = COMMON_STUBS + r'''
#if SDL_VERSION_ATLEAST(3, 4, 0)
#define MIME_LIST const char *const *
#else
#define MIME_LIST const char **
#endif

qboolean Image_EncodePNGMemory (const byte *data, int width, int height, int bpp, qboolean upsidedown,
    byte **png, size_t *pngsize, char *error_text, size_t error_text_size) {
    (void)data; (void)width; (void)height; (void)bpp; (void)upsidedown;
    *png = NULL; *pngsize = 0;
    q_strlcpy(error_text, "not in fake", error_text_size);
    return 0;
}
void Image_FreePNGMemory (void *png) { free(png); }
char **PL_GetClipboardFilePaths (int *count) { *count = 0; return NULL; }

static int video_ready = 1;
static Uint64 now_ns = 1000;
static const char *video_driver = "fake";
static int sdl_version = SDL_VERSIONNUM(3, 2, 12);
const char *SDL_GetCurrentVideoDriver (void) { return video_driver; }
int SDL_GetVersion (void) { return sdl_version; }
static int clipboard_reads;
SDL_InitFlags SDL_WasInit (SDL_InitFlags flags) { return video_ready ? flags : 0; }
/* a clock that always moves, so queued events are stamped after earlier tokens */
Uint64 SDL_GetTicksNS (void) { now_ns += 1000; return now_ns; }
const char *SDL_GetError (void) { return "fake backend failure"; }
void SDL_free (void *mem) { free(mem); }
static int pumps;
void SDL_PumpEvents (void) { ++pumps; }

/* Queued events own their mime strings: SDL copies them, and the arrays
   callers pass to SDL_SetClipboardData live on the stack. */
typedef struct {
    SDL_Event event;
    int count;
    char storage[8][64];
    const char *mimes[8];
} fake_event_t;
static fake_event_t queue[64];
static int queued;
static fake_event_t popped;
static void push_update_at (bool owner, const char *const *mime_types, int count, Uint64 timestamp) {
    assert(queued < (int)(sizeof(queue) / sizeof(queue[0])) && count <= 8);
    fake_event_t *entry = &queue[queued++];
    memset(entry, 0, sizeof(*entry));
    entry->count = count;
    for (int i = 0; i < count; ++i) {
        assert(strlen(mime_types[i]) < sizeof(entry->storage[i]));
        strcpy(entry->storage[i], mime_types[i]);
        entry->mimes[i] = entry->storage[i];
    }
    entry->event.type = SDL_EVENT_CLIPBOARD_UPDATE;
    entry->event.clipboard.owner = owner;
    entry->event.clipboard.num_mime_types = count;
    /* SDL stamps an event when it is queued */
    entry->event.clipboard.timestamp = timestamp;
    entry->event.clipboard.mime_types = (void *)entry->mimes;
}
static void push_update (bool owner, const char *const *mime_types, int count) {
    push_update_at(owner, mime_types, count, now_ns);
}
int SDL_PeepEvents (SDL_Event *events, int numevents, SDL_EventAction action, Uint32 minType, Uint32 maxType) {
    assert(action == SDL_GETEVENT && numevents == 1);
    for (int i = 0; i < queued; ++i) {
        if (queue[i].event.type >= minType && queue[i].event.type <= maxType) {
            popped = queue[i];
            for (int k = 0; k < popped.count; ++k)
                popped.mimes[k] = popped.storage[k];
            popped.event.clipboard.mime_types = (void *)popped.mimes;
            *events = popped.event;
            memmove(queue + i, queue + i + 1, (size_t)(queued - i - 1) * sizeof(queue[0]));
            --queued;
            return 1;
        }
    }
    return 0;
}

static SDL_ClipboardDataCallback held_callback, stale_callback;
static SDL_ClipboardCleanupCallback held_cleanup;
static void *held_userdata, *stale_userdata;
static int backend_fails, backend_reads_now;
static size_t last_sync_size;
static const char *text_mimes[] = { "text/plain;charset=utf-8" };
static const char *png_mimes[] = { "image/png" };
static const char *plain_mimes[] = { "text/plain" };
static char clipboard_text[256];

/* SDL_CancelClipboardData(0) */
static void cancel_data (void) {
    if (held_cleanup) held_cleanup(held_userdata);
    held_callback = NULL;
    held_cleanup = NULL;
    held_userdata = NULL;
}
/* SDL 3.2.12 SDL_SetClipboardData, with a backend that can fail or read synchronously */
bool SDL_SetClipboardData (SDL_ClipboardDataCallback callback, SDL_ClipboardCleanupCallback cleanup,
    void *userdata, MIME_LIST mime_types, size_t num_mime_types) {
    if (!((callback && mime_types && num_mime_types > 0) || (!callback && !mime_types && num_mime_types == 0)))
        return false;
    cancel_data();
    held_callback = callback;
    held_cleanup = cleanup;
    held_userdata = userdata;
    /* like a Cocoa pasteboard provider, keep the pointers past SDL's cleanup */
    stale_callback = callback;
    stale_userdata = userdata;
    if (backend_reads_now && callback) {
        size_t size = 0;
        const void *data = callback(userdata, mime_types[0], &size);
        last_sync_size = data ? size : 0;
    }
    if (backend_fails)
        return false; /* 3.2.12 returns without clearing the callback state */
    push_update(true, (const char **)mime_types, (int)num_mime_types);
    return true;
}
bool SDL_SetClipboardText (const char *text) {
    cancel_data();
    q_strlcpy(clipboard_text, text, sizeof(clipboard_text));
    push_update(true, text_mimes, 1);
    return true;
}
void *SDL_GetClipboardData (const char *mime_type, size_t *size) {
    *size = 0;
    ++clipboard_reads;
    if (!held_callback) return NULL;
    const void *data = held_callback(held_userdata, mime_type, size);
    if (!data) return NULL;
    void *copy = malloc(*size + 4);
    assert(copy);
    memcpy(copy, data, *size);
    return copy;
}
SDL_Surface *SDL_CreateSurface (int width, int height, SDL_PixelFormat format) {
    (void)width; (void)height; (void)format; return NULL;
}
void SDL_DestroySurface (SDL_Surface *surface) { (void)surface; }
SDL_IOStream *SDL_IOFromDynamicMem (void) { return NULL; }
bool SDL_SaveBMP_IO (SDL_Surface *surface, SDL_IOStream *dst, bool closeio) {
    (void)surface; (void)dst; (void)closeio; return false;
}
bool SDL_CloseIO (SDL_IOStream *context) { (void)context; return true; }
Sint64 SDL_GetIOSize (SDL_IOStream *context) { (void)context; return -1; }
SDL_PropertiesID SDL_GetIOProperties (SDL_IOStream *context) { (void)context; return 0; }
void *SDL_GetPointerProperty (SDL_PropertiesID props, const char *name, void *default_value) {
    (void)props; (void)name; return default_value;
}
bool SDL_SetPointerProperty (SDL_PropertiesID props, const char *name, void *value) {
    (void)props; (void)name; (void)value; return false;
}

static clipboard_publish_t publish (uint64_t request, clipboard_image_t *image) {
    char error[128];
    return Clipboard_PublishImage(request, image, error, sizeof(error));
}
static clipboard_publish_t publish_image (uint64_t request, int marker) {
    clipboard_image_t image = make_image(8, marker);
    return publish(request, &image);
}

int main (void) {
    char error[128];
    size_t size;
    clipboard_image_t image;
    uint64_t request;
    int released;

    /* published data survives the caller's job and is served repeatedly */
    image = make_image(64, 'A');
    request = Clipboard_BeginImageRequest();
    assert(Clipboard_PublishImage(request, &image, error, sizeof(error)) == CLIPBOARD_PUBLISHED);
    assert(!image.data && !image.release);
    for (int i = 0; i < 3; ++i) {
        byte *data = SDL_GetClipboardData("image/png", &size);
        assert(data && size == 64 && data[0] == 'A' && data[63] == 'A');
        free(data);
    }
    assert(!SDL_GetClipboardData("image/bmp", &size) && size == 0);
    assert(Clipboard_SetText("192.0.2.1:26000") && released_images == 1);
    assert(!stale_callback(stale_userdata, "image/png", &size) && size == 0);

    /* a failed publication releases once, although SDL keeps the userdata */
    backend_fails = 1;
    image = make_image(32, 'B');
    request = Clipboard_BeginImageRequest();
    assert(Clipboard_PublishImage(request, &image, error, sizeof(error)) == CLIPBOARD_FAILED);
    assert(strstr(error, "fake backend failure") && released_images == 2 && held_userdata);
    assert(!stale_callback(stale_userdata, "image/png", &size));
    backend_fails = 0;
    assert(Clipboard_SetText("after failure")); /* runs SDL's retained cleanup */
    assert(released_images == 2);

    /* Windows copies the data inside SDL_SetClipboardData, so ours is released at once */
    video_driver = "windows";
    backend_reads_now = 1;
    released = released_images;
    image = make_image(16, 'C');
    assert(publish(Clipboard_BeginImageRequest(), &image) == CLIPBOARD_PUBLISHED && last_sync_size == 16);
    assert(released_images == released + 1);
    assert(!SDL_GetClipboardData("image/png", &size) && size == 0);
    assert(Clipboard_SetText("after windows copy") && released_images == released + 1);
    backend_reads_now = 0;

    /* Cocoa's lazy promise is resolved right away, and the bytes are kept */
    video_driver = "cocoa";
    int reads = clipboard_reads;
    image = make_image(16, 'Q');
    assert(publish(Clipboard_BeginImageRequest(), &image) == CLIPBOARD_PUBLISHED);
    assert(clipboard_reads == reads + 1);
    byte *kept = SDL_GetClipboardData("image/png", &size);
    assert(kept && size == 16 && kept[0] == 'Q');
    free(kept);
    video_driver = "fake";

    /* a newer image releases the older one exactly once */
    released = released_images;
    image = make_image(8, 'D');
    assert(publish(Clipboard_BeginImageRequest(), &image) == CLIPBOARD_PUBLISHED && released_images == released + 1);

    /* screenshot, then an address copy: the address stays */
    released = released_images;
    image = make_image(8, 'E');
    request = Clipboard_BeginImageRequest();
    assert(Clipboard_SetText("server address"));
    assert(publish(request, &image) == CLIPBOARD_SUPERSEDED);
    assert(released_images == released + 2 && !strcmp(clipboard_text, "server address") && !held_cleanup);

    /* an older image cannot replace a newer one */
    clipboard_image_t older = make_image(8, 'F'), newer = make_image(8, 'G');
    uint64_t older_request = Clipboard_BeginImageRequest();
    uint64_t newer_request = Clipboard_BeginImageRequest();
    assert(publish(older_request, &older) == CLIPBOARD_SUPERSEDED);
    assert(publish(newer_request, &newer) == CLIPBOARD_PUBLISHED);
    byte *served = SDL_GetClipboardData("image/png", &size);
    assert(served && served[0] == 'G');
    free(served);

    /* a queued external change is processed before publishing */
    image = make_image(8, 'H');
    request = Clipboard_BeginImageRequest();
    push_update(false, plain_mimes, 1);
    int pumps_before = pumps;
    assert(publish(request, &image) == CLIPBOARD_SUPERSEDED && pumps == pumps_before + 1 && queued == 0);

    /* a change queued before the request is older than it and does not cancel it */
    push_update(false, plain_mimes, 1);
    now_ns += 1000;
    request = Clipboard_BeginImageRequest();
    assert(publish_image(request, 'i') == CLIPBOARD_PUBLISHED);

    /* Wayland on SDL 3.2 echoes our own offer back as an external selection */
    video_driver = "wayland";
    assert(publish_image(Clipboard_BeginImageRequest(), 'J') == CLIPBOARD_PUBLISHED);
    request = Clipboard_BeginImageRequest();
    push_update(false, png_mimes, 1);
    assert(publish_image(request, 'K') == CLIPBOARD_PUBLISHED);
    /* only one echo per copy */
    request = Clipboard_BeginImageRequest();
    push_update(false, png_mimes, 1);
    push_update(false, png_mimes, 1);
    assert(publish_image(request, 'L') == CLIPBOARD_SUPERSEDED);
    /* another client's copy cancels our data first, so ownership is the tell */
    assert(publish_image(Clipboard_BeginImageRequest(), 'M') == CLIPBOARD_PUBLISHED);
    request = Clipboard_BeginImageRequest();
    cancel_data();
    push_update(false, png_mimes, 1);
    assert(publish_image(request, 'N') == CLIPBOARD_SUPERSEDED);
    /* different offered types are never ours */
    assert(publish_image(Clipboard_BeginImageRequest(), 'O') == CLIPBOARD_PUBLISHED);
    request = Clipboard_BeginImageRequest();
    push_update(false, plain_mimes, 1);
    assert(publish_image(request, 'P') == CLIPBOARD_SUPERSEDED);
    /* SDL 3.4 filters its own Wayland offers, and other drivers never echo */
    sdl_version = SDL_VERSIONNUM(3, 4, 16);
    assert(publish_image(Clipboard_BeginImageRequest(), 'Q') == CLIPBOARD_PUBLISHED);
    request = Clipboard_BeginImageRequest();
    push_update(false, png_mimes, 1);
    assert(publish_image(request, 'R') == CLIPBOARD_SUPERSEDED);
    sdl_version = SDL_VERSIONNUM(3, 2, 12);
    video_driver = "x11";
    assert(publish_image(Clipboard_BeginImageRequest(), 'S') == CLIPBOARD_PUBLISHED);
    request = Clipboard_BeginImageRequest();
    push_update(false, png_mimes, 1);
    assert(publish_image(request, 'T') == CLIPBOARD_SUPERSEDED);
    video_driver = "fake";

    /* no video, or no data */
    video_ready = 0;
    image = make_image(8, 'N');
    assert(publish(Clipboard_BeginImageRequest(), &image) == CLIPBOARD_FAILED && !image.data);
    video_ready = 1;
    memset(&image, 0, sizeof(image));
    assert(publish(Clipboard_BeginImageRequest(), &image) == CLIPBOARD_FAILED);

    /* shutdown invalidates work still being encoded */
    image = make_image(8, 'O');
    request = Clipboard_BeginImageRequest();
    Clipboard_Shutdown();
    assert(publish(request, &image) == CLIPBOARD_SUPERSEDED);

    /* SDL_VideoQuit cancels the data; stale providers find nothing */
    image = make_image(8, 'P');
    assert(publish(Clipboard_BeginImageRequest(), &image) == CLIPBOARD_PUBLISHED);
    cancel_data();
    assert(!stale_callback(stale_userdata, "image/png", &size));

    assert(released_images == made_images);
    assert(z_outstanding == 0);
    puts("clipboard ownership (SDL 3.2.12 core semantics): passed");
    return 0;
}
'''

# --- real SDL: encoders, parser, image jobs and Linux file lists ---------------

LODEPNG_DEFINES = (
    "#define LODEPNG_NO_COMPILE_DECODER\n"
    "#define LODEPNG_NO_COMPILE_CPP\n"
    "#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS\n"
)

# image.c includes lodepng.c for its static allocators, so the extracted
# encoder gets its own translation unit, as it does in the engine.
IMAGE_TU = '#include "quakedef.h"\n' + LODEPNG_DEFINES + '#include "lodepng.h"\n#include "lodepng.c"\n'
image_source = read("image.c")
for declaration in (
    "static byte *CopyFlipped(",
    "static void Image_SetWriteError (",
    "qboolean Image_WritePNG_OSPath (",
    "qboolean Image_WriteEncoded_OSPath (",
    "void Image_FreePNGMemory (",
    "qboolean Image_EncodePNGMemory (",
):
    IMAGE_TU += function(image_source, declaration)

REAL_TEST = COMMON_STUBS + r'''
unsigned char *stbi_load_from_memory (unsigned char const *buffer, int len, int *x, int *y,
    int *channels_in_file, int desired_channels);
void stbi_image_free (void *retval_from_stbi_load);
static int sounds;
void S_NotificationSound_Copy (void) { ++sounds; }
static int jpg_result = 1;
qboolean Image_WriteTGA_OSPath (const char *path, byte *data, int width, int height, int bpp, qboolean upsidedown) {
    (void)path; (void)data; (void)width; (void)height; (void)bpp; (void)upsidedown;
    return 1;
}
qboolean Image_WriteJPG_OSPath (const char *path, byte *data, int width, int height, int bpp, int quality, qboolean upsidedown) {
    (void)path; (void)data; (void)width; (void)height; (void)bpp; (void)quality; (void)upsidedown;
    return jpg_result;
}
'''

screen_source = read("gl_screen.c")
REAL_TEST += between(screen_source, "typedef enum\n{\n\tSCR_SCREENSHOT_PNG", "} scr_screenshot_format_t;")
REAL_TEST += between(screen_source, "typedef struct scr_screenshot_job_s", "} scr_screenshot_job_t;")
for declaration in (
    "static qboolean SCR_ScreenshotWrite (",
    "static void SCR_ImageJobProcess (",
    "static void SCR_ScreenshotReport (",
    "static void SCR_ImageJobFinish (",
):
    REAL_TEST += function(screen_source, declaration)

linux_source = read("pl_linux.c")
REAL_TEST += function(linux_source, "static qboolean PL_ClipboardMayOffer (")
REAL_TEST += function(linux_source, "char **PL_GetClipboardFilePaths (int *count)")

REAL_TEST += r'''
static void expected_pixel (int x, int y, byte *rgb) {
    rgb[0] = (byte)(x * 37 + y * 11 + 1);
    rgb[1] = (byte)(x * 5 + y * 71 + 2);
    rgb[2] = (byte)(x * 13 + y * 3 + 200);
}
static byte *make_pixels (int width, int height, clipboard_pixels_t layout, int bottom_up) {
    size_t bpp = layout == CLIPBOARD_PIXELS_BGRA32 ? 4 : 3;
    byte *pixels = malloc((size_t)width * (size_t)height * bpp);
    assert(pixels);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            byte rgb[3];
            byte *p = pixels + ((size_t)(bottom_up ? height - 1 - y : y) * (size_t)width + (size_t)x) * bpp;
            expected_pixel(x, y, rgb);
            if (bpp == 4) {
                p[0] = rgb[2]; p[1] = rgb[1]; p[2] = rgb[0];
                p[3] = (byte)(x * y * 7 + 3); /* not opaque: exports must ignore it */
            } else
                memcpy(p, rgb, 3);
        }
    }
    return pixels;
}
static void check_pixels (const byte *data, size_t size, int width, int height) {
    int w, h, channels;
    byte *decoded = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 3);
    assert(decoded && w == width && h == height && channels == 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            byte rgb[3];
            expected_pixel(x, y, rgb);
            if (memcmp(decoded + ((size_t)y * (size_t)w + (size_t)x) * 3, rgb, 3)) {
                fprintf(stderr, "pixel %d,%d of %dx%d differs\n", x, y, width, height);
                abort();
            }
        }
    }
    stbi_image_free(decoded);
}
static uint32_t le32 (const byte *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32 (byte *p, uint32_t value) {
    p[0] = (byte)value; p[1] = (byte)(value >> 8); p[2] = (byte)(value >> 16); p[3] = (byte)(value >> 24);
}
static void check_png (const byte *data, size_t size, int width, int height) {
    assert(size > 33 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8));
    assert(data[25] == 2); /* truecolour without alpha, so it pastes opaque */
    check_pixels(data, size, width, height);
}
static void check_bmp (const byte *data, size_t size, int width, int height) {
    assert(size > 54 && data[0] == 'B' && data[1] == 'M');
    uint32_t offset = le32(data + 10), header = le32(data + 14);
    uint32_t image_size = le32(data + 34), colors = le32(data + 46);
    assert(header == 40 && (int32_t)le32(data + 18) == width && (int32_t)le32(data + 22) == height);
    assert((data[28] | data[29] << 8) == 24 && le32(data + 30) == 0 && colors == 0);
    /* SDL's Windows backend copies only biSizeImage pixel bytes into CF_DIB */
    assert(image_size == (uint32_t)(((width * 3 + 3) & ~3) * height));
    assert(offset >= 14 + header && (size_t)offset + image_size <= size);
    /* and rebuilds a BMP from that DIB when it is read back */
    size_t rebuilt_size = 14 + header + image_size;
    byte *rebuilt = calloc(1, rebuilt_size);
    assert(rebuilt);
    rebuilt[0] = 'B';
    rebuilt[1] = 'M';
    put32(rebuilt + 2, (uint32_t)rebuilt_size);
    put32(rebuilt + 10, 14 + header);
    memcpy(rebuilt + 14, data + 14, header);
    memcpy(rebuilt + 14 + header, data + offset, image_size);
    check_pixels(rebuilt, rebuilt_size, width, height);
    free(rebuilt);
}
static void check_image (const clipboard_image_t *image, int width, int height) {
    if (image->format == CLIPBOARD_IMAGE_BMP)
        check_bmp(image->data, image->size, width, height);
    else
        check_png(image->data, image->size, width, height);
}
static void check_served (int width, int height) {
    clipboard_image_t served;
    memset(&served, 0, sizeof(served));
    served.format = Clipboard_NativeImageFormat();
    served.data = SDL_GetClipboardData(Clipboard_ImageMimeType(served.format), &served.size);
    assert(served.data);
    check_image(&served, width, height);
    SDL_free(served.data);
}
static void release_png (void *data) { ++released_images; Image_FreePNGMemory(data); }
static void release_sdl (void *data) { ++released_images; SDL_free(data); }
static void track (clipboard_image_t *image) {
    assert(image->data && image->size && image->release);
    ++made_images;
    image->release = image->format == CLIPBOARD_IMAGE_BMP ? release_sdl : release_png;
}
static clipboard_image_t encode (int width, int height, clipboard_pixels_t layout, int bottom_up,
    clipboard_image_format_t format) {
    char error[128];
    clipboard_image_t image;
    byte *pixels = make_pixels(width, height, layout, bottom_up);
    if (!Clipboard_EncodeImage(pixels, width, height, layout, bottom_up, format, &image, error, sizeof(error))) {
        fprintf(stderr, "encoding %dx%d failed: %s\n", width, height, error);
        abort();
    }
    free(pixels); /* the encoded image must not refer to its source */
    track(&image);
    return image;
}
static clipboard_publish_t publish (uint64_t request, clipboard_image_t *image) {
    char error[128];
    return Clipboard_PublishImage(request, image, error, sizeof(error));
}
static void check_text (const char *expected) {
    char *text = SDL_GetClipboardText();
    assert(text && !strcmp(text, expected));
    SDL_free(text);
}

static void expect_uris (const char *data, size_t size, clipboard_uri_format_t format,
    int expected_count, const char *const *expected) {
    int count;
    qboolean too_many;
    char **paths = Clipboard_ParseFileURIs(data, size, format, &count, &too_many);
    if (count != expected_count) {
        fprintf(stderr, "uri case <%.*s> (format %d): %d paths, wanted %d\n",
            (int)size, data, (int)format, count, expected_count);
        abort();
    }
    assert(!too_many && (paths != NULL) == (count > 0));
    for (int i = 0; i < count; ++i)
        assert(!strcmp(paths[i], expected[i]));
    PL_FreeClipboardFilePaths(paths, count);
    assert(z_outstanding == 0);
}
#define URIS(data, format, ...) do { \
    static const char *const expected[] = { __VA_ARGS__ }; \
    expect_uris(data, strlen(data), format, (int)(sizeof(expected) / sizeof(expected[0])), expected); \
} while (0)
#define NO_URIS(data, format) expect_uris(data, strlen(data), format, 0, NULL)

static const char *offer_uri, *offer_gnome, *offer_text;
static int uri_reads, gnome_reads, text_reads;
static const void * SDLCALL offer_data (void *userdata, const char *mime_type, size_t *size) {
    const char *text = NULL;
    (void)userdata;
    if (!strcmp(mime_type, "text/uri-list")) { text = offer_uri; ++uri_reads; }
    else if (!strcmp(mime_type, "x-special/gnome-copied-files")) { text = offer_gnome; ++gnome_reads; }
    else if (!strncmp(mime_type, "text/plain", 10)) { text = offer_text; ++text_reads; }
    *size = text ? strlen(text) : 0;
    return text;
}
static void offer (const char *uri, const char *gnome, const char *text) {
    const char *mime_types[3];
    size_t count = 0;
    if (uri) mime_types[count++] = "text/uri-list";
    if (gnome) mime_types[count++] = "x-special/gnome-copied-files";
    if (text) mime_types[count++] = "text/plain;charset=utf-8";
    offer_uri = uri;
    offer_gnome = gnome;
    offer_text = text;
    assert(SDL_SetClipboardData(offer_data, NULL, NULL, mime_types, count));
    uri_reads = gnome_reads = text_reads = 0;
}
static void expect_clipboard_files (int expected_count, const char *const *expected) {
    int count;
    char **paths = PL_GetClipboardFilePaths(&count);
    assert(count == expected_count && (paths != NULL) == (count > 0));
    for (int i = 0; i < count; ++i)
        assert(!strcmp(paths[i], expected[i]));
    PL_FreeClipboardFilePaths(paths, count);
    assert(z_outstanding == 0);
}

static scr_screenshot_job_t *new_job (scr_screenshot_format_t format, const char *path, int width, int height) {
    scr_screenshot_job_t *job = calloc(1, sizeof(*job));
    assert(job);
    job->pixels = make_pixels(width, height, CLIPBOARD_PIXELS_RGB24, 1);
    job->width = width;
    job->height = height;
    job->quality = 90;
    job->format = format;
    job->layout = CLIPBOARD_PIXELS_RGB24;
    job->bottom_up = 1;
    job->save = 1;
    q_strlcpy(job->name, path, sizeof(job->name));
    q_strlcpy(job->path, path, sizeof(job->path));
    job->clipboard_request = Clipboard_BeginImageRequest();
    return job;
}
static void process (scr_screenshot_job_t *job) {
    SCR_ImageJobProcess(job);
    assert(!job->pixels);
    if (job->clipboard.data)
        track(&job->clipboard);
}
static byte *read_file (const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    byte *data = malloc((size_t)length);
    assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return data;
}

int main (int argc, char **argv) {
    char path[1024], error[128];
    size_t size;
    int count, released;
    qboolean too_many;
    assert(argc == 2);
    const char *dir = argv[1];

    /* encoders: odd widths, both layouts, both row orders, both formats */
    static const int sizes[][2] = { {1, 1}, {2, 1}, {3, 2}, {5, 3}, {7, 4}, {64, 3}, {3, 64}, {257, 2} };
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        for (int layout = CLIPBOARD_PIXELS_RGB24; layout <= CLIPBOARD_PIXELS_BGRA32; ++layout)
            for (int bottom_up = 0; bottom_up <= 1; ++bottom_up)
                for (int format = CLIPBOARD_IMAGE_PNG; format <= CLIPBOARD_IMAGE_BMP; ++format) {
                    clipboard_image_t image = encode(sizes[s][0], sizes[s][1], (clipboard_pixels_t)layout,
                        bottom_up, (clipboard_image_format_t)format);
                    assert(image.format == (clipboard_image_format_t)format);
                    check_image(&image, sizes[s][0], sizes[s][1]);
                    Clipboard_ReleaseImage(&image);
                    assert(!image.data && !image.release);
                }
    {
        clipboard_image_t image;
        byte pixel[4] = {0};
        assert(!Clipboard_EncodeImage(NULL, 1, 1, CLIPBOARD_PIXELS_RGB24, 0, CLIPBOARD_IMAGE_PNG, &image, error, sizeof(error)));
        assert(error[0] && !image.data);
        assert(!Clipboard_EncodeImage(pixel, 0, 1, CLIPBOARD_PIXELS_RGB24, 0, CLIPBOARD_IMAGE_PNG, &image, error, sizeof(error)));
        assert(!Clipboard_EncodeImage(pixel, 1, -1, CLIPBOARD_PIXELS_RGB24, 0, CLIPBOARD_IMAGE_BMP, &image, error, sizeof(error)));
        assert(!Clipboard_EncodeImage(pixel, CLIPBOARD_MAX_IMAGE_SIDE + 1, 1, CLIPBOARD_PIXELS_RGB24, 0, CLIPBOARD_IMAGE_PNG, &image, error, sizeof(error)));
        assert(!Clipboard_EncodeImage(pixel, 1, 1, (clipboard_pixels_t)99, 0, CLIPBOARD_IMAGE_PNG, &image, error, sizeof(error)));
        assert(!Clipboard_EncodeImage(pixel, 1, 1, CLIPBOARD_PIXELS_RGB24, 0, CLIPBOARD_IMAGE_PNG, NULL, error, sizeof(error)));
    }

    /* URI lists */
    URIS("file:///tmp/a", CLIPBOARD_URI_LIST, "/tmp/a");
    URIS("# comment\r\nfile:///tmp/a%20b.dem\r\n\r\nfile://localhost/tmp/c\nFILE://LOCALHOST/tmp/d\n",
        CLIPBOARD_URI_LIST, "/tmp/a b.dem", "/tmp/c", "/tmp/d");
    URIS("  file:///tmp/spaced \t\n", CLIPBOARD_URI_LIST, "/tmp/spaced");
    URIS("file:/tmp/single", CLIPBOARD_URI_LIST, "/tmp/single");
    URIS("file:///tmp/caf\xc3\xa9.bsp\nfile:///tmp/caf%C3%A9.bsp\n", CLIPBOARD_URI_LIST, "/tmp/caf\xc3\xa9.bsp");
    URIS("file:///tmp/%e4%b8%ad%20%25.dem", CLIPBOARD_URI_LIST, "/tmp/\xe4\xb8\xad %.dem");
    URIS("sftp://host/x\nhttps://example.com/a.dem\nfile:///tmp/ok\n", CLIPBOARD_URI_LIST, "/tmp/ok");
    static const char *const rejected[] = {
        "file://server/share/x", "sftp://host/x", "https://example.com/a.dem", "file:///tmp/%zz",
        "file:///tmp/%4", "file:///tmp/%", "file:///tmp/a%00b", "file:///tmp/a?b", "file:///tmp/a#b",
        "file:///tmp/a\001b", "file://", "file:", "file:relative/x", "/tmp/plain-path",
        "file://localhost", "file://localhostx/tmp/a", "file:///tmp/a\x7f",
    };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i)
        for (int format = CLIPBOARD_URI_LIST; format <= CLIPBOARD_URI_TEXT; format += 2)
            NO_URIS(rejected[i], (clipboard_uri_format_t)format);
    URIS("copy\nfile:///tmp/a\nfile:///tmp/b", CLIPBOARD_GNOME_COPIED_FILES, "/tmp/a", "/tmp/b");
    URIS("cut\r\nfile:///tmp/a\r\n", CLIPBOARD_GNOME_COPIED_FILES, "/tmp/a");
    NO_URIS("move\nfile:///tmp/a", CLIPBOARD_GNOME_COPIED_FILES);
    NO_URIS("file:///tmp/a\nfile:///tmp/b", CLIPBOARD_GNOME_COPIED_FILES);
    NO_URIS("\ncopy\nfile:///tmp/a", CLIPBOARD_GNOME_COPIED_FILES);
    URIS("hello there\nfile:///tmp/x.dem\nnot a uri\n", CLIPBOARD_URI_TEXT, "/tmp/x.dem");
    NO_URIS("just chat text", CLIPBOARD_URI_TEXT);
    {
        static const char *const c_only[] = { "/tmp/c" }, *const a_only[] = { "/tmp/a" };
        static const char with_nul[] = "file:///tmp/a\0b\nfile:///tmp/c";
        static const char trailing_nul[] = "file:///tmp/a\n\0\0";
        expect_uris(with_nul, sizeof(with_nul) - 1, CLIPBOARD_URI_LIST, 1, c_only);
        expect_uris(trailing_nul, sizeof(trailing_nul) - 1, CLIPBOARD_URI_LIST, 1, a_only);
        char *data = malloc(CLIPBOARD_MAX_URI_BYTES + 1);
        assert(data);
        memset(data, '\n', CLIPBOARD_MAX_URI_BYTES + 1);
        memcpy(data, "file:///tmp/a", 13);
        expect_uris(data, CLIPBOARD_MAX_URI_BYTES, CLIPBOARD_URI_LIST, 1, a_only);
        expect_uris(data, CLIPBOARD_MAX_URI_BYTES + 1, CLIPBOARD_URI_LIST, 0, NULL);
        free(data);
    }
    {
        char *uri = malloc(8 + MAX_OSPATH + 2);
        assert(uri);
        memcpy(uri, "file:///", 8);
        memset(uri + 8, 'a', MAX_OSPATH - 2);
        uri[8 + MAX_OSPATH - 2] = 0; /* decodes to MAX_OSPATH - 1 bytes */
        char **paths = Clipboard_ParseFileURIs(uri, strlen(uri), CLIPBOARD_URI_LIST, &count, &too_many);
        assert(count == 1 && strlen(paths[0]) == MAX_OSPATH - 1);
        PL_FreeClipboardFilePaths(paths, count);
        uri[8 + MAX_OSPATH - 2] = 'a';
        uri[8 + MAX_OSPATH - 1] = 0;
        NO_URIS(uri, CLIPBOARD_URI_LIST);
        free(uri);
    }
    size_t many_size = (CLIPBOARD_MAX_FILES + 1) * 32;
    char *many = malloc(many_size);
    assert(many);
    many[0] = 0;
    for (int i = 0; i < CLIPBOARD_MAX_FILES; ++i)
        snprintf(many + strlen(many), many_size - strlen(many), "file:///tmp/f%d\n", i);
    char **paths = Clipboard_ParseFileURIs(many, strlen(many), CLIPBOARD_URI_LIST, &count, &too_many);
    assert(count == CLIPBOARD_MAX_FILES && !too_many && !strcmp(paths[CLIPBOARD_MAX_FILES - 1], "/tmp/f1023"));
    PL_FreeClipboardFilePaths(paths, count);
    strcat(many, "file:///tmp/one-too-many\n");
    paths = Clipboard_ParseFileURIs(many, strlen(many), CLIPBOARD_URI_LIST, &count, &too_many);
    assert(!paths && count == 0 && too_many && z_outstanding == 0);
    {
        /* long paths hit the zone budget well before the file count */
        size_t big_size = 80 * 4100;
        char *big = malloc(big_size), segment[4000];
        assert(big);
        memset(segment, 'a', sizeof(segment) - 1);
        segment[sizeof(segment) - 1] = 0;
        big[0] = 0;
        for (int i = 0; i < 60; ++i)
            snprintf(big + strlen(big), big_size - strlen(big), "file:///%d%s\n", i, segment);
        paths = Clipboard_ParseFileURIs(big, strlen(big), CLIPBOARD_URI_LIST, &count, &too_many);
        assert(count == 60 && !too_many);
        PL_FreeClipboardFilePaths(paths, count);
        for (int i = 60; i < 70; ++i)
            snprintf(big + strlen(big), big_size - strlen(big), "file:///%d%s\n", i, segment);
        paths = Clipboard_ParseFileURIs(big, strlen(big), CLIPBOARD_URI_LIST, &count, &too_many);
        assert(!paths && count == 0 && too_many && z_outstanding == 0);
        free(big);
    }
    {
        static const char alphabet[] = "file:/%0aF?#\r\n \tlocahst\x01\xc3\xa9";
        uint32_t rng = 7;
        for (int iteration = 0; iteration < 20000; ++iteration) {
            char buffer[96];
            size_t length = (size_t)(iteration % 96);
            for (size_t j = 0; j < length; ++j) {
                rng = rng * 1664525u + 1013904223u;
                buffer[j] = (rng >> 24) % 7 == 0 ? 0 : alphabet[(rng >> 16) % (sizeof(alphabet) - 1)];
            }
            if (length >= 8 && iteration % 3 == 0)
                memcpy(buffer, "file:///", 8);
            for (int format = CLIPBOARD_URI_LIST; format <= CLIPBOARD_URI_TEXT; ++format) {
                paths = Clipboard_ParseFileURIs(buffer, length, (clipboard_uri_format_t)format, &count, &too_many);
                for (int i = 0; i < count; ++i) {
                    assert(paths[i][0] == '/' && strlen(paths[i]) < MAX_OSPATH);
                    for (int k = 0; k < i; ++k)
                        assert(strcmp(paths[i], paths[k]));
                }
                PL_FreeClipboardFilePaths(paths, count);
                assert(z_outstanding == 0);
            }
        }
    }

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    clipboard_image_format_t native = Clipboard_NativeImageFormat();
    static const char *native_mimes[1], *plain_mimes[] = { "text/plain" };
    native_mimes[0] = Clipboard_ImageMimeType(native);
    clipboard_image_t image;
    uint64_t request;
    SDL_Event event;

    /* published data outlives its job and is served repeatedly */
    image = encode(5, 3, CLIPBOARD_PIXELS_RGB24, 1, native);
    request = Clipboard_BeginImageRequest();
    assert(Clipboard_PublishImage(request, &image, error, sizeof(error)) == CLIPBOARD_PUBLISHED && !image.data);
    for (int i = 0; i < 3; ++i)
        check_served(5, 3);
    assert(SDL_HasClipboardData(native_mimes[0]));
    assert(!SDL_GetClipboardData(native == CLIPBOARD_IMAGE_PNG ? "image/bmp" : "image/png", &size));
    released = released_images;
    assert(Clipboard_SetText("192.0.2.1:26000") && released_images == released + 1);
    check_text("192.0.2.1:26000");

    /* screenshot, then an address copy: the address stays */
    image = encode(3, 2, CLIPBOARD_PIXELS_BGRA32, 1, native);
    request = Clipboard_BeginImageRequest();
    assert(Clipboard_SetText("newer address"));
    assert(publish(request, &image) == CLIPBOARD_SUPERSEDED && !image.data);
    check_text("newer address");

    /* an older image cannot replace a newer one */
    clipboard_image_t older = encode(2, 2, CLIPBOARD_PIXELS_RGB24, 1, native);
    clipboard_image_t newer = encode(4, 1, CLIPBOARD_PIXELS_RGB24, 1, native);
    uint64_t older_request = Clipboard_BeginImageRequest();
    uint64_t newer_request = Clipboard_BeginImageRequest();
    assert(publish(older_request, &older) == CLIPBOARD_SUPERSEDED);
    assert(publish(newer_request, &newer) == CLIPBOARD_PUBLISHED);
    check_served(4, 1);

    /* an external change still in SDL's queue is handled before publishing */
    request = Clipboard_BeginImageRequest();
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_CLIPBOARD_UPDATE;
    event.clipboard.owner = false;
    event.clipboard.num_mime_types = 1;
    event.clipboard.mime_types = (void *)plain_mimes;
    assert(SDL_PushEvent(&event));
    image = encode(2, 3, CLIPBOARD_PIXELS_RGB24, 1, native);
    assert(publish(request, &image) == CLIPBOARD_SUPERSEDED);
    check_served(4, 1);

    /* a change queued before the request is older than it and does not cancel it */
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_CLIPBOARD_UPDATE;
    event.clipboard.owner = false;
    event.clipboard.num_mime_types = 1;
    event.clipboard.mime_types = (void *)native_mimes;
    assert(SDL_PushEvent(&event) && event.clipboard.timestamp);
    SDL_Delay(2);
    request = Clipboard_BeginImageRequest();
    image = encode(6, 2, CLIPBOARD_PIXELS_RGB24, 1, native);
    assert(publish(request, &image) == CLIPBOARD_PUBLISHED);
    check_served(6, 2);

    /* PNG screenshot: file and clipboard from one encoding */
    snprintf(path, sizeof(path), "%s/shot.png", dir);
    scr_screenshot_job_t *job = new_job(SCR_SCREENSHOT_PNG, path, 7, 3);
    process(job);
    assert(job->ok && job->clipboard.data && job->clipboard.format == native);
    byte *file = read_file(path, &size);
    check_png(file, size, 7, 3);
    if (native == CLIPBOARD_IMAGE_PNG)
        assert(size == job->clipboard.size && !memcmp(file, job->clipboard.data, size));
    free(file);
    console[0] = 0;
    SCR_ImageJobFinish(job, 1);
    assert(strstr(console, "Wrote ") && !strstr(console, "failed") && !job->clipboard.data);
    check_served(7, 3);
    free(job);

    /* a failed save still copies */
    snprintf(path, sizeof(path), "%s/missing/shot.png", dir);
    job = new_job(SCR_SCREENSHOT_PNG, path, 5, 2);
    process(job);
    assert(!job->ok && job->clipboard.data);
    console[0] = 0;
    SCR_ImageJobFinish(job, 1);
    assert(strstr(console, "Couldn't create") && !strstr(console, "clipboard copy failed"));
    check_served(5, 2);
    free(job);

    /* JPG: a separate lossless clipboard image */
    snprintf(path, sizeof(path), "%s/shot.jpg", dir);
    job = new_job(SCR_SCREENSHOT_JPG, path, 3, 5);
    process(job);
    assert(job->ok && job->clipboard.data);
    SCR_ImageJobFinish(job, 1);
    check_served(3, 5);
    free(job);

    /* a failed clipboard encoding leaves the saved file alone */
    job = new_job(SCR_SCREENSHOT_JPG, path, 4, 4);
    job->layout = (clipboard_pixels_t)99;
    process(job);
    assert(job->ok && !job->clipboard.data && job->clipboard_error[0]);
    console[0] = 0;
    SCR_ImageJobFinish(job, 1);
    assert(strstr(console, "Wrote ") && strstr(console, "Screenshot clipboard copy failed: invalid image parameters"));
    check_served(3, 5);
    free(job);

    /* a failed JPG save still copies */
    jpg_result = 0;
    job = new_job(SCR_SCREENSHOT_JPG, path, 6, 1);
    process(job);
    assert(!job->ok && job->clipboard.data);
    SCR_ImageJobFinish(job, 1);
    check_served(6, 1);
    free(job);
    jpg_result = 1;

    /* superseded while encoding: the file is kept, so is the newer text */
    snprintf(path, sizeof(path), "%s/later.png", dir);
    job = new_job(SCR_SCREENSHOT_PNG, path, 2, 2);
    process(job);
    assert(Clipboard_SetText("copied while encoding")); /* also releases the published JPG copy */
    released = released_images;
    console[0] = 0;
    developer.value = 1;
    SCR_ImageJobFinish(job, 1);
    developer.value = 0;
    assert(strstr(console, "Wrote ") && strstr(console, "Image copy skipped"));
    assert(released_images == released + 1);
    check_text("copied while encoding");
    free(job);

    /* shutting down: clipboard output is released unpublished */
    job = new_job(SCR_SCREENSHOT_PNG, path, 2, 2);
    process(job);
    released = released_images;
    SCR_ImageJobFinish(job, 0);
    assert(released_images == released + 1);
    check_text("copied while encoding");
    free(job);

    /* clipboard-only texture copy: message and sound follow publication */
    job = calloc(1, sizeof(*job));
    assert(job);
    job->pixels = make_pixels(9, 4, CLIPBOARD_PIXELS_BGRA32, 1);
    job->width = 9;
    job->height = 4;
    job->layout = CLIPBOARD_PIXELS_BGRA32;
    job->bottom_up = 1;
    q_strlcpy(job->label, "texture image: ^mwall^m (9x4)", sizeof(job->label));
    job->clipboard_request = Clipboard_BeginImageRequest();
    process(job);
    assert(!job->ok && job->clipboard.data);
    console[0] = 0;
    int sounds_before = sounds;
    SCR_ImageJobFinish(job, 1);
    assert(sounds == sounds_before + 1 && strstr(console, "copied texture image: ^mwall^m (9x4)"));
    check_served(9, 4);
    free(job);

    /* file-manager clipboard formats: first usable representation only */
    {
        static const char *const uri_files[] = { "/tmp/u1", "/tmp/u2" };
        static const char *const gnome_files[] = { "/tmp/g" };
        static const char *const text_files[] = { "/tmp/t" };
        offer("file:///tmp/u1\r\nfile:///tmp/u2\r\n", "copy\nfile:///tmp/g", "file:///tmp/t");
        expect_clipboard_files(2, uri_files);
        assert(uri_reads == 1 && gnome_reads == 0 && text_reads == 0);
        offer("sftp://host/remote.dem\n", "cut\nfile:///tmp/g", "file:///tmp/t");
        expect_clipboard_files(1, gnome_files);
        assert(uri_reads == 1 && gnome_reads == 1 && text_reads == 0);
        offer(NULL, NULL, "file:///tmp/t");
        expect_clipboard_files(1, text_files);
        assert(uri_reads == 0 && gnome_reads == 0 && text_reads == 1);
        offer(NULL, NULL, "just chat text");
        expect_clipboard_files(0, NULL);
        offer(many, NULL, "file:///tmp/t");
        console[0] = 0;
        expect_clipboard_files(0, NULL);
        assert(strstr(console, "file list is too large") && text_reads == 0);
        offer("file:///tmp/first\nfile:///tmp/second", NULL, "x");
        char *first = PL_GetClipboardFilePath();
        assert(first && !strcmp(first, "/tmp/first") && z_outstanding == 1);
        Z_Free(first);
    }
    free(many);

    /* shutdown invalidates queued work; SDL_Quit releases the published image */
    image = encode(2, 2, CLIPBOARD_PIXELS_RGB24, 1, native);
    request = Clipboard_BeginImageRequest();
    Clipboard_Shutdown();
    assert(publish(request, &image) == CLIPBOARD_SUPERSEDED);
    image = encode(2, 2, CLIPBOARD_PIXELS_RGB24, 1, native);
    assert(publish(Clipboard_BeginImageRequest(), &image) == CLIPBOARD_PUBLISHED);
    Clipboard_Shutdown();
    SDL_Quit();

    assert(released_images == made_images);
    assert(z_outstanding == 0);
    printf("clipboard layer with SDL %d.%d.%d (%s): encoders, parser, image jobs and file lists passed\n",
        SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
        SDL_VERSIONNUM_MICRO(SDL_GetVersion()), native == CLIPBOARD_IMAGE_BMP ? "BMP" : "PNG");
    return 0;
}
'''


def main():
    cc = shlex.split(os.environ.get("CC", "cc"))
    sdl_cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True))
    sdl_libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl3"], text=True))
    sanitize = ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    strict = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function"]

    with tempfile.TemporaryDirectory(prefix="qssm-clipboard-") as directory:
        work = Path(directory)
        (work / "quakedef.h").write_text(STUB_HEADER)
        # compiled from the work directory so its #include "quakedef.h" finds the stub
        (work / "clipboard_sdl.c").write_text(read("clipboard_sdl.c"))
        (work / "image_tu.c").write_text(IMAGE_TU)
        (work / "stb_tu.c").write_text(
            "#define STB_IMAGE_IMPLEMENTATION\n#define STBI_ONLY_PNG\n#define STBI_ONLY_BMP\n"
            '#include "stb_image.h"\n')
        (work / "fake.c").write_text(FAKE_TEST)
        (work / "real.c").write_text(REAL_TEST)

        def build(name, sources, libs):
            objects = []
            for source, flags in sources:
                obj = work / f"{name}-{Path(source).stem}.o"
                subprocess.run(cc + ["-c", str(work / source), "-o", str(obj), f"-I{work}", f"-I{QUAKE}"]
                               + sdl_cflags + sanitize + flags, check=True)
                objects.append(str(obj))
            binary = work / name
            subprocess.run(cc + objects + ["-o", str(binary)] + sanitize + libs, check=True)
            return binary

        sanitizer_env = dict(os.environ)
        sanitizer_env["ASAN_OPTIONS"] = ("detect_stack_use_after_return=1:"
                                         + os.environ.get("ASAN_OPTIONS", ""))

        fake = build("fake", [("fake.c", strict), ("clipboard_sdl.c", strict)], [])
        subprocess.run([str(fake)], check=True, env=sanitizer_env)

        # stb_image uses pow(); Linux requires an explicit math library link.
        real = build("real", [("real.c", strict), ("clipboard_sdl.c", strict),
                              ("image_tu.c", ["-w"]), ("stb_tu.c", ["-w"])], sdl_libs + ["-lm"])
        env = dict(sanitizer_env, SDL_VIDEO_DRIVER="dummy")
        # SDL's own teardown is outside this test; ownership is checked by counters
        env["ASAN_OPTIONS"] = "detect_leaks=0:" + env["ASAN_OPTIONS"]
        subprocess.run([str(real), str(work)], check=True, env=env)


if __name__ == "__main__":
    main()
