"""Exercise production SDL3 video helpers with simulated monitors/window state.

Run with python3 Misc/stress/test_sdl3_video.py [--sanitize]. Requires cc and
SDL3 headers via pkg-config; no display server or physical monitors are needed.
"""

from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "Quake/gl_vidsdl.c").read_text()


def function(declaration):
    start = source.index(declaration + "\n{")
    return source[start:source.index("\n}", start) + 2] + "\n"


constants = "\n".join(line for line in source.splitlines() if line.startswith((
    "#define MAX_MODE_LIST", "#define MAX_BPPS_LIST", "#define MAX_RATES_LIST",
    "#define DEFAULT_REFRESHRATE")))
mode_type = source[source.index("typedef struct {"):source.index("static const char *gl_vendor;")]
menu_start = source.index("typedef struct {\n\tint width,height;")
menu_state = source[menu_start:source.index("\n/*", menu_start)]

common = r'''
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
typedef struct { float value; } cvar_t;
static cvar_t vid_width = {2560}, vid_height = {1440}, vid_bpp = {32};
static cvar_t vid_refreshrate = {144}, vid_desktopfullscreen;
static qboolean vid_changed;
static struct { int refreshrate; } vid;
static SDL_Window *draw_context;
static SDL_WindowFlags window_flags;
static SDL_DisplayID window_display = 11, primary_display = 11, queried_display;
static SDL_DisplayMode primary_modes[2], secondary_modes[MAX_MODE_LIST + 5];
static int secondary_count = 2, enumeration_failure, current_mode_failure;
static int queries, cvar_writes;

SDL_DisplayID SDL_GetPrimaryDisplay(void) { return primary_display; }
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *window)
{
    assert(window && window == draw_context);
    return window_display;
}
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *window)
{
    assert(window && window == draw_context);
    return window_flags;
}
const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID display)
{
    if (current_mode_failure || !display)
        return NULL;
    return display == 11 ? primary_modes : secondary_modes;
}
SDL_DisplayMode **SDL_GetFullscreenDisplayModes(SDL_DisplayID display, int *count)
{
    SDL_DisplayMode **result;
    queried_display = display;
    queries++;
    *count = 0;
    if (enumeration_failure || !display)
        return NULL;
    *count = display == 11 ? 2 : secondary_count;
    result = calloc(*count + 1, sizeof(*result));
    assert(result);
    for (int i = 0; i < *count; i++)
        result[i] = (display == 11 ? primary_modes : secondary_modes) + i;
    return result;
}
void SDL_free(void *ptr) { free(ptr); }

static void Cvar_SetValueQuick(cvar_t *cvar, float value)
{
    cvar->value = value;
    cvar_writes++;
    vid_changed = true;
}
static void Cvar_SetValue(const char *name, float value)
{
    assert(!strcmp(name, "vid_refreshrate"));
    Cvar_SetValueQuick(&vid_refreshrate, value);
}
'''

production = "\n".join(function(declaration) for declaration in (
    "static int VID_RefreshRateHz (const SDL_DisplayMode *mode)",
    "static SDL_DisplayID VID_GetDisplay (void)",
    "static int VID_GetCurrentRefreshRate (void)",
    "qboolean VID_IsMinimized (void)",
    "static SDL_DisplayMode *VID_GetDisplayModeForDisplay(SDL_DisplayID display, int width, int height, int refreshrate, int bpp)",
    "static SDL_DisplayMode *VID_GetDisplayMode(int width, int height, int refreshrate, int bpp)",
    "static qboolean VID_ValidMode (int width, int height, int refreshrate, int bpp, qboolean fullscreen)",
    "static int VID_ModeAbsDiff (int a, int b)",
    "static qboolean VID_FindClosestFullscreenMode (int width, int height, int refreshrate, int bpp, vmode_t *bestmode)",
    "static void VID_InitModelist (void)",
    "static void VID_Menu_Init (void)",
    "static void VID_Menu_RebuildBppList (qboolean update_cvars)",
    "static void VID_Menu_RebuildRateList (qboolean update_cvars)",
    "void VID_OnDisplayChange (void)",
    "static void VID_Menu_ChooseNextMode (int dir)",
    "static void VID_Menu_ChooseNextBpp (int dir)",
    "static void VID_Menu_ChooseNextRate (int dir)",
))

