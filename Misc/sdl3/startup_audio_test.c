/* Build with the Windows SDL3 headers/library and Quake include directory.
 * Exercises the real backend with SDL's dummy device and real worker threads. */
#include "quakedef.h"
#include <SDL3/SDL.h>

static SDL_ThreadID test_main_thread;
static int test_delay_open, test_fail_open, test_open_calls, test_resume_calls;
static SDL_ThreadID test_open_thread;
static int test_delay_close;
static int test_fail_resume, test_fail_thread, test_fail_init, test_fail_alloc;

static SDL_Thread *Test_CreateThread (SDL_ThreadFunction fn, const char *name, void *data)
{
    return test_fail_thread ? NULL : SDL_CreateThread (fn, name, data);
}

static bool Test_InitSubSystem (SDL_InitFlags flags)
{
    if (test_fail_init) return SDL_SetError ("injected subsystem failure");
    return SDL_InitSubSystem (flags);
}

static void *Test_Malloc (size_t size)
{
    return test_fail_alloc ? NULL : malloc (size);
}

static SDL_AudioStream *Test_OpenStream (SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
    SDL_AudioStreamCallback callback, void *userdata)
{
    test_open_calls++;
    test_open_thread = SDL_GetCurrentThreadID ();
    SDL_Delay (test_delay_open);
    if (test_fail_open)
    {
        SDL_SetError ("injected startup open failure");
        return NULL;
    }
    return SDL_OpenAudioDeviceStream (device, spec, callback, userdata);
}

static bool Test_ResumeStream (SDL_AudioStream *stream)
{
    test_resume_calls++;
    if (test_fail_resume) return SDL_SetError ("injected resume failure");
    return SDL_ResumeAudioStreamDevice (stream);
}

static void Test_DestroyStream (SDL_AudioStream *stream)
{
    if (stream) SDL_Delay (test_delay_close);
    SDL_DestroyAudioStream (stream);
}

#define SDL_OpenAudioDeviceStream Test_OpenStream
#define SDL_ResumeAudioStreamDevice Test_ResumeStream
#define SDL_DestroyAudioStream Test_DestroyStream
#undef SDL_CreateThread
#define SDL_CreateThread Test_CreateThread
#define SDL_InitSubSystem Test_InitSubSystem
#define malloc Test_Malloc
#include "../../Quake/snd_sdl.c"
#undef SDL_OpenAudioDeviceStream
#undef SDL_ResumeAudioStreamDevice
#undef SDL_DestroyAudioStream
#undef SDL_CreateThread
#undef SDL_InitSubSystem
#undef malloc

volatile dma_t *shm;
cvar_t snd_mixspeed = {"snd_mixspeed", "44100"};
cvar_t snd_surround = {"snd_surround", "1"};
cvar_t loadas8bit = {"loadas8bit", "0"};

int S_GetMasterVolumeScale (void) { return 256; }
int q_snprintf (char *dest, size_t size, const char *fmt, ...)
{
    int result;
    va_list args;
    va_start (args, fmt);
    result = vsnprintf (dest, size, fmt, args);
    va_end (args);
    return result;
}
size_t q_strlcpy (char *dest, const char *src, size_t size)
{
    size_t len = strlen (src);
    if (size) { size_t copied = q_min (len, size - 1); memcpy (dest, src, copied); dest[copied] = 0; }
    return len;
}
void Con_Printf (const char *fmt, ...)
{
    if (SDL_GetCurrentThreadID () != test_main_thread) { fputs ("console called from worker\n", stderr); exit (1); }
}
void Con_Warning (const char *fmt, ...) { Con_Printf (fmt); }
void Con_DPrintf (const char *fmt, ...) { Con_Printf (fmt); }

