"""Exercise the actual BSP texture readers under ASan/UBSan, without a GL context.

Run: python3 Misc/stress/test_bsp_miptex.py
Requires a C compiler and sdl2-config. Only allocation/console services are stubbed;
the loader and texture-format arithmetic are extracted from the current sources.
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
model = (ROOT / "Quake/gl_model.c").read_text()
textures = (ROOT / "Quake/gl_texmgr.c").read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


source = r'''
#include "quakedef.h"
#include "q_ctype.h"
#include <assert.h>
#undef main

static qmodel_t model;
qmodel_t *loadmodel = &model;
char loadname[32] = "miptex-test";
static int little_long(int value) { return SDL_SwapLE32(value); }
int (*LittleLong)(int) = little_long;
qboolean gl_texture_NPOT = true;
qboolean gl_packed_pixels;
qboolean gl_texture_s3tc, gl_texture_rgtc, gl_texture_bptc;
qboolean gl_texture_etc2, gl_texture_astc, gl_texture_e5bgr9;
int gl_hardware_maxsize = 16384, lightmap_bytes = 4;
cvar_t gl_max_size = { .value = 0 };
void Con_Printf(const char *fmt, ...) { (void)fmt; }
void Con_Warning(const char *fmt, ...) { (void)fmt; }
void Con_DWarning(const char *fmt, ...) { (void)fmt; }
void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
void *Hunk_AllocNameNoFill(int size, const char *name) {
    (void)name;
    assert(size > 0 && size < 4 * 1024 * 1024);
    void *result = malloc(size);
    assert(result);
    memset(result, 0xa5, size);
    return result;
}
'''

start = textures.rindex("static struct", 0, textures.index("} compressedformats[]"))
source += textures[start:textures.index("\n};", start) + 3] + "\n"
for declaration in ("int TexMgr_Pad (", "int TexMgr_SafeTextureSize (",
                    "size_t TexMgr_ImageSize (", "enum srcformat TexMgr_FormatForCode ("):
    source += function(textures, declaration)
source += (ROOT / "Quake/strlcpy.c").read_text()
source += function((ROOT / "Quake/common.c").read_text(), "int q_strncasecmp(")

start = model.rindex("typedef struct", 0, model.index("} bspx_lump_t;"))
end = model.index("static void Q1BSPX_Reset", start)
source += model[start:end]
source += function(model, "static texture_t *Mod_LoadMipTex(")
start = model.rindex("typedef struct", 0, model.index("} miptexbound_t;"))
source += model[start:model.index("static void Mod_LoadTextures", start)]
source += function(model, "static qboolean Mod_BSPLumpValid (")
source += function(model, "static qboolean Mod_BSPTextureName (")

source += r'''
static void put32(byte *out, uint32_t value) {
    value = SDL_SwapLE32(value);
    memcpy(out, &value, 4);
}
static void mipheader(byte *out, unsigned int width, unsigned int height) {
    memset(out, 0, sizeof(miptex_t));
    memcpy(out, "abcdefghijklmnop", 16); /* deliberately not NUL terminated */
    put32(out + 16, width);
    put32(out + 20, height);
    put32(out + 24, sizeof(miptex_t));
}
static texture_t *readmip(const byte *data, size_t length,
                         enum srcformat *format, unsigned int *copied) {
    unsigned int width, height;
    return Mod_LoadMipTex(data, length, format, &width, &height, copied);
}
static void check_pixels(void) {
    byte data[1600], before[1600];
    enum srcformat format;
    unsigned int copied;
    texture_t *tx;
    model.bspversion = BSPVERSION;
    memset(data, 0, sizeof(data));
    mipheader(data + 1, 16, 16); /* unaligned entry */
    memset(data + 41, 7, 256);
    memcpy(before, data, sizeof(data));
    for (int i = 0; i < 2; i++) {
        tx = readmip(data + 1, 296, &format, &copied);
        assert(tx && format == SRC_INDEXED && copied == 256);
        assert(tx->width == 16 && tx->height == 16 && strlen(tx->name) == 15);
        for (int p = 0; p < 256; p++) assert(((byte *)(tx + 1))[p] == 7);
        free(tx);
        assert(!memcmp(before, data, sizeof(data))); /* shared header remains raw */
    }
    tx = readmip(data + 1, 168, &format, &copied);
    assert(tx && copied == 128);
    for (int p = 0; p < 256; p++) assert(((byte *)(tx + 1))[p] == (p < 128 ? 7 : 0));
    free(tx);
    assert(!readmip(data + 1, 39, &format, &copied));
    put32(data + 25, 0);
    tx = readmip(data + 1, 40, &format, &copied);
    assert(tx && copied == 256 && ((byte *)(tx + 1))[0] == 2 && ((byte *)(tx + 1))[4] == 6);
    free(tx);
    const uint32_t bad_offsets[] = {1, 39, 297, UINT32_MAX};
    for (size_t i = 0; i < countof(bad_offsets); i++) {
        put32(data + 25, bad_offsets[i]);
        assert(!readmip(data + 1, 296, &format, &copied));
    }
    mipheader(data, 65536, 65536);
    assert(!readmip(data, 40, &format, &copied)); /* multiplication/allocation overflow */
    mipheader(data, UINT32_MAX, 16);
    assert(!readmip(data, 40, &format, &copied));
    mipheader(data, 0, 16);
    assert(!readmip(data, 40, &format, &copied));

    /* A real extension format and complete mip chain, followed by corrupt variants. */
    mipheader(data, 16, 16);
    put32(data + 24, 0);
    put32(data + 40, 0xaf2bfb00u);
    enum srcformat rgba = TexMgr_FormatForCode("RGBA");
    size_t pixels = TexMgr_ImageSize(16, 16, rgba);
    assert(rgba != SRC_EXTERNAL && pixels == 1364);
    put32(data + 44, 16 + pixels);
    memcpy(data + 48, "RGBA", 4);
    put32(data + 52, 16); put32(data + 56, 16);
    memset(data + 60, 0x6b, pixels);
    tx = readmip(data, 60 + pixels, &format, &copied);
    assert(tx && format == rgba && copied == pixels && ((byte *)(tx + 1))[pixels - 1] == 0x6b);
    free(tx);
    const uint32_t bad_chunks[] = {0, 7, UINT32_MAX};
    for (size_t i = 0; i < countof(bad_chunks); i++) {
        put32(data + 44, bad_chunks[i]);
        tx = readmip(data, 60 + pixels, &format, &copied);
        assert(tx && format == SRC_INDEXED);
        free(tx);
    }
    put32(data + 44, 16 + pixels);
    put32(data + 52, 0);
    tx = readmip(data, 60 + pixels, &format, &copied);
    assert(tx && format == SRC_INDEXED);
    free(tx);

    gl_texture_NPOT = false;
    put32(data + 52, INT_MAX); /* must reject before power-of-two rounding overflows */
    tx = readmip(data, 60 + pixels, &format, &copied);
    assert(tx && format == SRC_INDEXED);
    free(tx);
    gl_texture_NPOT = true;

    model.bspversion = BSPVERSION_QUAKE64;
    memset(data, 0, sizeof(data));
    mipheader(data, 16, 16);
    put32(data + 24, 2); /* Q64 shift occupies the ordinary offset[0] slot */
    memset(data + 44, 9, 256);
    tx = readmip(data, 300, &format, &copied);
    assert(tx && tx->shift == 2 && copied == 256 && ((byte *)(tx + 1))[0] == 9);
    free(tx);
    model.bspversion = BSPVERSION;
    puts("PASS: indexed/truncated/missing pixels, shared and unaligned headers, dimensions, RGBA extensions, Quake64");
}
static void check_tables(void) {
    byte data[1025] = {0};
    byte *table = data + 1;
    int count;
    size_t *ends;
    for (size_t length = 0; length < 4; length++) {
        ends = Mod_LoadMiptexBounds(table, length, &count);
        assert(!ends && count == 0);
    }
    const uint32_t bad_counts[] = {UINT32_MAX, INT_MAX, 256};
    for (size_t i = 0; i < countof(bad_counts); i++) {
        put32(table, bad_counts[i]);
        ends = Mod_LoadMiptexBounds(table, 1024, &count);
        assert(!ends && count == 0);
    }
    put32(table, 4);
    put32(table + 4, 512); put32(table + 8, 128);
    put32(table + 12, 128); put32(table + 16, UINT32_MAX);
    ends = Mod_LoadMiptexBounds(table, 1024, &count);
    assert(count == 4 && ends[0] == 1024 && ends[1] == 512 && ends[2] == 512 && !ends[3]);
    free(ends);
    put32(table + 4, 0); put32(table + 8, 1024);
    put32(table + 12, 1023); put32(table + 16, 0x80000000u);
    ends = Mod_LoadMiptexBounds(table, 1024, &count);
    assert(count == 4 && !ends[0] && !ends[1] && !ends[2] && !ends[3]);
    free(ends);

    char name[17];
    texinfo_t info = {0};
    lump_t lump = {.fileofs = 1, .filelen = 1024};
    put32(table + 4, 128);
    memcpy(table + 128, "abcdefghijklmnop", 16);
    assert(Mod_BSPTextureName(data, &lump, &info, name) && !strcmp(name, "abcdefghijklmnop"));
    put32(table + 4, 4);
    assert(Mod_BSPTextureName(data, &lump, &info, name) && !strcmp(name, "notexture"));
    put32(table, INT_MAX);
    assert(!Mod_BSPTextureName(data, &lump, &info, name));
    assert(Mod_BSPLumpValid(sizeof(data), &lump, 0, NULL));
    lump.fileofs = -1;
    assert(!Mod_BSPLumpValid(sizeof(data), &lump, 0, NULL));
    lump.fileofs = 1025; lump.filelen = 0;
    assert(Mod_BSPLumpValid(sizeof(data), &lump, 0, NULL));
    lump.fileofs++;
    assert(!Mod_BSPLumpValid(sizeof(data), &lump, 0, NULL));
    puts("PASS: truncated/overflowed tables, unordered/shared/missing offsets, material names, ordinary lump bounds");
}
static void check_bspx(void) {
    byte *data = calloc(1, 256);
    lump_t lump = {.fileofs = 0, .filelen = 128};
    int length;
    memcpy(data + 128, "BSPX", 4);
    Q1BSPX_Setup(&model, (char *)data, 136, &lump, 1);
    assert(bspxheader && bspxheader->numlumps == 0);
    Q1BSPX_Setup(&model, (char *)data, 135, &lump, 1);
    assert(!bspxheader);
    const uint32_t bad_counts[] = {UINT32_MAX, INT_MAX, 4};
    for (size_t i = 0; i < countof(bad_counts); i++) {
        put32(data + 132, bad_counts[i]);
        Q1BSPX_Setup(&model, (char *)data, 256, &lump, 1);
        assert(!bspxheader);
    }
    put32(data + 132, 1);
    memcpy(data + 136, "RGBLIGHTING", 11);
    put32(data + 160, 240); put32(data + 164, 16);
    Q1BSPX_Setup(&model, (char *)data, 256, &lump, 1);
    assert(Q1BSPX_FindLump("RGBLIGHTING", &length) == data + 240 && length == 16);
    put32(data + 160, 0xfffffff0u); put32(data + 164, 32);
    Q1BSPX_Setup(&model, (char *)data, 256, &lump, 1);
    assert(!bspxheader); /* old unsigned addition wrapped to an in-range value */
    put32(data + 160, 240); put32(data + 164, 17);
    Q1BSPX_Setup(&model, (char *)data, 256, &lump, 1);
    assert(!bspxheader);
    free(data);
    puts("PASS: zero-entry/truncated BSPX headers, count overflow, wrapped/negative/out-of-file BSPX ranges");
}
static uint32_t state = 0x71924ab3;
static uint32_t next_random(void) { state = state * 1664525u + 1013904223u; return state; }
static void check_mutations(void) {
    for (int run = 0; run < 20000; run++) {
        size_t length = next_random() % 512;
        byte *data = malloc(length + 1);
        for (size_t i = 0; i < length; i++) data[i] = next_random() >> 24;
        int count;
        size_t *ends = Mod_LoadMiptexBounds(data, length, &count);
        free(ends);
        if (length >= 40) {
            mipheader(data, 1 + next_random() % 32, 1 + next_random() % 32);
            for (int j = 0; j < 4; j++) put32(data + 24 + j * 4, next_random() % 600);
        }
        enum srcformat format;
        unsigned int copied;
        texture_t *tx = readmip(data, length, &format, &copied);
        free(tx);
        free(data);
    }
    puts("PASS: 20000 deterministic malformed table/entry mutations");
}
int main(void) {
    strcpy(model.name, "maps/miptex-test.bsp");
    check_pixels(); check_tables(); check_bspx(); check_mutations();
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-miptex-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(source)
    flags = shlex.split(subprocess.check_output(["sdl2-config", "--cflags"], text=True))
    subprocess.run([os.environ.get("CC", "cc"), "-O1", "-g", "-std=gnu11", "-DUSE_SDL2",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
                    "-I", str(ROOT / "Quake"), str(path / "test.c"), "-o", str(path / "test"),
                    *flags, "-lm"], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=30)
