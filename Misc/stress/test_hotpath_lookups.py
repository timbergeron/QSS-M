"""Check indexed cvar lookup and static sound combining against linear references.

Run with python3 Misc/stress/test_hotpath_lookups.py (requires cc and SDL2).
Timings measure these routines only, not whole-engine FPS.
"""

from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
cvar = (ROOT / "Quake/cvar.c").read_text()
sound = (ROOT / "Quake/snd_dma.c").read_text()
common = (ROOT / "Quake/common.c").read_text()
mathlib = (ROOT / "Quake/mathlib.c").read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


SOURCE = r'''
#include "quakedef.h"
#include <assert.h>

static double now(void) {
    return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}
void *Z_Malloc(int size) { void *p = calloc(1, size); assert(p); return p; }
void Con_Printf(const char *fmt, ...) { (void)fmt; }
void Con_SafePrintf(const char *fmt, ...) { (void)fmt; }
void *Cache_Check(cache_user_t *cache) { return cache->data; }
qboolean Cmd_Exists(const char *name) { return !strcmp(name, "test_command"); }
void Cvar_SetQuick(cvar_t *var, const char *value) {
    assert(Cvar_FindVar(var->name) == var); /* visible during initialization */
    var->string = strdup(value);
    var->value = atof(value);
    if (var->callback) var->callback(var);
}
'''
start = cvar.index("static cvar_t\t*cvar_vars;")
SOURCE += cvar[start:cvar.index("typedef struct mapcvar_s", start)]
for declaration in (
    "cvar_t *Cvar_FindVar (", "cvar_t *Cvar_FindVarAfter (",
    "void Cvar_RegisterAlias(", "void Cvar_RegisterVariable (", "cvar_t *Cvar_Create (",
):
    SOURCE += function(cvar, declaration)
SOURCE += function(common, "int Q_strcmp (")
SOURCE += (ROOT / "Quake/strlcpy.c").read_text()

SOURCE += r'''
static cvar_t *linear_lookup(const char *name) {
    for (cvar_t *var = cvar_vars; var; var = var->next)
        if (!Q_strcmp(name, var->name)) return var;
    for (struct cvaralias_s *alias = cvar_aliases; alias; alias = alias->next)
        if (!Q_strcmp(name, alias->name)) return alias->cvar;
    return NULL;
}
static unsigned int random_state = 1234567;
static unsigned int next_random(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}
'''
names = sorted({name for path in (ROOT / "Quake").glob("*.c")
                for name in re.findall(r'cvar_t\s+\w+\s*=\s*\{\s*"([^"\n]+)"',
                                       path.read_text(errors="replace"))})
