"""Exercise production area-list collision filtering with ASan/UBSan.

Run: python3 Misc/stress/test_trigger_clipping.py (requires cc and SDL3).
Use --baseline REV to reproduce the fatal trigger check before the fix.
Engine types and the list walker are real; narrow-phase collision is recorded
and returns a fixed hit so these tests isolate candidate filtering/traversal.
No game assets or window are needed.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', help='read world.c from this git revision')
options = parser.parse_args()
world = (subprocess.check_output(['git', 'show', f'{options.baseline}:Quake/world.c'],
                                 cwd=ROOT, text=True) if options.baseline else
         (ROOT / 'Quake/world.c').read_text())


def function(declaration):
    start = world.index(declaration)
    return world[start:world.index('\n}', start) + 2] + '\n'


source = r'''
#include "quakedef.h"
#include <assert.h>
#undef main
THREAD_LOCAL qcvm_t *qcvm;
cvar_t pr_checkextension;
static qcvm_t vm;
static edict_t entities[5];
static int calls, checks;
static edict_t *last_hit;
void Sys_Error(const char *fmt, ...) {
    fprintf(stderr, "FAIL: %s\n", fmt);
    exit(1);
}
int NUM_FOR_EDICT(edict_t *ent) { return (int)(ent - entities); }
trace_t SV_ClipMoveToEntity(edict_t *ent, vec3_t start, vec3_t mins,
                          vec3_t maxs, vec3_t end, unsigned int mask) {
    (void)start; (void)mins; (void)maxs; (void)end; (void)mask;
    trace_t result = {0};
    assert(ent->v.solid != SOLID_NOT && ent->v.solid != SOLID_TRIGGER &&
           ent->v.solid != SOLID_EXT_BSPTRIGGER);
    calls++;
    last_hit = ent;
    result.fraction = 0.5f;
    return result;
}
'''
start = world.index('typedef struct\n{')
source += world[start:world.index('} moveclip_t;', start) + len('} moveclip_t;')]
source += r'''
static const laggedentinfo_t *SV_LaggedPlayerState(const moveclip_t *clip,
                                                 const edict_t *ent, size_t slot) {
    (void)clip; (void)ent; (void)slot;
    return NULL;
}
'''
source += function('static void SV_ClipToLinks (')
source += r'''
static void node_init(areanode_t *node) {
    memset(node, 0, sizeof(*node));
    node->axis = -1;
    node->solid_edicts.next = node->solid_edicts.prev = &node->solid_edicts;
    node->trigger_edicts.next = node->trigger_edicts.prev = &node->trigger_edicts;
}
static void append(areanode_t *node, edict_t *ent) {
    link_t *head = &node->solid_edicts;
    ent->area.prev = head->prev;
    ent->area.next = head;
    head->prev->next = &ent->area;
    head->prev = &ent->area;
}
static moveclip_t clip_init(void) {
    moveclip_t clip = {0};
    for (int i = 0; i < 3; i++) {
        clip.boxmins[i] = -100;
        clip.boxmaxs[i] = 100;
    }
    clip.trace.fraction = 1;
    clip.hitcontents = CONTENTMASK_ANYSOLID;
    return clip;
}
static void trace_check(areanode_t *root, moveclip_t clip, edict_t *expected) {
    /* A trace must not relink entities or alter their QuakeC fields. */
    edict_t before[5];
    memcpy(before, entities, sizeof(before));
    calls = 0;
    last_hit = NULL;
    SV_ClipToLinks(root, &clip);
    assert(calls == (expected ? 1 : 0));
    assert(last_hit == expected && clip.trace.ent == expected);
    assert(clip.trace.fraction == (expected ? 0.5f : 1.0f));
    assert(!memcmp(before, entities, sizeof(before)));
    checks++;
}
int main(void) {
    qcvm = &vm;
    vm.edicts = entities;
    vm.edict_size = sizeof(edict_t);
    /* Cover both trigger types in root and recursively visited child lists. */
    for (int child = 0; child < 3; child++) {
        for (int extended = 0; extended < 2; extended++) {
            areanode_t root, children[2];
            memset(entities, 0, sizeof(entities));
            node_init(&root);
            node_init(&children[0]);
            node_init(&children[1]);
            areanode_t *list = &root;
            if (child) {
                root.axis = 0;
                root.children[0] = &children[0];
                root.children[1] = &children[1];
                list = &children[child - 1];
            }
            edict_t *first = &entities[1], *target = &entities[2];
            edict_t *last = &entities[3];
            first->v.solid = last->v.solid = SOLID_BBOX;
            append(list, first);
            append(list, target);
            append(list, last);
            /* Simulate QC changing solidity after entities were linked. */
            first->v.solid = last->v.solid =
                extended ? SOLID_EXT_BSPTRIGGER : SOLID_TRIGGER;
            moveclip_t clip = clip_init();
            trace_check(&root, clip, NULL); /* all candidates nonblocking */
            target->v.solid = SOLID_BBOX;
            trace_check(&root, clip, target); /* still reaches real blocker */
            target->v.solid = SOLID_SLIDEBOX;
            trace_check(&root, clip, target);
            clip.passedict = target;
            trace_check(&root, clip, NULL);
            clip.passedict = NULL;
            clip.lagowner = target;
            trace_check(&root, clip, NULL);
            clip.lagowner = NULL;
            clip.type = MOVE_NOMONSTERS;
            trace_check(&root, clip, NULL);
            target->v.solid = SOLID_BSP;
            trace_check(&root, clip, target);
            clip.type = MOVE_NORMAL;
            target->v.solid = SOLID_NOT;
            /* Temporary trigger solidity can be restored without relinking. */
            first->v.solid = SOLID_BBOX;
            trace_check(&root, clip, first);
            first->v.solid = SOLID_NOT;
            last->v.solid = SOLID_BBOX;
            trace_check(&root, clip, last);
            last->v.absmin[0] = 101;
            trace_check(&root, clip, NULL);
        }
    }
    printf("PASS: %d trigger collision filtering cases\n", checks);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='qssm-trigger-clipping-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    cc = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run([*cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'Quake'), str(work / 'test.c'),
                    '-o', str(work / 'test'), *flags, '-lm'], check=True)
    subprocess.run([str(work / 'test')], check=True)
