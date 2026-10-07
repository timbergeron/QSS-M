"""Observer HUD resources resolve once, including misses, until a HUD reload.

Compile the resource phase of the actual SCR_ShowObsFrags and SCR_LoadPics.
The filesystem/texture boundary uses two generations of controlled pictures.
This catches per-frame disk searches and pointers surviving a game/HUD change.
Run with CC, or from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
screen = (ROOT / "Quake/gl_screen.c").read_text(encoding="utf-8")
start = screen.index("void SCR_ShowObsFrags(void)")
end = screen.index("\n\tif ((cl.gametype == GAME_DEATHMATCH)", start)
resource_phase = screen[start:end] + r'''
    observed_weapon = weapon_icons;
    observed_quad = sb_quad;
    observed_sigil = sb_sigil[3];
}
'''
start = screen.index("void SCR_LoadPics (void)")
reset = screen[start:screen.index("\n}", start) + 2]
state = "\n".join(re.findall(r"^static qboolean\s+scr_obsitems\w*[^\n]*;", screen, re.M))

source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
typedef struct { int generation, slot; } qpic_t;
typedef struct { float value; } cvar_t;
typedef struct { int unused; } scoreboard_t;
#define false 0
#define true 1
#define OBSITEMS_HUD 1
#define GAME_DEATHMATCH 1
#define ca_connected 1
#define CLAMP(a,x,b) ((x)<(a)?(a):(x)>(b)?(b):(x))
#define TEXPREF_ALPHA 1
#define TEXPREF_PAD 2
#define TEXPREF_NOPICMIP 4
#define CHECK(c,msg) do { if (!(c)) { puts("FAIL: " msg); exit(1); } } while (0)
static struct { int notobserver, intermission, gametype; } cl;
static struct { int demoplayback, state; } cls;
static cvar_t scr_sbar={1}, scr_viewsize={100}, scr_obsitems={3};
static int qeintermission, crxintermission;
static int obs_frags_active, obs_frags_first, obs_frags_count;
static qpic_t *scr_net, *scr_turtle;
static qpic_t pictures[2][10];
static qpic_t missing_wad_picture;
static qpic_t *observed_weapon, *observed_quad, *observed_sigil;
static int generation, present, disk_calls, wad_calls, missing_wad_slot;
static qboolean M_LivePreview_UseScores(void) { return false; }
static void SCR_DrawLivePreviewScores(void) {}
static qboolean COM_FileExists(const char *path, void *unused) {
    (void)unused; CHECK(!strcmp(path,"gfx/ibar2.lmp"),"optional weapon path");
    disk_calls++; return present;
}
static qpic_t *Draw_CachePic(const char *path) {
    CHECK(!strcmp(path,"gfx/ibar2.lmp"),"weapon picture path");
    CHECK(present,"missing optional picture must not use fatal loader");
    return &pictures[generation][0];
}
static qpic_t *Draw_TryCachePic(const char *path, unsigned flags) {
    CHECK(flags==7,"weapon icon retains alpha, padding and no picmip");
    return COM_FileExists(path,NULL) ? Draw_CachePic(path) : NULL;
}
static qpic_t *Draw_PicFromWad(const char *name) {
    static const char *names[]={"sb_quad","sb_invuln","sb_invis","sb_key1","sb_key2",
        "sb_sigil1","sb_sigil2","sb_sigil3","sb_sigil4"};
    int i; wad_calls++;
    for(i=0;i<9;i++) if(!strcmp(name,names[i]))
        return i+1==missing_wad_slot ? &missing_wad_picture : &pictures[generation][i+1];
    CHECK(0,"unexpected WAD picture"); return NULL;
}
'''
source += state + "\n" + reset + "\n" + resource_phase + "\n"
source += r'''
static void frames(int count) { int i; for(i=0;i<count;i++) SCR_ShowObsFrags(); }
static void reload(int next_generation, int next_present) {
    generation=next_generation; present=next_present; disk_calls=wad_calls=missing_wad_slot=0;
    SCR_LoadPics();
}
int main(void) {
    /* A missing optional icon is a cached result, not a search each frame. */
    cls.demoplayback=1; cl.notobserver=1;
    reload(0,0); frames(1000);
    CHECK(disk_calls==1,"absent weapon icon must be searched once per HUD generation");
    CHECK(wad_calls==9,"powerup/key/rune icons must resolve once per HUD generation");
    CHECK(observed_weapon==NULL,"absent weapon icon is null");
    CHECK(observed_quad==&pictures[0][1] && observed_sigil==&pictures[0][9],"all WAD icons retain their meaning");

    reload(1,1); frames(1000);
    CHECK(disk_calls==1 && wad_calls==9,"present icons also resolve only once");
    CHECK(observed_weapon==&pictures[1][0] && observed_quad==&pictures[1][1],"reload uses new pictures");

    reload(0,0); frames(1000);
    CHECK(observed_weapon==NULL,"present to missing reload must drop the old weapon pointer");
    CHECK(observed_quad==&pictures[0][1],"WAD pointer must not survive a reload");

    reload(0,0); missing_wad_slot=1; frames(1000);
    CHECK(observed_quad==&missing_wad_picture,"missing WAD icon retains the loader's placeholder");
    CHECK(disk_calls==1 && wad_calls==9,"missing WAD icon must not resolve again each frame");
    reload(1,1); frames(1000);
    CHECK(observed_quad==&pictures[1][1],"reload replaces a missing WAD placeholder with the new icon");

    reload(1,1); scr_obsitems.value=2; frames(10);
    CHECK(!disk_calls && !wad_calls,"rings only must not load observer HUD icons");
    scr_obsitems.value=3; cl.intermission=1; frames(10);
    CHECK(!disk_calls && !wad_calls,"intermission must not load observer HUD icons");
    cl.intermission=0; scr_viewsize.value=120; frames(10);
    CHECK(!disk_calls && !wad_calls,"hidden HUD must not load icons");
    scr_viewsize.value=100; cls.demoplayback=0; frames(10);
    CHECK(!disk_calls && !wad_calls,"ordinary player must not load observer icons");
    cl.notobserver=0; frames(1000);
    CHECK(disk_calls==1 && wad_calls==9,"live observer loads lazily after HUD is enabled");
    CHECK(observed_weapon==&pictures[1][0],"re-enabled HUD displays new icon");
    puts("PASS: observer HUD caches present and absent icons; game/HUD reloads drop stale pointers; hidden HUD stays lazy");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-observerhud-") as tmp:
    work = Path(tmp)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        command = [*cc, "/nologo", "/W3", "/O2", "test.c", f"/Fe:{binary}"]
    else:
        command = [*cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-fsanitize=address,undefined", "test.c", "-o", str(binary)]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
