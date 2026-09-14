"""Drive production tick deadlines with a fake 64-bit clock.

Covers boot, crossing UINT32_MAX and a dedicated server that has been up for
weeks, where 32-bit tick math used to misjudge elapsed time, plus the Discord
community cache that deliberately keeps 32-bit wrap-safe arithmetic.
Run with: python3 Misc/stress/test_tick_deadlines.py
"""

from pathlib import Path
import os
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
menu = (ROOT / "Quake/menu.c").read_text()
host_cmd = (ROOT / "Quake/host_cmd.c").read_text()
discord = (ROOT / "Quake/discord.c").read_text()
discord_h = (ROOT / "Quake/discord.h").read_text()


def function(text, declaration):
    start = text.index(declaration)
    return text[start:text.index("\n}", start) + 2] + "\n"


source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef enum { false, true } qboolean;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
#define MODVOTE_COOLDOWN_MS 1000U
#define DAY_MS (24ULL * 60 * 60 * 1000)

static Uint64 fake_ticks;
static int delays, ready_after;
Uint64 SDL_GetTicks(void) { return fake_ticks; }
void SDL_Delay(Uint32 ms) { fake_ticks += ms; ++delays; }

enum { VERSIONGITHUB_IDLE, VERSIONGITHUB_LOADING, VERSIONGITHUB_READY };
typedef struct { int state; } versionremoteinfo_t;
void M_Version_StartGitHubFetch(void) {}
void M_Version_GetGitHubInfo(versionremoteinfo_t *release, versionremoteinfo_t *commit) {
    int state = (ready_after >= 0 && delays >= ready_after) ?
        VERSIONGITHUB_READY : VERSIONGITHUB_LOADING;
    if (release) release->state = state;
    if (commit) commit->state = state;
}
'''
source += re.search(r"typedef enum\s*\{\s*DISCORD_COMMUNITY_IDLE.*?\} discord_community_status_t;",
                    discord_h, re.S).group(0) + "\n"
source += "\n".join(re.findall(r"^#define DISCORD_COMMUNITY_(?:READY|ERROR)_CACHE_MS .*$", discord, re.M)) + "\n"
source += r'''
typedef struct { int value; } SDL_AtomicInt;
typedef struct SDL_Thread SDL_Thread;
int SDL_GetAtomicInt(SDL_AtomicInt *a) { return a->value; }
int SDL_SetAtomicInt(SDL_AtomicInt *a, int v) { int old = a->value; a->value = v; return old; }
static SDL_AtomicInt discord_community_status, discord_community_online_count,
    discord_community_completed_ticks, discord_shutting_down;
static int threads_started;
static qboolean DiscordWorker_Begin(void) { return true; }
static void DiscordWorker_End(void) {}
static int DiscordCommunityThread(void *data) { (void)data; return 0; }
SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *name, void *data) {
    (void)fn; (void)name; (void)data; ++threads_started;
    return (SDL_Thread *)&threads_started;
}
void SDL_DetachThread(SDL_Thread *thread) { (void)thread; }
'''
source += function(discord, "static void DiscordCommunity_SetResult(")
source += function(discord, "void Discord_CommunityRefresh(void)")
source += function(host_cmd, "static qboolean Host_Modvote_CooldownActive")
source += function(menu, "qboolean M_Version_WaitForGitHubInfo")
source += r'''
/* The expression the modvote check used before it moved to 64-bit ticks. */
#define SDL_TICKS_PASSED(A, B) ((Sint32)((B) - (A)) <= 0)
static int legacy_cooldown_active(Uint32 now, Uint32 last_vote) {
    return !SDL_TICKS_PASSED(now, last_vote + MODVOTE_COOLDOWN_MS);
}

