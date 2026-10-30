"""Check production BSP preflight for collision cycles and visible-leaf counts.

Run: python3 Misc/stress/test_bsp_clip_cycles.py
Compiles the actual preflight functions and BSP structs; no game data or GL needed.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
model = (ROOT / "Quake/gl_model.c").read_text()


def function(name):
    match = re.search(r"^static [^\n]*\b" + name + r"\s*\([^;]*?\)\s*\{", model, re.M)
    if not match:
        return ""  # Let the regression run against the old preflight too.
    end, depth = match.end(), 1
    while depth:
        depth += (model[end] == "{") - (model[end] == "}")
        end += 1
    return model[match.start():end] + "\n"


source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned char byte;
typedef int qboolean;
typedef float vec4_t[4];
#define QUAKE_GAME
#include "bspfile.h"
#define true 1
#define false 0
#define Q_UNUSED(x) (void)(x)
static int LittleLong(int value) {
    const uint16_t endian = 1;
    if (*(const byte *)&endian) return value;
    uint32_t u = (uint32_t)value;
    return (int)((u >> 24) | ((u >> 8) & 0xff00u) |
                 ((u << 8) & 0xff0000u) | (u << 24));
}
static short LittleShort(short value) {
    const uint16_t endian = 1;
    if (*(const byte *)&endian) return value;
    uint16_t u = (uint16_t)value;
    return (short)((u >> 8) | (u << 8));
}
static int allocations, fail_allocation;
static void *test_malloc(size_t size) {
    if (++allocations == fail_allocation) return NULL;
    return malloc(size);
}
static void *test_calloc(size_t count, size_t size) {
    if (++allocations == fail_allocation) return NULL;
    return calloc(count, size);
}
#define malloc test_malloc
#define calloc test_calloc
'''
for name in ("Mod_BSPNodeChild", "Mod_BSPNodesCyclic", "Mod_BSPClipnodeChild",
             "Mod_BSPClipHullsInvalid", "Mod_BSPTreeInvalid", "Mod_BSPIndicesInvalid"):
    source += function(name)
source += r'''
#undef malloc
#undef calloc
typedef struct {
    byte *data;
    size_t used;
    dheader_t *header;
    int format, nclips;
    byte *clips;
    dmodelq1_t *models;
} fixture_t;
static void *add_lump(fixture_t *f, int index, size_t size) {
    f->header->lumps[index].fileofs = (int)f->used;
    f->header->lumps[index].filelen = (int)size;
    void *result = f->data + f->used;
    f->used += (size + 3) & ~(size_t)3;
    return result;
}
static void child(fixture_t *f, int node, int side, int value) {
    if (f->format)
        ((dlclipnode_t *)f->clips)[node].children[side] = LittleLong(value);
    else
        ((dsclipnode_t *)f->clips)[node].children[side] = LittleShort((short)value);
}
static fixture_t fixture_with_leafs(int format, int nclips, int nmodels, int nleafs) {
    fixture_t f = {0};
    f.format = format;
    f.nclips = nclips;
    f.data = calloc(1, 1024 + (size_t)nleafs * sizeof(dl2leaf_t) + (size_t)nclips * sizeof(dlclipnode_t) +
                       (size_t)nmodels * sizeof(dmodelq1_t));
    if (!f.data) exit(2);
    f.header = (dheader_t *)f.data;
    f.used = sizeof(dheader_t);
    add_lump(&f, LUMP_PLANES, sizeof(dplane_t));
    add_lump(&f, LUMP_VERTEXES, sizeof(dvertex_t));
    if (format == 2) {
        dl2node_t *node = add_lump(&f, LUMP_NODES, sizeof(*node));
        node->children[0] = LittleLong(-1); node->children[1] = LittleLong(-2);
        dl2leaf_t *leaves = add_lump(&f, LUMP_LEAFS, (size_t)nleafs * sizeof(*leaves));
        for (int i = 0; i < nleafs; i++) leaves[i].contents = LittleLong(i ? -1 : -2);
    } else if (format == 1) {
        dl1node_t *node = add_lump(&f, LUMP_NODES, sizeof(*node));
        node->children[0] = LittleLong(-1); node->children[1] = LittleLong(-2);
        dl1leaf_t *leaves = add_lump(&f, LUMP_LEAFS, (size_t)nleafs * sizeof(*leaves));
        for (int i = 0; i < nleafs; i++) leaves[i].contents = LittleLong(i ? -1 : -2);
    } else {
        dsnode_t *node = add_lump(&f, LUMP_NODES, sizeof(*node));
        node->children[0] = LittleShort(-1); node->children[1] = LittleShort(-2);
        dsleaf_t *leaves = add_lump(&f, LUMP_LEAFS, (size_t)nleafs * sizeof(*leaves));
        for (int i = 0; i < nleafs; i++) leaves[i].contents = LittleLong(i ? -1 : -2);
    }
    f.clips = add_lump(&f, LUMP_CLIPNODES,
                      (size_t)nclips * (format ? sizeof(dlclipnode_t) : sizeof(dsclipnode_t)));
    for (int i = 0; i < nclips; i++) { child(&f, i, 0, -1); child(&f, i, 1, -2); }
    f.models = add_lump(&f, LUMP_MODELS, (size_t)nmodels * sizeof(*f.models));
    for (int i = 0; i < nmodels; i++) f.models[i].visleafs = LittleLong(1);
    return f;
}
static fixture_t fixture(int format, int nclips, int nmodels) {
    return fixture_with_leafs(format, nclips, nmodels, 2);
}
static fixture_t hexen_fixture(int format) {
    /* Five Q1 model records occupy the same space as four Hexen II records. */
    fixture_t f = fixture(format, 4, 5);
    dmodelh2_t *models = (dmodelh2_t *)f.models;
    memset(models, 0, 4 * sizeof(*models));
    for (int i = 0; i < 4; i++) models[i].visleafs = LittleLong(1);
    models[1].headnode[1] = LittleLong(1); /* existing Hexen II format discriminator */
    return f;
}
static int checks;
static void check(fixture_t *f, const char *name, const char *expected) {
    const char *reason = Mod_BSPIndicesInvalid(f->header, f->format);
    checks++;
    if (expected ? !reason || !strstr(reason, expected) : reason != NULL) {
        fprintf(stderr, "FAIL format %d: %s: expected %s, got %s\n", f->format,
                name, expected ? expected : "accepted", reason ? reason : "accepted");
        exit(1);
    }
    free(f->data);
}
int main(void) {
    for (int format = 0; format <= 2; format++) {
        fixture_t f = fixture(format, 4, 1);
        check(&f, "terminal contents", NULL);
        f = fixture(format, 4, 1); child(&f, 0, 0, 0);
        check(&f, "self cycle, front", "clip hull that loops");
        f = fixture(format, 4, 1); child(&f, 0, 1, 0);
        check(&f, "self cycle, back", "clip hull that loops");
        f = fixture(format, 4, 1); child(&f, 0, 0, 1); child(&f, 1, 1, 0);
        check(&f, "two-node cycle", "clip hull that loops");
        f = fixture(format, 4, 1); child(&f, 0, 0, 1); child(&f, 1, 1, 2); child(&f, 2, 0, 1);
        check(&f, "cycle below root", "clip hull that loops");
        f = fixture(format, 4, 1);
        child(&f, 0, 0, 1); child(&f, 0, 1, 2); child(&f, 1, 0, 3); child(&f, 2, 0, 3);
        check(&f, "shared subtree and shared hull roots", NULL);
        f = fixture(format, 4, 1); child(&f, 3, 0, 3);
        check(&f, "unreachable cycle", NULL);
        f = fixture(format, 4, 1); child(&f, 3, 0, 3); f.models[0].headnode[3] = LittleLong(3);
        check(&f, "unused hull 3", NULL);
        f = fixture(format, 4, 1); child(&f, 3, 0, 3); f.models[0].headnode[2] = LittleLong(3);
        check(&f, "cycle reachable from hull 2", "clip hull that loops");
        f = fixture(format, 4, 2); child(&f, 3, 0, 3); f.models[1].headnode[1] = LittleLong(3);
        check(&f, "cycle reachable from inline model", "clip hull that loops");
        f = fixture(format, 4, 1);
        f.models[0].headnode[1] = LittleLong(-1); f.models[0].headnode[2] = LittleLong(-2);
        check(&f, "negative contents roots", NULL);
        f = fixture(format, 0, 1);
        check(&f, "noclip fallback to render hull", NULL);
        if (format) {
            f = fixture(format, 4, 1); child(&f, 0, 0, 4);
            check(&f, "out-of-range child", "out of range child");
        }
        const int count = format ? 100000 : 40000;
        f = fixture(format, count, 1);
        for (int i = 0; i < count - 1; i++) child(&f, i, 0, i + 1);
        check(&f, "deep acyclic chain, including high-bit BSP29 children", NULL);
        f = fixture(format, count, 1); child(&f, 0, 0, 32768); child(&f, 32768, 0, 0);
        check(&f, "cycle through high-bit child index", "clip hull that loops");
        for (int allocation = 3; allocation <= 4; allocation++) {
            f = fixture(format, 4, 1);
            allocations = 0; fail_allocation = allocation; /* after the render-node walk's two allocations */
            check(&f, "collision validation allocation failure", "out of memory");
            fail_allocation = 0;
        }
        const int bad_counts[] = {-1, INT32_MIN, 2, 1000000, INT32_MAX};
        for (size_t i = 0; i < sizeof(bad_counts) / sizeof(bad_counts[0]); i++) {
            for (int model = 0; model < 2; model++) {
                f = fixture(format, 4, 2);
                f.models[model].visleafs = LittleLong(bad_counts[i]);
                check(&f, "invalid world/inline visible-leaf count", "invalid visible leaf count");
                f = hexen_fixture(format);
                ((dmodelh2_t *)f.models)[model].visleafs = LittleLong(bad_counts[i]);
                check(&f, "invalid Hexen II world/inline visible-leaf count", "invalid visible leaf count");
            }
        }
        f = fixture(format, 4, 2);
        f.models[0].visleafs = f.models[1].visleafs = 0;
        check(&f, "zero world and inline visible-leaf counts", NULL);
        f = hexen_fixture(format);
        ((dmodelh2_t *)f.models)[0].visleafs = ((dmodelh2_t *)f.models)[1].visleafs = 0;
        check(&f, "zero Hexen II visible-leaf counts", NULL);
        const int valid_counts[] = {1, 8, 9, 16, 17};
        for (size_t i = 0; i < sizeof(valid_counts) / sizeof(valid_counts[0]); i++) {
            f = fixture_with_leafs(format, 4, 2, 18);
            f.models[0].visleafs = f.models[1].visleafs = LittleLong(valid_counts[i]);
            check(&f, "visible-leaf byte boundaries and unused leaf records", NULL);
        }
        f = fixture_with_leafs(format, 4, 2, 18);
        f.models[0].visleafs = LittleLong(18);
        check(&f, "solid leaf zero cannot be counted as visible", "invalid visible leaf count");
    }
    printf("PASS: %d BSP-preflight cases across BSP29 and both BSP2 formats\n", checks);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-clip-cycles-") as temporary:
    work = Path(temporary)
    (work / "test.c").write_text(source)
    compiler = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    command = ([*compiler, "/nologo", "/std:c11", "/W3", "/O2", "test.c",
                f"/I{ROOT / 'Quake'}", f"/Fe:{binary}"]
               if Path(compiler[0]).stem.lower() == "cl" else
               [*compiler, "-std=c99", "-Wall", "-Wextra", "-O1", "-g",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-I", str(ROOT / "Quake"), "test.c", "-o", str(binary)])
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
