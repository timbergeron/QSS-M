"""Verify actual snapshot solid-size encoding with an independent reference."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'Quake/sv_main.c').read_text()
start = text.index('\t\tstate->solidsize = CLAMP(0, (int)-ent->v.mins[0], 255);')
packing = text[start:text.index('\n\t}', start)]
text = (ROOT / 'Quake/cl_parse.c').read_text()
start = text.index('static int MSG_ReadSize16 (')
end = text.index('\n}', start) + 2
decoder = text[start:end]
source = r'''
#include <stdint.h>
#include <stdio.h>
#define CLAMP(a,x,b) ((x)<(a)?(a):(x)>(b)?(b):(x))
#define ES_SOLID_NOT 0
#define ES_SOLID_BSP 31
typedef struct {int unused;} sizebuf_t;
static unsigned short encoded;
static int MSG_ReadShort(void) {return encoded;}
''' + decoder + r'''
int main(void) {
 struct {uint32_t solidsize;} storage,*state=&storage;
 struct {struct {float mins[3],maxs[3];} v;} entity,*ent=&entity;
 while(scanf("%f %f %f",&ent->v.mins[0],&ent->v.mins[2],&ent->v.maxs[2])==3) {
''' + packing + r'''
  printf("%u\n",state->solidsize);
 }
 for(unsigned word=0;word<=65535;word++) {
  encoded=word;
  printf("%u\n",(uint32_t)MSG_ReadSize16(NULL));
 }
 return 0;
}
'''

def clamp(value, limit):
    return min(max(int(value), 0), limit)

samples = [(0, 0, 0), (-16, -24, 32), (-255, -255, 32767),
           (-999, -999, 99999), (16, 24, -99999), (0, 0, -32768)]
samples += [(-i % 600 - 300, -(i * 7 % 600) + 300, i * 41 - 40000)
            for i in range(2000)]
expected = []
for x, down, up in samples:
    word = int.from_bytes(bytes([clamp(-x, 255), clamp(-down, 255)])
                          + clamp(up + 32768, 65535).to_bytes(2, 'little'), 'little')
    expected.append(0 if word == 0x80000000 else word)
for word in range(65536):
    if word == 31:
        expected.append(31)
    else:
        x = (word % 32) * 8
        down = (word // 32 % 32) * 8
        up = (word // 1024) * 8 - 32 + 32768
        expected.append(int.from_bytes(bytes([x, down]) + up.to_bytes(2, 'little'), 'little'))
data = ''.join(f'{x} {down} {up}\n' for x, down, up in samples).encode()
with tempfile.TemporaryDirectory(prefix='qssm-solid-packing-') as temporary:
    work = Path(temporary)
    (work / 'test.c').write_text(source)
    compiler = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    command = ([*compiler, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}']
               if Path(compiler[0]).stem.lower() == 'cl' else
               [*compiler, '-std=c99', '-Wall', '-Wextra', '-O2', '-fsanitize=address,undefined',
                'test.c', '-o', str(binary)])
    subprocess.run(command, cwd=work, check=True)
    output = subprocess.check_output([str(binary)], input=data,
                                    env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
    assert [int(word) for word in output.split()] == expected, 'snapshot solid sizes changed'
print(f'PASS: {len(samples)} snapshot solid sizes and all 65536 compact decodings, signed heights, clamps and sentinels')
