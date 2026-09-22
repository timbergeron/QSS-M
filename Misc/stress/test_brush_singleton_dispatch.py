"""Run the real brush grouping dispatcher with recording draw/cull stubs."""
from pathlib import Path
import os, re, shlex, subprocess, tempfile

renderer = (Path(__file__).resolve().parents[2] / 'Quake/r_world.c').read_text()

def function(name):
    match = re.search(r'(?:static )?(?:void|int) ' + name + r'\s*\([^;]*?\)\s*\{', renderer)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (renderer[end] == '{') - (renderer[end] == '}')
        end += 1
    return renderer[match.start():end]

source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef struct {int unused;} qmodel_t;
typedef struct {qmodel_t *model;int frame,eligible,culled,draws;} entity_t;
static entity_t *r_bmodel_group_ents[16];
static int probes,culls,groups,reserve_ok=1;
static int R_ReserveBModelInstancingBuffers(int count){return reserve_ok;}
static int R_CanInstanceBrushEntity(entity_t *e,void *cache){probes++;return e->eligible;}
static int R_CullModelForEntity(entity_t *e){culls++;return e->culled;}
static void R_DrawBrushModel(entity_t *e){if(!R_CullModelForEntity(e))e->draws++;}
static void R_DrawBrushModelInstancedGroup(entity_t **ents,int count){
 int i;if(count==1){R_DrawBrushModel(ents[0]);return;}
 if(count>1){groups++;for(i=0;i<count;i++)ents[i]->draws++;}
}
#define CHECK(c,msg) do{if(!(c)){puts("FAIL: " msg);return 1;}}while(0)
'''
source += function('R_CompareInstancedBrushEntities')
source += function('R_DrawBrushModelsInstanced')
source += r'''
int main(void){
 qmodel_t models[2]={{0},{0}};
 entity_t a={&models[0],0,1,0,0},b={&models[0],0,1,0,0},c={&models[1],1,1,0,0};
 entity_t *single[]={&a},*pair[]={&a,&b},*mixed[]={&c,&b,&a};
 R_DrawBrushModelsInstanced(single,1);
 CHECK(a.draws==1&&culls==1&&probes==0,"a singleton must draw once without instancing probes or duplicate culling");
 probes=culls=0;a.draws=0;a.culled=1;
 R_DrawBrushModelsInstanced(single,1);
 CHECK(!a.draws&&culls==1&&!probes,"culled singleton must retain individual visibility checks");
 a.culled=0;a.eligible=0;probes=culls=0;
 R_DrawBrushModelsInstanced(single,1);
 CHECK(a.draws==1&&culls==1&&!probes,"unsupported singleton must retain individual fallback");
 a.eligible=1;a.draws=b.draws=c.draws=0;probes=culls=groups=0;
 R_DrawBrushModelsInstanced(mixed,3);
 CHECK(a.draws==1&&b.draws==1&&c.draws==1&&groups==1&&probes==2,"repeated models must still batch beside singleton models");
 a.draws=b.draws=0;b.frame=1;probes=culls=groups=0;
 R_DrawBrushModelsInstanced(pair,2);
 CHECK(a.draws==1&&b.draws==1&&!groups&&!probes&&culls==2,"different animation frames form separate singleton groups");
 b.frame=0;b.eligible=0;a.draws=b.draws=0;probes=culls=groups=0;
 R_DrawBrushModelsInstanced(pair,2);
 CHECK(a.draws==1&&b.draws==1&&!groups&&probes==2,"mixed eligibility inside a repeated group must retain fallback");
 b.eligible=1;reserve_ok=0;a.draws=b.draws=0;probes=culls=0;
 R_DrawBrushModelsInstanced(pair,2);
 CHECK(a.draws==1&&b.draws==1&&!probes&&culls==2,"allocation failure must still draw every entity individually");
 probes=culls=0;R_DrawBrushModelsInstanced(NULL,0);
 CHECK(!probes&&!culls,"empty input must do no work");
 puts("PASS: singleton dispatch, culling, grouping, frames and allocation fallback");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-brush-singleton-') as tmp:
    work = Path(tmp)
    (work / 'test.c').write_text(source)
    cc = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    args = [*cc, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}'] if Path(cc[0]).stem.lower() == 'cl' else [*cc, '-std=c99', '-Wall', '-O2', 'test.c', '-o', str(binary)]
    subprocess.run(args, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