checks = r'''
static SDL_DisplayMode mode(SDL_DisplayID display, int w, int h, float rate)
{
    SDL_DisplayMode result = {0};
    result.displayID = display;
    result.format = SDL_PIXELFORMAT_ARGB8888;
    result.w = w;
    result.h = h;
    result.refresh_rate = rate;
    result.pixel_density = 1;
    return result;
}

int main(void)
{
    vmode_t closest;
    primary_modes[0] = mode(11, 1920, 1080, 60);
    primary_modes[1] = mode(11, 1280, 720, 60);
    secondary_modes[0] = mode(73, 2560, 1440, 143.98f);
    secondary_modes[1] = mode(73, 2560, 1440, 120);

    /* Startup and failed window queries fall back to the primary monitor. */
    assert(VID_GetDisplay() == 11);
    VID_OnDisplayChange();
    assert(queries == 0);
    assert(VID_ValidMode(1920, 1080, 60, 32, true));
    assert(!VID_ValidMode(2560, 1440, 144, 32, true));
    draw_context = (SDL_Window *)&window_flags;
    window_display = 0;
    assert(VID_GetDisplay() == 11);

    /* Minimized and hidden are independent SDL flags. */
    window_flags = SDL_WINDOW_MINIMIZED;
    assert(VID_IsMinimized());
    window_flags = SDL_WINDOW_HIDDEN;
    assert(VID_IsMinimized());
    window_flags |= SDL_WINDOW_MINIMIZED;
    assert(VID_IsMinimized());
    window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_MAXIMIZED;
    assert(!VID_IsMinimized());
    window_flags = 0;
    assert(!VID_IsMinimized());

    /* The same request becomes valid after moving to a 144 Hz secondary. */
    window_display = 73;
    assert(VID_ValidMode(2560, 1440, 144, 32, true));
    assert(queried_display == 73);
    assert(!VID_ValidMode(1920, 1080, 60, 32, true));
    assert(VID_ValidMode(800, 600, 60, 32, false));
    assert(!VID_ValidMode(200, 100, 60, 32, false));
    vid_desktopfullscreen.value = 1;
    assert(VID_ValidMode(800, 600, 60, 32, true));
    vid_desktopfullscreen.value = 0;

    /* Refresh live pacing and choices while preserving pending settings. */
    VID_OnDisplayChange();
    assert(vid.refreshrate == 144 && nummodes == 2);
    assert(vid_menu_nummodes == 1 && vid_menu_modes[0].width == 2560);
    assert(vid_menu_numbpps == 1 && vid_menu_numrates == 2);
    assert(cvar_writes == 0 && !vid_changed);
    assert(VID_FindClosestFullscreenMode(2500, 1400, 143, 32, &closest));
    assert(closest.width == 2560 && closest.height == 1440 && closest.refreshrate == 144);
    vid_changed = true;
    secondary_modes[0].refresh_rate = 165;
    VID_OnDisplayChange();
    assert(vid.refreshrate == 165 && vid_changed && cvar_writes == 0);
    assert(vid_refreshrate.value == 144 && vid_menu_rates[0] == 165);

    /* Repeated monitor moves replace old resolutions instead of appending. */
    for (int i = 0; i < MAX_MODE_LIST + 1; i++)
    {
        window_display = 11;
        VID_OnDisplayChange();
        assert(vid.refreshrate == 60 && vid_menu_nummodes == 2);
        assert(vid_menu_modes[0].width == 1920);
        assert(vid_menu_numbpps == 0 && vid_menu_numrates == 0);
        assert(VID_FindClosestFullscreenMode(2500, 1400, 144, 32, &closest));
        assert(closest.width == 1920 && closest.refreshrate == 60);
        window_display = 73;
        VID_OnDisplayChange();
        assert(vid_menu_nummodes == 1 && vid_menu_modes[0].width == 2560);
    }
    assert(cvar_writes == 0);

    /* Unplug the secondary; unknown or unavailable rates keep safe pacing. */
    window_display = 0;
    VID_OnDisplayChange();
    assert(vid.refreshrate == 60 && queried_display == 11);
    current_mode_failure = 1;
    assert(VID_GetCurrentRefreshRate() == 60);
    current_mode_failure = 0;
    primary_modes[0].refresh_rate = 0;
    assert(VID_GetCurrentRefreshRate() == 60);

    /* SDL can return no exclusive modes or fail while monitors disappear. */
    window_display = 73;
    for (int fail = 0; fail < 2; fail++)
    {
        secondary_count = 0;
        enumeration_failure = fail;
        VID_OnDisplayChange();
        assert(!nummodes && !vid_menu_nummodes && !vid_menu_numrates && !vid_menu_numbpps);
        assert(!VID_FindClosestFullscreenMode(800, 600, 60, 32, &closest));
        VID_Menu_RebuildBppList(true);
        VID_Menu_RebuildRateList(true);
        VID_Menu_ChooseNextMode(1);
        VID_Menu_ChooseNextBpp(1);
        VID_Menu_ChooseNextRate(1);
        assert(cvar_writes == 0 && vid_bpp.value == 32 && vid_refreshrate.value == 144);
    }
    enumeration_failure = 0;

    /* Changing depth after a monitor move must populate its refresh choices. */
    secondary_count = 2;
    secondary_modes[0] = mode(73, 2560, 1440, 144);
    secondary_modes[0].format = SDL_PIXELFORMAT_XRGB8888; /* 24 color bits */
    secondary_modes[1] = mode(73, 2560, 1440, 120);
    secondary_modes[1].format = SDL_PIXELFORMAT_XRGB8888;
    VID_OnDisplayChange();
    assert(vid_bpp.value == 32 && vid_menu_numrates == 0);
    VID_Menu_ChooseNextBpp(1);
    assert(vid_bpp.value == 24 && vid_menu_numrates == 2);
    VID_Menu_ChooseNextRate(1);
    assert(vid_refreshrate.value == 120);

    /* Resizing/console edits after a display event must not use stale choices. */
    secondary_count = 3;
    secondary_modes[2] = mode(73, 1920, 1080, 60);
    VID_OnDisplayChange();
    vid_width.value = 1920;
    vid_height.value = 1080;
    VID_Menu_ChooseNextBpp(1);
    assert(vid_bpp.value == 32 && vid_refreshrate.value == 60);
    vid_width.value = 2560;
    vid_height.value = 1440;
    vid_bpp.value = 24;
    VID_Menu_ChooseNextRate(1);
    assert(vid_refreshrate.value == 144);
    vid_bpp.value = 32;
    cvar_writes = 0;

    /* More than 20 rates must retain a valid selected rate at the list's end. */
    secondary_count = 25;
    assert(secondary_count <= (int)(sizeof(secondary_modes) / sizeof(secondary_modes[0])));
    for (int i = 0; i < secondary_count; i++)
        secondary_modes[i] = mode(73, 2560, 1440, 240 - i);
    vid_refreshrate.value = 240 - (secondary_count - 1);
    VID_OnDisplayChange();
    assert(vid_menu_numrates == secondary_count && cvar_writes == 0);
    VID_Menu_RebuildRateList(true);
    assert(cvar_writes == 0); /* Opening the menu must not silently change it. */
    VID_Menu_ChooseNextRate(1);
    assert(vid_refreshrate.value == 240);
    VID_Menu_ChooseNextRate(-1);
    assert(vid_refreshrate.value == 240 - (secondary_count - 1));

    /* Exercise both menu arrays at the engine's maximum accepted mode count. */
    secondary_count = MAX_MODE_LIST + 5;
    for (int i = 0; i < secondary_count; i++)
        secondary_modes[i] = mode(73, 1024 + i, 768, 144);
    VID_OnDisplayChange();
    assert(nummodes == MAX_MODE_LIST && vid_menu_nummodes == MAX_MODE_LIST);
    vid_width.value = 1024;
    vid_height.value = 768;
    VID_Menu_ChooseNextMode(-1);
    assert(vid_width.value == 1024 + (MAX_MODE_LIST - 1));
    VID_Menu_ChooseNextMode(1);
    assert(vid_width.value == 1024);

    for (int i = 0; i < secondary_count; i++)
        secondary_modes[i] = mode(73, 1024, 768, 1000 - i);
    VID_OnDisplayChange();
    assert(nummodes == MAX_MODE_LIST && vid_menu_numrates == MAX_MODE_LIST);
    vid_refreshrate.value = 1000;
    VID_Menu_ChooseNextRate(-1);
    assert(vid_refreshrate.value == 1000 - (MAX_MODE_LIST - 1));
    VID_Menu_ChooseNextRate(1);
    assert(vid_refreshrate.value == 1000);

    puts("SDL3 video: PASS (minimize, secondary modes, monitor changes, pacing, pending settings, empty/bounded menus)");
    return 0;
}
'''


def main():
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True))
    if "--sanitize" in sys.argv:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                  "-fno-omit-frame-pointer", "-g"]
    with tempfile.TemporaryDirectory(prefix="qssm-sdl3-video-") as tmp:
        path = Path(tmp)
        test_source = path / "video.c"
        test_source.write_text(constants + "\n" + common + mode_type
                               + "static vmode_t modelist[MAX_MODE_LIST];\nstatic int nummodes;\n"
                               + menu_state + production + checks)
        binary = path / "video"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
                        "-Werror", "-Wno-sign-compare", *flags, str(test_source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
