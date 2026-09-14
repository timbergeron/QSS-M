"""Drive the production HID mouse startup wait with a scripted condition variable.

HID_WaitForStart runs with hid_start_mutex held while HID_MouseThread reports
readiness or failure through hid_start_state. SDL3's SDL_WaitConditionTimeout
returns true when signalled and false on timeout, and may wake spuriously, so
the recorded state, not the wait result, must decide the outcome.
Run with: python3 Misc/stress/test_hid_start_wait.py
"""

from pathlib import Path
import os
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
production = (ROOT / "Quake/in_sdl.c").read_text()


def function(declaration):
    start = production.index(declaration)
    return production[start:production.index("\n}", start) + 2] + "\n"


enum_line = next(line for line in production.splitlines()
                 if line.startswith("enum { HID_START_PENDING"))

source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int qboolean;
typedef uint64_t Uint64;
typedef int32_t Sint32;
typedef struct SDL_Condition SDL_Condition;
typedef struct SDL_Mutex SDL_Mutex;
#define q_min(a, b) ((a) < (b) ? (a) : (b))
'''
source += enum_line + "\n"
source += r'''
static int hid_start_state;
static SDL_Condition *hid_start_cond = (SDL_Condition *)1;
static SDL_Mutex *hid_start_mutex = (SDL_Mutex *)2;

static Uint64 fake_ticks;
static int waits, max_timeout, report_at, report_state, spurious_at;
Uint64 SDL_GetTicks(void) { return fake_ticks; }

/* Each call is one wait on the condition. The scripted thread reports on the
   report_at-th wait; a spurious wakeup returns early with nothing recorded;
   every other wait times out after its full timeout. */
bool SDL_WaitConditionTimeout(SDL_Condition *cond, SDL_Mutex *mutex, Sint32 timeout)
{
    assert(cond == hid_start_cond && mutex == hid_start_mutex);
    assert(timeout > 0 && timeout <= 1000);
    ++waits;
    if (timeout > max_timeout)
        max_timeout = timeout;
    if (waits == report_at) {
        fake_ticks += 3;
        hid_start_state = report_state;
        return true;
    }
    if (waits == spurious_at) {
        fake_ticks += 1;
        return true;
    }
    fake_ticks += (Uint64)timeout;
    return false;
}
'''
source += function("static qboolean HID_WaitForStart(Uint64 deadline)")
source += r'''
static void reset(int state, int at, int reported, int spurious) {
    hid_start_state = state;
    fake_ticks = 1000;
    waits = max_timeout = 0;
    report_at = at;
    report_state = reported;
    spurious_at = spurious;
}

int main(void) {
    /* Ready before the first wait: no waiting at all. */
    reset(HID_START_READY, 0, 0, 0);
    assert(HID_WaitForStart(fake_ticks + 5000) && waits == 0);

    /* The thread reports failure: fail at once, not after the timeout. */
    reset(HID_START_PENDING, 1, HID_START_FAILED, 0);
    assert(!HID_WaitForStart(fake_ticks + 5000));
    assert(waits == 1 && hid_start_state == HID_START_FAILED);

    /* Two full timeouts, then readiness: still ready. */
    reset(HID_START_PENDING, 3, HID_START_READY, 0);
    assert(HID_WaitForStart(fake_ticks + 5000));
    assert(waits == 3 && fake_ticks == 1000 + 2000 + 3);

    /* A spurious wakeup is not readiness. */
    reset(HID_START_PENDING, 2, HID_START_READY, 1);
    assert(HID_WaitForStart(fake_ticks + 5000) && waits == 2);

    /* Nothing reported by the deadline: the thread is told to stand down. */
    reset(HID_START_PENDING, 0, 0, 0);
    assert(!HID_WaitForStart(fake_ticks + 5000));
    assert(hid_start_state == HID_START_ABANDONED);
    assert(fake_ticks == 1000 + 5000 && waits == 5 && max_timeout == 1000);

    /* The last wait is capped by the remaining time, never overshooting. */
    reset(HID_START_PENDING, 0, 0, 0);
    assert(!HID_WaitForStart(fake_ticks + 2500));
    assert(fake_ticks == 1000 + 2500 && waits == 3);

    puts("hid start wait: PASS");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-hid-start-") as tmp:
    path = Path(tmp)
    (path / "hid.c").write_text(source)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2", "-Wall", "-Werror",
                    "-fsanitize=undefined", str(path / "hid.c"), "-o", str(path / "hid")], check=True)
    subprocess.run([str(path / "hid")], check=True)