SOURCE += "static const char *names[] = {" + ",".join(map(json.dumps, names)) + "};\n"
SOURCE += r'''
static cvar_t *volatile lookup_sink;
static double benchmark_lookup(cvar_t *(*lookup)(const char *), int repeats) {
    double start = now();
    for (int i = 0; i < repeats; ++i)
        lookup_sink = lookup(names[(unsigned int)i * 197u % Q_COUNTOF(names)]);
    return (now() - start) * 1e9 / repeats;
}
static void check_cvars(void) {
    /* Register out of order, exercising bucket collisions and sorted links. */
    for (int i = Q_COUNTOF(names) - 1; i >= 0; --i)
        assert(Cvar_Create(names[i], "1"));
    for (int i = 0; i < (int)Q_COUNTOF(names); ++i)
        assert(Cvar_FindVar(names[i]) == linear_lookup(names[i]));
    assert(!Cvar_FindVar("missing_test_variable"));
    assert(!Cvar_FindVar("HOST_MAXFPS")); /* lookup remains case-sensitive */
    assert(!Cvar_Create("test_command", "0"));
    cvar_t *base = Cvar_Create("test_original", "42");
    assert(base && base->value == 42);
    Cvar_RegisterAlias(base, "test_alias");
    assert(Cvar_FindVar("test_alias") == base);
    assert(Cvar_Create("test_alias", "99") == base && base->value == 42);
    cvar_t duplicate = { .name = "test_alias", .string = "3" };
    Cvar_RegisterVariable(&duplicate);
    assert(!(duplicate.flags & CVAR_REGISTERED));
    Cvar_RegisterAlias(base, "test_command");
    assert(!Cvar_FindVar("test_command"));
    for (int i = 0; i < 500; ++i) {
        char name[64];
        snprintf(name, sizeof(name), "user_%u", next_random());
        cvar_t *created = Cvar_Create(name, "2");
        assert(created && Cvar_FindVar(name) == linear_lookup(name));
    }
    int count = 0;
    const char *prev = "";
    for (cvar_t *var = Cvar_FindVarAfter("", 0); var; var = Cvar_FindVarAfter(var->name, 0)) {
        assert(strcmp(prev, var->name) < 0);
        assert(Cvar_FindVar(var->name) == var);
        prev = var->name;
        ++count;
    }
    base->flags |= CVAR_ARCHIVE;
    assert(Cvar_FindVarAfter("", CVAR_ARCHIVE) == base);
    assert(!Cvar_FindVarAfter(base->name, CVAR_ARCHIVE));
    double before = benchmark_lookup(linear_lookup, 500000);
    double after = benchmark_lookup(Cvar_FindVar, 500000);
    printf("Cvars (%d registered): linear %.1f ns, indexed %.1f ns per lookup (%.1fx)\n",
        count, before, after, before / after);
}

client_state_t cl;
channel_t *snd_channels;
int total_channels;
vec3_t listener_origin, listener_forward, listener_right, listener_up;
float voicevolumescale = 1;
static dma_t dma = { .channels = 2 };
volatile dma_t *shm = &dma;
static qboolean sound_started = true;
static int snd_blocked;
static cvar_t snd_show;
#define MAX_SFX MAX_SOUNDS
static sfx_t effects[MAX_SFX];
static sfx_t *known_sfx = effects;
static int num_sfx = MAX_SFX;
static void S_UpdateAmbientSounds(void) {}
static void S_Update_(void) {}
'''
start = sound.index("#define SOUND_PREVIEW_ENTNUM")
SOURCE += sound[start:sound.index("\n", sound.index("#define FIRST_STATIC_SOUND_CHANNEL", start))]
SOURCE += "\n" + function(mathlib, "float VectorNormalize (")
SOURCE += function(sound, "void SND_Spatialize (")
SOURCE += function(sound, "void S_Update (")
SOURCE += r'''
static void linear_sound_update(vec3_t origin, vec3_t forward, vec3_t right, vec3_t up) {
    channel_t *combine = NULL;
    VectorCopy(origin, listener_origin);
    VectorCopy(forward, listener_forward);
    VectorCopy(right, listener_right);
    VectorCopy(up, listener_up);
    for (int i = NUM_AMBIENTS; i < total_channels; ++i) {
        channel_t *ch = &snd_channels[i];
        if (!ch->sfx) continue;
        SND_Spatialize(ch);
        if (!ch->leftvol && !ch->rightvol) continue;
        if (i < FIRST_STATIC_SOUND_CHANNEL) continue;
        if (!combine || combine->sfx != ch->sfx) {
            combine = snd_channels + FIRST_STATIC_SOUND_CHANNEL;
            while (combine < ch && combine->sfx != ch->sfx) ++combine;
        }
        if (combine != ch) {
            combine->leftvol += ch->leftvol;
            combine->rightvol += ch->rightvol;
            ch->leftvol = ch->rightvol = 0;
        }
    }
}
static vec3_t origin, forward = {1, 0, 0}, right = {0, 1, 0}, up = {0, 0, 1};
static void check_sound_case(int channels, int types, int silent_percent) {
    size_t bytes = channels * sizeof(channel_t);
    channel_t *input = calloc(channels, sizeof(*input));
    channel_t *expected = malloc(bytes);
    channel_t *actual = malloc(bytes);
    assert(input && expected && actual);
    num_sfx = types; /* also exercise shrinking/reloading the sound registry */
    cl.viewentity = 1;
    voicevolumescale = 0.75f;
    for (int i = 0; i < channels; ++i) {
        channel_t *ch = input + i;
        ch->sfx = &effects[next_random() % types];
        ch->master_vol = next_random() % 256;
        ch->entnum = (next_random() % 8) ? 0 : cl.viewentity;
        ch->entchannel = (next_random() % 20) ? 0 : -2;
        ch->pos = next_random() % 10000;
        ch->end = 10000;
        ch->origin[0] = (int)(next_random() % 5000) - 2500;
        ch->origin[1] = (int)(next_random() % 5000) - 2500;
        ch->dist_mult = 1.0f / 1500.0f;
        if (next_random() % 100 < (unsigned int)silent_percent) ch->master_vol = 0;
        if (!(next_random() % 16)) ch->sfx = NULL;
    }
    /* A silent first static channel must still own the combined waveform. */
    if (channels > FIRST_STATIC_SOUND_CHANNEL + 1) {
        input[FIRST_STATIC_SOUND_CHANNEL].sfx = &effects[0];
        input[FIRST_STATIC_SOUND_CHANNEL].master_vol = 0;
        input[FIRST_STATIC_SOUND_CHANNEL + 1].sfx = &effects[0];
        input[FIRST_STATIC_SOUND_CHANNEL + 1].master_vol = 255;
        input[FIRST_STATIC_SOUND_CHANNEL + 1].entnum = cl.viewentity;
    }
    memcpy(expected, input, bytes);
    memcpy(actual, input, bytes);
    total_channels = channels;
    snd_channels = expected;
    linear_sound_update(origin, forward, right, up);
    snd_channels = actual;
    S_Update(origin, forward, right, up);
    assert(!memcmp(expected, actual, bytes)); /* volumes, phase, routing */
    free(input); free(expected); free(actual);
}
static double benchmark_sound(void (*update)(vec3_t, vec3_t, vec3_t, vec3_t), int repeats) {
    double start = now();
    for (int i = 0; i < repeats; ++i) update(origin, forward, right, up);
    return (now() - start) * 1e6 / repeats;
}
static void check_sounds(void) {
    const int counts[] = {FIRST_STATIC_SOUND_CHANNEL, 64, 256, 1024, 4096};
    for (int c = 0; c < (int)Q_COUNTOF(counts); ++c)
        for (int types = 1; types <= 256; types *= 4)
            for (int silent = 0; silent <= 100; silent += 25)
                check_sound_case(counts[c], types, silent);
    total_channels = 1024 + FIRST_STATIC_SOUND_CHANNEL;
    num_sfx = 256;
    snd_channels = calloc(total_channels, sizeof(*snd_channels));
    assert(snd_channels);
    voicevolumescale = 1;
    for (int types = 1; types <= 256; types *= 16) {
        for (int i = FIRST_STATIC_SOUND_CHANNEL; i < total_channels; ++i) {
            snd_channels[i].sfx = &effects[(i - FIRST_STATIC_SOUND_CHANNEL) % types];
            snd_channels[i].master_vol = 100;
            snd_channels[i].entnum = cl.viewentity;
        }
        double before = benchmark_sound(linear_sound_update, 10000);
        double after = benchmark_sound(S_Update, 10000);
        printf("1024 static sounds / %d effects: linear %.2f us, indexed %.2f us per update (%.1fx)\n",
            types, before, after, before / after);
    }
    free(snd_channels);
}
int main(void) {
    check_cvars();
    check_sounds();
    puts("PASS: cvar collisions/aliases/dynamic registration/order; identical sound channels across 125 cases");
    return 0;
}
'''


with tempfile.TemporaryDirectory(prefix="qssm-hotpath-") as tmp:
    path = Path(tmp)
    source = path / "test.c"
    binary = path / "test"
    source.write_text(SOURCE)
    flags = shlex.split(subprocess.check_output(["sdl2-config", "--cflags", "--libs"], text=True))
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-std=gnu11", "-DUSE_SDL2",
                    "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-I", str(ROOT / "Quake"),
                    str(source), "-o", str(binary), *flags, "-lm"], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
