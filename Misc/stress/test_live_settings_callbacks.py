"""Check deferred settings and SDL failure recovery under ASan/UBSan.

python3 Misc/stress/test_live_settings_callbacks.py
Compiles production functions against deterministic SDL/mixer test doubles.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(path, signature):
    text = (ROOT / path).read_text()
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


common = r'''
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define CLAMP(a,x,b) ((x)<(a)?(a):(x)>(b)?(b):(x))
typedef struct { float value; } cvar_t;
static void Con_SafePrintf(const char *fmt, ...) { (void)fmt; }
static void Con_Warning(const char *fmt, ...) { (void)fmt; }
static void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
static const char *SDL_GetError(void) { return "injected failure"; }
'''

vsync = common + r'''
static cvar_t vid_vsync;
static int vid_vsync_pending, vid_vsync_applied, gl_swap_control, gl_context, interval, calls, fail, query_fail, ignore;
static void VID_VSyncChanged(cvar_t *var);
static void Cvar_SetQuick(cvar_t *var, const char *value) {
    var->value = atof(value); VID_VSyncChanged(var);
}
static int SDL_GL_GetSwapInterval(int *value) {
    if (query_fail) return 0;
    *value = interval; return 1;
}
static int SDL_GL_SetSwapInterval(int value) {
    calls++; if (fail) return 0; if (!ignore) interval = value; return 1;
}
'''
vsync += function('Quake/gl_vidsdl.c', 'static void VID_VSyncChanged (')
vsync += function('Quake/gl_vidsdl.c', 'static qboolean VID_GetVSync (')
vsync += function('Quake/gl_vidsdl.c', 'static void VID_ApplyVSync (')
vsync += r'''
int main(void) {
    gl_swap_control = 1;
    vid_vsync.value = 1; VID_VSyncChanged(&vid_vsync);
    VID_ApplyVSync(); assert(calls == 0 && vid_vsync_pending);
    gl_context = 1; VID_ApplyVSync(); assert(calls == 1 && interval == 1);
    for (int i=0; i<100; i++) VID_ApplyVSync();
    assert(calls == 1);
    vid_vsync.value = 0; VID_VSyncChanged(&vid_vsync);
    fail = 1; VID_ApplyVSync(); assert(vid_vsync.value == 1 && !vid_vsync_pending);
    VID_ApplyVSync(); assert(calls == 2);
    fail = 0; vid_vsync.value = 0; VID_VSyncChanged(&vid_vsync);
    VID_ApplyVSync(); assert(interval == 0 && calls == 3);
    ignore = 1; vid_vsync.value = 1; VID_VSyncChanged(&vid_vsync); VID_ApplyVSync();
    assert(vid_vsync.value == 0 && gl_swap_control && !vid_vsync_pending);
    ignore = 0; vid_vsync.value = 1; VID_VSyncChanged(&vid_vsync); VID_ApplyVSync();
    assert(interval == 1 && vid_vsync_applied && gl_swap_control);
    query_fail = fail = 1; vid_vsync.value = 0; VID_VSyncChanged(&vid_vsync); VID_ApplyVSync();
    assert(vid_vsync.value == 1 && !vid_vsync_pending);
    assert(VID_GetVSync());
    puts("VSync: PASS (deferred, stable frames, failure/query rollback, ignored driver request, retry)");
}
'''

audio = common + r'''
static cvar_t snd_mixspeed = {44100}, snd_surround;
static int snd_initialized = 1, snd_settings_pending, snd_blocked, sound_started = 1;
static int scr_disabled_for_loading;
static struct { int speed; } dma = {44100}, *shm = &dma;
static int restarts, layouts, fail;
static int applied_surround;
static double now, snd_mixspeed_deadline;
static double Sys_DoubleTime(void) { return now; }
static int SNDDMA_GetSurround(void) { return applied_surround; }
static void SND_SettingsChanged(cvar_t *var);
static void Cvar_SetValueQuick(cvar_t *var, float value) {
    var->value = value; SND_SettingsChanged(var);
}
static void S_Restart_f(void) {
    assert(scr_disabled_for_loading);
    restarts++; snd_settings_pending = 0;
    if (fail) { fail = 0; sound_started = 0; return; }
    sound_started = 1; shm->speed = (int)snd_mixspeed.value; applied_surround = snd_surround.value > 0;
}
static void SNDDMA_DeviceChanged(void) { layouts++; applied_surround = snd_surround.value > 0; }
'''
audio += function('Quake/snd_dma.c', 'static void SND_SettingsChanged (')
audio += function('Quake/snd_dma.c', 'void S_DeferMixRate (')
audio += function('Quake/snd_dma.c', 'void S_ApplySettings (')
audio += r'''
int main(void) {
    Cvar_SetValueQuick(&snd_mixspeed, 11025); Cvar_SetValueQuick(&snd_mixspeed, 44100);
    S_ApplySettings(); assert(restarts == 0);
    snd_blocked = 1; Cvar_SetValueQuick(&snd_mixspeed, 48000); S_ApplySettings();
    assert(snd_settings_pending && restarts == 0);
    snd_blocked = 0; S_ApplySettings(); assert(restarts == 1 && shm->speed == 48000 && !scr_disabled_for_loading);
    Cvar_SetValueQuick(&snd_surround, 1); S_ApplySettings(); assert(restarts == 1 && layouts == 2);
    fail = 1; Cvar_SetValueQuick(&snd_surround, 0); Cvar_SetValueQuick(&snd_mixspeed, 22050); S_ApplySettings();
    assert(restarts == 3 && sound_started && snd_mixspeed.value == 48000 && shm->speed == 48000);
    assert(snd_surround.value == 1 && applied_surround);
    for (int i=0; i<100; i++) S_ApplySettings();
    assert(restarts == 3);
    now = 10; Cvar_SetValueQuick(&snd_mixspeed, 11025); S_DeferMixRate();
    now = 10.2; S_ApplySettings(); assert(restarts == 3 && snd_settings_pending);
    Cvar_SetValueQuick(&snd_mixspeed, 22050); S_DeferMixRate();
    now = 10.3; S_ApplySettings(); assert(restarts == 3);
    Cvar_SetValueQuick(&snd_surround, 0); S_ApplySettings();
    assert(restarts == 3 && !applied_surround && snd_settings_pending);
    now = 10.46; S_ApplySettings(); assert(restarts == 4 && shm->speed == 22050);
    Cvar_SetValueQuick(&snd_mixspeed, 44100); S_DeferMixRate();
    Cvar_SetValueQuick(&snd_mixspeed, 48000); S_ApplySettings();
    assert(restarts == 5 && shm->speed == 48000 && !snd_settings_pending);
    Cvar_SetValueQuick(&snd_mixspeed, NAN); assert(snd_mixspeed.value == 44100);
    Cvar_SetValueQuick(&snd_mixspeed, -1); assert(snd_mixspeed.value == 8000);
    Cvar_SetValueQuick(&snd_mixspeed, INFINITY); assert(snd_mixspeed.value == 44100);
    Cvar_SetValueQuick(&snd_mixspeed, 1e20f); assert(snd_mixspeed.value == 192000);
    puts("Audio settings: PASS (menu debounce, console override, coalescing, blocked audio, surround, rate rollback, invalid rates)");
}
'''

surround = common + r'''
typedef unsigned char Uint8;
typedef struct { int format, channels, freq; } SDL_AudioSpec;
#define SND_MIX_CHANNELS 2
#define SDL_AUDIO_S8 1
#define SDL_AUDIO_U8 2
#define SDL_AUDIO_S16 3
static struct { int samplebits, signed8, speed; } dma = {16, 0, 44100}, *shm = &dma;
static int sdl_stream = 1, device_channels = 2, sdl_scratch_bytes = 4096, preferred = 6;
static Uint8 *sdl_scratch;
static cvar_t snd_surround;
static int sdl_surround_applied, alloc_fail;
static void Cvar_SetValueQuick(cvar_t *var, float value) { var->value = value; }
static void *mock_malloc(size_t bytes) { return alloc_fail ? NULL : malloc(bytes); }
static const char *sdl_devicename = "test";
static int locked, fail, calls;
static void SND_UpdateDeviceName(void) {}
static int SDL_GetAudioStreamDevice(int stream) { return stream; }
static int SND_GetPreferredOutputChannels(int device) { (void)device; return preferred; }
static void SDL_LockAudioStream(int stream) { (void)stream; assert(!locked); locked = 1; }
static void SDL_UnlockAudioStream(int stream) { (void)stream; assert(locked); locked = 0; }
static int SDL_SetAudioStreamFormat(int stream, SDL_AudioSpec *input, void *output) {
    (void)stream; (void)output; assert(locked && input->freq == 44100); calls++; return !fail;
}
#define malloc mock_malloc
'''
surround += function('Quake/snd_sdl.c', 'void SNDDMA_DeviceChanged (')
surround += function('Quake/snd_sdl.c', 'qboolean SNDDMA_GetSurround (')
surround += r'''
#undef malloc
int main(void) {
    sdl_scratch = malloc(sdl_scratch_bytes);
    snd_surround.value = 1; SNDDMA_DeviceChanged();
    assert(device_channels == 6 && sdl_scratch_bytes == 12288 && !locked);
    snd_surround.value = 0; SNDDMA_DeviceChanged();
    assert(device_channels == 2 && sdl_scratch_bytes == 4096);
    fail = 1; snd_surround.value = 1; Uint8 *old = sdl_scratch;
    SNDDMA_DeviceChanged(); assert(device_channels == 2 && sdl_scratch == old && !locked && snd_surround.value == 0);
    fail = 0; preferred = 8; snd_surround.value = 1; SNDDMA_DeviceChanged(); assert(device_channels == 8);
    SNDDMA_DeviceChanged(); assert(calls == 4);
    alloc_fail = 1; old = sdl_scratch; snd_surround.value = 0; SNDDMA_DeviceChanged();
    assert(device_channels == 8 && sdl_scratch == old && snd_surround.value == 1 && SNDDMA_GetSurround());
    alloc_fail = 0; snd_surround.value = 0; SNDDMA_DeviceChanged(); assert(device_channels == 2);
    preferred = 2; snd_surround.value = 1; SNDDMA_DeviceChanged(); assert(SNDDMA_GetSurround());
    fail = 1; preferred = 6; SNDDMA_DeviceChanged();
    assert(snd_surround.value == 1 && SNDDMA_GetSurround() && device_channels == 2);
    free(sdl_scratch);
    puts("Surround layout: PASS (6/2/8 channels, allocation/format rollback, stereo preference, stable frames)");
}
'''

texture = common + r'''
static cvar_t gl_picmip, gl_max_size;
static int pending;
static void TexMgr_SizeChanged(cvar_t *var);
static void Cvar_SetValueQuick(cvar_t *var, float value) {
    var->value = value; TexMgr_SizeChanged(var);
}
static void Mod_TexturePolicyChanged(cvar_t *var) { (void)var; pending++; }
'''
texture += function('Quake/gl_texmgr.c', 'static void TexMgr_SizeChanged (')
texture += r'''
int main(void) {
    Cvar_SetValueQuick(&gl_picmip, NAN); assert(gl_picmip.value == 0);
    Cvar_SetValueQuick(&gl_picmip, -1); assert(gl_picmip.value == 0);
    Cvar_SetValueQuick(&gl_picmip, 1e20f); assert(gl_picmip.value == 30);
    Cvar_SetValueQuick(&gl_picmip, 2.5f); assert(gl_picmip.value == 2);
    Cvar_SetValueQuick(&gl_max_size, -1e20f); assert(gl_max_size.value == -1048576);
    Cvar_SetValueQuick(&gl_max_size, INFINITY); assert(gl_max_size.value == 0);
    assert(pending == 6);
    puts("Texture settings: PASS (nonfinite values, bounds, fractional levels)");
}
'''

lightmaps = common + r'''
#define SURF_DRAWTURB 1
typedef struct { int numverts; } glpoly_t;
typedef struct {
    int dlightframe, cached_dlight, flags;
    unsigned int dlightbits[2];
} msurface_t;
typedef struct { char name[8]; void *vertexes; int numsurfaces; msurface_t *surfaces; } qmodel_t;
static void *r_pcurrentvertbase;
static qmodel_t *currentmodel;
static int lm_defer_fill, gl_glsl_water_able, r_framecount = 200, builds, jobs;
static glpoly_t poly;
static void GL_CreateSurfaceLightmap(qmodel_t *m, msurface_t *s) {
    (void)m;
    assert(s->dlightframe != r_framecount && !s->cached_dlight && !s->dlightbits[0] && !s->dlightbits[1]);
    builds++;
}
static glpoly_t *AllocSurfacePoly(msurface_t *s) { (void)s; return &poly; }
static void LM_QueueFillJob(qmodel_t *m, msurface_t *s, glpoly_t *p) { (void)m; (void)s; (void)p; jobs++; }
static void BuildSurfaceDisplayList(msurface_t *s) { (void)s; }
'''
lightmaps += function('Quake/r_brush.c', 'void GL_BuildModel (')
lightmaps += r'''
int main(void) {
    msurface_t surfaces[2] = {{.dlightframe=200, .cached_dlight=1, .dlightbits={3,4}},
                            {.dlightframe=2, .cached_dlight=1, .dlightbits={5,6}, .flags=SURF_DRAWTURB}};
    qmodel_t model = {.name="world", .numsurfaces=2, .surfaces=surfaces};
    GL_BuildModel(&model); assert(builds == 2 && r_framecount == 200);
    lm_defer_fill = 1; GL_BuildModel(&model); assert(builds == 4 && jobs == 2);
    r_framecount = 0; GL_BuildModel(&model); assert(builds == 6 && r_framecount == 0);
    model.name[0] = '*'; GL_BuildModel(&model); assert(builds == 6);
    puts("Lightmaps: PASS (stale dynamic-light marks cleared, serial/deferred/legacy/inline paths)");
}
'''

models = common + r'''
enum { mod_alias, mod_brush, mod_sprite };
#define MAX_MOD_KNOWN 4
#define LERP_RESETANIM 1
typedef struct { void *data; } cache_user_t;
typedef struct { int type; cache_user_t cache; } qmodel_t;
typedef struct { int lerpflags; struct { int valid; } aliaslightcache; } entity_t;
static entity_t entities[2], statics[1];
static struct { int num_entities, num_statics; entity_t *entities, viewent;
    struct { entity_t *ent; } static_entities[1]; } cl;
static qmodel_t mod_known[4];
static int mod_numknown = 4, frees, wheel_clears, reloads;
static void Cache_Free(cache_user_t *c) { assert(c->data); c->data=NULL; frees++; }
static void Wheel_ClearIcons(void) { wheel_clears++; }
static void *Mod_Extradata(qmodel_t *m) { assert(!m->cache.data && frees == 1); reloads++; return m->cache.data=m; }
'''
models += function('Quake/gl_model.c', 'static void Mod_RefreshReplacementModels (')
models += r'''
int main(void) {
    int data;
    cache_user_t sound = {&data};
    mod_known[0] = (qmodel_t){mod_alias, {&data}};
    mod_known[1] = (qmodel_t){mod_alias, {NULL}};
    mod_known[2] = (qmodel_t){mod_brush, {&data}};
    mod_known[3] = (qmodel_t){mod_sprite, {&data}};
    cl.num_entities=2; cl.entities=entities; cl.num_statics=1; cl.static_entities[0].ent=statics;
    entities[0].aliaslightcache.valid = statics[0].aliaslightcache.valid = cl.viewent.aliaslightcache.valid = 1;
    Mod_RefreshReplacementModels();
    assert(frees == 1 && reloads == 1 && wheel_clears == 1 && sound.data == &data);
    assert(mod_known[0].cache.data && !mod_known[1].cache.data);
    assert(entities[0].lerpflags & LERP_RESETANIM && entities[1].lerpflags & LERP_RESETANIM);
    assert(statics[0].lerpflags & LERP_RESETANIM && cl.viewent.lerpflags & LERP_RESETANIM);
    assert(!entities[0].aliaslightcache.valid && !statics[0].aliaslightcache.valid && !cl.viewent.aliaslightcache.valid);
    assert(mod_known[2].cache.data == &data && mod_known[3].cache.data == &data);
    puts("Model replacements: PASS (eager alias reload, entity/static/viewmodel animation reset, other caches preserved)");
}
'''

eviction = common + r'''
#define SRC_LIGHTMAP 1
#define TEXPREF_UI 1
typedef struct gltexture_s { struct gltexture_s *next; void *owner; int source_format, flags; } gltexture_t;
static gltexture_t *active_gltextures;
static gltexture_t first, victim, last;
static int reloads, freed, colormaps;
static int in_reload_images;
static int plcolour_none;
static int TexMgr_TextureSizeChanged(const gltexture_t *g) { return 1; }
static void TexMgr_ColormapTexture_Free(void *p) { assert(!p); colormaps++; }
static void TexMgr_ReloadImage(gltexture_t *g, int top, int bottom) {
    (void)top; (void)bottom;
    assert(!g->owner); reloads++;
    if (g == &first && !in_reload_images) { assert(g->next == &victim); g->next=victim.next; freed++; }
}
'''
eviction += function('Quake/gl_texmgr.c', 'void TexMgr_ReloadTextureSettings (')
eviction += r'''
int main(void) {
    victim.owner=&victim; victim.next=&last; first.next=&victim;
    active_gltextures=&first; TexMgr_ReloadTextureSettings();
    assert(reloads == 2 && freed == 1 && colormaps == 1);
    puts("Texture reload: PASS (owner eviction during upload preserves traversal)");
}
'''

real_headers = r'''
#include "quakedef.h"
#include <assert.h>
#undef main
client_state_t cl;
client_static_t cls;
void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
void Sys_Error(const char *fmt, ...) { (void)fmt; abort(); }
'''
poses = real_headers + 'cvar_t r_lerpmodels;\n'
alias_source = (ROOT / 'Quake/r_alias.c').read_text()
lerptype_end = alias_source.index('} lerpdata_t;') + len('} lerpdata_t;')
lerptype_start = alias_source.rindex('typedef struct', 0, lerptype_end)
poses += alias_source[lerptype_start:lerptype_end] + '\n'
poses += function('Quake/mathlib.c', 'void R_ConcatTransforms (')
poses += function('Quake/r_alias.c', 'void R_SetupAliasFrame (')
poses += r'''
struct fixture {
    aliashdr_t hdr;
    maliasframedesc_t extra[1];
    boneinfo_t bone;
    bonepose_t pose;
};
int main(void) {
    struct fixture f = {0};
    entity_t e = {0}; qmodel_t model = {0}; lerpdata_t out = {0};
    e.model = &model; r_lerpmodels.value = 1; cl.time = 1;
    f.hdr.numframes = f.hdr.nummorphposes = 1;
    f.hdr.frames[0].numposes = 1; f.hdr.frames[0].interval = .1f;
    e.lerp.state.previouspose = 57; e.lerp.state.currentpose = 58;
    R_SetupAliasFrame(&f.hdr, &e, &out);
    assert(out.pose1 == 0 && out.pose2 == 0 && e.lerp.state.previouspose == 0);
    e.lerp.state.previouspose = -1; e.lerp.state.currentpose = 0;
    R_SetupAliasFrame(&f.hdr, &e, &out); assert(out.pose1 == 0 && out.pose2 == 0);
    f.hdr.nummorphposes = 0; f.hdr.numboneposes = f.hdr.numbones = 1;
    f.hdr.boneinfo = (byte *)&f.bone - (byte *)&f.hdr;
    f.hdr.boneposedata = (byte *)&f.pose - (byte *)&f.hdr;
    f.bone.parent = -1;
    for (int i=0; i<3; i++) f.pose.mat[i*4+i] = f.bone.inverse.mat[i*4+i] = 1;
    e.lerp.state.previouspose = 30; e.lerp.state.currentpose = 31;
    R_SetupAliasFrame(&f.hdr, &e, &out);
    assert(out.pose1 == 0 && out.pose2 == 0 && out.bonestate && out.bonestate[0].mat[0] == 1);
    f.hdr.numboneposes = f.hdr.numbones = 0; f.hdr.nummorphposes = 7;
    f.hdr.frames[0].firstpose = 5; f.hdr.frames[0].numposes = 2;
    e.lerpflags = LERP_EXPLICIT; e.lerp.snap.lerpfrac = 0; e.lerp.snap.time[0] = .15;
    R_SetupAliasFrame(&f.hdr, &e, &out); assert(out.pose1 == 6 && out.pose2 == 5);
    f.hdr.numframes = 2; f.hdr.nummorphposes = 3; f.hdr.frames[0].firstpose = 0;
    maliasframedesc_t second = {.firstpose=2, .numposes=1, .interval=.1f};
    memcpy((byte *)&f.hdr + offsetof(aliashdr_t,frames) + sizeof(maliasframedesc_t), &second, sizeof(second));
    e.lerp.snap.frame2 = 1; e.lerp.snap.lerpfrac = .75f; e.lerp.snap.time[1] = .15;
    R_SetupAliasFrame(&f.hdr, &e, &out); assert(out.pose1 == 1 && out.pose2 == 2);
    puts("Alias animation: PASS (fewer morph/bone poses, negative stale poses, explicit group offsets)");
}
'''

lighting = real_headers + r'''
static int traces;
int VectorCompare(const vec3_t a, const vec3_t b) { return !memcmp(a,b,sizeof(vec3_t)); }
static int R_LightPointCacheEligible(entity_t *e, vec3_t p) { return 1; }
static int R_LightPointTraceSample(vec3_t p, aliaslight_sample_t *s) {
    traces++; s->valid=true; s->surfindex=traces; return 1;
}
static int R_LightPointEvaluateCachedSample(const aliaslight_sample_t *s) { return s->surfindex; }
static int R_LightPointWithRaise(vec3_t p, float raise) { abort(); }
'''
lighting += function('Quake/gl_rlight.c', 'int R_LightPointCachedAlias (')
lighting += r'''
int main(void) {
    qmodel_t world = {0}, model = {0}; entity_t visible = {0}, hidden = {0};
    byte samples[3] = {0};
    cl.worldmodel=&world; world.lightdata=samples; world.lighting_generation=1;
    visible.model=hidden.model=&model;
    assert(R_LightPointCachedAlias(&hidden,hidden.origin,0) == 1 && traces == 1);
    assert(R_LightPointCachedAlias(&hidden,hidden.origin,0) == 1 && traces == 1);
    world.lighting_generation=2;
    assert(R_LightPointCachedAlias(&visible,visible.origin,0) == 2);
    world.lighting_generation=3; /* same allocation, hidden entity was not drawn */
    assert(R_LightPointCachedAlias(&hidden,hidden.origin,0) == 3 && traces == 3);
    assert(R_LightPointCachedAlias(&hidden,hidden.origin,0) == 3 && traces == 3);
    puts("Alias lighting: PASS (generation changes invalidate hidden entities even with identical sample pointers)");
}
'''

sizes = real_headers + r'''
cvar_t gl_picmip, gl_max_size, r_fastturb;
qboolean gl_texture_NPOT;
int gl_hardware_maxsize = 4096;
'''
for signature in ('int TexMgr_Pad (', 'int TexMgr_SafeTextureSize (', 'int TexMgr_SafeTextureSize2 (',
                  'int TexMgr_PadConditional (', 'static qboolean TexMgr_TextureSizeChanged ('):
    sizes += function('Quake/gl_texmgr.c', signature)
sizes += r'''
int main(void) {
    gltexture_t g = {.name="gfx/caustics", .source_format=SRC_RGBA,
                     .source_width=64, .source_height=32, .width=64, .height=32};
    gl_texture_NPOT=true;
    assert(!TexMgr_TextureSizeChanged(&g));
    gl_max_size.value=16; assert(!TexMgr_TextureSizeChanged(&g));
    gl_picmip.value=2; assert(TexMgr_TextureSizeChanged(&g));
    g.width=16; g.height=8; assert(!TexMgr_TextureSizeChanged(&g));
    g.flags=TEXPREF_NOPICMIP; g.width=64; g.height=32;
    assert(!TexMgr_TextureSizeChanged(&g));
    g.source_format=SRC_EXTERNAL; assert(!TexMgr_TextureSizeChanged(&g));
    g.flags=0; assert(TexMgr_TextureSizeChanged(&g));
    g.source_format=SRC_INDEXED; g.flags=TEXPREF_PAD|TEXPREF_NOPICMIP;
    g.source_width=30; g.source_height=18; g.width=30; g.height=18;
    assert(!TexMgr_TextureSizeChanged(&g));
    gl_max_size.value=0; assert(!TexMgr_TextureSizeChanged(&g)); /* NPOT needs no padding */
    gl_texture_NPOT=false; assert(TexMgr_TextureSizeChanged(&g));
    g.width=32; g.height=32; assert(!TexMgr_TextureSizeChanged(&g));
    strcpy(g.name,"maps/fixture.bsp:wall"); gl_max_size.value=-16;
    assert(TexMgr_TextureSizeChanged(&g)); /* grayscale changes at identical size */
    puts("Texture filtering: PASS (unchanged sizes, no-picmip, unknown formats, indexed padding, grayscale)");
}
'''

inputs = real_headers + r'''
qmodel_t *loadmodel;
static byte *mod_base;
char loadname[32];
cvar_t gl_loadlitfiles;
static cvar_t external_lits_dir = {.string=""};
static unsigned int mod_lighting_generation;
qofs_t com_filesize;
static int identity(int n) { return n; }
int (*LittleLong)(int) = identity;
void Con_DPrintf2(const char *fmt, ...) { (void)fmt; }
void Con_Printf(const char *fmt, ...) { (void)fmt; }
size_t q_strlcpy(char *d, const char *s, size_t n) { snprintf(d,n,"%s",s); return strlen(s); }
size_t q_strlcat(char *d, const char *s, size_t n) { size_t len=strlen(d); return len+q_strlcpy(d+len,s,n-len); }
int q_snprintf(char *d, size_t n, const char *fmt, ...) {
    va_list a; va_start(a,fmt); int r=vsnprintf(d,n,fmt,a); va_end(a); return r;
}
void COM_StripExtension(const char *s, char *d, size_t n) {
    if (s != d) q_strlcpy(d,s,n);
    char *dot=strrchr(d,'.'); if (dot) *dot=0;
}
const char *COM_SkipPath(const char *s) { const char *slash=strrchr(s,'/'); return slash?slash+1:s; }
qboolean COM_FileExists(const char *name, unsigned int *id) { return false; }
static void *hunk[32];
static int hunkcount, external_size;
static byte external[32], hdr[8], rgb[6];
static int use_hdr, use_rgb;
void *Hunk_AllocName(int n, const char *name) { assert(hunkcount<32); return hunk[hunkcount++]=calloc(1,n); }
void *Hunk_AllocNameNoFill(int n, const char *name) { return Hunk_AllocName(n,name); }
int Hunk_LowMark(void) { return hunkcount; }
void Hunk_FreeToLowMark(int mark) { while(hunkcount>mark) free(hunk[--hunkcount]); }
byte *COM_LoadHunkFile(const char *name, unsigned int *id) {
    if (!external_size) return NULL;
    byte *p=Hunk_AllocName(external_size,name); memcpy(p,external,external_size);
    com_filesize=external_size; *id=1; return p;
}
static void *Q1BSPX_FindLump(char *name, int *size) {
    *size=0;
    if (use_hdr && !strcmp(name,"LIGHTING_E5BGR9")) { *size=sizeof(hdr); return hdr; }
    if (use_rgb && !strcmp(name,"RGBLIGHTING")) { *size=sizeof(rgb); return rgb; }
    return NULL;
}
'''
model_source = (ROOT / 'Quake/gl_model.c').read_text()
type_start = model_source.index('typedef struct mod_lightingsource_s')
type_end = model_source.index('} mod_lightingsource_t;', type_start) + len('} mod_lightingsource_t;')
inputs += model_source[type_start:type_end] + '\n'
inputs += function('Quake/gl_model.c', 'static void Mod_CaptureLightingSource (')
inputs += function('Quake/gl_model.c', 'static void Mod_LoadLighting (')
inputs += r'''
int main(void) {
    qmodel_t m={0}; lump_t lump={.fileofs=0,.filelen=2}; byte raw[]={10,20};
    strcpy(m.name,"maps/fixture.bsp"); m.path_id=1; loadmodel=&m; mod_base=raw;
    use_hdr=use_rgb=1; hdr[0]=99; rgb[0]=70;
    Mod_CaptureLightingSource(&lump);
    int mark=Hunk_LowMark(); raw[0]=0; hdr[0]=0; rgb[0]=0;
    Mod_LoadLighting(m.lighting_source);
    assert(m.lightdata[0]==10 && m.lightdata[3]==20 && m.lightdatasamples==2 && !(m.flags&MOD_HDRLIGHTING));
    unsigned int generation=m.lighting_generation;
    Hunk_FreeToLowMark(mark); gl_loadlitfiles.value=1;
    Mod_LoadLighting(m.lighting_source); assert(m.lightdata[0]==99 && m.flags&MOD_HDRLIGHTING);
    assert(m.lighting_generation!=generation);
    Hunk_FreeToLowMark(mark); m.lighting_source->hdr=NULL;
    Mod_LoadLighting(m.lighting_source); assert(m.lightdata[0]==70 && !(m.flags&MOD_HDRLIGHTING));
    Hunk_FreeToLowMark(mark);
    memcpy(external,"QLIT",4); ((int *)external)[1]=1; external[8]=90; external_size=14;
    Mod_LoadLighting(m.lighting_source); assert(m.lightdata[0]==90 && m.lightdatasamples==2);
    Hunk_FreeToLowMark(mark); external_size=1; /* malformed header must fall back without an out-of-bounds read */
    Mod_LoadLighting(m.lighting_source); assert(m.lightdata[0]==70);
    Hunk_FreeToLowMark(mark); external_size=0; m.bspversion=BSPVERSION_QUAKE64;
    Mod_LoadLighting(m.lighting_source); assert(m.lightdatasamples==1 && !(m.flags&MOD_HDRLIGHTING));
    Hunk_FreeToLowMark(0);
    puts("Lighting inputs: PASS (immutable BSP snapshots, BSPX HDR/RGB, external precedence, short headers, Quake64)");
}
'''

wav = common + 'typedef unsigned char byte;\nstatic byte *data_p;\n'
wav += function('Quake/snd_mem.c', 'static int GetLittleLong (')
wav += r'''
int main(void) {
    byte bytes[] = {0x78,0x56,0x34,0xc0, 0xff,0xff,0xff,0x7f, 0xff,0xff,0xff,0xff};
    data_p = bytes;
    assert((unsigned int)GetLittleLong() == 0xc0345678u);
    assert(GetLittleLong() == 0x7fffffff);
    assert(GetLittleLong() == -1 && data_p == bytes+12);
    puts("WAV integers: PASS (high-bit bytes without signed-shift overflow)");
}
'''

with tempfile.TemporaryDirectory(prefix='live-settings-callbacks-') as directory:
    path = Path(directory)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    for name, source in [('vsync', vsync), ('audio', audio), ('surround', surround), ('texture', texture),
                         ('lightmaps', lightmaps), ('models', models), ('eviction', eviction),
                         ('poses', poses), ('lighting', lighting), ('sizes', sizes), ('inputs', inputs), ('wav', wav)]:
        cfile, binary = path / f'{name}.c', path / name
        cfile.write_text(source)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-I', str(ROOT / 'Quake'), *flags,
                        str(cfile), '-o', str(binary), '-lm'], check=True)
        subprocess.run([str(binary)], check=True)
