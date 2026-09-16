"""Exercise production mouse routing with SDL/HID and QuakeC test doubles.

Checks movement counts, source selection and input consumption, not physical
input latency or the real HID/SDL scale relationship.
Run with: python3 Misc/stress/test_mouse_sources.py
"""

from pathlib import Path
import os
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
production = (ROOT / "Quake/in_sdl.c").read_text()


def function(declaration):
    start = production.index(declaration)
    return production[start:production.index("\n}", start) + 2] + "\n"


def region(start_marker, end_marker):
    start = production.index(start_marker)
    return production[start:production.index(end_marker, start)] + "\n"


source = r'''
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef enum { false, true } qboolean;
typedef struct { int unused; } usercmd_t;
typedef struct cvar_s { const char *name, *string; int flags; float value; } cvar_t;
enum { CVAR_NONE };
enum { key_game, key_console, key_menu, key_message } key_dest;
enum { ca_disconnected, ca_connected, SIGNONS = 4, YAW = 1, PITCH = 0 };
enum { OFS_PARM0, OFS_PARM1, OFS_PARM2, OFS_PARM3, OFS_RETURN };
enum { CSIE_MOUSEABS = 1, CSIE_MOUSEDELTA = 2 };
typedef struct {
    qboolean cursorforced;
    struct { int Menu_InputEvent, CSQC_InputEvent; } extfuncs;
} qcvm_t;
struct {
    int state, signon;
    qboolean demoplayback;
    qcvm_t menu_qcvm;
} cls;
struct {
    int modtype, eyecam, fullpitch;
    qboolean paused;
    float match_pause_time, viewangles[3], accummoves[3], csqc_sensitivity;
    qcvm_t qcvm;
} cl;
struct { float cursorpos[2]; } vid;
struct { float basefov; } r_refdef;
struct { float value; } sensitivity, scr_fov, scr_menuscale, scr_sbarscale,
    m_side, m_yaw, m_pitch, m_forward, lookstrafe, cl_maxpitch, cl_minpitch;
struct { int state; } in_strafe, in_mlook;
cvar_t in_disablemacosxmouseaccel = {"in_disablemacosxmouseaccel", "2", CVAR_NONE, 2};
qboolean windowhasfocus, no_mouse, hid_mouse_active, noclip_anglehack;
qboolean consume, wheel_open, hid_mouse_init_failed;
const char *hid_init_stage;
unsigned int hid_open_result;
int hid_absolute_values, hid_mouse_mutex;
enum { kIOReturnNotPermitted = 1, kIOHIDRequestTypeListenEvent,
       kIOHIDAccessTypeGranted, kIOHIDAccessTypeDenied };
typedef int IOHIDAccessType;
int input_access;
int hid_mouse_x, hid_mouse_y;
typedef int IOReturn;
typedef struct { uint32_t page, usage; qboolean relative; } hid_element_t;
typedef hid_element_t *IOHIDElementRef;
typedef struct { hid_element_t element; int value; } hid_value_t;
typedef hid_value_t *IOHIDValueRef;
enum { kHIDPage_GenericDesktop = 1, kHIDUsage_GD_X = 48, kHIDUsage_GD_Y = 49 };
IOHIDElementRef IOHIDValueGetElement(IOHIDValueRef v) { return &v->element; }
uint32_t IOHIDElementGetUsagePage(IOHIDElementRef e) { return e->page; }
uint32_t IOHIDElementGetUsage(IOHIDElementRef e) { return e->usage; }
int IOHIDValueGetIntegerValue(IOHIDValueRef v) { return v->value; }
qboolean IOHIDElementIsRelative(IOHIDElementRef e) { return e->relative; }
int IOHIDCheckAccess(int request) { (void)request; return input_access; }
void SDL_LockMutex(int mutex) { (void)mutex; }
void SDL_UnlockMutex(int mutex) { (void)mutex; }
qboolean HID_MouseInit(void) { return false; }
#define Con_DPrintf Con_Printf
int hid_dx_pending, hid_dy_pending;
int glwidth = 640, glheight = 480, qc_calls, host_framecount;
double realtime;
float globals[5], vectors[5][3], wheel_dx, wheel_dy;
char printed[4096];
qcvm_t *qcvm;
#define q_min(a, b) ((a) < (b) ? (a) : (b))
#define q_snprintf snprintf
#define CLAMP(a, b, c) ((b) < (a) ? (a) : (b) > (c) ? (c) : (b))
#define DEG2RAD(x) ((x) * 0.017453292519943295)
#define G_FLOAT(x) globals[x]
#define G_VECTORSET(i, x, y, z) do { \
    vectors[i][0] = (x); vectors[i][1] = (y); vectors[i][2] = (z); \
} while (0)
void Con_Printf(const char *fmt, ...) {
    size_t used = strlen(printed);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(printed + used, sizeof(printed) - used, fmt, ap);
    va_end(ap);
}
void PR_SwitchQCVM(qcvm_t *vm) { assert(!vm || !qcvm); qcvm = vm; }
void PR_ExecuteProgram(int fn) {
    assert(qcvm && fn);
    ++qc_calls;
    globals[OFS_RETURN] = consume;
}
qboolean Pong_Enabled(void) { return false; }
void Pong_MouseMove(int x, int y) { (void)x; (void)y; }
void SDL_GetMouseState(float *x, float *y) { *x = 0; *y = 0; }
void V_StopPitchDrift(void) {}
qboolean Wheel_IsOpen(void) { return wheel_open; }
void Wheel_UpdateMouse(float x, float y) { wheel_dx += x; wheel_dy += y; }
void HID_MouseGetMovement(int *x, int *y) {
    *x = hid_dx_pending; *y = hid_dy_pending;
    hid_dx_pending = hid_dy_pending = 0;
}
'''
source += re.search(r"^static \w+\s+total_dx, total_dy = 0;$", production, re.M).group(0) + "\n"
source += "#ifdef __APPLE__\n" + function("static void HID_InputCallback(") + "#endif\n"
source += region("extern cvar_t in_disablemacosxmouseaccel;",
                 "#endif /* HID mouse routing */")
