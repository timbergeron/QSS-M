"""Exercise production static/baseline parsers and message readers with ASan/UBSan.

Run: python3 Misc/stress/test_static_model_indices.py (requires cc and SDL3).
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
args = argparse.ArgumentParser(description=__doc__)
args.add_argument('--baseline', type=Path, help='run against a saved cl_parse.c to reproduce the bug')
options = args.parse_args()
parser = (options.baseline or ROOT / 'Quake/cl_parse.c').read_text()
common = (ROOT / 'Quake/common.c').read_text()


def function(text, declaration):
    start = text.index(declaration)
    return text[start:text.index('\n}', start) + 2] + '\n'


source = r'''
#include "quakedef.h"
#include <assert.h>
#include <setjmp.h>
#undef main
client_state_t cl;
server_t sv;
THREAD_LOCAL qcvm_t *qcvm;
cvar_t cl_shownet;
sizebuf_t net_message;
qboolean msg_badread;
int msg_readcount;
entity_state_t nullentitystate;
int r_trace_line_cache_counter;
static jmp_buf parse_error;
static int linked, checks;
static char error_text[256];
static qmodel_t model;
static entity_t entities[64];
void Host_Error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_text, sizeof(error_text), fmt, args);
    va_end(args);
    longjmp(parse_error, 1);
}
void Host_EndGame(const char *fmt, ...) { (void)fmt; abort(); }
void Con_SafePrintf(const char *fmt, ...) { (void)fmt; }
void Con_DPrintf(const char *fmt, ...) { (void)fmt; }
void Con_Warning(const char *fmt, ...) { (void)fmt; }
const char *PR_GetString(int num) { (void)num; abort(); }
void PR_SwitchQCVM(qcvm_t *vm) { (void)vm; abort(); }
void *Hunk_Alloc(int size) {
    assert(size == sizeof(entities));
    memset(entities, 0, sizeof(entities));
    return entities;
}
void CL_LinkStaticEnt(struct cl_static_entities_s *ent) {
    assert(ent->ent->model == &model);
    linked++;
}
/* These wire types are unused by the baseline fixtures below. */
float MSG_ReadFloat(void) { abort(); }
int MSG_ReadLong(void) { abort(); }
'''
for declaration in ('int MSG_ReadChar (', 'int MSG_ReadByte (', 'int MSG_ReadShort (',
                    'float MSG_ReadCoord16 (', 'float MSG_ReadCoord24 (',
                    'float MSG_ReadCoord (', 'float MSG_ReadAngle (',
                    'float MSG_ReadAngle16 (', 'unsigned int MSG_ReadEntity('):
    source += function(common, declaration)
for declaration in ('static int MSG_ReadSize16 (', 'static unsigned int CLFTE_ReadDelta(',
                    'static void CLFTE_ParseBaseline(', 'static void CL_ParseBaseline (',
                    'static void CL_ParseStatic ('):
    source += function(parser, declaration)
source += r'''
static byte wire[128];
static int length;
static void putbyte(unsigned value) { wire[length++] = value; }
static void putshort(unsigned value) { putbyte(value); putbyte(value >> 8); }
static int fixture(int version, int protocol, unsigned index, int extras) {
    length = 0;
    if (version == 6) {
        /* FTE: reset, model, skin and frame, with 16-bit values. */
        unsigned bits = UF_EXTEND1 | UF_RESET | UF_MODEL | UF_SKIN | UF_FRAME | UF_16BIT;
        putbyte(bits); putbyte(bits >> 8);
        putshort(37); /* frame precedes model in FTE */
        putshort(index);
        putshort(3); /* skin */
        return length;
    }
    int large = version != 1 || protocol == PROTOCOL_VERSION_BJP3;
    if (version == 2)
        putbyte(B_LARGEMODEL | B_LARGEFRAME | (extras ? B_ALPHA | B_SCALE : 0));
    if (large) putshort(index); else putbyte(index);
    if (version == 2 || version == 7) putshort(37); else putbyte(37);
    putbyte(0); putbyte(3); /* colormap, skin */
    for (int i = 0; i < 3; i++) { putshort(8 * (i + 1)); putbyte(16 * i); }
    if (version == 2 && extras) { putbyte(127); putbyte(16); }
    return length;
}
static void check(int version, int protocol, unsigned index, int bytes, int complete) {
    free(cl.static_entities);
    memset(&cl, 0, sizeof(cl));
    cl.protocol = protocol;
    cl.protocol_pext2 = version == 6 ? PEXT2_REPLACEMENTDELTAS : 0;
    for (int i = 0; i < MAX_MODELS; i++) cl.model_precache[i] = &model;
    net_message.data = wire;
    net_message.cursize = bytes;
    msg_readcount = 0;
    msg_badread = false;
    linked = 0;
    error_text[0] = 0;
    int rejected = setjmp(parse_error);
    if (!rejected) CL_ParseStatic(version);
    int expected_reject = !complete || index >= MAX_MODELS;
    checks++;
    if (!!rejected != expected_reject) {
        fprintf(stderr, "version %d protocol %d model %u bytes %d: rejected=%d expected=%d\n",
                version, protocol, index, bytes, rejected, expected_reject);
        exit(1);
    }
    if (rejected) {
        assert(strstr(error_text, "CL_ParseStatic:"));
        assert(!linked && !cl.num_statics);
    } else {
        assert(!msg_badread && msg_readcount == bytes);
        assert(linked == 1 && cl.num_statics == 1);
        assert(entities[0].baseline.modelindex == index);
        assert(entities[0].netstate.modelindex == index);
        assert(entities[0].model == &model && entities[0].frame == 37);
        assert(entities[0].skinnum == 3);
    }
}
int main(void) {
    const unsigned indices[] = {0, 1, 255, 256, 4095, 4096, 32767, 32768, 65535};
    const int versions[] = {1, 1, 2, 2, 6, 7};
    const int protocols[] = {PROTOCOL_NETQUAKE, PROTOCOL_VERSION_BJP3,
                            PROTOCOL_FITZQUAKE, PROTOCOL_RMQ,
                            PROTOCOL_FITZQUAKE, PROTOCOL_VERSION_DP7};
    for (int pass = 0; pass < 2; pass++)
    for (int f = 0; f < 6; f++) {
        for (int n = 0; n < (int)(sizeof(indices) / sizeof(indices[0])); n++) {
            unsigned index = indices[n];
            if (!f && index > 255) continue;
            int size = fixture(versions[f], protocols[f], index, f == 3);
            if (!pass) { check(versions[f], protocols[f], index, size, 1); continue; }
            /* Truncate every payload field, including the model and fields
             * after a valid model. FTE's flag-header decoder is separate. */
            for (int cut = versions[f] == 6 ? 2 : 0; cut < size; cut++)
                check(versions[f], protocols[f], index, cut, 0);
        }
    }
    free(cl.static_entities);
    printf("PASS: %d static entity cases across NetQuake, BJP3, FitzQuake, RMQ, FTE and DP7\n", checks);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='qssm-static-models-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    cc = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run([*cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'Quake'), str(work / 'test.c'),
                    '-o', str(work / 'test'), *flags, '-lm'], check=True)
    subprocess.run([str(work / 'test')], check=True)
