"""Generate throwaway profiling source; production source is untouched."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[1]
text=(REPO/'Quake/gl_vidsdl.c').read_text()
state='''
static struct { int enabled, skipped, frames, done; double begin, lastend, period, render, swap; } benchvid = {-1};
'''
text=text.replace('void GL_BeginRendering (int *x, int *y, int *width, int *height)',state+'\nvoid GL_BeginRendering (int *x, int *y, int *width, int *height)',1)
text=text.replace('void GL_BeginRendering (int *x, int *y, int *width, int *height)\n{','void GL_BeginRendering (int *x, int *y, int *width, int *height)\n{\n\tbenchvid.begin = Sys_DoubleTime ();',1)
old='''\t\tSDL_GL_SwapWindow(draw_context);'''
new='''
		double before = Sys_DoubleTime (), after;
		SDL_GL_SwapWindow(draw_context);
		after = Sys_DoubleTime ();
		if (benchvid.enabled < 0) benchvid.enabled = COM_CheckParm ("-benchvid") != 0;
		if (benchvid.enabled && !benchvid.done && cls.demoplayback && !cls.timedemo && cls.signon == SIGNONS)
		{
			if (++benchvid.skipped > 3000 && benchvid.lastend > 0)
			{
				benchvid.frames++;
				benchvid.period += after - benchvid.lastend;
				benchvid.render += before - benchvid.begin;
				benchvid.swap += after - before;
				if (benchvid.period >= 5)
				{
					Con_Printf ("BENCH_VID frames=%d seconds=%.6f fps=%.2f render_us=%.3f swap_us=%.3f other_us=%.3f\\n", benchvid.frames, benchvid.period, benchvid.frames / benchvid.period, benchvid.render * 1e6 / benchvid.frames, benchvid.swap * 1e6 / benchvid.frames, (benchvid.period-benchvid.render-benchvid.swap) * 1e6 / benchvid.frames);
					benchvid.done = 1;
				}
			}
		}
		benchvid.lastend = after;'''
assert text.count(old)==1
text=text.replace(old,new,1)
(ROOT/'gl_vidsdl_profile.c').write_text(text)
(ROOT/'video_profile.targets').write_text('''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemGroup><ClCompile Remove="..\\..\\Quake\\gl_vidsdl.c"/><ClCompile Include="$(MSBuildThisFileDirectory)gl_vidsdl_profile.c"/></ItemGroup></Project>''')
print('Generated isolated video timing source')
