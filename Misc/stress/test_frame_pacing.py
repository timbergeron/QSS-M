"""Exercise the production FPS filter and wait with a deterministic clock.

Run with python3 Misc/stress/test_frame_pacing.py. Add --benchmark to compare
the old 1 ms polling loop with precise waits using real SDL3 timers (pkg-config
must find sdl3). The benchmark simulates rendering; it does not measure a GPU.
"""

from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
host = (ROOT / "Quake/host.c").read_text()
main = (ROOT / "Quake/main_sdl.c").read_text()


def function(text, declaration):
    start = text.index(declaration)
    return text[start:text.index("\n}", start) + 2] + "\n"


production = "\n".join(function(host, declaration) for declaration in (
    "static double Host_FrameInterval (void)",
    "void Host_Throttle (double elapsed)",
    "qboolean Host_FilterTime (double time)",
))
start = main.index("\t\tif (time < sys_throttle.value && !cls.timedemo)")
wait = main[start:main.index("\n\n", start)]

common = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int qboolean;
#define true 1
#define false 0
#define q_min(a,b) ((a) < (b) ? (a) : (b))
#define CLAMP(a,b,c) ((b) < (a) ? (a) : (b) > (c) ? (c) : (b))
typedef struct { float value; } cvar_t;
static cvar_t host_maxfps = {250}, host_timescale, host_framerate, pq_lag;
static cvar_t sys_throttle = {0.02f};
enum { ca_disconnected, ca_connected };
static struct { int state, timedemo; } cls = {ca_connected, 0};
static struct { int refreshrate; } vid = {60};
static double realtime, oldrealtime, host_frametime;
'''

fake = r'''
typedef uint64_t Uint64;
static double now;
static Uint64 precise_ns;
static int coarse_ms;
static double Sys_DoubleTime(void) { return now; }
static void SDL_Delay(unsigned ms) { coarse_ms += ms; now += ms / 1000.0; }
static void SDL_DelayPrecise(Uint64 ns) { precise_ns += ns; now += ns / 1e9; }
'''

checks = r'''
static void reset(void) {
    realtime = oldrealtime = now = 0;
    host_maxfps.value = 250; sys_throttle.value = 0.02f;
    host_timescale.value = host_framerate.value = pq_lag.value = 0;
    cls.state = ca_connected; cls.timedemo = 0; vid.refreshrate = 60;
    precise_ns = 0; coarse_ms = 0;
}