source += function("static void IN_ApplyMouseMotion(float")
source += function("void IN_MouseMotion(float")
source += function("void IN_MouseMove(usercmd_t *cmd)")

source += "#ifdef __APPLE__\n" + function("static void IN_MouseAccel_Changed (cvar_t *var)")
mac_info = region("\tqboolean hid_selected =", "#elif defined(_WIN32)")
source += "void print_mac_info(void) {\n" + mac_info + "}\n#endif\n"

# Include the production discard operation that runs even without IN_Move.
discard = production.split(
    "\t/* IN_Move is not called while disconnected.", 1)[1]
discard = discard[discard.index("\tif (hid_mouse_active"):discard.index("#endif")]
source += "void discard_inactive(void) {\n" + discard + "}\n"
source += r'''
static void reset(void) {
    memset(&cls, 0, sizeof(cls)); memset(&cl, 0, sizeof(cl));
    memset(&vid, 0, sizeof(vid)); memset(globals, 0, sizeof(globals));
    cls.state = ca_connected; cls.signon = SIGNONS;
    key_dest = key_game; windowhasfocus = true;
    no_mouse = false; hid_mouse_active = true; consume = wheel_open = false;
    total_dx = total_dy = hid_dx_pending = hid_dy_pending = qc_calls = 0;
    wheel_dx = wheel_dy = 0;
    r_refdef.basefov = scr_fov.value = 90;
    sensitivity.value = m_yaw.value = m_pitch.value = cl.csqc_sensitivity = 1;
    in_strafe.state = 0; in_mlook.state = 1;
    scr_menuscale.value = scr_sbarscale.value = 1;
    in_disablemacosxmouseaccel.value = 2;
    realtime = 100; host_framecount = 1000; printed[0] = 0;
    hid_mouse_init_failed = false; hid_init_stage = NULL; hid_open_result = 0;
    hid_absolute_values = 0; input_access = kIOHIDAccessTypeGranted;
#ifdef __APPLE__
    in_mousesources.value = 0;
    sdl_shadow_dx = sdl_shadow_dy = 0;
    memset(mouse_source_samples, 0, sizeof(mouse_source_samples));
    memset(&mousestroke, 0, sizeof(mousestroke));
#endif
    assert(!qcvm);
}
static void frame(double seconds) { realtime += seconds; host_framecount++; }
static void move(int x, int y) {
    usercmd_t cmd = {0};
    hid_dx_pending += x; hid_dy_pending += y;
    IN_MouseMove(&cmd);
}
int main(void) {
    reset();
    IN_MouseMotion(30, 12, 100, 200); // SDL's version of the same movement
    move(10, 4);                    // unaccelerated HID report
#ifdef __APPLE__
    assert(cl.viewangles[YAW] == -10 && cl.viewangles[PITCH] == 4);
#else
    assert(cl.viewangles[YAW] == -30 && cl.viewangles[PITCH] == 12);
#endif
    assert(vid.cursorpos[0] == 100 && vid.cursorpos[1] == 200);
    float yaw = cl.viewangles[YAW];
    move(0, 0); // a second IN_Move during command sending must not replay it
    assert(cl.viewangles[YAW] == yaw);

    // Ordinary input retains the engine's zero-motion QC event behavior.
    reset(); hid_mouse_active = false;
    cl.qcvm.extfuncs.CSQC_InputEvent = 1;
    IN_MouseMotion(0, 0, 50, 60);
    assert(qc_calls == 1);

    reset(); hid_mouse_active = false;
    for (int i = 0; i < 4; ++i)
        IN_MouseMotion(0.25f, -0.25f, 100.5f, 200.25f);
    move(0, 0);
    assert(cl.viewangles[YAW] == -1 && cl.viewangles[PITCH] == -1);
    assert(vid.cursorpos[0] == 100.5f && vid.cursorpos[1] == 200.25f);

    reset(); hid_mouse_active = false;
    IN_MouseMotion(9, 2, 50, 60); move(0, 0);
    assert(cl.viewangles[YAW] == -9 && cl.viewangles[PITCH] == 2);

#ifdef __APPLE__
    // SDL3 fractions must survive each diagnostic stage, including idle samples.
    reset(); in_mousesources.value = 1;
    for (int i = 0; i < 4; ++i) {
        IN_MouseMotion(0.25f, 0, 100.5f, 200.25f); move(5, 0); frame(0.01);
    }
    IN_MouseMotion(0.25f, 0, 100.5f, 200.25f); move(0, 0);
    assert(mouse_source_samples[MOUSE_SOURCE_SDL_DISCARDED] == 1);
    frame(0.2); move(0, 0);
    assert(strstr(printed, "hid 20,0 sdl 1.25,0 sdl/hid 0.062"));
    assert(cl.viewangles[YAW] == -20);
    assert(vid.cursorpos[0] == 100.5f && vid.cursorpos[1] == 200.25f);

    // An absolute position must not become an enormous relative turn.
    reset(); hid_mouse_mutex = 1; hid_mouse_x = hid_mouse_y = 0;
    hid_value_t report = {{kHIDPage_GenericDesktop, kHIDUsage_GD_X, false}, 12000};
    HID_InputCallback(NULL, 0, NULL, &report);
    assert(hid_mouse_x == 0 && hid_mouse_y == 0 && hid_absolute_values == 1);
    report.element.relative = true; report.value = 4;
    HID_InputCallback(NULL, 0, NULL, &report);
    report.element.usage = kHIDUsage_GD_Y; report.value = -3;
    HID_InputCallback(NULL, 0, NULL, &report);
    assert(hid_mouse_x == 4 && hid_mouse_y == -3);
    report.element.page = 99; report.value = 12000;
    HID_InputCallback(NULL, 0, NULL, &report);
    assert(hid_mouse_x == 4 && hid_mouse_y == -3);

    // SDL arriving first must not count again when HID arrives next frame.
    reset();
    IN_MouseMotion(30, 12, 0, 0); move(0, 0);
    frame(0.004); move(10, 4);
    assert(cl.viewangles[YAW] == -10 && cl.viewangles[PITCH] == 4);

    // SDL's late copy of motion HID already delivered is dropped.
    reset();
    IN_MouseMotion(30, 12, 0, 0); move(10, 4);
    frame(0.01); IN_MouseMotion(3, 1, 0, 0); move(0, 0);
    assert(cl.viewangles[YAW] == -10 && cl.viewangles[PITCH] == 4);
    assert(mouse_source_samples[MOUSE_SOURCE_SDL_DISCARDED] == 1);

    // A long hitch is one frame: SDL events delayed by it are still copies.
    reset();
    move(10, 4); frame(1.0);
    IN_MouseMotion(3, 1, 0, 0); move(0, 0);
    assert(cl.viewangles[YAW] == -10);

    // Idle time cannot change the selected backend or admit a late copy.
    reset();
    move(10, 4);
    frame(0.1); move(0, 0); frame(0.1); move(0, 0); frame(0.1);
    IN_MouseMotion(30, 12, 0, 0); move(0, 0);
    assert(cl.viewangles[YAW] == -10 && cl.viewangles[PITCH] == 4);

    // Explicit ordinary mode accepts trackpad motion immediately after HID.
    in_disablemacosxmouseaccel.value = 0;
    frame(0.001); IN_MouseMotion(6, 2, 0, 0); move(0, 0);
    assert(cl.viewangles[YAW] == -16 && cl.viewangles[PITCH] == 6);

    // in_disablemacosxmouseaccel 0/1 selects SDL even while HID runs.
    reset(); in_disablemacosxmouseaccel.value = 1;
    IN_MouseMotion(9, 2, 0, 0); move(50, 20);
    assert(cl.viewangles[YAW] == -9 && cl.viewangles[PITCH] == 2);
    assert(hid_dx_pending == 0 && hid_dy_pending == 0);

    // A mode change must not replay motion pending in either old backend.
    reset(); in_disablemacosxmouseaccel.value = 0;
    IN_MouseMotion(30, 12, 0, 0);
    hid_dx_pending = 10; hid_dy_pending = 4;
    in_disablemacosxmouseaccel.value = 2;
    IN_MouseAccel_Changed(&in_disablemacosxmouseaccel);
    move(0, 0);
    assert(cl.viewangles[YAW] == 0 && cl.viewangles[PITCH] == 0);

    reset();
    IN_MouseMotion(30, 12, 0, 0);
    hid_dx_pending = 10; hid_dy_pending = 4;
    in_disablemacosxmouseaccel.value = 0;
    IN_MouseAccel_Changed(&in_disablemacosxmouseaccel);
    IN_MouseMotion(6, 2, 0, 0); move(0, 0);
    assert(cl.viewangles[YAW] == -6 && cl.viewangles[PITCH] == 2);
    assert(!sdl_shadow_dx && !sdl_shadow_dy);

    // Failed raw input does not claim that system acceleration is on or off.
    reset(); hid_mouse_active = false; input_access = kIOHIDAccessTypeDenied;
    print_mac_info();
    assert(strstr(printed, "System settings (not measured)"));
    assert(!strstr(printed, "Bypassed"));

    // Starting HID is not evidence of receiving any relative motion.
    reset(); print_mac_info();
    assert(strstr(printed, "No relative HID aim motion observed"));
    printed[0] = 0; move(10, 4); print_mac_info();
    assert(!strstr(printed, "No relative HID aim motion observed"));

    // Held-back SDL motion does not survive leaving gameplay.
    reset();
    IN_MouseMotion(30, 12, 0, 0);
    key_dest = key_console; discard_inactive();
    key_dest = key_game; move(0, 0);
    assert(cl.viewangles[YAW] == 0 && cl.viewangles[PITCH] == 0);

    // in_mousesources reports both sources for a stroke once motion stops.
    reset(); in_mousesources.value = 1;
    IN_MouseMotion(20, 0, 0, 0); move(10, 0);
    frame(0.05); IN_MouseMotion(20, 0, 0, 0); move(10, 0);
    frame(0.05); move(0, 0);
    assert(!printed[0]); // gap not reached yet
    frame(0.2); move(0, 0);
    assert(strstr(printed, "hid 20,0 sdl 40,0 sdl/hid 2.000 (0.05s, ~400 hid counts/s)"));
    assert(cl.viewangles[YAW] == -20);
    // tiny strokes and a stroke cut short by leaving gameplay
    printed[0] = 0;
    IN_MouseMotion(2, 0, 0, 0); move(1, 0); frame(0.3); move(0, 0);
    assert(!printed[0]);
    frame(0.01); IN_MouseMotion(30, 0, 0, 0); move(30, 0);
    key_dest = key_console; move(0, 0);
    assert(strstr(printed, "hid 30,0 sdl 30,0 sdl/hid 1.000"));

    reset(); cl.qcvm.extfuncs.CSQC_InputEvent = 1; consume = true;
    IN_MouseMotion(30, 12, 100, 200);
    assert(qc_calls == 0); // suppress duplicate relative notifications, too
    move(10, 4);
    assert(qc_calls == 1 && globals[OFS_PARM0] == CSIE_MOUSEDELTA);
    assert(vectors[OFS_PARM1][0] == 10 && vectors[OFS_PARM1][1] == 4);
    assert(cl.viewangles[YAW] == 0 && cl.viewangles[PITCH] == 0 && !qcvm);
    consume = false; move(5, 2);
    assert(qc_calls == 2 && cl.viewangles[YAW] == -5);

    reset(); cl.qcvm.extfuncs.CSQC_InputEvent = 1; cl.qcvm.cursorforced = true;
    IN_MouseMotion(30, 12, 100, 200); move(10, 4);
    assert(qc_calls == 1 && globals[OFS_PARM0] == CSIE_MOUSEABS);
    assert(vectors[OFS_PARM1][0] == 100 && vectors[OFS_PARM2][0] == 200);
    assert(cl.viewangles[YAW] == 0 && cl.viewangles[PITCH] == 0);

    reset(); key_dest = key_menu; cls.menu_qcvm.extfuncs.Menu_InputEvent = 1;
    IN_MouseMotion(8, 3, 100, 200); move(10, 4);
    assert(qc_calls == 1 && globals[OFS_PARM0] == CSIE_MOUSEDELTA);
    assert(vectors[OFS_PARM1][0] == 8 && cl.viewangles[YAW] == 0);
    key_dest = key_game; move(0, 0);
    assert(cl.viewangles[YAW] == 0); // no accumulated menu motion

    reset(); windowhasfocus = false;
    IN_MouseMotion(30, 12, 100, 200); move(10, 4);
    assert(cl.viewangles[YAW] == 0 && cl.viewangles[PITCH] == 0);
    windowhasfocus = true; move(0, 0);
    assert(cl.viewangles[YAW] == 0);

    reset(); cls.state = ca_disconnected; cls.signon = 0;
    hid_dx_pending = 500; hid_dy_pending = 200;
    discard_inactive(); // the disconnected host does not call IN_Move
    assert(hid_dx_pending == 0 && hid_dy_pending == 0);
    cls.state = ca_connected; cls.signon = SIGNONS; move(0, 0);
    assert(cl.viewangles[YAW] == 0);

    reset(); wheel_open = true;
    IN_MouseMotion(30, 12, 100, 200); move(10, 4);
    assert(wheel_dx == 10 && wheel_dy == 4 && cl.viewangles[YAW] == 0);

    reset(); cl.paused = true; move(10, 4);
    cl.paused = false; move(0, 0);
    assert(cl.viewangles[YAW] == 0);
    reset(); cls.demoplayback = true; move(10, 4);
    assert(cl.viewangles[YAW] == 0);
    reset(); cl.modtype = 1; cl.eyecam = 1; move(10, 4);
    assert(cl.viewangles[YAW] == 0);
#endif
    puts("mouse source routing: PASS");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-mouse-sources-") as tmp:
    path = Path(tmp)
    (path / "mouse.c").write_text(source)
    for platform, flag in (("macOS HID", "-D__APPLE__"), ("SDL fallback", "-U__APPLE__")):
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2", flag,
                        "-fsanitize=undefined,address", "-Wno-unused-function",
                        str(path / "mouse.c"), "-lm",
                        "-o", str(path / "mouse")], check=True)
        print(platform, flush=True)
        subprocess.run([str(path / "mouse")], check=True)