static void wait_case(Uint64 start, Uint32 timeout, int ready, int expect_ready,
                      Uint64 min_elapsed, Uint64 max_elapsed) {
    versionremoteinfo_t release, commit;
    fake_ticks = start; delays = 0; ready_after = ready;
    qboolean got = M_Version_WaitForGitHubInfo(&release, &commit, timeout);
    assert(got == expect_ready);
    assert(fake_ticks - start >= min_elapsed && fake_ticks - start <= max_elapsed);
}

int main(void) {
    const Uint64 wrap = UINT32_MAX;

    /* Modvote cooldown: the first second after boot is still a cooldown. */
    assert(Host_Modvote_CooldownActive(500, 0));
    assert(!Host_Modvote_CooldownActive(1000, 0));
    assert(Host_Modvote_CooldownActive(5999, 5000));
    assert(!Host_Modvote_CooldownActive(6000, 5000));

    /* Crossing UINT32_MAX neither blocks nor frees a vote early. */
    assert(Host_Modvote_CooldownActive(wrap + 100, wrap - 200));
    assert(!Host_Modvote_CooldownActive(wrap + 801, wrap - 200));

    /* Dedicated server up for weeks: an old vote no longer blocks the slot. */
    Uint64 last = 1000, now = last + 25 * DAY_MS;
    assert(!Host_Modvote_CooldownActive(now, last));
    assert(legacy_cooldown_active((Uint32)now, (Uint32)last)); /* old bug */
    assert(!Host_Modvote_CooldownActive(60 * DAY_MS, 59 * DAY_MS));

    /* GitHub info wait: finishes early, times out on time, across the wrap. */
    wait_case(1000, 200, 3, true, 30, 30);
    wait_case(1000, 200, -1, false, 200, 210);
    wait_case(wrap - 20, 200, -1, false, 200, 210);
    wait_case(wrap + 50 * DAY_MS, 200, -1, false, 200, 210);
    wait_case(wrap - 5, 0, -1, false, 0, 0);

    /* Discord community cache: the int-sized atomic keeps low 32 bits, and the
       unsigned difference must hold across the wrap and after weeks of uptime. */
    fake_ticks = 1000; Discord_CommunityRefresh();
    assert(threads_started == 1 && SDL_GetAtomicInt(&discord_community_status) == DISCORD_COMMUNITY_LOADING);
    fake_ticks = 2000; Discord_CommunityRefresh();
    assert(threads_started == 1); /* a running fetch is not restarted */
    fake_ticks = wrap - 10000; DiscordCommunity_SetResult(DISCORD_COMMUNITY_READY, 3);
    fake_ticks = wrap + 40000; Discord_CommunityRefresh();
    assert(threads_started == 1); /* 50 s after a ready result: still cached */
    fake_ticks = wrap + 50001; Discord_CommunityRefresh();
    assert(threads_started == 2); /* past the 60 s ready cache */
    fake_ticks = wrap + 50 * DAY_MS; DiscordCommunity_SetResult(DISCORD_COMMUNITY_ERROR, 0);
    fake_ticks = wrap + 50 * DAY_MS + 14999; Discord_CommunityRefresh();
    assert(threads_started == 2);
    fake_ticks = wrap + 50 * DAY_MS + 15000; Discord_CommunityRefresh();
    assert(threads_started == 3); /* 15 s error cache expired */
    DiscordCommunity_SetResult(DISCORD_COMMUNITY_READY, 3);
    SDL_SetAtomicInt(&discord_shutting_down, 1);
    fake_ticks += 10 * DAY_MS; Discord_CommunityRefresh();
    assert(threads_started == 3); /* shutdown never starts a fetch */

    puts("tick deadlines: PASS");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-tick-deadlines-") as tmp:
    path = Path(tmp)
    (path / "ticks.c").write_text(source)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2", "-Wall",
                    "-Wshorten-64-to-32", "-Werror=shorten-64-to-32",
                    "-fsanitize=undefined", str(path / "ticks.c"),
                    "-o", str(path / "ticks")], check=True)
    subprocess.run([str(path / "ticks")], check=True)