#define CHECK(condition) do { if (!(condition)) { fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main (void)
{
    dma_t dma;
    Uint64 start;
    int i, before_resume, samplepos;
    SDL_SetHint (SDL_HINT_AUDIO_DRIVER, "dummy");
    CHECK (SDL_Init (0));
    test_main_thread = SDL_GetCurrentThreadID ();
    snd_mixspeed.value = 44100;
    snd_surround.value = 1;
    test_delay_open = 100;

    start = SDL_GetTicks ();
    CHECK (SNDDMA_BeginInit (&dma));
    CHECK (SDL_GetTicks () - start < 80); /* 100 ms open must not block the caller */
    CHECK (dma.samplepos == 0 && test_resume_calls == 0);
    CHECK (SNDDMA_FinishInit ());
    CHECK (test_open_thread != test_main_thread && test_resume_calls == 1);
    start = SDL_GetTicks ();
    do {
        SNDDMA_LockBuffer ();
        samplepos = SNDDMA_GetDMAPos ();
        SNDDMA_Submit ();
        if (!samplepos) SDL_Delay (1);
    } while (!samplepos && SDL_GetTicks () - start < 1000);
    CHECK (samplepos != 0);
    test_delay_close = 100;
    start = SDL_GetTicks ();
    SNDDMA_BeginShutdown ();
    CHECK (SDL_GetTicks () - start < 80);
    CHECK (shm != NULL); /* callback storage remains alive until the join */
    SNDDMA_BeginShutdown (); /* repeated shutdown requests are harmless */
    SNDDMA_Shutdown ();
    CHECK (shm == NULL && !SDL_WasInit (SDL_INIT_AUDIO));
    test_delay_close = 0;

    /* Quitting during startup joins without starting playback or freeing
     * storage that the still-opening stream will reference. */
    before_resume = test_resume_calls;
    CHECK (SNDDMA_BeginInit (&dma));
    SNDDMA_Shutdown ();
    CHECK (test_resume_calls == before_resume && shm == NULL && !SDL_WasInit (SDL_INIT_AUDIO));
    SNDDMA_Shutdown ();

    test_fail_open = 1;
    CHECK (SNDDMA_BeginInit (&dma));
    CHECK (!SNDDMA_FinishInit ());
    CHECK (shm == NULL && !SDL_WasInit (SDL_INIT_AUDIO));
    test_fail_open = 0;
    test_delay_open = 0;

    test_fail_resume = 1;
    CHECK (SNDDMA_BeginInit (&dma));
    CHECK (!SNDDMA_FinishInit ());
    CHECK (shm == NULL && !SDL_WasInit (SDL_INIT_AUDIO));
    test_fail_resume = 0;

    test_fail_init = 1;
    CHECK (!SNDDMA_BeginInit (&dma));
    CHECK (shm == NULL && !SDL_WasInit (SDL_INIT_AUDIO));
    test_fail_init = 0;
    test_fail_alloc = 1;
    CHECK (!SNDDMA_BeginInit (&dma));
    CHECK (shm == NULL && !SDL_WasInit (SDL_INIT_AUDIO));
    test_fail_alloc = 0;

    test_fail_thread = 1;
    CHECK (SNDDMA_BeginInit (&dma));
    CHECK (test_open_thread == test_main_thread && SNDDMA_FinishInit ());
    SNDDMA_Shutdown ();
    CHECK (shm == NULL && !SDL_WasInit (SDL_INIT_AUDIO));
    test_fail_thread = 0;

    /* snd_restart's synchronous path keeps formats and repeated teardown. */
    for (i = 0; i < 8; i++)
    {
        loadas8bit.value = i & 1;
        snd_mixspeed.value = (i & 2) ? 22050 : 48000;
        CHECK (SNDDMA_Init (&dma));
        CHECK (test_open_thread == test_main_thread);
        CHECK (dma.samplebits == ((i & 1) ? 8 : 16) && dma.speed == (int)snd_mixspeed.value);
        SNDDMA_Shutdown ();
        CHECK (shm == NULL && !SDL_WasInit (SDL_INIT_AUDIO));
    }
    SDL_Quit ();
    puts ("PASS: asynchronous open/close, callback, early quit, init/alloc/open/resume/thread failure, repeated synchronous restart");
    return 0;
}
