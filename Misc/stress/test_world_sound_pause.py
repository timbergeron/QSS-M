"""Compile the real mixer: pause/resume loops and one-shots, keep UI/voice live.

No audio device or game data required. Also checks the pause policy for local
single player, remote multiplayer and demo playback.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
dma = (ROOT / "Quake/snd_dma.c").read_text()
mix = (ROOT / "Quake/snd_mix.c").read_text()
header = (ROOT / "Quake/q_sound.h").read_text()


def function(text, signature):
    start = text.index(signature + "\n{")
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


source = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
typedef float vec_t;
typedef float vec3_t[3];
typedef unsigned char byte;
typedef struct { void *data; } cache_user_t;
#define MAX_QPATH 64
#define true 1
#define false 0
#define q_min(a,b) ((a)<(b)?(a):(b))
#define q_max(a,b) ((a)>(b)?(a):(b))
#define CLAMP(a,x,b) q_min(q_max(a,x),b)
#define PAINTBUFFER_SIZE 2048
#define MAX_RAW_SAMPLES 4096
#define ca_connected 1
#define SIGNONS 4
#define key_game 0
#define SOUND_PREVIEW_CHANNEL 3
#define NOTIFICATION_SOUND_CHANNEL 4
#define SND_LOOP_DEFAULT 0
#define SND_LOOP_FORCE 1
#define SND_LOOP_DISABLE (-1)
''' + header[header.index('/* !!!'):header.index('/* channel_t.looping')] + r'''
static channel_t snd_channels[7];
static int total_channels = 7, paintedtime, s_rawend = -1, key_dest;
static struct { int state, signon, demoplayback, demopaused; } cls;
static struct { int paused; } cl;
static struct { int active, paused; } sv;
static struct { int maxclients; } svs;
static dma_t device = {.speed = 1000}, *shm = &device;
static portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];
static portable_samplepair_t s_rawsamples[MAX_RAW_SAMPLES];
static struct { float value; } sfxvolume = {1}, sndspeed;
static int muted, snd_vol, snd_scaletable[32][256];
static int sound_started = 1, snd_blocked, soundtime, device_time;
static qboolean snd_world_was_paused;
static struct { float value; } _snd_mixahead = {0.1f};
static void SNDDMA_LockBuffer(void) {}
static void SNDDMA_Submit(void) {}
static void GetSoundtime(void) { soundtime = device_time; }
static float snd_world_gain = 1;
static int exclusive, captured_count;
static int captured[40000];
typedef int filter_t;
static void S_LowpassFilter(int *p, int stride, int n, filter_t *f) {}
static void S_UnderwaterFilter(int n) {}
static void S_UpdateLevels(int n) {}
static sfxcache_t *S_LoadSound(sfx_t *s) { return s->cache.data; }
static qboolean S_SoundPreview_ShouldMixChannel(int i) { return !exclusive || i == SOUND_PREVIEW_CHANNEL; }
static void S_TransferPaintBuffer(int end) {
    for (int i = 0; i < end - paintedtime; i++) {
        assert(captured_count < 40000);
        captured[captured_count++] = paintbuffer[i].left;
    }
}
''' + function(dma, 'qboolean S_WorldSoundPaused (void)') + function(dma, 'qboolean S_IsWorldChannel (int channel)') + function(dma, 'void S_AdvanceWorldSoundTime (int samples)') + function(mix, 'static void SND_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count, int paintbufferstart)') + function(mix, 'static void SND_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int count, int paintbufferstart)') + function(mix, 'void S_PaintChannels (int endtime)') + function(dma, 'static void S_Update_ (void)') + r'''
static sfx_t effect;
static void start(int i, int looping) {
    channel_t *ch = &snd_channels[i];
    memset(ch, 0, sizeof(*ch));
    ch->sfx = &effect;
    ch->leftvol = ch->rightvol = 128;
    ch->end = paintedtime + 10000;
    ch->looping = looping;
}
int main(void) {
    cls.state = ca_connected; cls.signon = SIGNONS;
    sv.active = true; svs.maxclients = 1;
    assert(!S_WorldSoundPaused());
    key_dest = 1; assert(S_WorldSoundPaused());
    svs.maxclients = 2; assert(!S_WorldSoundPaused());
    sv.active = false; assert(!S_WorldSoundPaused());
    cl.paused = true; assert(S_WorldSoundPaused());
    cl.paused = false; cls.demoplayback = true;
    assert(!S_WorldSoundPaused());
    cls.demopaused = true; assert(S_WorldSoundPaused());
    cls.demopaused = false; cls.demoplayback = false;
    sv.active = true; svs.maxclients = 1; key_dest = key_game;

    sfxcache_t *cache = calloc(1, sizeof(*cache) + 20000);
    cache->length = 10000; cache->width = 2; cache->loopstart = 0;
    for (int i = 0; i < 10000; i++) ((short *)cache->data)[i] = 256;
    effect.cache.data = cache;
    start(0, SND_LOOP_DEFAULT); /* ambient loop */
    start(1, SND_LOOP_DEFAULT); /* elevator */
    start(2, SND_LOOP_DISABLE); /* weapon one-shot */
    S_PaintChannels(10);
    assert(snd_channels[1].pos == 10);
    key_dest = 1;
    S_PaintChannels(200);
    assert(snd_world_gain == 0);
    int pos = snd_channels[1].pos, remaining = snd_channels[1].end - paintedtime;
    assert(pos >= 59 && pos <= 61);
    assert(captured[10] > captured[30] && captured[30] > captured[58]);
    for (int i = 70; i < 200; i++) assert(captured[i] == 0);
    S_PaintChannels(12000);
    for (int i = 0; i < 3; i++) {
        assert(snd_channels[i].pos == pos && snd_channels[i].sfx);
        assert(snd_channels[i].end - paintedtime == remaining);
    }
    /* A device underrun must not expire frozen channels on resume. */
    S_AdvanceWorldSoundTime(5000); paintedtime += 5000;
    assert(snd_channels[1].end - paintedtime == remaining);
    start(3, SND_LOOP_DISABLE); /* preview */
    start(4, SND_LOOP_DISABLE); /* menu/console notification */
    start(5, SND_LOOP_DISABLE); snd_channels[5].entchannel = -2; /* voice */
    S_PaintChannels(paintedtime + 100);
    assert(snd_channels[1].pos == pos);
    for (int i = 3; i <= 5; i++) assert(snd_channels[i].pos == 100);
    assert(captured[captured_count - 1] > 0);
    memset(&snd_channels[3], 0, 3 * sizeof(channel_t));
    key_dest = key_game;
    S_PaintChannels(paintedtime + 100);
    assert(snd_world_gain == 1 && snd_channels[1].pos == pos + 100);
    assert(snd_channels[2].sfx); /* one-shot survives a long pause */
    S_PaintChannels(paintedtime + 10000);
    assert(snd_channels[1].sfx && !snd_channels[2].sfx);
    /* Eight-bit sounds use the same fade/freeze path. */
    cache->width = 1; memset(cache->data, 1, 10000);
    for (int i = 0; i < 32; i++) snd_scaletable[i][1] = i * 2048;
    key_dest = 1;
    S_PaintChannels(paintedtime + 100);
    pos = snd_channels[1].pos;
    S_PaintChannels(paintedtime + 1000);
    assert(snd_channels[1].pos == pos && snd_world_gain == 0);
    key_dest = key_game;
    S_PaintChannels(paintedtime + 100);
    assert(snd_channels[1].pos == (pos + 100) % 10000);
    /* Resuming on the same update as an underrun preserves remaining audio. */
    device.speed = 1000; device.samples = 4096; device.channels = 2;
    device.buffer = (byte *)captured; /* only checked for presence by S_Update_ */
    key_dest = 1; start(1, SND_LOOP_DEFAULT);
    device_time = paintedtime;
    S_Update_();
    pos = snd_channels[1].pos;
    remaining = snd_channels[1].end - paintedtime;
    key_dest = key_game; device_time = paintedtime + 5000;
    S_Update_();
    assert(snd_channels[1].pos == pos + 100);
    assert(snd_channels[1].end - paintedtime == remaining - 100);
    free(cache);
    puts("world sound pause: PASS");
}
'''

with tempfile.TemporaryDirectory(prefix="world-sound-pause-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wno-unused-function",
                   "-fsanitize=address,undefined", "-g", str(path / "test.c"),
                   "-lm", "-o", str(path / "test")], check=True)
    environment = os.environ.copy()
    # LeakSanitizer cannot inspect processes inside the managed sandbox.
    environment.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    subprocess.run([str(path / "test")], check=True, env=environment)
