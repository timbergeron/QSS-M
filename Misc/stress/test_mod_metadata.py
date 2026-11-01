"""Exercise manifest titles and offline cache lifecycle using the engine JSON parser.

Run: python3 Misc/stress/test_mod_metadata.py [optional-live-manifest.json]
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
menu = (ROOT / 'Quake/menu.c').read_text()
common = (ROOT / 'Quake/common.c').read_text()


def function(text, name):
    match = re.search(r'^(?:static )?[^\n]+\b' + name + r'\s*\([^;]*?\n\{', text, re.M)
    assert match, name
    start = match.start()
    return text[start:text.index('\n}', start) + 2]


source = r'''
#include "quakedef.h"
#include "json.h"
#include "q_ctype.h"
#include <assert.h>
#include <strings.h>
#define vsnprintf_func vsnprintf
#define DOWNLOAD_MODS_MAX_URL 1024
#define DOWNLOAD_MODS_MAX_MANIFEST_BYTES (1024 * 1024)
typedef struct { char install_dir[MAX_QPATH]; char title[128]; } modmetadataitem_t;
static modmetadataitem_t *modmetadata_items;
static unsigned int modmetadata_revision;
static qboolean modmetadata_initialized;
static char modmetadata_source[DOWNLOAD_MODS_MAX_URL];
quakeparms_t *host_parms;
static char configured_source[1024] = "https://example.com/one";
static int fetches, clears, rebuilds;
static void M_DownloadMods_StartFetch(qboolean force) { (void)force; fetches++; }
static qboolean M_DownloadMods_ManifestUrl(char *out, size_t n) {
    q_strlcpy(out, configured_source, n); return configured_source[0] != 0;
}
static qboolean CL_DownloadRepoIsQ1Tools_Test(void) { return false; }
#define CL_DownloadRepoIsQ1Tools CL_DownloadRepoIsQ1Tools_Test
static void M_DownloadMods_ClearItemsForSource(const char *url, qboolean seed) {
    (void)url; (void)seed; clears++;
}
static void M_DownloadMods_RebuildVisibleList(void) { rebuilds++; }
static qboolean M_DownloadMods_ApplyManifestJson(const char *text, int *count) {
    (void)text; (void)count; return true;
}
void Sys_Error(const char *fmt, ...) { (void)fmt; abort(); }
'''
source += '\n'.join(function(common, n) for n in ('Vec_Grow', 'Vec_Free', 'q_strcasecmp', 'q_vsnprintf', 'q_snprintf', 'UTF8_WriteCodePoint'))
source += '\n' + '\n'.join(function(menu, n) for n in (
    'M_DownloadMods_InstallDirNameOkay', 'M_DownloadMods_ManifestString',
    'M_ModMetadata_ApplyJson', 'M_ModMetadata_CachePath', 'M_ModMetadata_SaveCache',
    'M_ModMetadata_LoadCache', 'M_ModMetadata_EnsureSource',
    'M_ModMetadata_Revision', 'M_ModMetadata_Description'))
source += r'''
static void expect(const char *dir, const char *title) {
    char out[128] = "";
    assert(M_ModMetadata_Description(dir, out, sizeof(out)));
    assert(!strcmp(out, title));
}
int main(int argc, char **argv) {
    quakeparms_t parms = {0}; host_parms = &parms; parms.userdir = argv[1];
    assert(M_ModMetadata_Revision() == 1);
    assert(fetches == 1 && clears == 1 && rebuilds == 1);
    const char *valid = "{\"mods\":[{\"install_dir\":\"newjam\",\"title\":\"New Jam\"}]}";
    assert(M_ModMetadata_ApplyJson(valid));
    expect("NEWJAM", "New Jam");
    unsigned int revision = M_ModMetadata_Revision();
    assert(!M_ModMetadata_ApplyJson("{broken"));
    assert(!M_ModMetadata_ApplyJson("{\"mods\":[]}"));
    assert(!M_ModMetadata_ApplyJson("[{\"install_dir\":\"../escape\",\"title\":\"Bad\"}]"));
    assert(!M_ModMetadata_ApplyJson("[{\"install_dir\":\"x\",\"title\":\"Bad\\nTitle\"}]"));
    assert(M_ModMetadata_Revision() == revision);
    expect("newjam", "New Jam");
    M_ModMetadata_SaveCache(valid);
    VEC_FREE(modmetadata_items); modmetadata_initialized = false;
    expect("newjam", "New Jam"); /* offline restart loads before any fetch result */
    assert(fetches == 2);
    assert(M_ModMetadata_ApplyJson("[{\"id\":\"alias\",\"name\":\"First\"},{\"folder\":\"ALIAS\",\"name\":\"Last\"}]"));
    expect("alias", "Last");
    q_strlcpy(configured_source, "https://example.com/two", sizeof(configured_source));
    char out[128];
    assert(!M_ModMetadata_Description("newjam", out, sizeof(out)));
    assert(!M_ModMetadata_Description("alias", out, sizeof(out)));
    assert(fetches == 3 && VEC_SIZE(modmetadata_items) == 0);
    q_strlcpy(configured_source, "https://example.com/one", sizeof(configured_source));
    expect("newjam", "New Jam"); /* source switch restores its cached catalogue */
    char path[MAX_OSPATH]; assert(M_ModMetadata_CachePath(path, sizeof(path)));
    FILE *file = fopen(path, "wb"); assert(file);
    fprintf(file, "%s\n{broken", modmetadata_source); fclose(file);
    M_ModMetadata_LoadCache(); expect("newjam", "New Jam");
    file = fopen(path, "wb"); assert(file);
    fprintf(file, "%s\n", modmetadata_source);
    for (int i = 0; i <= DOWNLOAD_MODS_MAX_MANIFEST_BYTES; i++) fputc('x', file);
    fclose(file);
    M_ModMetadata_LoadCache(); expect("newjam", "New Jam");
    configured_source[0] = 0;
    assert(!M_ModMetadata_Description("newjam", out, sizeof(out)));
    if (argc > 2) {
        file = fopen(argv[2], "rb"); assert(file);
        fseek(file, 0, SEEK_END); long size = ftell(file); rewind(file);
        char *text = malloc(size + 1); assert(text);
        assert(fread(text, 1, size, file) == (size_t)size); text[size] = 0; fclose(file);
        assert(M_ModMetadata_ApplyJson(text)); free(text);
        assert(VEC_SIZE(modmetadata_items) > 10);
        printf("live manifest: %zu title mappings\n", VEC_SIZE(modmetadata_items));
    }
    VEC_FREE(modmetadata_items);
    puts("mod metadata lifecycle checks passed");
}
'''

# Keep local overrides (including a deliberately blank line) ahead of metadata.
detect = function(common, 'COM_DetectGameDescription')
assert detect.index('fgets(') < detect.index('M_ModMetadata_Description(') < detect.index('memset(&signatures')
assert 'return description[0] !=' in detect[:detect.index('M_ModMetadata_Description(')]
frame = function(menu, 'M_DownloadMods_Frame')
assert frame.index('M_DownloadMods_PollFetch') < frame.index('if (!downloadmodinstall.active')
assert 'metadata_revision != revision' in (ROOT / 'Quake/discord.c').read_text()

with tempfile.TemporaryDirectory(prefix='qssm-mod-metadata-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(source)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    command = shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=gnu11', '-Wall', '-Wextra', '-I', str(ROOT / 'Quake'), *flags,
        str(tmp / 'test.c'), str(ROOT / 'Quake/json.c'), str(ROOT / 'Quake/strlcpy.c'),
        '-o', str(tmp / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(tmp / 'test'), str(tmp), *sys.argv[1:]], check=True)
