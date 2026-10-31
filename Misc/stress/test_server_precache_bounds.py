"""Exercise production server precache boundaries with ASan/UBSan.

Run: python3 Misc/stress/test_server_precache_bounds.py (requires cc and SDL3).
Use --baseline REV --case NAME to reproduce an individual failure before a fix.
The harness extracts production functions/blocks; networking, QC arguments and
model loading are stubbed, with the engine's actual types and catalog sizes.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', help='read production sources from this git revision')
parser.add_argument('--case', choices=('inline', 'models-warning', 'sounds-warning',
                                     'models-send', 'sounds-send', 'setmodel', 'ambient',
                                     'registration'))
options = parser.parse_args()


def read(name):
    if options.baseline:
        return subprocess.check_output(['git', 'show', f'{options.baseline}:Quake/{name}'],
                                       cwd=ROOT, text=True)
    return (ROOT / 'Quake' / name).read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index('\n}', start) + 2] + '\n'


server = read('sv_main.c')
commands = read('pr_cmds.c')
source = r'''
#include "quakedef.h"
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <sanitizer/asan_interface.h>
#undef main
server_t sv;
THREAD_LOCAL qcvm_t *qcvm;
cvar_t developer, pr_checkextension, sv_gameplayfix_setmodelrealbox;
vec3_t vec3_origin;
static qmodel_t world;
static edict_t entity;
static char localmodels[MAX_MODELS][8];
static const char *sample;
static vec3_t position = {1, 2, 3};
static jmp_buf error_jump;
static int warnings, string_writes, byte_writes, model_loads, size_updates, checks;
static const char **send_catalog;
static char error_text[256], last_string[512];
#undef G_EDICT
#undef G_STRING
#undef G_VECTOR
#undef G_FLOAT
#define G_EDICT(o) (&entity)
#define G_STRING(o) sample
#define G_VECTOR(o) position
#define G_FLOAT(o) ((o) == OFS_PARM2 ? 0.5f : 1.0f)
void PR_RunError(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_text, sizeof(error_text), fmt, args);
    va_end(args);
    longjmp(error_jump, 1);
}
void Con_Printf(const char *fmt, ...) { (void)fmt; }
void Con_Warning(const char *fmt, ...) { (void)fmt; warnings++; }
const char *NET_QSocketGetTrueAddressString(const struct qsocket_s *socket) {
    (void)socket; return "test";
}
char *va(const char *fmt, ...) {
    static char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}
void MSG_WriteByte(sizebuf_t *message, int value) {
    (void)message; (void)value; byte_writes++;
}
void MSG_WriteShort(sizebuf_t *message, int value) { (void)message; (void)value; }
void MSG_WriteString(sizebuf_t *message, const char *value) {
    (void)message;
    string_writes++;
    if (send_catalog) assert(value == send_catalog[string_writes]);
    snprintf(last_string, sizeof(last_string), "%s", value);
}
qmodel_t *Mod_ForName(const char *name, qboolean crash) {
    (void)name; (void)crash; model_loads++; return &world;
}
int PR_SetEngineString(const char *value) {
    assert(value && !strcmp(value, sample)); return 17;
}
static void SetMinMaxSize(edict_t *ent, float *mins, float *maxs, qboolean rotate) {
    (void)mins; (void)maxs; (void)rotate; assert(ent == &entity); size_updates++;
}
'''
for declaration in ('int SV_Precache_Model(', 'int SV_Precache_Sound(',
                    'static void PF_sv_setmodel (', 'static void PF_sv_ambientsound ('):
    source += function(commands, declaration)

start = server.index('\tif (qcvm->worldmodel->numsubmodels ')
block = server[start:server.index('\n//\n// load the rest of the entities', start)]
source += 'static void fill_inline(void) { int i;\n' + block + '\n}\n'
for kind in ('models', 'sounds'):
    start = server.index(f'\tif (client->limit_{kind} < MAX_')
    end = server.index('\n\t}', start) + len('\n\t}')
    source += (f'static void warn_{kind}(client_t *client) {{ unsigned int i;\n'
               + server[start:end] + '\n}\n')
    # Include the original loop, terminator and signon cursor assignment.
    marker = ('for (i = 1,s = sv.model_precache+1' if kind == 'models'
              else 'for (i = 1, s = sv.sound_precache+1')
    start = server.index(marker)
    end = server.index(f'client->signon_{kind} = i;', start) + len(f'client->signon_{kind} = i;')
    source += (f'static void send_{kind}(client_t *client) {{ unsigned int i; const char **s;\n'
               + server[start:end] + '\n}\n')

source += r'''
static void reset(void) {
    free(sv.ambientsounds);
    memset(&sv, 0, sizeof(sv));
    memset(&entity, 0, sizeof(entity));
    memset(&world, 0, sizeof(world));
    qcvm = &sv.qcvm;
    qcvm->worldmodel = &world;
    sv.active = true;
    sv.state = ss_loading;
    pr_checkextension.value = 0;
    warnings = string_writes = byte_writes = model_loads = size_updates = 0;
    send_catalog = NULL;
    error_text[0] = last_string[0] = 0;
}
static void catalog(int sounds, int count) {
    const char **table = sounds ? sv.sound_precache : sv.model_precache;
    for (int i = 0; i < count; i++) table[i] = "entry";
    if (count) table[count-1] = "last";
}
static void test_inline(void) {
    const int counts[] = {1, 2, MAX_MODELS-2, MAX_MODELS-1, MAX_MODELS, MAX_MODELS+1, INT_MAX};
    for (unsigned n = 0; n < sizeof(counts)/sizeof(counts[0]); n++) {
        reset();
        int count = world.numsubmodels = counts[n];
        sv.model_precache[0] = "";
        sv.model_precache[1] = "world";
        sv.models[1] = &world;
        fill_inline();
        checks++;
        if (count >= MAX_MODELS) {
            assert(!sv.active && !model_loads);
            assert(!sv.model_precache[2] && !sv.models[2]);
        } else {
            assert(sv.active && model_loads == count-1);
            for (int i = 1; i < count; i++) {
                assert(sv.model_precache[i+1] == localmodels[i]);
                assert(sv.models[i+1] == &world);
            }
            if (count+1 < MAX_MODELS) assert(!sv.model_precache[count+1]);
        }
        assert(!strcmp(sv.model_precache[1], "world") && sv.models[1] == &world);
    }
}
static void test_catalog(int sounds, int sending) {
    int maximum = sounds ? MAX_SOUNDS : MAX_MODELS;
    const int counts[] = {0, 1, 2, 255, 256, 257, 1024, maximum-1, maximum};
    const unsigned limits[] = {1, 2, 256, 1024, maximum-1, maximum, maximum+1, maximum*2};
    for (unsigned c = 0; c < sizeof(counts)/sizeof(counts[0]); c++)
    for (unsigned l = 0; l < sizeof(limits)/sizeof(limits[0]); l++) {
        reset();
        client_t client = {0};
        int count = counts[c];
        unsigned limit = limits[l];
        catalog(sounds, count);
        client.limit_models = client.limit_sounds = limit;
        checks++;
        if (sending) {
            send_catalog = sounds ? sv.sound_precache : sv.model_precache;
            /* Catch one-past reads even though the next struct field exists. */
            __asan_poison_memory_region(send_catalog + maximum, sizeof(*send_catalog));
            if (sounds) send_sounds(&client); else send_models(&client);
            __asan_unpoison_memory_region(send_catalog + maximum, sizeof(*send_catalog));
            unsigned end = count < (int)limit ? (unsigned)count : limit;
            if (!end) end = 1;
            assert(string_writes == (int)end-1 && byte_writes == 1);
            assert((sounds ? client.signon_sounds : client.signon_models) == end);
        } else {
            if (sounds) warn_sounds(&client); else warn_models(&client);
            assert(warnings == (limit < (unsigned)count));
            if (warnings) {
                char expected[64];
                snprintf(expected, sizeof(expected), "(%u / %d)", limit, count);
                assert(strstr(last_string, expected));
                assert(string_writes == 2 && byte_writes == 3);
            } else assert(!string_writes && !byte_writes);
        }
    }
}
static int try_setmodel(void) {
    if (setjmp(error_jump)) return 1;
    PF_sv_setmodel();
    return 0;
}
static void check_setmodel(int count, int extension, int missing) {
    reset();
    catalog(0, count);
    for (int i = 0; i < count; i++) sv.models[i] = &world;
    /* Adjacent nonempty catalogs must never be mistaken for model entries. */
    sv.sound_precache[0] = "";
    sv.sound_precache[1] = "new";
    sample = missing ? "new" : "last";
    pr_checkextension.value = extension;
    int rejected = try_setmodel();
    checks++;
    assert(!!rejected == (missing && (!extension || count == MAX_MODELS)));
    if (rejected) {
        assert(error_text[0] && !size_updates && !entity.v.modelindex);
        assert(!model_loads);
    } else {
        assert(entity.v.model == 17 && size_updates == 1);
        int index = missing ? count : count-1;
        assert(entity.v.modelindex == index);
        assert(!strcmp(sv.model_precache[index], sample));
        assert(sv.models[index] == &world);
        assert(model_loads == missing);
    }
}
static void test_setmodel(void) {
    const int counts[] = {1, 2, MAX_MODELS-1, MAX_MODELS};
    for (unsigned n = 0; n < sizeof(counts)/sizeof(counts[0]); n++)
    for (int extension = 0; extension < 2; extension++)
    for (int missing = 0; missing < 2; missing++)
        check_setmodel(counts[n], extension, missing);
}
static void test_ambient(void) {
    const int counts[] = {1, 2, MAX_SOUNDS-1, MAX_SOUNDS};
    for (unsigned n = 0; n < sizeof(counts)/sizeof(counts[0]); n++)
    for (int missing = 0; missing < 2; missing++) {
        reset();
        int count = counts[n];
        catalog(1, count);
        /* A matching string in the next field is outside the sound catalog. */
        sv.lightstyles[0] = "new";
        sample = missing ? "new" : "last";
        PF_sv_ambientsound();
        checks++;
        assert(sv.num_ambients == !missing);
        if (!missing) {
            const struct ambientsound_s *ambient = &sv.ambientsounds[0];
            assert(ambient->soundindex == (unsigned)(count-1));
            assert(ambient->volume == 0.5f && ambient->attenuation == 1.0f);
            assert(!memcmp(ambient->origin, position, sizeof(position)));
        }
    }
}
static void test_registration(void) {
    for (int sounds = 0; sounds < 2; sounds++) {
        reset();
        int maximum = sounds ? MAX_SOUNDS : MAX_MODELS;
        catalog(sounds, maximum-1);
        const char **table = sounds ? sv.sound_precache : sv.model_precache;
        int index = sounds ? SV_Precache_Sound("final") : SV_Precache_Model("final");
        checks++;
        assert(index == maximum-1 && !strcmp(table[maximum-1], "final"));
        index = sounds ? SV_Precache_Sound("overflow") : SV_Precache_Model("overflow");
        checks++;
        assert(!index && !strcmp(table[maximum-1], "final"));
        index = sounds ? SV_Precache_Sound("final") : SV_Precache_Model("final");
        checks++;
        assert(index == maximum-1);
        assert(model_loads == !sounds);
    }
}
int main(int argc, char **argv) {
    const char *only = argc > 1 ? argv[1] : "";
#define RUN(name, call) do { if (!*only || !strcmp(only, name)) { call; } } while (0)
    RUN("inline", test_inline());
    RUN("models-warning", test_catalog(0, 0));
    RUN("sounds-warning", test_catalog(1, 0));
    RUN("models-send", test_catalog(0, 1));
    RUN("sounds-send", test_catalog(1, 1));
    RUN("setmodel", test_setmodel());
    RUN("ambient", test_ambient());
    RUN("registration", test_registration());
    free(sv.ambientsounds);
    printf("PASS: %d server precache boundary cases%s%s\n", checks, *only ? ": " : "", only);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='qssm-server-precaches-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    cc = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run([*cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'Quake'), str(work / 'test.c'),
                    '-o', str(work / 'test'), *flags, '-lm'], check=True)
    subprocess.run([str(work / 'test'), *([options.case] if options.case else [])], check=True)
