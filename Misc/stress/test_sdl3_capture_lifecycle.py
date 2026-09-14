"""Check voice stop/drain/restart against real SDL3 audio streams.

Compiles the production capture functions, replacing only device pause/resume
(no microphone is opened). Clearing Stop would lose queued speech; omitting
Flush would strand the resampler tail; omitting the Start clear would replay
old speech. Requires SDL3, using the vendored framework on macOS.
"""

from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
production = (ROOT / "Quake/snd_voip.c").read_text()
start = production.index("static void SDL_Capture_Start(")
stop = production.index("static void SDL_Capture_Shutdown(", start)
functions = production[start:stop]
start = production.index("static unsigned int SDL_Capture_Update(")
stop = production.index("static snd_capture_driver_t SDL_Capture =", start)
functions += production[start:stop]

PREAMBLE = r'''
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { SDL_AudioStream *stream; } sdlcapture_t;
/* Real streams/conversion, but no physical device to pause or resume. */
static bool device_control(SDL_AudioStream *stream) { return stream != NULL; }
#define SDL_PauseAudioStreamDevice device_control
#define SDL_ResumeAudioStreamDevice device_control
'''

CHECKS = r'''
int main(void)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, 48000 };
    sdlcapture_t capture = { SDL_CreateAudioStream(&spec, &spec) };
    short speech[30000], received[30000];
    unsigned int bytes, got;
    assert(capture.stream);
    for (int i = 0; i < 30000; ++i)
        speech[i] = (short)(i - 15000);

    /* S_Voip_Transmit first fills its 32 KiB buffer, stops the microphone,
       then drains the remaining speech on following frames. */
    assert(SDL_PutAudioStreamData(capture.stream, speech, sizeof(speech)));
    bytes = SDL_Capture_Update(&capture, (unsigned char *)received, 1, 32768);
    assert(bytes == 32768);
    SDL_Capture_Stop(&capture);
    while ((got = SDL_Capture_Update(&capture, (unsigned char *)received + bytes,
                                    1, sizeof(received) - bytes)) != 0)
        bytes += got;
    assert(bytes == sizeof(speech));
    assert(memcmp(speech, received, sizeof(speech)) == 0);

    /* A new recording must discard any leftovers from the previous one. */
    assert(SDL_PutAudioStreamData(capture.stream, speech, 1000));
    SDL_Capture_Start(&capture);
    assert(SDL_Capture_Update(&capture, (unsigned char *)received, 1, sizeof(received)) == 0);
    assert(SDL_PutAudioStreamData(capture.stream, speech, 1000));
    assert(SDL_Capture_Update(&capture, (unsigned char *)received, 1, sizeof(received)) == 1000);
    assert(memcmp(speech, received, 1000) == 0);
    SDL_DestroyAudioStream(capture.stream);

    /* A 48 kHz microphone feeding a 16 kHz codec retains its complete tail:
       4800 input frames (100 ms) must produce 1600 output frames. */
    SDL_AudioSpec output = { SDL_AUDIO_S16, 1, 16000 };
    capture.stream = SDL_CreateAudioStream(&spec, &output);
    assert(capture.stream);
    assert(SDL_PutAudioStreamData(capture.stream, speech, 4800 * sizeof(short)));
    bytes = SDL_Capture_Update(&capture, (unsigned char *)received, 1, sizeof(received));
    assert(bytes < 3200); /* the resampler is still holding the tail */
    SDL_Capture_Stop(&capture);
    while ((got = SDL_Capture_Update(&capture, (unsigned char *)received + bytes,
                                    1, sizeof(received) - bytes)) != 0)
        bytes += got;
    assert(bytes == 3200);
    SDL_DestroyAudioStream(capture.stream);
    puts("capture lifecycle: PASS (queued speech, clean restart, resampler tail)");
    return 0;
}
'''


def main():
    if sys.platform == "darwin":
        sdl_flags = ["-F", str(ROOT / "macOS"), "-framework", "SDL3",
                     "-Wl,-rpath," + str(ROOT / "macOS")]
    else:
        sdl_flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "sdl3 >= 3.2.12", "sdl3 < 4"], text=True))
    with tempfile.TemporaryDirectory(prefix="qssm-capture-lifecycle-") as tmp:
        source = Path(tmp) / "capture.c"
        binary = Path(tmp) / "capture"
        source.write_text(PREAMBLE + functions + CHECKS)
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", str(source), *sdl_flags,
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
