"""Check the SDL3 audio stream adapter and microphone reads under sanitizers.

Run with python3 Misc/stress/test_sdl3_audio.py (requires cc with address and
undefined-behavior sanitizers). Compiles the production paint path, stream
callback, period and ring policy from snd_sdl.c and SDL_Capture_Update from
snd_voip.c against test doubles; no SDL, audio device or Quake data is needed.
"""

from pathlib import Path
import os
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
snd_sdl = (ROOT / "Quake/snd_sdl.c").read_text()
start = snd_sdl.index("static int SND_Scaled16 (int sample, int scale)")
end = snd_sdl.index("\n/*\n================\nSND_OpenAudioStream", start)
playback = snd_sdl[start:end]

snd_voip = (ROOT / "Quake/snd_voip.c").read_text()
start = snd_voip.index("static unsigned int SDL_Capture_Update(")
end = snd_voip.index("static snd_capture_driver_t SDL_Capture =", start)
capture = snd_voip[start:end]

PLAYBACK_DOUBLES = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t Uint8;
#define SDLCALL
#define CLAMP(_minval, x, _maxval) ((x) < (_minval) ? (_minval) : (x) > (_maxval) ? (_maxval) : (x))
#define q_min(a, b) ((a) < (b) ? (a) : (b))
#define SND_MIX_CHANNELS 2
#define SND_RING_PERIODS 10

typedef struct
{
    int channels, samples, submission_chunk, samplepos, samplebits, signed8, speed;
    unsigned char *buffer;
} dma_t;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef struct { int value; } SDL_AtomicInt;

static dma_t dma_storage;
static dma_t *shm;
static int buffersize;
static int device_channels = 2;
static Uint8 *sdl_scratch;
static int sdl_scratch_bytes;
static SDL_AtomicInt sdl_stream_failed;

static int master_scale = 256;
static int S_GetMasterVolumeScale(void) { return master_scale; }

static int SDL_CompareAndSwapAtomicInt(SDL_AtomicInt *a, int oldval, int newval)
{
    if (a->value != oldval)
        return 0;
    a->value = newval;
    return 1;
}

static int failures;
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)

static Uint8 captured[1 << 16];
static int captured_bytes, put_calls, largest_put, put_should_fail;

static int SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len)
{
    (void)stream;
    put_calls++;
    if (put_should_fail)
        return 0;
    CHECK(len > 0 && captured_bytes + len <= (int)sizeof(captured));
    if (len <= 0 || captured_bytes + len > (int)sizeof(captured))
        return 0;
    memcpy(captured + captured_bytes, buf, len);
    captured_bytes += len;
    if (len > largest_put)
        largest_put = len;
    return 1;
}
'''

PLAYBACK_CHECKS = r'''
enum { GUARD = 32 };

static Uint8 *guarded(int bytes)
{
    Uint8 *block = malloc(bytes + 2 * GUARD);
    memset(block, 0xA5, GUARD);
    memset(block + GUARD + bytes, 0x5A, GUARD);
    return block + GUARD;
}

static void check_and_release(Uint8 *p, int bytes)
{
    int i, intact = 1;
    for (i = 0; i < GUARD; i++)
        intact &= p[i - GUARD] == 0xA5 && p[bytes + i] == 0x5A;
    CHECK(intact);
    free(p - GUARD);
}

static void setup(int bits, int ring_frames, int channels, int scratch_frames)
{
    shm = &dma_storage;
    memset(shm, 0, sizeof(*shm));
    shm->samplebits = bits;
    shm->channels = SND_MIX_CHANNELS;
    shm->samples = ring_frames * SND_MIX_CHANNELS;
    shm->speed = 44100;
    buffersize = shm->samples * (bits / 8);
    shm->buffer = guarded(buffersize);
    device_channels = channels;
    sdl_scratch_bytes = scratch_frames * channels * (bits / 8);
    sdl_scratch = guarded(sdl_scratch_bytes);
    captured_bytes = put_calls = largest_put = put_should_fail = 0;
    sdl_stream_failed.value = 0;
    master_scale = 256;
}

static void teardown(void)
{
    check_and_release(shm->buffer, buffersize);
    check_and_release(sdl_scratch, sdl_scratch_bytes);
    shm = NULL;
    sdl_scratch = NULL;
}

static void fill16(int left_base, int right_base)
{
    short *ring = (short *)shm->buffer;
    int frame;
    for (frame = 0; frame < shm->samples / 2; frame++)
    {
        ring[frame * 2] = (short)(left_base + frame);
        ring[frame * 2 + 1] = (short)(right_base - frame);
    }
}

