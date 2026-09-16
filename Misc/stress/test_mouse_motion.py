"""Exercise production mouse motion accumulation with engine test doubles.

Feeds IN_MouseMotion directly, bypassing OS acceleration, and checks that
fractional deltas survive into the view angles. Checks movement math and
input consumption, not physical input latency.
Run with: python3 Misc/stress/test_mouse_motion.py
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


# Track the production accumulator type rather than restating it here.
accumulators = re.search(r"^static \w+\s+total_dx, total_dy = 0;$",
                         production, re.M).group(0)

source = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef enum { false, true } qboolean;
typedef struct { int unused; } usercmd_t;
enum { key_game, key_console, key_menu, key_message } key_dest;
enum { ca_disconnected, ca_connected, SIGNONS = 4, YAW = 1, PITCH = 0 };
enum { OFS_PARM0, OFS_PARM1, OFS_PARM2, OFS_PARM3, OFS_RETURN };
enum { CSIE_MOUSEDELTA = 2, CSIE_MOUSEABS = 3 };
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
qboolean windowhasfocus, hid_mouse_active, noclip_anglehack, consume, no_mouse;
struct { float value; } in_disablemacosxmouseaccel;
int glwidth = 640, glheight = 480, qc_calls, pong_x, pong_y, hid_dx_pending, hid_dy_pending;
float globals[5], vectors[5][3];
qcvm_t *qcvm;
#define q_min(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(a, b, c) ((b) < (a) ? (a) : (b) > (c) ? (c) : (b))
#define DEG2RAD(x) ((x) * 0.017453292519943295)
#define G_FLOAT(x) globals[x]
#define G_VECTORSET(i, x, y, z) do { \
    vectors[i][0] = (x); vectors[i][1] = (y); vectors[i][2] = (z); \
} while (0)
void PR_SwitchQCVM(qcvm_t *vm) { assert(!vm || !qcvm); qcvm = vm; }
void PR_ExecuteProgram(int fn) {
    assert(qcvm && fn);
    ++qc_calls;
    globals[OFS_RETURN] = consume;
}
qboolean Pong_Enabled(void) { return false; }
void Pong_MouseMove(int x, int y) { pong_x = x; pong_y = y; }
void SDL_GetMouseState(float *x, float *y) { *x = 0; *y = 0; }
void V_StopPitchDrift(void) {}
qboolean Wheel_IsOpen(void) { return false; }
void Wheel_UpdateMouse(float x, float y) { (void)x; (void)y; }
void HID_MouseGetMovement(int *x, int *y) {
    *x = hid_dx_pending; *y = hid_dy_pending;
    hid_dx_pending = hid_dy_pending = 0;
}
'''
source += accumulators + "\n"
# Routing and accumulation run together; diagnostic formatting is covered by
# test_mouse_sources.py. Include the real source selection in this math test.
start = production.index("typedef enum\n{\n\tMOUSE_SOURCE_NONE")
end = production.index("} mousesource_t;", start) + len("} mousesource_t;")
source += production[start:end] + "\n"
source += re.search(r"^static \w+ sdl_shadow_dx, sdl_shadow_dy;$",
                    production, re.M).group(0) + "\n"
source += function("static qboolean IN_UseHIDMouse(")
source += function("static mousesource_t IN_SelectMouseSource(")
source += r'''
void IN_MouseSourcesSample(int hx, int hy, float sx, float sy, mousesource_t source) {}
void IN_MouseSourcesFinishStroke(void) {}
'''
source += function("static void IN_ApplyMouseMotion(")
source += function("void IN_MouseMotion(")
source += function("void IN_MouseMove(usercmd_t *cmd)")
source += r'''
static void reset(void) {
    memset(&cls, 0, sizeof(cls)); memset(&cl, 0, sizeof(cl));
    memset(&vid, 0, sizeof(vid)); memset(globals, 0, sizeof(globals));
    cls.state = ca_connected; cls.signon = SIGNONS;
    key_dest = key_game; windowhasfocus = true;
    hid_mouse_active = consume = no_mouse = false;
    in_disablemacosxmouseaccel.value = 2;
    sdl_shadow_dx = sdl_shadow_dy = 0;
    total_dx = total_dy = 0; qc_calls = pong_x = pong_y = 0;
    hid_dx_pending = hid_dy_pending = 0;
    sensitivity.value = m_yaw.value = m_pitch.value = 1.0f;
    cl.csqc_sensitivity = 1.0f;
    r_refdef.basefov = scr_fov.value = 90.0f;
    in_strafe.state = 0; in_mlook.state = 1;
    scr_menuscale.value = scr_sbarscale.value = 1.0f;
    assert(!qcvm);
}
static void move(void) {
    usercmd_t cmd = {0};
    IN_MouseMove(&cmd);
}
int main(void) {
    /* Whole-pixel motion keeps its existing meaning. */
    reset();
    IN_MouseMotion(9, 2, 50, 60); move();
    assert(cl.viewangles[YAW] == -9 && cl.viewangles[PITCH] == 2);
    assert(vid.cursorpos[0] == 50 && vid.cursorpos[1] == 60);

    /* Fractional deltas accumulate instead of truncating to zero. */
    reset();
    for (int i = 0; i < 4; ++i)
        IN_MouseMotion(0.25f, -0.25f, 100.0f, 200.0f);
    move();
    assert(fabsf(cl.viewangles[YAW] + 1.0f) < 0.00001f);
    assert(fabsf(cl.viewangles[PITCH] + 1.0f) < 0.00001f);

    /* A second IN_Move in the same frame must not replay the motion. */
    float yaw = cl.viewangles[YAW];
    move();
    assert(cl.viewangles[YAW] == yaw);

    /* Unfocused motion still tracks the cursor but never turns the view. */
    reset(); windowhasfocus = false;
    IN_MouseMotion(30.5f, 12.5f, 100, 200); move();
    assert(cl.viewangles[YAW] == 0 && cl.viewangles[PITCH] == 0);
    assert(vid.cursorpos[0] == 100 && vid.cursorpos[1] == 200);

    /* Motion is dropped while not fully signed on. */
    reset(); cls.signon = 0;
    IN_MouseMotion(30, 12, 100, 200);
    cls.signon = SIGNONS; move();
    assert(cl.viewangles[YAW] == 0);

    /* MenuQC receives the fractional delta and can consume it. */
    reset(); key_dest = key_menu; cls.menu_qcvm.extfuncs.Menu_InputEvent = 1;
    consume = true;
    IN_MouseMotion(0.5f, 1.5f, 100, 200);
    assert(qc_calls == 1 && globals[OFS_PARM0] == CSIE_MOUSEDELTA);
    assert(vectors[OFS_PARM1][0] == 0.5f && vectors[OFS_PARM1][1] == 1.5f);
    key_dest = key_game; move();
    assert(cl.viewangles[YAW] == 0);

    /* CSQC consuming motion keeps it out of the view angles. */
    reset(); cl.qcvm.extfuncs.CSQC_InputEvent = 1; consume = true;
    IN_MouseMotion(4.25f, 2, 100, 200); move();
    assert(qc_calls == 1 && globals[OFS_PARM0] == CSIE_MOUSEDELTA);
    assert(vectors[OFS_PARM1][0] == 4.25f);
    assert(cl.viewangles[YAW] == 0 && !qcvm);

    /* Pong still receives integer window coordinates. */
    reset(); cl.paused = true;
    IN_MouseMotion(1, 1, 123.75f, 45.25f);
    assert(pong_x == 123 && pong_y == 45);

    /* QC absolute cursor positions stay whole virtual pixels under scaling. */
    reset(); key_dest = key_menu; cls.menu_qcvm.extfuncs.Menu_InputEvent = 1;
    cls.menu_qcvm.cursorforced = true; scr_menuscale.value = 1.5f;
    IN_MouseMotion(0, 0, 101, 50);
    assert(qc_calls == 1 && globals[OFS_PARM0] == CSIE_MOUSEABS);
    assert(vectors[OFS_PARM1][0] == 67 && vectors[OFS_PARM2][0] == 33);
    reset(); cl.qcvm.extfuncs.CSQC_InputEvent = 1; cl.qcvm.cursorforced = true;
    scr_sbarscale.value = 1.5f;
    IN_MouseMotion(0, 0, 101, 50);
    assert(qc_calls == 1 && globals[OFS_PARM0] == CSIE_MOUSEABS);
    assert(vectors[OFS_PARM1][0] == 67 && vectors[OFS_PARM2][0] == 33);

#ifdef __APPLE__
    /* Native HID deltas stay integer and still reach the view. */
    reset(); hid_mouse_active = true; hid_dx_pending = 10; hid_dy_pending = 4;
    move();
    assert(cl.viewangles[YAW] == -10 && cl.viewangles[PITCH] == 4);
#endif

    puts("mouse motion accumulation: PASS");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-mouse-motion-") as tmp:
    path = Path(tmp)
    (path / "motion.c").write_text(source)
    for platform, flag in (("macOS", "-D__APPLE__"), ("SDL", "-U__APPLE__")):
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2", flag,
                        "-fsanitize=undefined", str(path / "motion.c"), "-lm",
                        "-o", str(path / "motion")], check=True)
        print(platform, flush=True)
        subprocess.run([str(path / "motion")], check=True)
