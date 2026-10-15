"""Exercise the real muzzle-flash flags and alias renderer for SNG tap firing.

Run from a Visual Studio developer shell on Windows, or with CC on Unix.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
client = (ROOT / "Quake/cl_main.c").read_text()
alias = (ROOT / "Quake/r_alias.c").read_text()
mathlib = (ROOT / "Quake/mathlib.c").read_text()


def function(text, declaration):
    start = text.index(declaration)
    return text[start:text.index("\n}", start) + 2] + "\n"


flags_start = client.index("\t\t\t//johnfitz -- assume muzzle flash")
flags_end = client.index("\n\t\t\t//johnfitz\n", flags_start)
type_end = alias.index("} lerpdata_t;") + len("} lerpdata_t;")
type_start = alias.rindex("typedef struct", 0, type_end)
source = r'''
#include "quakedef.h"
#undef main
#define CHECK(c, message) do { if (!(c)) { puts("FAIL: " message); exit(1); } } while (0)
client_state_t cl;
client_static_t cls;
cvar_t r_lerpmodels;
void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
void Sys_Error(const char *fmt, ...) { (void)fmt; abort(); }
'''
source += alias[type_start:type_end] + "\n"
source += "static void MuzzleLerp(entity_t *ent) {\n" + client[flags_start:flags_end] + "\n}\n"
source += function(mathlib, "void R_ConcatTransforms (")
source += function(alias, "void R_SetupAliasFrame (")
source += r'''
static struct {
    aliashdr_t hdr;
    maliasframedesc_t extra[8];
    boneinfo_t bone;
    bonepose_t poses[9];
} fixture;
static entity_t entities[2];
static qmodel_t model;
static lerpdata_t out;

static void Setup(const char *name, int mode, int skeletal) {
    memset(&cl, 0, sizeof(cl));
    memset(&fixture, 0, sizeof(fixture));
    memset(&model, 0, sizeof(model));
    memcpy(model.name, name, strlen(name) + 1);
    cl.entities = entities;
    cl.viewentity = 1;
    cl.viewent.model = &model;
    r_lerpmodels.value = mode;
    fixture.hdr.numframes = 9;
    if (skeletal) {
        fixture.hdr.numboneposes = 9;
        fixture.hdr.numbones = 1;
        fixture.hdr.boneinfo = (byte *)&fixture.bone - (byte *)&fixture.hdr;
        fixture.hdr.boneposedata = (byte *)fixture.poses - (byte *)&fixture.hdr;
        fixture.bone.parent = -1;
        fixture.bone.inverse.mat[0] = fixture.bone.inverse.mat[5] = fixture.bone.inverse.mat[10] = 1;
        for (int i = 0; i < 9; i++) {
            fixture.poses[i].mat[0] = fixture.poses[i].mat[5] = fixture.poses[i].mat[10] = 1;
            fixture.poses[i].mat[3] = (float)i;
        }
    } else {
        fixture.hdr.nummorphposes = 9;
    }
    for (int i = 0; i < 9; i++) {
        fixture.hdr.frames[i].firstpose = i;
        fixture.hdr.frames[i].numposes = 1;
        fixture.hdr.frames[i].interval = .1f;
    }
    cl.time = .5;
    cl.viewent.lerpflags = LERP_RESETANIM;
    R_SetupAliasFrame(&fixture.hdr, &cl.viewent, &out);
    CHECK(out.pose1 == 0 && out.pose2 == 0, "initial idle pose");
}

/* Relinking sees the previous rendered frame before V_CalcRefdef assigns
 * the newly received weapon frame. Keep that order in the reproduction. */
static void FireFrame(int frame, double time) {
    cl.time = time;
    MuzzleLerp(&entities[1]);
    cl.viewent.frame = frame;
    R_SetupAliasFrame(&fixture.hdr, &cl.viewent, &out);
}

static void Tap(int skeletal, double time) {
    FireFrame(1, time);
    CHECK(out.pose1 == 4 && out.pose2 == 4,
        "SNG first shot must use the top firing pose without rotating from idle");
    FireFrame(1, time + .05);
    CHECK(out.pose1 == 4 && out.pose2 == 4,
        "a single SNG shot must stay on the top firing pose");
    if (skeletal)
        CHECK(out.bonestate && out.bonestate[0].mat[3] == 4,
            "replacement SNG bones must use the top firing pose");
    cl.time = time + .11;
    cl.viewent.frame = 0;
    R_SetupAliasFrame(&fixture.hdr, &cl.viewent, &out);
    CHECK(out.pose1 == 0 && out.pose2 == 0, "tap release must return to idle without rotating");
    cl.time = time + .25;
    R_SetupAliasFrame(&fixture.hdr, &cl.viewent, &out);
    CHECK(out.pose1 == 0 && out.pose2 == 0, "tap settles at the top idle pose");
}

int main(void) {
    for (int skeletal = 0; skeletal < 2; skeletal++) {
        Setup("progs/v_nail2.mdl", 3, skeletal);
        Tap(skeletal, 1);
        Tap(skeletal, 2);
        FireFrame(1, 3);
        for (int frame = 2; frame <= 8; frame++) {
            FireFrame(frame, 3 + frame * .125);
            static const int previous[] = {4, 5, 6, 7, 8, 1, 2};
            static const int current[] = {5, 6, 7, 8, 1, 2, 3};
            CHECK(out.pose1 == previous[frame - 2] && out.pose2 == current[frame - 2],
                "sustained fire keeps its consecutive poses");
        }
        FireFrame(1, 5);
        CHECK(out.pose1 == 3 && out.pose2 == 4, "sustained fire wraps without restarting at idle");
    }
    for (int mode = 0; mode < 3; mode++) {
        Setup("progs/v_nail2.mdl", mode, 0);
        FireFrame(1, 1);
        CHECK(out.pose2 == 1 && out.pose1 == (mode == 2 ? 0 : 1),
            "legacy, disabled and full interpolation modes retain their behavior");
    }
    Setup("progs/v_nail2.mdl", 3, 0);
    cl.viewent.lerp.state.previouspose = cl.viewent.lerp.state.currentpose = 6;
    cl.viewent.lerpflags = LERP_WRESET;
    cl.viewent.frame = 1;
    cl.time = 1;
    R_SetupAliasFrame(&fixture.hdr, &cl.viewent, &out);
    CHECK(out.pose1 == 4 && out.pose2 == 4, "switching into a firing SNG must not blend a half turn");

    Setup("progs/v_nail2.mdl", 3, 0);
    cl.viewent.frame = 1;
    cl.viewent.lerpflags = LERP_EXPLICIT;
    cl.viewent.lerp.snap.frame2 = 2;
    cl.viewent.lerp.snap.lerpfrac = .25f;
    R_SetupAliasFrame(&fixture.hdr, &cl.viewent, &out);
    CHECK(out.pose1 == 1 && out.pose2 == 2 && out.blend == .25f,
        "explicit CSQC poses must not be remapped");

    Setup("progs/v_nail2.mdl", 3, 0);
    entity_t world = {0};
    world.model = &model; world.frame = 1; world.lerpflags = LERP_RESETANIM;
    R_SetupAliasFrame(&fixture.hdr, &world, &out);
    CHECK(out.pose1 == 1 && out.pose2 == 1, "world SNG models must not be remapped");

    Setup("progs/v_nail2.mdl", 3, 0);
    fixture.hdr.numframes = 2;
    FireFrame(1, 1);
    CHECK(out.pose1 == 1 && out.pose2 == 1, "nonstandard SNG frame layouts must not be remapped");

    const char *others[] = {"progs/v_nail.mdl", "progs/v_shot.mdl",
        "progs/v_shot2.mdl", "progs/v_rock2.mdl", "progs/v_light.mdl"};
    for (int i = 0; i < (int)countof(others); i++) {
        Setup(others[i], 3, 0);
        FireFrame(1, 1);
        CHECK(out.pose1 == 1 && out.pose2 == 1, "other weapons retain their first-shot reset");
    }
    puts("PASS: SNG taps use the top barrel without rotation; sustained fire interpolates and other modes/weapons stay intact");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-sng-lerp-") as directory:
    work = Path(directory)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        command = [*cc, "/nologo", "/std:c11", "/W3", "/O2", "/I", str(ROOT / "Quake"),
                   "/I", str(ROOT / "Windows/SDL3/include"), "test.c", f"/Fe:{binary}"]
    else:
        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True))
        command = [*cc, "-std=gnu11", "-Wall", "-Wextra", "-O2", "-fsanitize=address,undefined",
                   "-I", str(ROOT / "Quake"), *flags, "test.c", "-o", str(binary), "-lm"]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
