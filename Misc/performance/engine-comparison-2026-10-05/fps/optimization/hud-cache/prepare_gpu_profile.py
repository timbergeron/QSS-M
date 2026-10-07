"""Async, sampled GPU timestamps in a disposable build, with fixed demo interval."""
from pathlib import Path
ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
text = (REPO / 'Quake/gl_vidsdl.c').read_text()
state = r'''
typedef void (APIENTRY *bench_counter_t)(GLuint, GLenum);
typedef void (APIENTRY *bench_result_t)(GLuint, GLenum, unsigned long long *);
static struct {
    int enabled, done, frames, tick, active, samples, pending[32];
    GLuint queries[64];
    bench_counter_t counter;
    bench_result_t result;
    double start, begin, lastend, period, render, swap, gpu;
} benchgpu = {-1};

static void BenchGPU_Begin(void)
{
    int slot, i;
    if (benchgpu.enabled < 0) {
        benchgpu.enabled = COM_CheckParm("-benchgpu") != 0;
        if (benchgpu.enabled) {
            benchgpu.counter = (bench_counter_t)SDL_GL_GetProcAddress("glQueryCounter");
            benchgpu.result = (bench_result_t)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
            if (!benchgpu.counter || !benchgpu.result || !GL_GenQueriesFunc || !GL_GetQueryObjectuivFunc)
                Sys_Error("BENCH_GPU timer query API unavailable");
            GL_GenQueriesFunc(64, benchgpu.queries);
        }
    }
    benchgpu.active = -1;
    benchgpu.begin = Sys_DoubleTime();
    if (!benchgpu.enabled || benchgpu.done || !cls.demoplayback || cls.timedemo || cls.signon != SIGNONS)
        return;
    if (!benchgpu.start) benchgpu.start = benchgpu.begin;
    if (benchgpu.begin - benchgpu.start < 2 || benchgpu.begin - benchgpu.start >= 12)
        return;
    for (i = 0; i < 32; ++i) {
        GLuint available = 0;
        unsigned long long a, b;
        if (!benchgpu.pending[i]) continue;
        GL_GetQueryObjectuivFunc(benchgpu.queries[i*2+1], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) continue;
        benchgpu.result(benchgpu.queries[i*2], GL_QUERY_RESULT, &a);
        benchgpu.result(benchgpu.queries[i*2+1], GL_QUERY_RESULT, &b);
        benchgpu.gpu += (b-a) * 1e-9;
        benchgpu.samples++;
        benchgpu.pending[i] = 0;
    }
    if ((++benchgpu.tick & 15) != 0) return;
    slot = (benchgpu.tick >> 4) & 31;
    if (benchgpu.pending[slot]) return;
    benchgpu.counter(benchgpu.queries[slot*2], 0x8E28); /* GL_TIMESTAMP */
    benchgpu.active = slot;
}
'''
text = text.replace('void GL_BeginRendering (int *x, int *y, int *width, int *height)', state + '\nvoid GL_BeginRendering (int *x, int *y, int *width, int *height)', 1)
text = text.replace('void GL_BeginRendering (int *x, int *y, int *width, int *height)\n{', 'void GL_BeginRendering (int *x, int *y, int *width, int *height)\n{\n\tBenchGPU_Begin();', 1)
old = '\t\tSDL_GL_SwapWindow(draw_context);'
new = r'''
        double before, after;
        if (benchgpu.active >= 0) {
            benchgpu.counter(benchgpu.queries[benchgpu.active*2+1], 0x8E28);
            benchgpu.pending[benchgpu.active] = 1;
        }
        before = Sys_DoubleTime();
        SDL_GL_SwapWindow(draw_context);
        after = Sys_DoubleTime();
        if (benchgpu.enabled && !benchgpu.done && benchgpu.start && cls.demoplayback && cls.signon == SIGNONS) {
            if (before-benchgpu.start >= 2 && before-benchgpu.start < 12 && benchgpu.lastend > 0) {
                benchgpu.frames++;
                benchgpu.period += after-benchgpu.lastend;
                benchgpu.render += before-benchgpu.begin;
                benchgpu.swap += after-before;
            }
            if (before-benchgpu.start >= 12) {
                Con_Printf("BENCH_GPU frames=%d seconds=%.6f fps=%.2f render_us=%.3f swap_us=%.3f other_us=%.3f gpu_us=%.3f samples=%d\n",
                    benchgpu.frames, benchgpu.period, benchgpu.frames/benchgpu.period,
                    benchgpu.render*1e6/benchgpu.frames, benchgpu.swap*1e6/benchgpu.frames,
                    (benchgpu.period-benchgpu.render-benchgpu.swap)*1e6/benchgpu.frames,
                    benchgpu.samples ? benchgpu.gpu*1e6/benchgpu.samples : 0, benchgpu.samples);
                benchgpu.done = 1;
            }
        }
        benchgpu.lastend = after;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
(ROOT / 'gl_vidsdl_gpu.c').write_text(text)
(ROOT / 'gpu_profile.targets').write_text('''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemGroup><ClCompile Remove="..\\..\\Quake\\gl_vidsdl.c"/><ClCompile Include="$(MSBuildThisFileDirectory)gl_vidsdl_gpu.c"/></ItemGroup></Project>''')
print('Generated disposable GPU timestamp source')
