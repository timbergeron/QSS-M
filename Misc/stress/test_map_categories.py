"""Exercise the real map discovery, grouping, filtering and navigation code.

Run with python3 Misc/stress/test_map_categories.py (requires cc and SDL2 headers).
Rendering and description I/O are stubbed; discovery and lookup use temporary
loose maps and in-memory package directories, including overridden map names.
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
host = (ROOT / "Quake/host_cmd.c").read_text()
menu = (ROOT / "Quake/menu.c").read_text()
common = (ROOT / "Quake/common.c").read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


source = r'''
#define _GNU_SOURCE
#include "quakedef.h"
#include "q_ctype.h"
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#undef strcasecmp
#undef strncasecmp
#include <strings.h>
#define q_strcasecmp strcasecmp
#define q_strncasecmp strncasecmp
#define q_strcasestr strcasestr
client_static_t cls;
client_state_t cl;
searchpath_t *com_searchpaths;
filelist_item_t *extralevels;
qboolean descriptionsParsed = true, mapshint, keydown[MAX_KEYS];
static qboolean extramaps_initialized, downloads;
int max_word_length, file_from_pak;
cvar_t developer, registered = { .value = 1 };
qofs_t com_filesize;
double com_findfile_time;
unsigned int com_findfile_calls;
qboolean com_modified;
static pack_t *loaded_pack;
#define MAXDESC 50
void *Z_Malloc(int size) { void *p = calloc(1, size); assert(p); return p; }
void Z_Free(void *p) { free(p); }
void Sys_Error(const char *fmt, ...) { fprintf(stderr, "%s\n", fmt); abort(); }
int q_snprintf(char *out, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int result = vsnprintf(out, size, fmt, ap); va_end(ap); return result;
}
void S_LocalSound(const char *s) { (void)s; }
void M_ForceMousemove(void) {}
qboolean CL_QWMapListDownloadsAvailable(void) { return downloads; }
void ExtraMaps_ParseDescriptions(void) { ExtraMaps_Init(); descriptionsParsed = true; }
qboolean isSpecialMap(const char *name) { return !strcmp(name, "authmdl"); }
void SaveMapDescriptionsToJSON(filelist_item_t *levels) { (void)levels; }
qboolean Mod_LoadMapDescription(char *out, size_t size, const char *name) {
    (void)size; (void)name; out[0] = 0; return true;
}
void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
void Con_DPrintf2(const char *fmt, ...) { (void)fmt; }
double Sys_DoubleTime(void) { return 0; }
int Sys_FileType(const char *path) {
    struct stat st;
    return !stat(path, &st) && S_ISREG(st.st_mode) ? FS_ENT_FILE : FS_ENT_NONE;
}
// Source queries must not open or decompress files.
qofs_t Sys_FileOpenRead(const char *path, int *handle) {
    (void)path; (void)handle; abort();
}
int Sys_FileOpenStdio(FILE *file) { (void)file; abort(); }
void Sys_FileSeek(int handle, qofs_t pos) { (void)handle; (void)pos; abort(); }
FILE *FSZIP_Deflate(FILE *file, qofs_t in, qofs_t out, const char *name) {
    (void)file; (void)in; (void)out; (void)name; abort();
}
// Archive decoding is separate from mount ownership and file lookup.
pack_t *COM_LoadPackFile(const char *name) { (void)name; return loaded_pack; }
pack_t *FSZIP_LoadArchive(const char *name) { (void)name; return loaded_pack; }
'''
for declaration in (
    "void Vec_Grow(", "void Vec_Clear(", "void Vec_Free(",
    "int q_strnaturalcmp (", "void COM_StripExtension (", "const char *COM_FileGetExtension (",
    "long COM_filelength (", "static unsigned int COM_HashFileName (",
    "static int COM_FindPackFileIndex (",
):
    source += function(common, declaration)
start = common.index("static int COM_FindFile_impl (")
source += common[start:common.index("static qboolean COM_ConfigDirPath (", start)]
for declaration in (
    "qboolean COM_IsPackageExtension(",
    "static qboolean COM_PackageDirNameToPackageName(const char *pakdir, char *pakfile, size_t pakfile_size)\n{",
    "static qboolean COM_AddPackage(",
):
    source += function(common, declaration)
for declaration in (
    "void FileList_Add (", "static void FileList_Clear (",
    "static void ExtraMaps_Add (",
    "void ExtraMaps_Init (", "static void ExtraMaps_Clear (", "void ExtraMaps_NewGame (",
    "filelist_item_t* FindLevelInList(", "void UpdateMaxWordLength (", "void FileList_Add_MapDesc (",
):
    source += function(host, declaration)

start = menu.index("typedef struct", menu.index("/* Scrolling ticker"))
source += menu[start:menu.index("static void M_Ticker_Update", start)]
start = menu.index("typedef struct", menu.index("/* Listbox */"))
source += menu[start:menu.index("void M_List_CheckIntegrity", start)]
for declaration in (
    "void M_List_AutoScroll(", "void M_List_CenterCursor(",
    "void M_List_GetVisibleRange(", "qboolean M_List_SelectNextMatch(",
    "qboolean M_List_SelectNextActive(", "void M_List_UpdateMouseSelection(",
    "qboolean M_List_Key(", "void M_List_Mousemove(",
):
    source += function(menu, declaration)
start = menu.index("#define MAX_VIS_MAPS")
source += menu[start:menu.index("static int M_Maps_DescriptionX", start)]
for declaration in (
    "static void M_Maps_UpdateViewsize(", "static void M_Maps_Refilter(",
    "static void M_Maps_Init(", "static qboolean M_Maps_HasDownloadGap(",
    "static qboolean M_Maps_MouseYInDownloadGap(",
):
    source += function(menu, declaration)

source += r'''
static int row(const char *name)
{
    for (int i = 0; i < mapsmenu.list.numitems; ++i)
        if (!strcmp(mapsmenu.items[mapsmenu.filtered_indices[i]].name, name))
            return i;
    return -1;
}
static void search(const char *text)
{
    strcpy(mapsmenu.list.search.text, text);
    mapsmenu.list.search.len = strlen(text);
    M_Maps_Refilter();
}
static void press(int key)
{
    assert(M_List_Key(&mapsmenu.list, key));
    M_Maps_EnsureSelectableCursor(key);
    assert(M_Maps_IsSelectable(mapsmenu.list.cursor));
}
static void expect_maps(const char **names, size_t count)
{
    size_t n = 0;
    assert(M_Maps_ContentCount() == (int)count);
    for (int i = 0; i < mapsmenu.list.numitems; ++i) {
        const mapitem_t *item = &mapsmenu.items[mapsmenu.filtered_indices[i]];
        if (!item->active || item->download_menu) continue;
        assert(n < count);
        if (strcmp(item->name, names[n])) {
            fprintf(stderr, "map %zu: expected %s, got %s\n", n, names[n], item->name);
            abort();
        }
        ++n;
    }
    assert(n == count);
}
static int heading_count(const char *name)
{
    int count = 0;
    for (int i = 0; i < mapsmenu.list.numitems; ++i) {
        const mapitem_t *item = &mapsmenu.items[mapsmenu.filtered_indices[i]];
        if (!item->active && !strcmp(item->name, name)) ++count;
    }
    return count;
}
static void check_mount(searchpath_t *owner, const char *path, const char *pure,
                        pack_t *pack, const char *map)
{
    assert(com_searchpaths == NULL);
    loaded_pack = pack;
    assert(COM_AddPackage(owner, path, pure));
    searchpath_t *mount = com_searchpaths;
    assert(mount->pack == pack);
    assert(!strcmp(mount->gamedir, owner ? owner->gamedir : pure));
    char mappath[MAX_QPATH];
    snprintf(mappath, sizeof(mappath), "maps/%s.bsp", map);
    assert(COM_FileSearchPath(mappath) == mount);
    ExtraMaps_Init();
    M_Maps_Init();
    assert(heading_count(owner ? owner->gamedir : pure) == 1);
    assert(row(map) >= 0 && M_Maps_IsSelectable(row(map)));
    ExtraMaps_Clear();
    Z_Free(mount);
    com_searchpaths = NULL;
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    packfile_t modfiles[] = {
        { .name = "maps/start.bsp", .filelen = 40000 },
        { .name = "maps/official.bsp", .filelen = 40000 },
        { .name = "maps/small.bsp", .filelen = 1024 },
        { .name = "maps/authmdl.bsp", .filelen = 40000 },
        { .name = "models/stray.bsp", .filelen = 40000 },
        { .name = "maps/subdir/stray.bsp", .filelen = 40000 },
    };
    packfile_t idfiles[] = {
        { .name = "maps/start.bsp", .filelen = 40000 },
        { .name = "maps/e1m1.bsp", .filelen = 40000 },
        { .name = "maps/e2m1.bsp", .filelen = 40000 },
        { .name = "maps/end.bsp", .filelen = 40000 },
        { .name = "maps/dm1.bsp", .filelen = 40000 },
    };
    pack_t modpak = { .numfiles = 6, .files = modfiles };
    pack_t idpak = { .numfiles = 5, .files = idfiles };
    searchpath_t idpack = { .path_id = 1, .purename = "id1/pak0.pak", .pack = &idpak };
    // Auto-mounted # directories can share an ID with an unrelated mod.
    searchpath_t nested = { .path_id = 4, .purename = "id1/#extra", .next = &idpack };
    searchpath_t idloose = { .path_id = 1, .purename = "id1", .next = &nested };
    packfile_t depfiles[] = {
        { .name = "maps/start.bsp", .filelen = 40000 },
        { .name = "maps/dep_intro.bsp", .filelen = 40000 },
        { .name = "maps/dependency.bsp", .filelen = 40000 },
    };
    pack_t deppak = { .numfiles = 3, .files = depfiles };
    // Numeric IDs need not reflect search priority (e.g. nested gamedirs).
    searchpath_t deppack = { .path_id = 4, .purename = "quoth/pak0.pak", .pack = &deppak, .next = &idloose };
    searchpath_t deploose = { .path_id = 4, .purename = "quoth", .next = &deppack };
    searchpath_t modpack = { .path_id = 2, .purename = "ad/pak0.pk3", .pack = &modpak, .next = &deploose };
    searchpath_t modloose = { .path_id = 2, .purename = "ad", .next = &modpack };
    idloose.gamedir = idpack.gamedir = idloose.purename;
    nested.gamedir = nested.purename;
    deploose.gamedir = deppack.gamedir = deploose.purename;
    modloose.gamedir = modpack.gamedir = modloose.purename;
    strcpy(modloose.filename, argv[1]);
    strcpy(idloose.filename, argv[2]);
    strcpy(nested.filename, argv[3]);
    // QSS-M fills filename for packages too, unlike Ironwail.
    strcpy(modpack.filename, "/quake/ad/pak0.pk3");
    check_mount(&modloose, "/quake/ad/pak0.pak", "ad/pak0.pak", &modpak, "start");
    check_mount(&modloose, "/quake/ad/paks/addon.pk3", "ad/paks/addon.pk3", &modpak, "start");
    check_mount(&modloose, "/quake/qssm.pak", "qssm.pak", &modpak, "start");
    check_mount(NULL, "/quake/standalone.kpf", "standalone.kpf", &modpak, "start");
    char extracted[MAX_OSPATH];
    snprintf(extracted, sizeof(extracted), "%s/paks/extra.pk3dir", argv[1]);
    check_mount(&modloose, extracted, "ad/paks/extra.pk3dir", NULL, "extracted_bonus");
    check_mount(&nested, extracted, "id1/#extra/extra.pk3dir", NULL, "extracted_bonus");
    com_searchpaths = &modloose;
    ExtraMaps_Init();
    M_Maps_Init();
    const char *expected[] = {
        "intro", "start", "custom2", "custom10", "customdm2", "customend", "official",
        "dep_intro", "dependency", "basecustom", "dm1", "e1m1", "e2m1", "end", "nested_bonus"
    };
    expect_maps(expected, sizeof(expected) / sizeof(*expected));
    assert(mapsmenu.list.cursor == row("intro"));
    assert(!strcmp(FindLevelInList(extralevels, "start")->map_gamedir, "ad"));
    assert(!strcmp(FindLevelInList(extralevels, "nested_bonus")->map_gamedir, "id1/#extra"));
    assert(row("ad") == 0 && !M_Maps_IsSelectable(row("ad")));
    assert(row("ad") < row("quoth") && row("quoth") < row("id1"));
    assert(row("id1/#extra") > row("end"));
    assert(row("nested_bonus") > row("id1/#extra"));
    assert(row("start") == row("intro") + 1); // loose and packaged start maps together
    assert(row("e2m1") == row("e1m1") + 1); // no original-episode separators
    assert(row("stray") < 0 && row("authmdl") < 0 && row("small") < 0);
    assert(M_Maps_IsStart("INTRO") && M_Maps_IsStart("Start2"));
    assert(M_Maps_IsStart("ad_start") && !M_Maps_IsStart("ad_end"));

    mapsmenu.list.cursor = row("official");
    press(K_DOWNARROW);
    assert(mapsmenu.list.cursor == row("dep_intro"));
    press(K_UPARROW);
    assert(mapsmenu.list.cursor == row("official"));
    for (int i = 0; i < 50; ++i) { press(K_DOWNARROW); press(K_PGDN); press(K_PGUP); }
    press(K_HOME);
    assert(mapsmenu.list.cursor == row("intro"));
    press(K_UPARROW);
    assert(mapsmenu.list.cursor == row("nested_bonus"));
    press(K_DOWNARROW);
    assert(mapsmenu.list.cursor == row("intro"));
    mapsmenu.list.scroll = 0;
    M_List_Mousemove(&mapsmenu.list, row("quoth") * 8);
    assert(M_Maps_IsSelectable(mapsmenu.list.cursor));

    search("quoth");
    assert(mapsmenu.list.numitems == 0); // headings are not search results
    search("custom");
    assert(mapsmenu.list.numitems == 5);
    for (int i = 0; i < mapsmenu.list.numitems; ++i) assert(M_Maps_IsSelectable(i));
    mapsmenu.list.cursor = row("basecustom");
    search("");
    assert(mapsmenu.list.cursor == row("basecustom"));
    search("no-such-map");
    assert(mapsmenu.list.numitems == 0 && mapsmenu.list.cursor == -1);
    search("");
    assert(M_Maps_IsSelectable(mapsmenu.list.cursor));
    // Description matching continues to work without retaining separator text.
    strcpy(extralevels->data, "Unique test title");
    search("Unique test");
    assert(mapsmenu.list.numitems == 1);

    downloads = true;
    M_Maps_Init();
    assert(row("Download") == 0 && M_Maps_ContentCount() == 15);
    assert(M_Maps_HasDownloadGap() && M_Maps_MouseYInDownloadGap(12));
    search("Download");
    assert(mapsmenu.list.numitems == 1 && !M_Maps_HasDownloadGap());
    search("");
    cls.state = ca_connected;
    cls.signon = SIGNONS;
    strcpy(cl.mapname, "official");
    M_Maps_Init();
    assert(mapsmenu.list.cursor == row("official"));
    cls.state = ca_disconnected;

    // Newly downloaded maps get their gamedir immediately, without a rescan.
    char path[MAX_OSPATH];
    snprintf(path, sizeof(path), "%s/maps/download2.bsp", argv[1]);
    FILE *file = fopen(path, "wb"); assert(file); fclose(file);
    FileList_Add_MapDesc("download2");
    assert(!strcmp(FindLevelInList(extralevels, "download2")->map_gamedir, "ad"));
    snprintf(path, sizeof(path), "%s/maps/download_base.bsp", argv[2]);
    file = fopen(path, "wb"); assert(file); fclose(file);
    FileList_Add_MapDesc("download_base");
    assert(!strcmp(FindLevelInList(extralevels, "download_base")->map_gamedir, "id1"));
    remove(path);
    // A source lookup must also distinguish the colliding ID on update.
    FileList_Add_MapDesc("nested_bonus");
    assert(!strcmp(FindLevelInList(extralevels, "nested_bonus")->map_gamedir, "id1/#extra"));
    FileList_Add_MapDesc("start");
    assert(!strcmp(FindLevelInList(extralevels, "start")->map_gamedir, "ad"));
    assert(COM_FileSearchPath("maps/start.bsp") == &modpack);
    assert(COM_FileSearchPath("maps/intro.bsp") == &modloose);
    assert(COM_FileSearchPath("maps/dep_intro.bsp") == &deppack);
    assert(COM_FileSearchPath("maps/nested_bonus.bsp") == &nested);
    assert(COM_FileSearchPath("maps/missing.bsp") == NULL);
    registered.value = 0;
    assert(COM_FileSearchPath("maps/intro.bsp") == NULL);
    assert(COM_FileSearchPath("maps/start.bsp") == &modpack);
    registered.value = 1;
    M_Maps_Init();
    assert(row("download2") > row("start") && row("download2") < row("quoth"));
    assert(row("download_base") > row("id1"));

    // Separate install/user roots for one gamedir still produce one group.
    searchpath_t usermod = { .path_id = 2, .purename = "ad", .next = &modloose };
    usermod.gamedir = usermod.purename;
    snprintf(usermod.filename, sizeof(usermod.filename), "%s/user", argv[1]);
    com_searchpaths = &usermod;
    ExtraMaps_NewGame();
    M_Maps_Init();
    assert(heading_count("ad") == 1);
    assert(row("user_bonus") > row("ad") && row("user_bonus") < row("quoth"));
    assert(COM_FileSearchPath("maps/start.bsp") == &usermod);
    assert(!strcmp(FindLevelInList(extralevels, "start")->map_gamedir, "ad"));

    // Changing mods rebuilds origin data, including the formerly overridden start.
    com_searchpaths = &idloose;
    ExtraMaps_NewGame();
    M_Maps_Init();
    const char *base[] = {"start", "basecustom", "dm1", "e1m1", "e2m1", "end", "nested_bonus"};
    expect_maps(base, sizeof(base) / sizeof(*base));
    assert(row("ad") < 0 && row("quoth") < 0 && row("id1") >= 0);

    // A top-priority gamedir with no start map should still open at the top.
    modpak.files = modfiles + 1;
    modpak.numfiles = 1;
    com_searchpaths = &modpack;
    ExtraMaps_NewGame();
    M_Maps_Init();
    assert(mapsmenu.list.cursor == row("official"));
    ExtraMaps_Clear();
    descriptionsParsed = true;
    downloads = false;
    M_Maps_Init();
    assert(mapsmenu.list.numitems == 0 && mapsmenu.list.cursor == -1);
    VEC_FREE(mapsmenu.items);
    VEC_FREE(mapsmenu.filtered_indices);
    puts("Map discovery, source precedence, grouping, search and navigation passed.");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-map-categories-") as temp:
    temp = Path(temp)
    for gamedir, maps in (("mod", ("intro", "custom10", "custom2", "customend", "customdm2")),
                         ("id1", ("basecustom",)), ("id1/#extra", ("nested_bonus",)),
                         ("mod/paks/extra.pk3dir", ("extracted_bonus",)),
                         ("mod/user", ("user_bonus", "start"))):
        (temp / gamedir / "maps").mkdir(parents=True)
        for name in maps:
            (temp / gamedir / "maps" / f"{name}.bsp").touch()
    test_c = temp / "test.c"
    test_c.write_text(source)
    sdl_flags = shlex.split(subprocess.check_output(["sdl2-config", "--cflags"], text=True))
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=gnu11", "-DUSE_SDL2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(ROOT / "Quake"),
        *sdl_flags, str(test_c), str(ROOT / "Quake/strlcpy.c"), "-o", str(temp / "test"),
    ], check=True)
    subprocess.run([str(temp / "test"), str(temp / "mod"), str(temp / "id1"),
                    str(temp / "id1/#extra")], check=True)
