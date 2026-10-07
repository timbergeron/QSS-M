"""Known sounds survive map changes and recycle stale entries instead of failing.

Compile the actual S_FindName and S_ClearPrecache against a stub cache.
A map change keeps names and cached samples, a cache flush still forces a
reload, and a table filled by earlier maps gives way to the current map's
sounds instead of hitting "out of sfx_t".
Run with CC, or from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / "Quake/snd_dma.c").read_text(encoding="utf-8")


def function(signature):
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
#define MAX_QPATH 64
#define MAX_SFX 8
#define NUM_AMBIENTS 4
typedef int qboolean;
#define true 1
#define false 0
#define CHECK(x,m) do { if (!(x)) { puts("FAIL: " m); exit(1); } } while (0)
typedef struct { void *data; } cache_user_t;
typedef struct { char name[MAX_QPATH]; cache_user_t cache; unsigned int generation; } sfx_t;
static sfx_t storage[MAX_SFX], *known_sfx = storage, *ambient_sfx[NUM_AMBIENTS];
static int num_sfx, snd_initialized = 1, stops, frees;
static unsigned int sfx_generation;
static void Sys_Error (const char *s, ...) { printf ("FAIL: %s\n", s); exit (1); }
static void Cache_Free (cache_user_t *c) { c->data = NULL; frees++; }
static void S_SoundPreview_Release (void) {}
static void S_StopAllSounds (qboolean a, qboolean b) { (void)a; (void)b; stops++; }
static size_t q_strlcpy (char *d, const char *s, size_t n) { size_t z = strlen (s), k = z < n - 1 ? z : n - 1; memcpy (d, s, k); d[k] = 0; return z; }
static char sample = 's';
''' + function("static sfx_t *S_FindName (const char *name)") + function("void S_ClearPrecache (void)") + r'''
int main (void)
{
	char name[16];
	sfx_t *a, *b;
	int i;
	a = S_FindName ("weapons/rocket.wav"); a->cache.data = &sample;
	ambient_sfx[0] = a;
	S_ClearPrecache ();
	CHECK (stops == 1 && !ambient_sfx[0], "map change stops sounds and clears ambients");
	b = S_FindName ("weapons/rocket.wav");
	CHECK (b == a && b->cache.data == &sample && frees == 0, "map change keeps the cached sample");
	b->cache.data = NULL; /* Cache_Flush on game change or memory pressure */
	CHECK (S_FindName ("weapons/rocket.wav") == a && !a->cache.data, "flushed sample is reloaded by name");
	/* Fill the table with sounds from older maps. */
	for (i = 1; i < MAX_SFX; i++)
	{
		S_ClearPrecache ();
		snprintf (name, sizeof (name), "old%d.wav", i);
		S_FindName (name)->cache.data = &sample;
	}
	CHECK (num_sfx == MAX_SFX, "table full of earlier maps");
	S_ClearPrecache ();
	b = S_FindName ("weapons/rocket.wav");
	CHECK (b == a && num_sfx == MAX_SFX, "current map still finds kept sounds");
	b = S_FindName ("new.wav");
	CHECK (b != a && !strcmp (b->name, "new.wav") && !b->cache.data && frees == 1, "stale entry recycled and its sample freed");
	/* Recycling never takes a sound this map already asked for. */
	for (i = 0; i < MAX_SFX - 2; i++)
	{
		snprintf (name, sizeof (name), "map%d.wav", i);
		S_FindName (name);
	}
	CHECK (!strcmp (a->name, "weapons/rocket.wav"), "current-map sound kept while recycling");
	puts ("PASS: sounds survive map changes, flushed samples reload, stale entries recycle without overflow");
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
