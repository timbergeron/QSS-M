"""Disposable native timedemo stage / asynchronous GPU timer instrumentation."""
from pathlib import Path
R=Path(__file__).resolve().parent;repo=R.parents[1]
vid=(repo/'Quake/gl_vidsdl.c').read_text(encoding='utf-8')
state=r'''
typedef void (APIENTRY *benchtg_counter_t)(GLuint, GLenum);
typedef void (APIENTRY *benchtg_result_t)(GLuint, GLenum, unsigned long long *);
static struct {
    int enabled, active, capture, tick, pending[32];
    GLuint queries[64];
    benchtg_counter_t counter;
    benchtg_result_t result;
    double begin, split, lastend;
    struct { int frames, samples; double world, hud, swap, other, gpu; } stats;
} benchtg = {-1};

void BenchTDRender_Reset_f(void)
{
    memset(&benchtg.stats, 0, sizeof(benchtg.stats));
    memset(benchtg.pending, 0, sizeof(benchtg.pending));
    benchtg.tick = benchtg.capture = 0;
    benchtg.begin = benchtg.split = benchtg.lastend = 0;
    benchtg.active = -1;
}
static void BenchTDRender_Poll(void)
{
    int i;
    if (benchtg.enabled != 1) return;
    for (i=0;i<32;++i) {
        GLuint available=0;
        unsigned long long a,b;
        if (!benchtg.pending[i]) continue;
        GL_GetQueryObjectuivFunc(benchtg.queries[i*2+1],GL_QUERY_RESULT_AVAILABLE,&available);
        if (!available) continue;
        benchtg.result(benchtg.queries[i*2],GL_QUERY_RESULT,&a);
        benchtg.result(benchtg.queries[i*2+1],GL_QUERY_RESULT,&b);
        benchtg.stats.gpu += (b-a)*1e-9;
        benchtg.stats.samples++;
        benchtg.pending[i]=0;
    }
}
void BenchTDRender_Stats_f(void)
{
    double n=benchtg.stats.frames;
    BenchTDRender_Poll();
    if (!n) Sys_Error("BENCH_TD_RENDER no frames");
    Con_Printf("BENCH_TD_RENDER frames=%d world_us=%.3f hud_us=%.3f swap_us=%.3f other_us=%.3f gpu_us=%.3f samples=%d\n",
        benchtg.stats.frames,benchtg.stats.world*1e6/n,benchtg.stats.hud*1e6/n,
        benchtg.stats.swap*1e6/n,benchtg.stats.other*1e6/n,
        benchtg.stats.samples ? benchtg.stats.gpu*1e6/benchtg.stats.samples : 0,benchtg.stats.samples);
}
static void BenchTDRender_Begin(void)
{
    int slot;
    if (benchtg.enabled<0) {
        benchtg.enabled=COM_CheckParm("-benchtdrender")!=0;
        if (benchtg.enabled) {
            benchtg.counter=(benchtg_counter_t)SDL_GL_GetProcAddress("glQueryCounter");
            benchtg.result=(benchtg_result_t)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
            if (!benchtg.counter || !benchtg.result || !GL_GenQueriesFunc || !GL_GetQueryObjectuivFunc)
                Sys_Error("BENCH_TD_RENDER unavailable GPU timestamps");
            GL_GenQueriesFunc(64,benchtg.queries);
        }
    }
    BenchTDRender_Poll();
    benchtg.active=-1;
    benchtg.capture=benchtg.enabled && cls.timedemo && cls.signon==SIGNONS;
    if (!benchtg.capture) { benchtg.lastend=0; return; }
    benchtg.begin=Sys_DoubleTime();
    benchtg.split=0;
    if (benchtg.lastend) benchtg.stats.other += benchtg.begin-benchtg.lastend;
    if ((++benchtg.tick & 15)!=0) return;
    slot=(benchtg.tick>>4)&31;
    if (benchtg.pending[slot]) return;
    benchtg.counter(benchtg.queries[slot*2],0x8E28);
    benchtg.active=slot;
}
void BenchTDRender_3DEnd(void)
{
    if (benchtg.capture) benchtg.split=Sys_DoubleTime();
}
'''
needle='void GL_BeginRendering (int *x, int *y, int *width, int *height)'
assert vid.count(needle)==1
vid=vid.replace(needle,state+'\n'+needle,1).replace(needle+'\n{',needle+'\n{\n\tBenchTDRender_Begin();',1)
needle='\t\tSDL_GL_SwapWindow(draw_context);';assert vid.count(needle)==1
vid=vid.replace(needle,r'''
        double before,after;
        if (benchtg.active>=0) {
            benchtg.counter(benchtg.queries[benchtg.active*2+1],0x8E28);
            benchtg.pending[benchtg.active]=1;
        }
        before=Sys_DoubleTime();
        SDL_GL_SwapWindow(draw_context);
        after=Sys_DoubleTime();
        if (benchtg.capture) {
            double split=benchtg.split ? benchtg.split : before;
            benchtg.stats.frames++;
            benchtg.stats.world += split-benchtg.begin;
            benchtg.stats.hud += before-split;
            benchtg.stats.swap += after-before;
            benchtg.lastend=after;
        }
''',1)
scr=(repo/'Quake/gl_screen.c').read_text(encoding='utf-8')
needle='void SCR_UpdateScreen (void)';scr=scr.replace(needle,'void BenchTDRender_3DEnd(void);\n'+needle,1)
needle='\t\tV_RenderView ();';assert scr.count(needle)==1;scr=scr.replace(needle,needle+'\n\t\tBenchTDRender_3DEnd();',1)
needle='void SCR_Init (void)';assert scr.count(needle)==1;scr=scr.replace(needle,'void BenchTDRender_Reset_f(void);\nvoid BenchTDRender_Stats_f(void);\n'+needle,1)
needle='\tCvar_RegisterVariable (&scr_fade);';assert scr.count(needle)==1
scr=scr.replace(needle,'\tCmd_AddCommand("bench_td_render_reset",BenchTDRender_Reset_f);\n\tCmd_AddCommand("bench_td_render_stats",BenchTDRender_Stats_f);\n'+needle,1)
(R/'gl_vidsdl_td_render.c').write_text(vid,encoding='utf-8');(R/'gl_screen_td_render.c').write_text(scr,encoding='utf-8')
(R/'td_render.targets').write_text('''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemGroup><ClCompile Remove="..\\..\\Quake\\gl_vidsdl.c"/><ClCompile Remove="..\\..\\Quake\\gl_screen.c"/><ClCompile Include="$(MSBuildThisFileDirectory)gl_vidsdl_td_render.c"/><ClCompile Include="$(MSBuildThisFileDirectory)gl_screen_td_render.c"/></ItemGroup></Project>''')
print('Prepared isolated timedemo render/GPU profiler; production renderer unchanged.')