int main(void)
{
    /* The plan's up-mix assertion. */
    {
        short out[8] = {0};
        short expected[6] = {1000, -500, 250, 0, 1000, -500};
        CHECK(SND_Upmix16(out, 1000, -500, 6) == out + 6);
        CHECK(memcmp(out, expected, sizeof(expected)) == 0);
    }

    /* Engine-owned period and ring policy; 44.1 kHz matches SDL2's 65536-byte ring. */
    CHECK(SND_PeriodFrames(11025) == 256 && SND_PeriodFrames(22050) == 512);
    CHECK(SND_PeriodFrames(44100) == 1024 && SND_PeriodFrames(48000) == 1024 && SND_PeriodFrames(96000) == 4096);
    /* 48 kHz should have a comparable period to 44.1 kHz, and still hold
     * the default 100 ms mix-ahead in the ring after the period reduction. */
    CHECK((double)SND_PeriodFrames(48000) / 48000 <= (double)SND_PeriodFrames(44100) / 44100);
    CHECK(SND_RingSamples(SND_PeriodFrames(48000)) / SND_MIX_CHANNELS >= 4800);
    CHECK(SND_RingSamples(256) == 8192 && SND_RingSamples(1024) == 32768);
    CHECK(SND_RingSamples(2048) == 65536 && SND_RingSamples(4096) == 131072);
    CHECK(SND_RingSamples(1024) * 2 == 65536);

    /* Eight-frame stereo ring: from samplepos 14, twenty frames wrap to samplepos 6. */
    {
        short *out = (short *)captured;
        int i, ordered = 1;
        setup(16, 8, 2, 8);
        fill16(1000, -1000);
        shm->samplepos = 14;
        SND_StreamCallback(NULL, NULL, 20 * 4, 20 * 4);
        CHECK(shm->samplepos == 6);
        CHECK(captured_bytes == 80 && put_calls == 3 && largest_put <= sdl_scratch_bytes);
        for (i = 0; i < 20; i++)
        {
            int frame = (7 + i) % 8;
            ordered &= out[i * 2] == 1000 + frame && out[i * 2 + 1] == -1000 - frame;
        }
        CHECK(ordered);
        teardown();
    }

    /* A request ending mid-frame rounds up to whole frames: 5 bytes is two 4-byte frames. */
    setup(16, 8, 2, 8);
    fill16(1000, -1000);
    SND_StreamCallback(NULL, NULL, 5, 5);
    CHECK(captured_bytes == 8 && shm->samplepos == 4);
    teardown();

    /* Requests larger than the ring are painted in scratch-sized chunks. */
    setup(16, 8, 2, 8);
    fill16(1000, -1000);
    SND_StreamCallback(NULL, NULL, 113, 113);
    CHECK(captured_bytes == 116 && put_calls == 4 && largest_put == 32);
    CHECK(shm->samplepos == (116 / 2) % 16);
    teardown();

    /* Master volume is applied once, including silence at zero. */
    {
        short *out = (short *)captured;
        setup(16, 8, 2, 8);
        fill16(1000, -1000);
        master_scale = 128;
        SND_StreamCallback(NULL, NULL, 4, 4);
        CHECK(out[0] == 500 && out[1] == -500);
        master_scale = 0;
        SND_StreamCallback(NULL, NULL, 4, 4);
        CHECK(out[2] == 0 && out[3] == 0);
        teardown();
    }

    /* Unsigned 8-bit: silence is 128 and scaling is centered on it. */
    setup(8, 8, 2, 8);
    memset(shm->buffer, 200, buffersize);
    master_scale = 0;
    SND_StreamCallback(NULL, NULL, 2, 2);
    CHECK(captured[0] == 128 && captured[1] == 128);
    master_scale = 128;
    SND_StreamCallback(NULL, NULL, 2, 2);
    CHECK(captured[2] == 164 && captured[3] == 164);
    teardown();

    /* 5.1 16-bit surround: each ring frame expands to six channels in order. */
    {
        short *out = (short *)captured;
        int frame, mapped = 1;
        setup(16, 8, 6, 4);
        fill16(1000, -500);
        SND_StreamCallback(NULL, NULL, 6 * 12, 6 * 12);
        CHECK(captured_bytes == 72 && put_calls == 2 && largest_put == 48 && shm->samplepos == 12);
        for (frame = 0; frame < 6; frame++)
        {
            short want[6];
            int l = 1000 + frame, r = -500 - frame;
            SND_Upmix16(want, l, r, 6);
            mapped &= memcmp(out + frame * 6, want, sizeof(want)) == 0;
        }
        CHECK(mapped);
        teardown();
    }

    /* 5.1 unsigned 8-bit surround keeps a silent LFE at 128. */
    setup(8, 8, 6, 4);
    {
        int i;
        for (i = 0; i < buffersize; i += 2)
        {
            shm->buffer[i] = 200;
            shm->buffer[i + 1] = 100;
        }
    }
    SND_StreamCallback(NULL, NULL, 6, 6);
    {
        const Uint8 want[6] = {200, 100, 150, 128, 200, 100};
        CHECK(memcmp(captured, want, sizeof(want)) == 0 && shm->samplepos == 2);
    }
    teardown();

    /* No DMA or scratch storage: nothing is painted. */
    setup(16, 8, 2, 8);
    {
        dma_t *saved = shm;
        shm = NULL;
        SND_StreamCallback(NULL, NULL, 16, 16);
        CHECK(put_calls == 0);
        shm = saved;
    }
    teardown();

    /* A rejected put is recorded once for the main thread and stops painting. */
    setup(16, 8, 2, 2);
    fill16(1000, -1000);
    put_should_fail = 1;
    SND_StreamCallback(NULL, NULL, 64, 64);
    CHECK(put_calls == 1 && sdl_stream_failed.value == 1);
    SND_StreamCallback(NULL, NULL, 64, 64);
    CHECK(sdl_stream_failed.value == 1);
    teardown();

    if (failures)
        return 1;
    printf("playback: PASS (upmix, policy, ring wrap, partial and large requests, volume, u8 silence, surround, failure)\n");
    return 0;
}
'''

CAPTURE_DOUBLES = r'''
#include <stdio.h>
#include <string.h>

