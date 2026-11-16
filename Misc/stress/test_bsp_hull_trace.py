"""Compile the production hull trace against a single-plane regression fixture.

Run: python Misc/stress/test_bsp_hull_trace.py
On Windows, run from an MSVC developer command prompt.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
world = (ROOT / "Quake/world.c").read_text()
physics = (ROOT / "Quake/sv_phys.c").read_text()
mathlib = (ROOT / "Quake/mathlib.c").read_text()
point_start = world.index("int SV_HullPointContents (hull_t *hull, int num, vec3_t p)\n{")
trace_start = world.index("enum\n{\n\trht_solid")
trace_end = world.index("/*\n==================\nSV_ClipMoveToEntity", trace_start)

source = r'''
#include "quakedef.h"
#include <assert.h>
#undef main

vec3_t vec3_origin;
cvar_t pr_checkextension = {"pr_checkextension", "1"};
static qcvm_t vm;
THREAD_LOCAL qcvm_t *qcvm = &vm;
static edict_t world;
static mplane_t plane = {{0.0f, 0.9950370192527771f, -0.0995040014386177f},
                         -874.836669921875f, 4};
static mclipnode_t node = {0, {CONTENTS_SOLID, CONTENTS_EMPTY}};
static hull_t hull;
void SV_Impact(edict_t *mover, edict_t *hit);

void Sys_Error(const char *message, ...) {
    fputs(message, stderr);
    abort();
}

void Con_DPrintf(const char *message, ...) {
    (void)message;
}
'''
source += world[point_start:world.index("\n}", point_start) + 2]
source += "\n" + world[trace_start:trace_end]
for text, declaration in (
    (mathlib, "int VectorCompare ("),
    (mathlib, "void CrossProduct ("),
    (mathlib, "void VectorScale ("),
    (physics, "#define\tSTOP_EPSILON"),
    (physics, "#define\tMAX_CLIP_PLANES"),
):
    start = text.index(declaration)
    source += text[start:text.index("\n}", start) + 2] + "\n"
source += r'''
void SV_Impact(edict_t *mover, edict_t *hit) {
    (void)mover;
    (void)hit;
}

trace_t SV_Move(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
                int type, edict_t *mover) {
    (void)mins;
    (void)maxs;
    (void)type;
    (void)mover;
    trace_t trace = {0};
    trace.fraction = 1;
    trace.allsolid = true;
    VectorCopy(end, trace.endpos);
    SV_RecursiveHullCheck(&hull, start, end, &trace, CONTENTMASK_ANYSOLID);
    if (trace.fraction < 1 || trace.startsolid)
        trace.ent = &world;
    return trace;
}

int main(void) {
    hull.planes = &plane;
    hull.clipnodes = &node;
    world.v.solid = SOLID_BSP;
    vm.edicts = &world;
    pr_checkextension.value = 1;

    /* The leaning wall plane and short drop from UltraViolence. */
    vec3_t start = {574.081299f, -845.9000244f, 333.0f};
    vec3_t end = {574.081299f, -845.9000244f, 332.0f};
    assert(SV_HullPointContents(&hull, 0, start) == CONTENTS_EMPTY);
    assert(SV_HullPointContents(&hull, 0, end) == CONTENTS_SOLID);

    trace_t trace = SV_Move(start, vec3_origin, vec3_origin, end, MOVE_NORMAL, NULL);

    assert(trace.fraction < 1 && "trace must collide with the wall");
    assert(!trace.startsolid && !trace.allsolid);
    assert(trace.plane.normal[1] == -plane.normal[1]);
    assert(SV_HullPointContents(&hull, 0, trace.endpos) == CONTENTS_EMPTY);

    /* A rounded slide endpoint can re-hit that same plane at fraction zero. */
    edict_t player = {0};
    vec3_t contact = {574.081299f, -846.143921f, 330.561127f};
    VectorCopy(contact, player.v.origin);
    player.v.velocity[2] = -11.111111f;
    SV_FlyMove(&player, 1.0f / 72.0f, NULL);
    assert(player.v.origin[2] < contact[2] && "a repeated wall contact must not stop the slide");
    assert(SV_HullPointContents(&hull, 0, player.v.origin) == CONTENTS_EMPTY);
    puts("PASS: extended hull trace blocks entry and player slides down the wall");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-bsp-hull-trace-") as directory:
    work = Path(directory)
    (work / "test.c").write_text(source)
    includes = [ROOT / "Quake", ROOT / "Windows/SDL3/include"]
    if os.name == "nt":
        exe = work / "test.exe"
        command = ["cl", "/nologo", "/O2", "/fp:precise",
                   *("/I" + str(path) for path in includes),
                   "test.c", "/Fe:" + str(exe)]
    else:
        exe = work / "test"
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-O2",
                   "-ffp-contract=off", *("-I" + str(path) for path in includes),
                   "test.c", "-o", str(exe), "-lm"]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(exe)], cwd=work, check=True)
