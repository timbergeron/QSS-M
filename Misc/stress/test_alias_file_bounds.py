#!/usr/bin/env python3
"""Run production MDL preflight and pose-cycle code under ASan/UBSan."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(file, declaration):
    source = (ROOT / 'Quake' / file).read_text()
    start = source.index(declaration)
    return source[start:source.index('\n}', start) + 2] + '\n'


source = r'''
#include "quakedef.h"
#include <assert.h>
#include <float.h>
#undef main
static int littlelong(int n) { return n; }
static float littlefloat(float n) { return n; }
#define LittleLong littlelong
#define LittleFloat littlefloat
'''
source += function('gl_model.c', 'static const void *Mod_AliasTake (')
source += function('gl_model.c', 'static const char *Mod_ValidateAliasModel (')
source += function('r_alias.c', 'static double R_AliasPoseCycle (')
texture = (ROOT / 'Quake/gl_texmgr.c').read_text()
a = texture.rfind('typedef struct', 0, texture.index('} floodfill_t;'))
b = texture.index('/*', texture.index('static void TexMgr_FloodFillSkin ('))
source += 'unsigned int d_8to24table[256];\n' + texture[a:b]

source += r'''
static aliashdr_t *pheader;
static int posenum;
trivertx_t *poseverts_mdl[MAXALIASFRAMES];
static qmodel_t model, *loadmodel = &model;
static byte *mod_base;
static char loadname[] = "test";
static cvar_t gl_load24bit;
static union { max_align_t align; byte data[65536]; } arena;
static size_t arena_used;
static gltexture_t uploaded[8];
static int uploads;
void *Hunk_AllocName(int n, const char *name) {
    (void)name; void *p = arena.data + arena_used; arena_used += (n+15)&~15;
    assert(arena_used <= sizeof(arena.data)); return p;
}
int Hunk_LowMark(void) { return (int)arena_used; }
void Hunk_FreeToLowMark(int n) { arena_used = n; }
static void Mod_AliasBadFile(const char *s) { (void)s; abort(); }
static qboolean Mod_CheckFullbrights(byte *pixels, int count) { (void)pixels; (void)count; return false; }
byte *Image_LoadImage(const char *n, int *w, int *h, enum srcformat *f, qboolean *m) {
    (void)n; (void)w; (void)h; (void)f; (void)m; return NULL;
}
int q_snprintf(char *out, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vsnprintf(out, size, fmt, ap); va_end(ap); return n;
}
gltexture_t *TexMgr_LoadImage(qmodel_t *owner, const char *name, int w, int h,
    enum srcformat fmt, byte *pixels, const char *file, src_offset_t offset, unsigned flags) {
    (void)owner; (void)name; (void)w; (void)h; (void)fmt; (void)file; (void)offset; (void)flags;
    assert(uploads < 8); gltexture_t *t = &uploaded[uploads++]; t->source_width = pixels[0]; return t;
}
'''
source += function('gl_model.c', 'static void *Mod_LoadAllSkins (')
source += function('gl_model.c', 'static void *Mod_LoadAliasGroup (')
source += r'''
static byte data[65536];
static size_t used;
static void *append(size_t n) { void *p = data + used; used += n; assert(used <= sizeof(data)); return p; }
static void fixture(int skins, int poses, int forge) {
    memset(data, 0, sizeof(data)); used = 0;
    mdl_t *m = append(sizeof(*m));
    m->version = ALIAS_VERSION; m->numskins = 1; m->skinwidth = 4; m->skinheight = 1;
    m->numverts = 3; m->numtris = 1; m->numframes = 1;
    daliasskintype_t *s = append(sizeof(*s));
    s->type = skins ? ALIAS_SKIN_GROUP : ALIAS_SKIN_SINGLE;
    if (skins) {
        daliasskingroup_t *g = append(sizeof(*g)); g->numskins = skins;
        for (int i = 0; i < skins; i++) { daliasskininterval_t *t = append(sizeof(*t)); t->interval = .1f; }
    }
    append(4 * (skins ? skins : 1));
    append(3 * sizeof(stvert_t));
    dtriangle_t *t = append(sizeof(*t)); t->vertindex[1] = 1; t->vertindex[2] = 2;
    daliasframetype_t *f = append(sizeof(*f)); f->type = poses ? ALIAS_GROUP : ALIAS_SINGLE;
    if (poses) {
        daliasgroup_t *g = append(sizeof(*g)); g->numframes = poses;
        for (int i = 0; i < poses; i++) { daliasinterval_t *t = append(sizeof(*t)); t->interval = .1f; }
    }
    for (int i = 0; i < (poses ? poses : 1); i++) {
        daliasframe_t *f = append(sizeof(*f)); memset(f->name, 'x', sizeof(f->name));
        trivertx_t *v = append(3 * sizeof(*v) * (forge ? 2 : 1)); v[0].lightnormalindex = 255;
    }
}
static void check_truncations(int skins, int poses, int forge) {
    fixture(skins, poses, forge);
    int type = forge ? PV_QUAKEFORGE : PV_QUAKE1;
    assert(!Mod_ValidateAliasModel(data, used, type));
    for (size_t n = 0; n < used; n++) {
        byte *exact = malloc(n ? n : 1); memcpy(exact, data, n);
        assert(Mod_ValidateAliasModel(exact, n, type)); free(exact);
    }
}
static void check_skin_slots(void) {
    for (int count = 1; count <= 100; count++) {
        fixture(count, 0, 0);
        byte *pixels = data + sizeof(mdl_t) + 8 + count * 4;
        for (int j=0; j<count; j++) memset(pixels + j*4, j, 4);
        memset(&arena, 0, sizeof(arena)); arena_used = 0; uploads = 0;
        pheader = Hunk_AllocName(sizeof(*pheader), "header");
        pheader->skinwidth = 4; pheader->skinheight = 1;
        mod_base = data;
        void *end = Mod_LoadAllSkins(1, (daliasskintype_t *)(data + sizeof(mdl_t)));
        assert(end == pixels + count*4);
        assert(uploads == (count < 4 ? count : 4));
        int expected[4] = {0};
        for (int j=0; j<count; j++) expected[j&3] = j;
        for (int j=count; j<4; j++) expected[j] = expected[j-count];
        for (int j=0; j<4; j++) assert(pheader->textures[0][j].base->source_width == (unsigned int)expected[j]);
        assert(*((byte *)pheader + pheader->texels[0]) == 0);
    }
}
int main(int argc, char **argv) {
    for (int arg=1; arg<argc; arg++) {
        FILE *file = fopen(argv[arg], "rb"); assert(file);
        assert(!fseek(file, 0, SEEK_END)); long size = ftell(file); assert(size >= 0);
        rewind(file); byte *bytes = malloc(size ? (size_t)size : 1);
        assert(fread(bytes, 1, size, file) == (size_t)size); fclose(file);
        const char *error = Mod_ValidateAliasModel(bytes, size, PV_QUAKE1);
        if (error) fprintf(stderr, "%s: %s\n", argv[arg], error);
        assert(!error); free(bytes);
    }
    if (argc > 1) printf("PASS: %d real MDL files validated\n", argc-1);
    check_skin_slots();
    for (int vertical=0; vertical<2; vertical++) {
        byte *wide = malloc(40000); memset(wide, 1, 40000);
        TexMgr_FloodFillSkin(wide, vertical ? 1 : 40000, vertical ? 40000 : 1);
        for (int i=0; i<40000; i++) assert(wide[i] == 0);
        free(wide);
    }
    for (int f = 0; f < 2; f++) {
        check_truncations(0, 0, f); check_truncations(5, 2, f); check_truncations(9, 3, f);
    }
    fixture(5, 2, 0);
    /* Every count/type/index field, including negative and overflowing counts. */
    size_t fields[] = {48,52,56,60,64,68,84,88,
        sizeof(mdl_t)+4+4+5*4+5*4+3*sizeof(stvert_t)+4,
        sizeof(mdl_t)+4+4+5*4+5*4+3*sizeof(stvert_t)+sizeof(dtriangle_t),
        sizeof(mdl_t)+4+4+5*4+5*4+3*sizeof(stvert_t)+sizeof(dtriangle_t)+4};
    int values[] = {-1, 0, INT_MAX, INT_MIN};
    for (size_t i=0; i<sizeof(fields)/sizeof(*fields); i++) {
        int saved; memcpy(&saved, data+fields[i], 4);
        for (size_t j=0; j<sizeof(values)/sizeof(*values); j++) {
            memcpy(data+fields[i], &values[j], 4);
            /* Zero is valid for a triangle vertex or a type field; still exercise it. */
            const char *error = Mod_ValidateAliasModel(data, used, PV_QUAKE1);
            if (values[j] == INT_MAX || values[j] == INT_MIN || values[j] == -1) assert(error);
        }
        memcpy(data+fields[i], &saved, 4);
    }
    /* Mutate every region of complete files, with exact-size allocations so
     * a parser read beyond the supplied file is visible to ASan. */
    unsigned random = 0x6411;
    for (int trial=0; trial<10000; trial++) {
        fixture(trial&1 ? 5 : 0, trial&2 ? 2 : 0, trial&4);
        byte *mutant = malloc(used); memcpy(mutant, data, used);
        for (int flip=0; flip<4; flip++) {
            random = random*1664525u + 1013904223u;
            size_t offset = random % used;
            random = random*1664525u + 1013904223u;
            mutant[offset] = random >> 24;
        }
        (void)Mod_ValidateAliasModel(mutant, used, trial&4 ? PV_QUAKEFORGE : PV_QUAKE1);
        free(mutant);
    }
    float intervals[] = {0, -1, NAN, INFINITY, -INFINITY, .25f};
    for (int forge=0; forge<2; forge++) for (int i=0; i<6; i++) {
        fixture(5, 2, forge);
        const int type = forge ? PV_QUAKEFORGE : PV_QUAKE1;
        size_t skin_intervals = sizeof(mdl_t) + sizeof(daliasskintype_t) + sizeof(daliasskingroup_t);
        size_t frame_group = skin_intervals + 5*sizeof(daliasskininterval_t) + 5*4
                           + 3*sizeof(stvert_t) + sizeof(dtriangle_t) + sizeof(daliasframetype_t);
        size_t frame_intervals = frame_group + sizeof(daliasgroup_t);
        /* Unused skin intervals and subsequent frame intervals are accepted. */
        for (int j=0; j<5; j++) memcpy(data+skin_intervals+j*4, &intervals[i], 4);
        memcpy(data+frame_intervals+4, &intervals[i], 4);
        assert(!Mod_ValidateAliasModel(data, used, type));
        pheader = Hunk_AllocName(sizeof(*pheader), "header"); pheader->numverts = 3;
        maliasframedesc_t frame = {0}; posenum = 0;
        void *end = Mod_LoadAliasGroup(data+frame_group, &frame, type);
        assert(end == data+used && posenum == 2 && frame.interval == .1f);
        /* Invalid first intervals fall back; a positive duration is preserved. */
        memcpy(data+frame_intervals, &intervals[i], 4);
        assert(!Mod_ValidateAliasModel(data, used, type));
        posenum = 0;
        end = Mod_LoadAliasGroup(data+frame_group, &frame, type);
        assert(end == data+used && posenum == 2);
        assert(frame.interval == (i == 5 ? .25f : .1f));
        assert(isfinite(0.0f / frame.interval));
    }
    for (int p=1; p<100; p++) {
        double cycle = R_AliasPoseCycle(1e30, FLT_TRUE_MIN, p);
        assert(isfinite(cycle) && cycle >= 0 && cycle < p);
    }
    assert(R_AliasPoseCycle(INFINITY, .1f, 3) == 0);
    assert(R_AliasPoseCycle(-1, .1f, 3) == 0);
    assert(fabs(R_AliasPoseCycle(.25, .125f, 3) - 2) < .00001);
    puts("PASS: single/grouped MDL and QuakeForge; all truncation boundaries; malformed counts and indices; legacy intervals and safe duration fallback; bounded pose arithmetic; grouped skin slots and recoloring");
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-alias-bounds-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    cc = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run([*cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined,float-cast-overflow', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'Quake'), str(work / 'test.c'), '-o', str(work / 'test'), *flags, '-lm'], check=True)
    subprocess.run([str(work / 'test'), *sys.argv[1:]], check=True)
