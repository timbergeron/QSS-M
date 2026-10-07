"""Disposable aggregate CPU timers: read/parse versus rewind snapshots/events."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[1]
demo=(REPO/'Quake/cl_demo.c').read_text(encoding='utf-8')
state='''
int benchtd_enabled;
double benchtd_read, benchtd_next, benchtd_finish;
int benchtd_reads, benchtd_nexts, benchtd_finishes;
'''
demo=demo.replace('static qboolean CL_NextDemoFrame(void)',state+'\nstatic qboolean CL_NextDemoFrame(void)',1)
for name,ret,acc,count,storage in [('CL_NextDemoFrame','qboolean','benchtd_next','benchtd_nexts','static '),('CL_FinishDemoFrame','void','benchtd_finish','benchtd_finishes','')]:
    declaration=storage+ret+' '+name+'(void)'
    start=demo.index(declaration);end=demo.index('\n}',start)+2
    body=demo[start:end].replace(declaration,'static '+ret+' '+name+'_Inner(void)',1)
    return_code='qboolean result = '+name+'_Inner();' if ret=='qboolean' else name+'_Inner();'
    finish='return result;' if ret=='qboolean' else ''
    wrapper='''
DECL
{
    qboolean capture = benchtd_enabled && cls.timedemo && cls.signon == SIGNONS;
    double begin = capture ? Sys_DoubleTime() : 0;
    CALL
    if (capture) { ACC += Sys_DoubleTime()-begin; COUNT++; }
    FINISH
}
'''.replace('DECL',declaration).replace('CALL',return_code).replace('ACC',acc).replace('COUNT',count).replace('FINISH',finish)
    demo=demo[:start]+body+wrapper+demo[end:]
demo=demo.replace('\tcls.timedemo = true;','''
    benchtd_enabled = COM_CheckParm("-benchtd") != 0;
    benchtd_read = benchtd_next = benchtd_finish = 0;
    benchtd_reads = benchtd_nexts = benchtd_finishes = 0;
\tcls.timedemo = true;''',1)
marker='\tCon_Printf ("%i frames %5.1f seconds %5.1f fps\\n", frames, time, frames/time);'
assert marker in demo
demo=demo.replace(marker,marker+'''
    if (benchtd_enabled && benchtd_reads)
        Con_Printf("BENCH_TD read_us=%.3f next_us=%.3f finish_us=%.3f reads=%d snapshots=%d deltas=%d\\n",
            benchtd_read*1e6/benchtd_reads, benchtd_nexts ? benchtd_next*1e6/benchtd_nexts : 0,
            benchtd_finishes ? benchtd_finish*1e6/benchtd_finishes : 0,
            benchtd_reads, benchtd_nexts, benchtd_finishes);
''',1)
host=(REPO/'Quake/host.c').read_text(encoding='utf-8')
host=host.replace('void _Host_Frame (double time)','extern int benchtd_enabled, benchtd_reads;\nextern double benchtd_read;\nvoid _Host_Frame (double time)',1)
old='\t\tif (cls.state == ca_connected)\n\t\t\tCL_ReadFromServer ();'
assert host.count(old)==1
host=host.replace(old,'''\t\tif (cls.state == ca_connected)
        {
            qboolean capture = benchtd_enabled && cls.timedemo && cls.signon == SIGNONS;
            double begin = capture ? Sys_DoubleTime() : 0;
            CL_ReadFromServer();
            if (capture) { benchtd_read += Sys_DoubleTime()-begin; benchtd_reads++; }
        }''',1)
assert 'extern int benchtd_enabled' in host
(ROOT/'cl_demo_profile.c').write_text(demo,encoding='utf-8')
(ROOT/'host_td_profile.c').write_text(host,encoding='utf-8')
(ROOT/'timedemo_profile.targets').write_text('''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemGroup><ClCompile Remove="..\\..\\Quake\\cl_demo.c"/><ClCompile Remove="..\\..\\Quake\\host.c"/><ClCompile Include="$(MSBuildThisFileDirectory)cl_demo_profile.c"/><ClCompile Include="$(MSBuildThisFileDirectory)host_td_profile.c"/></ItemGroup></Project>''')
print('Generated isolated timedemo CPU timer source')