int main(void) {
    reset();
    /* At 250 FPS, 1 ms of render work leaves 3 ms to wait. */
    now = 0.001; client_wait(0.004, 0);
    assert(precise_ns >= 2999999 && precise_ns <= 3000001);
    assert(coarse_ms == 0);
    assert(Host_FilterTime(now));

    /* Filtered iterations already advanced realtime: don't wait twice. */
    reset();
    assert(!Host_FilterTime(0.001));
    now = 0.0015; client_wait(0.001, 0.001);
    assert(precise_ns >= 2499999 && precise_ns <= 2500001);
    assert(Host_FilterTime(now - 0.001));

    /* An over-budget render or vsync wait must not acquire another delay. */
    reset(); now = 0.008; client_wait(0.004, 0);
    assert(precise_ns == 0 && coarse_ms == 0);

    /* Explicit throttle opt-out, threshold, and timedemo keep their semantics. */
    reset(); sys_throttle.value = 0; client_wait(0.001, 0);
    sys_throttle.value = -1; client_wait(0.001, 0);
    sys_throttle.value = 0.02f; client_wait(0.03, 0);
    cls.timedemo = 1; client_wait(0.001, 0);
    assert(precise_ns == 0 && coarse_ms == 0);
    assert(Host_FilterTime(0.00001));

    /* Zero and negative FPS limits preserve the existing uncapped throttle. */
    reset(); host_maxfps.value = 0; client_wait(0.001, 0);
    host_maxfps.value = -72; client_wait(0.001, now);
    assert(precise_ns == 0 && coarse_ms == 2);

    /* Disconnected menus use monitor refresh, bounded by a positive cap. */
    reset(); cls.state = ca_disconnected; vid.refreshrate = 144;
    assert(fabs(Host_FrameInterval() - 1.0 / 144) < 1e-12);
    host_maxfps.value = 60;
    assert(fabs(Host_FrameInterval() - 1.0 / 60) < 1e-12);
    host_maxfps.value = -72; vid.refreshrate = 0;
    assert(fabs(Host_FrameInterval() - 1.0 / 60) < 1e-12);

    /* Sub-millisecond caps and the existing min/max clamps. */
    reset(); host_maxfps.value = 10000; client_wait(0.0002, 0);
    assert(precise_ns >= 199999 && precise_ns <= 200001);
    host_maxfps.value = 1;
    assert(fabs(Host_FrameInterval() - 0.1) < 1e-12);

    /* Synthetic lag's queued moves still get serviced every millisecond. */
    reset(); host_maxfps.value = 60; pq_lag.value = 50;
    client_wait(0.001, 0);
    assert(precise_ns == 1000000);

    /* Rendering cadence stays independent of simulation timescale. */
    reset(); host_timescale.value = 0.5f;
    assert(Host_FilterTime(0.004));
    assert(fabs(host_frametime - 0.002) < 1e-12);

    /* Sustained cadence, including filtered frames and small oversleeps. */
    const int rates[] = {60, 120, 144, 250, 500, 1000};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        reset(); host_maxfps.value = rates[r];
        double sampled = 0, last_frame = 0;
        int frames = 0;
        for (int loops = 0; frames < 200 && loops < 1000; ++loops) {
            double stamp = now, delta = stamp - sampled;
            if (Host_FilterTime(delta)) {
                double gap = stamp - last_frame;
                assert(gap >= 1.0 / rates[r] - 1e-12);
                assert(gap <= 1.0 / rates[r] + 0.000011);
                last_frame = stamp; ++frames;
                now += 0.0001 + (frames % 3) * 0.00005;
            }
            client_wait(delta, stamp);
            now += 0.00001; /* scheduler oversleep */
            sampled = stamp;
        }
        assert(frames == 200);
    }
    puts("frame pacing: all checks passed");
}
'''

benchmark = r'''
#include <time.h>
static int compare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
int main(void) {
    const int rates[] = {144, 250, 500};
    puts("mode,cap,mean_ms,p99_ms,cpu_percent");
    for (int r = 0; r < 3; ++r) for (int precise = 0; precise < 2; ++precise) {
        double gaps[2000], sum = 0;
        int frames = 0, count = rates[r] * 2;
        host_maxfps.value = rates[r];
        realtime = oldrealtime = 0;
        double began = Sys_DoubleTime(), sampled = began, last_frame = began;
        clock_t cpu = clock();
        while (frames < count) {
            double stamp = Sys_DoubleTime(), delta = stamp - sampled;
            if (Host_FilterTime(delta)) {
                gaps[frames++] = (stamp - last_frame) * 1000;
                last_frame = stamp;
                SDL_DelayPrecise(300000); /* simulated render work */
            }
            if (precise) client_wait(delta, stamp);
            else if (delta < sys_throttle.value) SDL_Delay(1);
            sampled = stamp;
        }
        double cpu_percent = 100.0 * (clock() - cpu) / CLOCKS_PER_SEC / (Sys_DoubleTime() - began);
        for (int i = 0; i < count; ++i) sum += gaps[i];
        qsort(gaps, count, sizeof(double), compare);
        printf("%s,%d,%.4f,%.4f,%.2f\n", precise ? "precise" : "legacy", rates[r],
            sum / count, gaps[count * 99 / 100], cpu_percent);
    }
}
'''

wrapper = "\nstatic void client_wait(double time, double newtime) {\n" + wait + "\n}\n"
with tempfile.TemporaryDirectory(prefix="qssm-frame-pacing-") as tmp:
    source = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    source.write_text(common + fake + production + wrapper + checks)
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(source), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    if "--benchmark" in sys.argv:
        sdl = '#include <SDL3/SDL.h>\nstatic double Sys_DoubleTime(void) { return SDL_GetTicksNS() / 1e9; }\n'
        source.write_text(common + sdl + production + wrapper + benchmark)
        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
        subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", str(source), "-lm", *flags, "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