typedef struct SDL_AudioStream SDL_AudioStream;
typedef struct { SDL_AudioStream *stream; } sdlcapture_t;

static int available, returned, requested, data_calls;
static int failures;
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)

static int SDL_GetAudioStreamAvailable(SDL_AudioStream *stream) { (void)stream; return available; }
static int SDL_GetAudioStreamData(SDL_AudioStream *stream, void *buf, int len)
{
    (void)stream;
    data_calls++;
    requested = len;
    if (returned < 0)
        return returned;
    memset(buf, 0x11, returned < len ? returned : len);
    return returned < len ? returned : len;
}
'''

CAPTURE_CHECKS = r'''
static unsigned int update(int avail, int give, unsigned int minbytes, unsigned int maxbytes)
{
    static unsigned char buffer[8192];
    sdlcapture_t c = { NULL };
    available = avail;
    returned = give;
    requested = -1;
    data_calls = 0;
    return SDL_Capture_Update(&c, buffer, minbytes, maxbytes);
}

int main(void)
{
    CHECK(update(-1, 100, 0, 1000) == 0 && data_calls == 0);           /* failure, not a huge size */
    CHECK(update(0, 0, 0, 1000) == 0 && data_calls == 0);
    CHECK(update(10, 10, 20, 1000) == 0 && data_calls == 0);           /* below minbytes */
    CHECK(update(101, 1000, 0, 1000) == 100 && requested == 100);       /* whole samples only */
    CHECK(update(5000, 5000, 0, 999) == 998 && requested == 998);       /* capped at maxbytes */
    CHECK(update(1, 1, 0, 1000) == 0 && data_calls == 0);               /* less than one sample */
    CHECK(update(400, -1, 0, 1000) == 0 && data_calls == 1);            /* read failure */
    CHECK(update(400, 0, 0, 1000) == 0);
    CHECK(update(400, 301, 0, 1000) == 300);                            /* short read stays aligned */
    if (failures)
        return 1;
    printf("capture: PASS (failure, empty, minbytes, alignment, maxbytes, read errors)\n");
    return 0;
}
'''


def build_and_run(path, name, text):
    source = path / f"{name}.c"
    binary = path / name
    source.write_text(text)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(source), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr)
        raise SystemExit(f"{name} failed")


def main():
    with tempfile.TemporaryDirectory(prefix="qssm-sdl3-audio-") as tmp:
        path = Path(tmp)
        build_and_run(path, "playback", PLAYBACK_DOUBLES + playback + PLAYBACK_CHECKS)
        build_and_run(path, "capture", CAPTURE_DOUBLES + capture + CAPTURE_CHECKS)


if __name__ == "__main__":
    main()
