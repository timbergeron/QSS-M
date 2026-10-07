"""Exercise the real file candidate search against loose files and a PACK mount.

Run from a Visual Studio developer shell on Windows, or with CC on POSIX.
The legacy loop provides the same search semantics but fails the filesystem
probe budget, so this test also reproduces the loading regression.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
common = (ROOT / "Quake/common.c").read_text(encoding="utf-8")
start = common.index("static int COM_FindFile_impl")
if "#define COM_DIRCACHE_SLOTS" in common:
    start = common.rindex("/*", 0, common.index("#define COM_DIRCACHE_SLOTS"))
find = common[start:common.index("\n/*\n===========\nCOM_FileExists", start)]
start = common.index("int COM_FOpenFile (")
opens = common[start:common.index("\n/*\n============\nCOM_CloseFile", start)]
image = (ROOT / "Quake/image.c").read_text(encoding="utf-8")
start = image.index("enum imgkind {")
locate = image[start:image.index("\nstatic qboolean Image_PrefetchTake", start)]
if "int COM_FOpenFileCandidates (" not in opens:
    opens += r'''
int COM_FOpenFileCandidates (const char *const *names, int count, FILE **file, unsigned int *path_id)
{
    int i;
    for (i=0; i<count; i++)
    {
        COM_FOpenFile(names[i],file,path_id);
        if (*file)
            return i;
    }
    return -1;
}
'''

source = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#define q_strcasecmp _stricmp
#else
#include <sys/stat.h>
#include <strings.h>
#define mkdir_one(p) mkdir(p,0700)
#define q_strcasecmp strcasecmp
#endif
#define MAX_OSPATH 256
#define FS_ENT_NONE 0
#define FS_ENT_FILE 1
#define FS_ENT_DIRECTORY 2
#define false 0
#define true 1
#define CHECK(x,m) do { if (!(x)) { puts("FAIL: " m); exit(1); } } while (0)
#define q_snprintf snprintf
#define countof(x) (sizeof(x)/sizeof((x)[0]))
static size_t q_strlcpy(char *d,const char *s,size_t n) { size_t z=strlen(s); if(n) { size_t k=z<n-1?z:n-1; memcpy(d,s,k);d[k]=0; } return z; }
typedef int qboolean;
typedef struct { char name[128]; int filelen, filepos, deflatedsize; } packfile_t;
typedef struct { char filename[256]; int handle,numfiles; packfile_t *files; } pack_t;
typedef struct searchpath_s { char filename[256]; pack_t *pack; unsigned int path_id; struct searchpath_s *next; } searchpath_t;
static searchpath_t *com_searchpaths;
static struct { float value; } developer={1}, registered={1};
static double com_findfile_time;
static unsigned int com_findfile_calls;
static int com_filesize, file_from_pak, type_calls;
static double Sys_DoubleTime(void) { return 0; }
static void Sys_Error(const char *s) { puts(s); exit(2); }
static void Con_DPrintf(const char *s,...) { (void)s; }
static void Con_DPrintf2(const char *s,...) { (void)s; }
static const char *COM_FileGetExtension(const char *s) { const char *p=strrchr(s,'.'); return p?p+1:""; }
static int COM_FindPackFileIndex(pack_t *p,const char *s) {
    int i; for(i=0;i<p->numfiles;i++) if(!strcmp(p->files[i].name,s)) return i; return -1;
}
static int COM_filelength(FILE *f) { long p=ftell(f), n; fseek(f,0,SEEK_END); n=ftell(f); fseek(f,p,SEEK_SET); return (int)n; }
static FILE *FSZIP_Deflate(FILE *f,int a,int b,const char *s) { (void)a;(void)b;(void)s;return f; }
static int Sys_FileOpenStdio(FILE *f) { (void)f;return 1; }
static void Sys_FileSeek(int h,int p) { (void)h;(void)p; }
static int Sys_FileOpenRead(const char *s,int *h) { (void)s;*h=-1;return -1; }
static struct { float value; } gl_load24bit_skins={1};
static char loadfilename[MAX_OSPATH];
static const char *COM_SkipPath(const char *s) {
    const char *a=strrchr(s,'/'); return a?a+1:s;
}
static int Sys_FileType(const char *s) {
    type_calls++;
#ifdef _WIN32
    DWORD a=GetFileAttributesA(s);
    return a==INVALID_FILE_ATTRIBUTES?FS_ENT_NONE:(a&FILE_ATTRIBUTE_DIRECTORY)?FS_ENT_DIRECTORY:FS_ENT_FILE;
#else
    struct stat st; return stat(s,&st)?FS_ENT_NONE:S_ISDIR(st.st_mode)?FS_ENT_DIRECTORY:FS_ENT_FILE;
#endif
}
''' + find + opens + locate + r'''
static void put(const char *path,const char *s) { FILE *f=fopen(path,"wb"); CHECK(f,"write fixture"); fputs(s,f); fclose(f); }
static void expect(const char *const *names,int n,int index,int byte,unsigned int path) {
    FILE *f=NULL; unsigned int id=999;
    int got=COM_FOpenFileCandidates(names,n,&f,&id);
    CHECK(got==index,"candidate order"); CHECK(f!=NULL,"successful open");
    CHECK(fgetc(f)==byte,"winning mount contents"); CHECK(id==path,"winning path id"); fclose(f);
}
int main(void) {
    searchpath_t high={"high",NULL,2,NULL}, low={"low",NULL,1,NULL}, packed={"",NULL,4,NULL};
    packfile_t entry={"textures/start/pack.png",1,0,0};
    pack_t pak={"fixture.pak",0,1,&entry};
    FILE *f=NULL; unsigned int id=123; int i, n=0; char namesbuf[96][MAX_OSPATH]; const char *names[96];
    const char *formats[]={"dds","tga","png","jpeg","jpg","pcx"};
    const char *stems[]={"textures/start/missing","textures/textures/start/missing","textures/missing"};
    mkdir_one("high"); mkdir_one("low"); mkdir_one("low/textures"); mkdir_one("high/textures");
    high.next=&packed; packed.pack=&pak; packed.next=&low; com_searchpaths=&high;
    put("fixture.pak","P");
    /* Missing parents should be tested once per batch, never once per suffix. */
    for(i=0;i<18;i++) { snprintf(namesbuf[n],MAX_OSPATH,"%s.%s",stems[i/6],formats[i%6]); names[n]=namesbuf[n]; n++; }
    snprintf(namesbuf[n],MAX_OSPATH,"%s.lmp",stems[0]); names[n]=namesbuf[n]; n++;
    /* textures/ exists, so the basename fallback still probes each file. */
    type_calls=0;
    CHECK(COM_FOpenFileCandidates(names,n,&f,&id)==-1 && !f,"missing image");
    CHECK(com_filesize==-1 && !file_from_pak && id==123,"miss state matches ordinary find");
    CHECK(type_calls<=28,"missing parent suffix searches must share filesystem probes");
    /* Files that appear after a batch, including a newly-created parent, are visible. */
    mkdir_one("high/textures/start"); put("high/textures/start/missing.dds","D");
    expect(names,n,0,'D',2);
    /* Format priority remains outside mount priority: lower DDS beats higher PNG. */
    { const char *c[]={"textures/order.dds","textures/order.png"};
      put("low/textures/order.dds","L"); put("high/textures/order.png","H");
      expect(c,2,0,'L',1); put("high/textures/order.dds","U"); expect(c,2,0,'U',2); }
    /* PACK hits are not filtered by the absence of a loose parent directory. */
    { const char *c[]={"textures/start/pack.dds","textures/start/pack.png"};
      expect(c,2,1,'P',4); CHECK(file_from_pak,"PACK origin retained"); }
    /* Image_Locate historically uses the open stream, even if size is -1. */
    { const char *c[]={"textures/start/pack.png"};
      entry.filelen=-1; expect(c,1,0,'P',4); entry.filelen=1; }
    /* Empty files count as a successful open, just like COM_FOpenFile. */
    { const char *c[]={"textures/empty.png"}; put("high/textures/empty.png","");
      expect(c,1,0,EOF,2); CHECK(com_filesize==0,"zero length retained"); }
    /* Existing parents never cache a missing filename across searches. */
    { const char *c[]={"textures/later.png"}; CHECK(COM_FOpenFileCandidates(c,1,&f,NULL)==-1,"absent existing-parent file");
      put("high/textures/later.png","N"); expect(c,1,0,'N',2); }
    /* A load scope shares missing-parent probes across searches, then forgets them. */
    { const char *c[]={"scoped/a.png"}, *d[]={"scoped/b.png"}; int before;
      COM_BeginLoadCache();
      CHECK(COM_FOpenFileCandidates(c,1,&f,NULL)==-1 && !f,"scoped miss");
      before=type_calls;
      CHECK(COM_FOpenFileCandidates(d,1,&f,NULL)==-1 && !f,"second scoped miss");
      CHECK(type_calls==before,"missing parent probed once per load scope");
      COM_EndLoadCache();
      mkdir_one("high/scoped"); put("high/scoped/b.png","S"); expect(d,1,0,'S',2); }
    /* Host_Error unwinds past open scopes; aborting must leave none behind. */
    { const char *c[]={"aborted/x.png"};
      COM_BeginLoadCache(); COM_BeginLoadCache();
      CHECK(COM_FOpenFileCandidates(c,1,&f,NULL)==-1,"miss inside aborted scope");
      COM_AbortLoadCache(); CHECK(!com_dircache_depth && !com_dircache_count,"abort drops cache");
      mkdir_one("high/aborted"); put("high/aborted/x.png","A"); expect(c,1,0,'A',2); }
    /* A bounded cache must gracefully fall back when more parents are requested. */
    for(i=0;i<95;i++) { snprintf(namesbuf[i],MAX_OSPATH,"absent%d/file.png",i); names[i]=namesbuf[i]; }
    names[95]="textures/order.png"; expect(names,96,95,'H',2);
    /* Shareware directory restrictions are still enforced. */
    registered.value=0; CHECK(COM_FOpenFileCandidates(names,96,&f,NULL)==-1 && !f,"shareware loose restrictions");
    /* PACK lookup remains available under those same restrictions. */
    { const char *c[]={"textures/start/pack.png"}; expect(c,1,0,'P',4); }
    registered.value=1;
    /* Exercise Image_Locate itself, including all aliases and format mappings. */
    put("high/textures/start/formats.jpeg","J");
    put("high/textures/start/formats.jpg","j");
    CHECK(Image_Locate("textures/start/formats",&f)==IMG_JPEG,"jpeg before jpg");
    CHECK(!strcmp(loadfilename,"textures/start/formats.jpeg") && fgetc(f)=='J',"located name and stream");
    fclose(f); put("high/textures/start/formats.dds","D");
    CHECK(Image_Locate("textures/start/formats",&f)==IMG_DDS,"DDS priority retained"); fclose(f);
    mkdir_one("high/textures/textures"); mkdir_one("high/textures/textures/start");
    put("high/textures/textures/start/prefix.png","T");
    put("high/textures/prefix.dds","B");
    CHECK(Image_Locate("textures/start/prefix",&f)==IMG_PNG,"prefix before basename fallback");
    CHECK(!strcmp(loadfilename,"textures/textures/start/prefix.png") && fgetc(f)=='T',"prefixed winner"); fclose(f);
    put("high/textures/onlybase.pcx","C");
    CHECK(Image_Locate("textures/start/onlybase",&f)==IMG_PCX && fgetc(f)=='C',"basename fallback retained"); fclose(f);
    put("high/plain.lmp","L");
    CHECK(Image_Locate("plain",&f)==IMG_LMP && fgetc(f)=='L',"original LMP last resort"); fclose(f);
    mkdir_one("high/progs"); put("high/progs/player.mdl_0.png","S");
    gl_load24bit_skins.value=0;
    CHECK(Image_Locate("progs/player.mdl_0",&f)==IMG_NONE && !f,"disabled player skins");
    CHECK(!strcmp(loadfilename,"progs/player.mdl_0.lmp"),"last attempted name on miss");
    gl_load24bit_skins.value=1;
    CHECK(Image_Locate("progs/player.mdl_0",&f)==IMG_PNG && fgetc(f)=='S',"enabled player skins"); fclose(f);
#ifndef _WIN32
    put("high/textures/literal\\name.png","B");
    CHECK(Image_Locate("textures/literal\\name",&f)==IMG_PNG && fgetc(f)=='B',"POSIX literal backslash filename"); fclose(f);
#endif
    puts("PASS: candidate priority, PACK and loose precedence, miss state, empty files, newly-added files, load scopes and abort, bounded cache, shareware restrictions, and probe budget");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-image-search-") as tmp:
    work = Path(tmp)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    command = ([*cc, "/nologo", "/W3", "/O2", "test.c", f"/Fe:{binary}"]
               if Path(cc[0]).stem.lower() == "cl" else
               [*cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-fsanitize=address,undefined", "test.c", "-o", str(binary)])
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], cwd=work, check=True)
