"""Known sounds survive map changes safely.

Compile the actual S_FindName, S_ClearPrecache and S_LoadSound against a stub
cache, file system and mixer. Checks: a map change keeps PACK-backed samples;
loose files and sounds whose PACK no longer provides them reload; entries are
never reassigned to another name while kept (long-lived sfx_t pointers stay
valid); more than a map's worth of entries starts over like the original
code; and cached samples made for a different output rate are reloaded.
Run with CC, or from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
dma = (ROOT / "Quake/snd_dma.c").read_text(encoding="utf-8")
mem = (ROOT / "Quake/snd_mem.c").read_text(encoding="utf-8")


def function(text, signature):
    start = text.index(signature)
    depth, end = 1, text.index("{", start) + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


source = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define MAX_QPATH 64
#define MAX_SOUNDS 4
#define MAX_SFX (MAX_SOUNDS * 2 + 2)
#define NUM_AMBIENTS 4
typedef int qboolean;
typedef long long qofs_t;
typedef unsigned char byte;
#define true 1
#define false 0
#define CHECK(x,m) do { if (!(x)) { puts("FAIL: " m); exit(1); } } while (0)
typedef struct { void *data; } cache_user_t;
typedef struct { char name[MAX_QPATH]; cache_user_t cache; unsigned int generation; const void *source; } sfx_t;
typedef struct { int length, loopstart, speed, width, stereo; byte data[1]; } sfxcache_t;
typedef struct { int speed; } dma_t;
typedef struct { int rate, width, channels, loopstart, samples, dataofs; } wavinfo_t;
typedef struct { int rate, width, channels; } snd_info_t;
typedef struct { snd_info_t info; } snd_stream_t;
static struct { qboolean suppress_precache_miss_warnings; int suppressed_sound_precache_warnings; } cl;
static dma_t dma = {44100}, *shm = &dma;
static sfx_t storage[MAX_SFX], *known_sfx = storage, *ambient_sfx[NUM_AMBIENTS];
static int num_sfx, snd_initialized = 1, file_from_pak, loads, frees;
static qofs_t com_filesize;
static unsigned int sfx_generation;
static int pak_a, pak_b, *provider = &pak_a;   /* which PACK currently provides sound/ files */
static int loose;                               /* files come from a loose directory */
static void Sys_Error (const char *s, ...) { printf ("FAIL: %s\n", s); exit (1); }
static void Con_Printf (const char *s, ...) { (void)s; }
static void S_SoundPreview_Release (void) {}
static void S_StopAllSounds (qboolean a, qboolean b) { (void)a; (void)b; }
static size_t q_strlcpy (char *d, const char *s, size_t n) { size_t z = strlen (s), k = z < n - 1 ? z : n - 1; memcpy (d, s, k); d[k] = 0; return z; }
static size_t q_strlcat (char *d, const char *s, size_t n) { size_t l = strlen (d); return l + q_strlcpy (d + l, s, n - l); }
static int q_strncasecmp (const char *a, const char *b, size_t n) { return strncmp (a, b, n); }
static char *va (const char *f, ...) { static char b[256]; va_list ap; va_start (ap, f); vsnprintf (b, sizeof (b), f, ap); va_end (ap); return b; }
static const char *COM_FileGetExtension (const char *s) { const char *p = strrchr (s, '.'); return p ? p + 1 : ""; }
static const void *COM_FileSearchPath (const char *f) { (void)f; return loose ? (const void *)&loose : (const void *)provider; }
static snd_stream_t *S_CodecOpenStreamExt (const char *n, qboolean l) { (void)n; (void)l; return NULL; }
static int S_CodecReadStream (snd_stream_t *s, int n, void *b) { (void)s; (void)n; (void)b; return 0; }
static void S_CodecCloseStream (snd_stream_t *s) { (void)s; }
static void FMod_CheckModel (const char *n, const void *d, size_t l) { (void)n; (void)d; (void)l; }
static byte *COM_LoadMallocFile (const char *n, unsigned int *id) { (void)n; (void)id; file_from_pak = !loose; com_filesize = 64; loads++; return (byte *)calloc (1, 64); }
static wavinfo_t GetWavinfo (const char *n, byte *d, qofs_t l) { wavinfo_t w = {11025, 1, 1, -1, 16, 0}; (void)n; (void)d; (void)l; return w; }
static void *Cache_Check (cache_user_t *c) { return c->data; }
static void Cache_Free (cache_user_t *c) { free (c->data); c->data = NULL; frees++; }
static void *Cache_Alloc (cache_user_t *c, int size, const char *n, void *x) { (void)n; (void)x; return c->data = calloc (1, size); }
static void ResampleSfx (sfx_t *s, int rate, int width, byte *data) { sfxcache_t *sc = (sfxcache_t *)s->cache.data; (void)rate; (void)width; (void)data; sc->speed = shm->speed; }
''' + function(dma, "static sfx_t *S_FindName (const char *name)") + function(dma, "void S_ClearPrecache (void)") + function(mem, "sfxcache_t *S_LoadSound (sfx_t *s)") + r'''
int main (void)
{
	sfx_t *a, *b, *keep;
	char name[16];
	int i;

	a = S_FindName ("weapons/rocket.wav");
	CHECK (S_LoadSound (a) && loads == 1 && a->source == &pak_a, "first load records its PACK");
	ambient_sfx[0] = a;
	S_ClearPrecache ();
	CHECK (!ambient_sfx[0], "map change clears ambients");
	CHECK (S_FindName ("weapons/rocket.wav") == a && a->cache.data && frees == 0, "PACK sample kept across a map change");
	CHECK (S_LoadSound (a) && loads == 1, "kept sample is not reloaded");

	S_ClearPrecache ();
	provider = &pak_b;                                   /* a different PACK now provides the file */
	S_FindName ("weapons/rocket.wav");
	CHECK (!a->cache.data && frees == 1, "sound shadowed by another PACK reloads");
	S_LoadSound (a);
	CHECK (loads == 2 && a->source == &pak_b, "reload records the new PACK");

	loose = 1;                                           /* a loose file: modders edit these */
	b = S_FindName ("misc/edit.wav");
	S_LoadSound (b);
	CHECK (b->source == NULL, "loose file has no reusable source");
	S_ClearPrecache ();
	S_FindName ("misc/edit.wav");
	CHECK (!b->cache.data, "loose file reloads on the next map");
	loose = 0;

	S_LoadSound (a);
	dma.speed = 48000;                                   /* audio restarted at another rate */
	i = loads;
	CHECK (S_LoadSound (a) && loads == i + 1 && ((sfxcache_t *)a->cache.data)->speed == 48000, "samples for the old rate are rebuilt");

	keep = a;                                            /* e.g. a cached temp-entity pointer */
	for (i = 0; num_sfx < MAX_SOUNDS; i++)
	{
		snprintf (name, sizeof (name), "old%d.wav", i);
		S_FindName (name);
	}
	S_ClearPrecache ();                                  /* not over the threshold yet */
	CHECK (num_sfx == MAX_SOUNDS && !strcmp (keep->name, "weapons/rocket.wav"), "kept entries keep their names");
	for (i = 0; i < MAX_SOUNDS; i++)
	{
		snprintf (name, sizeof (name), "new%d.wav", i);
		CHECK (S_FindName (name) != keep, "a new name never takes a kept slot");
	}
	CHECK (!strcmp (keep->name, "weapons/rocket.wav"), "long-lived pointer still names its sound");
	S_ClearPrecache ();                                  /* over a map's worth: start over */
	CHECK (num_sfx == 0 && keep->name[0] == 0, "table resets like the original code");
	puts ("PASS: PACK samples reused, shadowed and loose sounds reload, wrong-rate samples rebuilt, no slot reuse, reset past a map's worth");
	return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-sound-reuse-") as tmp:
    work = Path(tmp)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        args = [*cc, "/nologo", "/W3", "/O2", "test.c", f"/Fe:{binary}"]
    else:
        args = [*cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-fsanitize=address,undefined", "test.c", "-o", str(binary)]
    subprocess.run(args, cwd=work, check=True)
    subprocess.run([str(binary)], cwd=work, check=True)
