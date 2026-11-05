#!/usr/bin/env python3
"""Check production hunk payload sizing at allocation/segment boundaries."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
zone = (ROOT / 'Quake/zone.c').read_text()


def function(declaration):
    start = zone.index(declaration)
    return zone[start:zone.index('\n}', start) + 2] + '\n'


source = r'''
#include "quakedef.h"
#include <assert.h>
#include <setjmp.h>
#undef main
static jmp_buf failure;
void Sys_Error(const char *fmt, ...) { (void)fmt; longjmp(failure, 1); }
'''
source += zone[zone.index('#define\tHUNK_SENTINEL'):zone.index('typedef enum', zone.index('#define\tHUNK_SENTINEL'))]
for declaration in ('static int Hunk_Size (', 'static int Hunk_SegForOfs (',
                    'qboolean Hunk_IsContiguous (', 'int Hunk_PayloadSize ('):
    source += function(declaration)
source += r'''
int main(void) {
    for (int segment = 0; segment < 2; segment++) {
        hunk_numsegments = segment + 1;
        for (int i = 0; i <= segment; i++) {
            hunk_segments[i] = malloc(sizeof(hunkseg_t) + 256);
            hunk_segments[i]->base = i * 256;
            hunk_segments[i]->used = hunk_segments[i]->size = 256;
            memset(SEG_MEM(hunk_segments[i]), 0x67, 256);
        }
        hunk_low_used = (segment + 1) * 256;
        int from = segment * 256 + 16;
        byte *payload = SEG_MEM(hunk_segments[segment]) + 16 + sizeof(hunk_t);
        /* Copy ends exactly at the end of the malloc'd segment. ASan catches
         * the original end-start length, which includes the first header. */
        int bytes = Hunk_PayloadSize(payload, from, hunk_low_used);
        assert(bytes == 256 - 16 - (int)sizeof(hunk_t));
        byte *copy = malloc(bytes); memcpy(copy, payload, bytes);
        assert(copy[bytes-1] == 0x67); free(copy);
        assert(Hunk_PayloadSize(payload, from, hunk_low_used - 16) == bytes - 16);
        if (segment) {
            assert(!Hunk_IsContiguous(0, hunk_low_used));
            if (!setjmp(failure)) { Hunk_PayloadSize(payload, 0, hunk_low_used); abort(); }
        }
        if (!setjmp(failure)) { Hunk_PayloadSize(payload - 64, from, hunk_low_used); abort(); }
        for (int i = 0; i <= segment; i++) free(hunk_segments[i]);
    }
    puts("PASS: payload copy ends at segment boundary, nonzero marks, later segments, and non-contiguous/outside-range rejection");
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-alias-payload-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    cc = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run([*cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'Quake'), str(work / 'test.c'), '-o', str(work / 'test'), *flags, '-lm'], check=True)
    subprocess.run([str(work / 'test')], check=True)
