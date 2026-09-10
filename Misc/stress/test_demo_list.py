"""Check demo file enumeration and compare bulk sorting with insertion behavior.

Run with python3 Misc/stress/test_demo_list.py (requires cc and SDL2).
Timings cover list construction/sorting, excluding filesystem I/O and parsing.
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
menu = (ROOT / "Quake/menu.c").read_text()
common = (ROOT / "Quake/common.c").read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


SOURCE = r'''
#include "quakedef.h"
#include "q_ctype.h"
#include <assert.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <time.h>
#define vsnprintf_func vsnprintf

void Sys_Error(const char *fmt, ...) { (void)fmt; abort(); }
static double now(void) {
    return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}
'''
start = menu.index("typedef enum\n{\n\tDEMO_MINFRAMES_UNKNOWN")
SOURCE += menu[start:menu.index("} demoitem_t;", start) + len("} demoitem_t;")]
SOURCE += r'''
static struct {
    demoitem_t *items;
    int democount;
    int minframes_threshold;
} demosmenu;
static demoitem_t *demos_sort_base;
static qboolean M_Demos_CurrentGameIsId1(void) { return false; }
static qboolean M_Demos_MetadataCache_ApplyToItem(demoitem_t *di) {
    (void)di;
    return false;
}
static qboolean M_Demos_BuildCacheKey(char *key, size_t size,
                                    const char *name, searchpath_t *spath) {
    return q_snprintf(key, size, "%s/%s", spath->filename, name) < (int)size;
}
'''
SOURCE += (ROOT / "Quake/strlcpy.c").read_text()
SOURCE += (ROOT / "Quake/strlcat.c").read_text()
for declaration in (
    "int q_strcasecmp(", "int q_strncasecmp(", "char *q_strcasestr(",
    "int q_vsnprintf(", "int q_snprintf (",
    "void Vec_Grow(", "void Vec_Free(",
    "int char_to_int (", "int find_and_parse_date_time (",
    "int q_sortdemos (", "const char *COM_SkipPath (", "void COM_FileBase (",
    "static void COM_ListFiles(",
):
    SOURCE += function(common, declaration)
for declaration in (
    "static qboolean M_IsTimestampStart(", "static qboolean M_IsDatePrefix(",
    "static void M_InferDemoMapName(", "static void M_Demos_FormatFileDate(",
    "static void M_Demos_AddEx(", "static int M_Demos_CompareNames(",
    "static int M_Demos_CompareDates(", "static void M_Demos_ApplyOrder(",
    "static void M_Demos_SortItems(",
    "static qboolean M_Demos_FieldMatchesTerm(", "static qboolean M_Demos_DateMatchesTerm(",
):
    SOURCE += function(menu, declaration)

SOURCE += r'''
/* Independent reference: keep the first matching name and insert each new
 * record after all records with an equal or newer date, as the old menu did. */
static int reference_insert(demoitem_t *out, const demoitem_t *input, int n) {
    int count = 0;
    for (int i = 0; i < n; ++i) {
        int j, pos;
        for (j = 0; j < count; ++j)
            if (!q_strcasecmp(input[i].name, out[j].name)) break;
        if (j != count) continue;
        for (pos = 0; pos < count; ++pos)
            if (q_sortdemos(input[i].date, out[pos].date) > 0) break;
        memmove(out + pos + 1, out + pos, (count - pos) * sizeof(*out));
        out[pos] = input[i];
        ++count;
    }
    return count;
}

static unsigned int rng = 1234567;
static unsigned int next_random(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static searchpath_t sources[3];

static void check_case(int n, int mode, qboolean duplicates) {
    demoitem_t *input, *expected;
    char name[MAX_QPATH];
    double start, collect_ms, reference_ms, bulk_ms;
    int expected_count;
    assert(!demosmenu.items && !demosmenu.democount);
    demosmenu.minframes_threshold = mode % 2 ? 100 : 0;
    start = now();
    for (int i = 0; i < n; ++i) {
        int stamp = mode == 0 ? i : mode == 1 ? n - i :
                    mode == 2 ? 0 : (int)(next_random() % 1000);
        int id = duplicates ? i % 100 : i;
        const char *prefix = i % 3 == 0 ? "./" : i % 3 == 1 ? "demos/" : "demos/sub/";
        time_t mtime = i % 17 == 0 ? 0 : (time_t)1700000000 + stamp;
        q_snprintf(name, sizeof(name), "%s%s_%05d.%s", prefix,
                   i % 2 ? "dm2" : "DM2", id, i % 2 ? "dem" : "DEM");
        M_Demos_AddEx(name, COM_SkipPath(name), i % 2,
                      mtime, 1000 + i, &sources[i % 3]);
    }
    collect_ms = (now() - start) * 1000;
    assert(demosmenu.democount == n && VEC_SIZE(demosmenu.items) == (size_t)n);
    input = calloc(n ? n : 1, sizeof(*input));
    expected = calloc(n ? n : 1, sizeof(*expected));
    assert(input && expected);
    if (n) memcpy(input, demosmenu.items, n * sizeof(*input));
    for (int i = 0; i < n; ++i) {
        assert(!input[i].date[0]); /* no date conversions on menu entry */
        M_Demos_FormatFileDate(input[i].mtime, input[i].date, sizeof(input[i].date));
    }
    start = now();
    expected_count = reference_insert(expected, input, n);
    reference_ms = (now() - start) * 1000;
    start = now();
    M_Demos_SortItems();
    bulk_ms = (now() - start) * 1000;
    assert(demosmenu.democount == expected_count);
    assert(VEC_SIZE(demosmenu.items) == (size_t)expected_count);
    for (int i = 0; i < expected_count; ++i)
        memset(expected[i].date, 0, sizeof(expected[i].date));
    /* Compare the complete record: source, cache key, size, inferred map,
     * min-frame state and display label must all stay attached to the file. */
    if (expected_count)
        assert(!memcmp(demosmenu.items, expected, expected_count * sizeof(*expected)));
    M_Demos_SortItems();
    if (expected_count)
        assert(!memcmp(demosmenu.items, expected, expected_count * sizeof(*expected)));
    if (n >= 6000)
        printf("%d files, mode %d%s: collect %.2f ms, old sort/dedup %.2f ms, bulk %.2f ms\n",
               n, mode, duplicates ? " with overrides" : "", collect_ms, reference_ms, bulk_ms);
    free(input);
    free(expected);
    Vec_Free((void **)&demosmenu.items);
    demosmenu.democount = 0;
}

static void check_case_insensitive_override(void) {
    /* Later sources have newer dates; the first source must still win. */
    M_Demos_AddEx("demos/Foo.dem", "Foo.dem", false, 1, 100, &sources[0]);
    M_Demos_AddEx("demos/foo.DEM", "foo.DEM", true, 2, 200, &sources[1]);
    M_Demos_AddEx("./Foo.dem", "../Foo.dem", false, 0, 300, &sources[2]);
    M_Demos_AddEx("demos/sub/Foo.dem", "Foo.dem", false, 0, 400, &sources[2]);
    M_Demos_SortItems();
    assert(demosmenu.democount == 3);
    assert(!strcmp(demosmenu.items[0].name, "./Foo.dem"));
    assert(!strcmp(demosmenu.items[1].name, "demos/sub/Foo.dem"));
    assert(!strcmp(demosmenu.items[2].name, "demos/Foo.dem"));
    assert(demosmenu.items[2].source_searchpath == &sources[0]);
    assert(demosmenu.items[2].fsize == 100 && !demosmenu.items[2].from_id1);
    Vec_Free((void **)&demosmenu.items);
    demosmenu.democount = 0;
}

static void check_date_search(void) {
    const time_t times[] = {0, 1700000000};
    const char *misses[] = {"dm2", "player", "2026/09", "something", "UNKNOWN DATE"};
    for (int t = 0; t < (int)Q_COUNTOF(times); ++t) {
        char date[32], term[32];
        M_Demos_FormatFileDate(times[t], date, sizeof(date));
        /* Every nonempty substring must still match, both cold and cached. */
        for (int i = 0; date[i]; ++i) {
            for (int len = 1; len <= (int)strlen(date + i); ++len) {
                demoitem_t item = {.mtime = times[t]};
                memcpy(term, date + i, len);
                term[len] = 0;
                assert(M_Demos_DateMatchesTerm(&item, term));
                assert(M_Demos_DateMatchesTerm(&item, term));
                assert(!strcmp(item.date, date));
            }
        }
        for (int i = 0; i < (int)Q_COUNTOF(misses); ++i) {
            demoitem_t item = {.mtime = times[t]};
            assert(M_Demos_DateMatchesTerm(&item, misses[i]) ==
                   (q_strcasestr(date, misses[i]) != NULL));
        }
        demoitem_t item = {.mtime = times[t]};
        assert(!M_Demos_DateMatchesTerm(&item, "dm2") && !item.date[0]);
    }
}

static qboolean listed_file(void *ctx, const char *name, time_t mtime,
                           size_t size, searchpath_t *spath) {
    assert(ctx == spath);
    printf("%s\t%lld\t%zu\n", name, (long long)mtime, size);
    return true;
}

int main(int argc, char **argv) {
    if (argc == 4) {
        searchpath_t spath = {0};
        q_strlcpy(spath.filename, argv[1], sizeof(spath.filename));
        COM_ListFiles(&spath, &spath, argv[2], listed_file,
                      !strcmp(argv[3], "files") ? COM_LIST_NODIRS : 0);
        return 0;
    }
    q_strlcpy(sources[0].filename, "mod", sizeof(sources[0].filename));
    q_strlcpy(sources[1].filename, "id1", sizeof(sources[1].filename));
    q_strlcpy(sources[2].filename, "pak", sizeof(sources[2].filename));
    check_case_insensitive_override();
    const int counts[] = {0, 1, 37, 6000};
    for (int i = 0; i < (int)Q_COUNTOF(counts); ++i)
        for (int mode = 0; mode < 4; ++mode)
            check_case(counts[i], mode, false);
    check_case(6000, 3, true);
    check_date_search();
    puts("PASS: ordering, overrides, root/subfolder identity, complete records, rebuilds and lazy date search");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-demo-list-") as tmp:
    path = Path(tmp)
    source = path / "test.c"
    binary = path / "test"
    source.write_text(SOURCE)
    flags = shlex.split(subprocess.check_output(["sdl2-config", "--cflags", "--libs"], text=True))
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-std=gnu11", "-DUSE_SDL2",
                    "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "Quake"),
                    str(source), "-o", str(binary), *flags, "-lm"], check=True)

    fixtures = path / "files"
    fixtures.mkdir()
    (fixtures / "normal.dem").write_bytes(b"-1\n")
    (fixtures / "alias.dem").symlink_to("normal.dem")
    (fixtures / "directory.dem").mkdir()
    (fixtures / "directory-link.dem").symlink_to("directory.dem")
    (fixtures / "broken.dem").symlink_to("missing")
    os.mkfifo(fixtures / "pipe.dem")
    (fixtures / ".hidden.dem").write_bytes(b"-1\n")
    (fixtures / "normal.dz").write_bytes(b"compressed demo fixture")
    os.mkfifo(fixtures / "pipe.dz")
    (fixtures / "sub").mkdir()
    (fixtures / "sub" / "normal.dem").write_bytes(b"-1\n")

    def list_files(pattern, mode):
        output = subprocess.check_output(
            [str(binary), str(fixtures), pattern, mode], text=True, timeout=5)
        return {name: (int(mtime), int(size))
                for name, mtime, size in (line.split("\t") for line in output.splitlines())}

    demos = list_files("*.dem", "files")
    assert set(demos) == {"normal.dem", "alias.dem"}, demos
    for name, metadata in demos.items():
        stat = (fixtures / name).stat()
        assert metadata == (int(stat.st_mtime), stat.st_size), (name, metadata)
    assert set(list_files("*.dz", "files")) == {"normal.dz"}
    assert set(list_files("sub/*.dem", "files")) == {"sub/normal.dem"}
    assert not list_files("missing/*.dem", "files")
    # Unflagged callers must retain their existing directory and failed-stat results.
    unfiltered = list_files("*.dem", "all")
    assert set(unfiltered) == {"normal.dem", "alias.dem", "directory.dem",
                               "directory-link.dem", "broken.dem", "pipe.dem"}, unfiltered
    assert unfiltered["broken.dem"] == (0, 0)
    print("PASS: regular files/symlinks and metadata; excludes directories, broken links and pipes; unflagged behavior preserved", flush=True)
    subprocess.run([str(binary)], check=True, timeout=60)
