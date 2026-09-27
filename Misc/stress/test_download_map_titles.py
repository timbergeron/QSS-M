"""Exercise production catalog loading, URL names, freshness and menu filtering.

Uses temporary fixtures and stubs I/O/rendering; never opens a user's game/config.
Requires a C compiler and SDL3 headers, as does test_map_categories.py.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
console = (ROOT / "Quake/console.c").read_text()
menu = (ROOT / "Quake/menu.c").read_text()
common = (ROOT / "Quake/common.c").read_text()
client = (ROOT / "Quake/cl_main.c").read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


source = r'''
#define _GNU_SOURCE
#include "quakedef.h"
#include "q_ctype.h"
#include "json.h"
#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#undef strcasecmp
#undef strncasecmp
#include <strings.h>
#define q_strcasecmp strcasecmp
#define q_strncasecmp strncasecmp
#define q_strcasestr strcasestr
#define QW_MAPLIST_PATH "misc/qw_maps.txt"
#define QW_MAPLIST_CACHE_FILE "qw_maps.txt"
#define QW_MAPLIST_CACHE_DIR "backups"
#define QW_MAPLIST_REMOTE_URL "https://maps.quakeworld.nu/all/"
static unsigned int qw_maplist_generation;
static double qw_maplist_snapshot_time;
static qw_maplist_state_t qw_maplist_state;
static char *qw_maplist_buffer;
static const char **qw_maplist_names;
static int qw_maplist_count;
static char qw_maplist_path[MAX_OSPATH];
static qboolean qw_maplist_warned, qw_maplist_cache_warned;
char com_basedir[MAX_OSPATH] = "/nonexistent-test-base";
static const char *packaged, *cached, *title_file;
static int warnings, opens;
static qboolean too_large;
void Sys_Error(const char *fmt, ...) { (void)fmt; abort(); }
void Con_Warning(const char *fmt, ...) { (void)fmt; warnings++; }
int q_snprintf(char *out, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vsnprintf(out, size, fmt, ap); va_end(ap); return n;
}
static qboolean QWMapList_StartRefresh(qboolean force) { (void)force; return false; }
static void QWMapList_SetSourcePath(unsigned int id) { (void)id; strcpy(qw_maplist_path, "packaged"); }
byte *COM_LoadMallocFile(const char *path, unsigned int *id) {
    (void)path; (void)id; return packaged ? (byte *)strdup(packaged) : NULL;
}
byte *COM_LoadMallocFile_TextMode_OSPath(const char *path, long *length) {
    (void)path; (void)length; return cached ? (byte *)strdup(cached) : NULL;
}
int COM_FOpenFile(const char *path, FILE **out, unsigned int *id) {
    (void)path; (void)id; opens++;
    if (!title_file && !too_large) { *out = NULL; return -1; }
    *out = tmpfile(); assert(*out);
    if (too_large) return 8 * 1024 * 1024 + 1;
    size_t length = strlen(title_file);
    assert(fwrite(title_file, 1, length, *out) == length); rewind(*out); return (int)length;
}
'''
start = console.index('#define QW_MAPTITLES_PATH')
source += console[start:console.index('typedef struct\n{\n\tchar *data;', start)]
for declaration in (
    "void Vec_Grow(", "void Vec_Clear(", "void Vec_Free(",
    "void COM_StripExtension (", "size_t UTF8_WriteCodePoint(",
):
    source += function(common, declaration)
for declaration in (
    "static void QWMapList_FreeCache(", "static void QWMapList_Fail(",
    "static qboolean QWMapList_NameIsValid(", "void QWMapTitles_Invalidate(",
    "static qboolean QWMapTitles_Parse(", "void QWMapTitles_LoadOnce(",
    "const char *QWMapTitles_TitleForName(", "unsigned int QWMapList_Generation(",
    "static double QWMapList_SnapshotTime(", "static qboolean QWMapList_Parse(",
    "static void QWMapList_CachePath(", "static qboolean QWMapList_LoadCache(",
    "static qboolean QWMapList_LoadPackaged(", "const char *QWMapList_NameAt(",
    "int QWMapList_Count(", "static qboolean QWMapList_HrefToName(",
    "static int QWMapList_RemoteNameCompare(",
):
    source += function(console, declaration)
source += function(client, "static qboolean CL_EncodeDownloadPath(")
start = menu.index("typedef struct", menu.index("/* Scrolling ticker"))
source += menu[start:menu.index("static void M_Ticker_Update", start)]
start = menu.index("typedef struct", menu.index("/* Listbox */"))
source += menu[start:menu.index("void M_List_CheckIntegrity", start)]
source += function(menu, "void M_List_CenterCursor(")
start = menu.index("typedef struct", menu.index("Download Maps Menu"))
source += menu[start:menu.index("static void M_DownloadMaps_SetMessage", start)]
for declaration in (
    "static const char *M_DownloadMaps_DisplayName(", "static void M_DownloadMaps_Refilter(",
    "static void M_DownloadMaps_Rebuild(", "static const char *M_DownloadMaps_SelectedName(void)\n{",
):
    source += function(menu, declaration)

source += r'''
static const char *good = "{\"schema\":1,\"source\":\"https://maps.quakeworld.nu/all/\",\"maps\":["
    "{\"name\":\"a.bsp\",\"title\":\"The Tower\"},"
    "{\"name\":\"b.bsp\",\"title\":\"\"},"
    "{\"name\":\"c.bsp\",\"title\":\"Citadel\"}]}";
static void load_names(const char *text) {
    QWMapList_FreeCache(); qw_maplist_buffer = strdup(text);
    char error[128]; assert(QWMapList_Parse(qw_maplist_buffer, &qw_maplist_names, &qw_maplist_count, error, sizeof(error)));
    qw_maplist_state = QW_MAPLIST_LOADED;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    // The real JSON parser and title loader, with file length bounds and one-shot I/O.
    title_file = good; QWMapTitles_LoadOnce();
    assert(qw_maptitles_count == 3 && !strcmp(QWMapTitles_TitleForName("A.BSP"), "The Tower"));
    assert(!*QWMapTitles_TitleForName("b.bsp") && !*QWMapTitles_TitleForName("new.bsp"));
    QWMapTitles_LoadOnce(); assert(opens == 1);
    QWMapTitles_Invalidate(); title_file = "{}"; QWMapTitles_LoadOnce();
    assert(!qw_maptitles_count && warnings == 1);
    QWMapTitles_Invalidate(); title_file = NULL; QWMapTitles_LoadOnce(); assert(!qw_maptitles_count);
    QWMapTitles_Invalidate(); too_large = true; QWMapTitles_LoadOnce(); assert(!qw_maptitles_count);
    too_large = false; QWMapTitles_Invalidate();
    assert(!QWMapTitles_Parse("{\"schema\":2}"));
    assert(!QWMapTitles_Parse("{\"schema\":1,\"source\":\"https://maps.quakeworld.nu/all/\",\"maps\":["
        "{\"name\":\"a.bsp\",\"title\":\"bad\\nline\"}]}"));
    assert(!QWMapTitles_Parse("{\"schema\":1,\"source\":\"https://maps.quakeworld.nu/all/\",\"maps\":["
        "{\"name\":\"a.bsp\",\"title\":\"a\"},{\"name\":\"A.bsp\",\"title\":\"b\"}]}"));
    title_file = good; QWMapTitles_LoadOnce();

    // Refreshed menu copies survive global cache replacement and catalog invalidation.
    load_names("a.bsp\nb.bsp\nc.bsp\n");
    downloadmapsmenu.list.viewsize = 17; downloadmapsmenu.list.cursor = -1;
    M_DownloadMaps_Rebuild(); assert(downloadmapsmenu.list.numitems == 3);
    strcpy(downloadmapsmenu.list.search.text, "tower"); downloadmapsmenu.list.search.len = 5;
    M_DownloadMaps_Refilter(); assert(downloadmapsmenu.list.numitems == 1);
    assert(!strcmp(M_DownloadMaps_SelectedName(), "a.bsp"));
    strcpy(downloadmapsmenu.list.search.text, "b"); downloadmapsmenu.list.search.len = 1;
    M_DownloadMaps_Refilter(); assert(downloadmapsmenu.list.numitems == 1);
    assert(!strcmp(M_DownloadMaps_SelectedName(), "b.bsp"));
    downloadmapsmenu.list.search.text[0] = 0; downloadmapsmenu.list.search.len = 0;
    M_DownloadMaps_Refilter(); downloadmapsmenu.list.cursor = 2;
    load_names("b.bsp\nc.bsp\nd.bsp\n"); // same count, different ordering
    QWMapTitles_Invalidate();
    assert(!strcmp(M_DownloadMaps_SelectedName(), "c.bsp"));
    M_DownloadMaps_Rebuild(); assert(!strcmp(M_DownloadMaps_SelectedName(), "c.bsp"));
    assert(downloadmapsmenu.list.cursor == 1);
    assert(!strcmp(downloadmapsmenu.items[1].title, "Citadel"));
    load_names("d.bsp\n"); M_DownloadMaps_Rebuild(); assert(!strcmp(M_DownloadMaps_SelectedName(), "d.bsp"));
    strcpy(downloadmapsmenu.list.search.text, "absent"); downloadmapsmenu.list.search.len = 6;
    M_DownloadMaps_Refilter(); assert(downloadmapsmenu.list.numitems == 0 && M_DownloadMaps_SelectedName() == NULL);

    // URL decoding/encoding must preserve identity and reject escaped path traversal.
    char name[MAX_QPATH], url[256];
    assert(QWMapList_HrefToName("dm3%2B%2B.bsp", strlen("dm3%2B%2B.bsp"), name, sizeof(name)));
    assert(!strcmp(name, "dm3++.bsp"));
    assert(CL_EncodeDownloadPath(name, url, sizeof(url)) && !strcmp(url, "dm3%2B%2B.bsp"));
    assert(QWMapList_HrefToName("h&amp;k.bsp", strlen("h&amp;k.bsp"), name, sizeof(name)) && !strcmp(name, "h&k.bsp"));
    assert(!QWMapList_HrefToName("%2E%2E%2Fa.bsp", strlen("%2E%2E%2Fa.bsp"), name, sizeof(name)));
    assert(!QWMapList_HrefToName("a%00.bsp", 8, name, sizeof(name)));
    assert(!QWMapList_HrefToName("a%Q0.bsp", 8, name, sizeof(name)));
    assert(!QWMapList_HrefToName("%23a.bsp", 8, name, sizeof(name)));
    assert(!QWMapList_HrefToName("%20a.bsp", 8, name, sizeof(name)));
    assert(!CL_EncodeDownloadPath("++.bsp", url, 4));
    const char *aliases[] = { "testmapb.bsp", "testmapB.bsp" };
    qsort(aliases, 2, sizeof(*aliases), QWMapList_RemoteNameCompare);
    assert(!strcmp(aliases[0], "testmapB.bsp"));

    // A new packaged list supersedes an older cache; a newer cache still wins.
    char older[128], newer[128]; time_t now = time(NULL);
    snprintf(older, sizeof(older), "# refreshed=%lld\na.bsp\n", (long long)now - 200);
    snprintf(newer, sizeof(newer), "# refreshed=%lld\nb.bsp\n", (long long)now - 100);
    QWMapList_FreeCache(); qw_maplist_state = QW_MAPLIST_UNLOADED;
    cached = older; packaged = newer;
    assert(QWMapList_LoadCache() && QWMapList_LoadPackaged()); assert(!strcmp(QWMapList_NameAt(0), "b.bsp"));
    QWMapList_FreeCache(); qw_maplist_state = QW_MAPLIST_UNLOADED;
    cached = newer; packaged = older;
    assert(QWMapList_LoadCache() && QWMapList_LoadPackaged()); assert(!strcmp(QWMapList_NameAt(0), "b.bsp"));
    packaged = "not a valid map"; assert(QWMapList_LoadPackaged()); assert(!strcmp(QWMapList_NameAt(0), "b.bsp"));
    assert(QWMapList_SnapshotTime("# refreshed=9999999999999\n", 0) == 0);
    assert(QWMapList_SnapshotTime("# refreshed=nan\n", 0) == 0);
    assert(QWMapList_SnapshotTime("# refreshed=12bad\n", 42) == 42);
    assert(QWMapList_SnapshotTime("# legacy\na.bsp\n", 42) == 42);

    QWMapList_FreeCache(); QWMapTitles_Invalidate();
    VEC_FREE(downloadmapsmenu.items); VEC_FREE(downloadmapsmenu.filtered_indices);

    // The shipped JSON must also load through the engine's actual parser.
    FILE *catalog = fopen(argv[1], "rb"); assert(catalog);
    assert(fseek(catalog, 0, SEEK_END) == 0);
    long size = ftell(catalog); assert(size > 0 && size <= QW_MAPTITLES_MAX_BYTES);
    rewind(catalog);
    char *contents = malloc((size_t)size + 1); assert(contents);
    assert(fread(contents, 1, size, catalog) == (size_t)size); fclose(catalog); contents[size] = 0;
    title_file = contents; QWMapTitles_LoadOnce();
    assert(qw_maptitles_count > 0);
    assert(!strcmp(QWMapTitles_TitleForName("aerowalk.bsp"), "Aerowalk"));
    QWMapTitles_Invalidate(); free(contents);
    puts("Map title loading, search, reload, URL identity and snapshot freshness passed.");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-download-titles-") as directory:
    temp = Path(directory)
    (temp / "test.c").write_text(source)
    sdl = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True))
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(ROOT / "Quake"),
        *sdl, str(temp / "test.c"), str(ROOT / "Quake/json.c"), str(ROOT / "Quake/strlcpy.c"),
        "-o", str(temp / "test"),
    ], check=True)
    subprocess.run([str(temp / "test"), str(ROOT / "Misc/qssm_pak/misc/qw_map_titles.json")], check=True)
