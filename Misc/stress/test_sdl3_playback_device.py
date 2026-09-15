"""Open real SDL3 dummy playback using production period and device-name helpers.

Requires pkg-config sdl3. Exercises startup, 44.1/48 kHz playback and reopening
without speakers. In particular, SDL 3.2.12 must not crash naming a logical
device (SDL fixed that query in 3.2.14).
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
backend = (ROOT / "Quake/snd_sdl.c").read_text()


def function(declaration):
    start = backend.index(declaration)
    return backend[start:backend.index("\n}", start) + 2] + "\n"


source = r'''
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define q_snprintf SDL_snprintf
#define q_strlcpy SDL_strlcpy
static SDL_AudioStream *sdl_stream;
static int snd_playback_frames;
static char sdl_devicename[128];
'''
source += function("static int SND_PeriodFrames (int rate)")
source += function("SDL_AudioStream *SND_OpenAudioStream (")
source += function("static void SND_UpdateDeviceName (void)")
source += r'''
int main(void) {
    assert(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    assert(SDL_Init(SDL_INIT_AUDIO));
    SND_UpdateDeviceName();
    assert(strcmp(sdl_devicename, "System default") == 0);
    const int rates[] = {44100, 48000, 44100};
    for (int i = 0; i < 3; ++i) {
        SDL_AudioSpec mix = {SDL_AUDIO_S16, 2, rates[i]}, device;
        int frames;
        short silence[2048] = {0};
        snd_playback_frames = SND_PeriodFrames(mix.freq);
        sdl_stream = SND_OpenAudioStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &mix, NULL, NULL, snd_playback_frames);
        assert(sdl_stream);
        SND_UpdateDeviceName(); /* used to crash with SDL 3.2.12 */
        assert(sdl_devicename[0]);
        if (SDL_GetVersion() < SDL_VERSIONNUM(3, 2, 14))
            assert(strcmp(sdl_devicename, "System default") == 0);
        else
            assert(strcmp(sdl_devicename, "System default") != 0);
        assert(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sdl_stream), &device, &frames));
        assert(device.freq == rates[i] && frames == 1024);
        assert(SDL_PutAudioStreamData(sdl_stream, silence, sizeof(silence)));
        assert(SDL_FlushAudioStream(sdl_stream));
        assert(SDL_ResumeAudioStreamDevice(sdl_stream));
        Uint64 deadline = SDL_GetTicks() + 2000;
        int queued;
        do {
            SDL_Delay(10);
            queued = SDL_GetAudioStreamQueued(sdl_stream);
            assert(queued >= 0);
        } while (queued && SDL_GetTicks() < deadline);
        assert(queued == 0);
        printf("playback device: %d Hz, %d frames, %s: PASS\n", device.freq, frames, sdl_devicename);
        SDL_DestroyAudioStream(sdl_stream);
        sdl_stream = NULL;
    }
    SDL_Quit();
}
'''

flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
with tempfile.TemporaryDirectory(prefix="qssm-playback-device-") as tmp:
    cfile, binary = Path(tmp) / "test.c", Path(tmp) / "test"
    cfile.write_text(source)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(cfile), *flags, "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
