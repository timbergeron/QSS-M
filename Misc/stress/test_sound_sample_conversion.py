"""Exercise production sample conversion over every 8-bit stereo pair with UBSan."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'Quake/snd_mem.c').read_text()


def function(name):
    match = re.search(r'^static int ' + name + r'\s*\([^;]*?\)\s*\{', text, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end] + '\n'


source = r'''
#include <stdint.h>
#include <stdio.h>
typedef unsigned char byte;
static short LittleShort(short value) {
    const uint16_t endian=1;
    if (*(const byte *)&endian) return value;
    uint16_t u=(uint16_t)value;
    return (short)((u>>8)|(u<<8));
}
''' + function('ResampleSfx_GetMonoSample') + function('ResampleSfx_GetStereoSample') + r'''
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d\n",__LINE__); return 1; } } while(0)
int main(void) {
    byte samples[4]={0};
    for (int left=0;left<256;left++) {
        samples[0]=(byte)left;
        CHECK(ResampleSfx_GetMonoSample(samples,1,0)==(left-128)*256);
        for (int right=0;right<256;right++) {
            samples[1]=(byte)right;
            samples[2]=(byte)left; samples[3]=(byte)right;
            int expected=(left+right-256)*128;
            CHECK(ResampleSfx_GetStereoSample(samples,1,0)==expected);
            CHECK(ResampleSfx_GetStereoSample(samples,1,1)==expected);
        }
    }
    const short values[]={-32768,-32767,-1,0,1,32766,32767};
    for (size_t i=0;i<sizeof(values)/sizeof(values[0]);i++) {
        short mono=LittleShort(values[i]);
        CHECK(ResampleSfx_GetMonoSample((const byte *)&mono,2,0)==values[i]);
        for (size_t j=0;j<sizeof(values)/sizeof(values[0]);j++) {
            short stereo[4]={LittleShort(123),LittleShort(-456),LittleShort(values[i]),LittleShort(values[j])};
            int expected=((int)values[i]+values[j])/2;
            CHECK(ResampleSfx_GetStereoSample((const byte *)stereo,2,1)==expected);
        }
    }
    puts("PASS: all 65536 8-bit stereo pairs at two frame offsets, 256 mono samples and 16-bit boundaries");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-sample-conversion-') as temporary:
    work = Path(temporary)
    (work / 'test.c').write_text(source)
    cc = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    command = ([*cc, '/nologo', '/std:c11', '/W3', '/O2', 'test.c', f'/Fe:{binary}']
               if Path(cc[0]).stem.lower() == 'cl' else
               [*cc, '-std=c99', '-Wall', '-Wextra', '-O1', '-g',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all', 'test.c', '-o', str(binary)])
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
