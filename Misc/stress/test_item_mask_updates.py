"""Check the actual item-acquisition loop, including item bit 31, with UBSan."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'Quake/cl_parse.c').read_text()
start = text.index('\t\t\tif (cl.items != cl.stats[STAT_ITEMS])')
loop = text[start:text.index('\n\t\t\t}', start) + len('\n\t\t\t}')]
sigil_line = next(line.strip() for line in (ROOT / 'Quake/sbar.c').read_text().splitlines()
                  if 'if (cl.items & (' in line and '28+i' in line)
sigil_condition = sigil_line[4:-1]
source = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define STAT_ITEMS 0
int main(void) {
 struct {int32_t items,stats[1];double item_gettime[32],time;} cl;
 uint32_t before,after;int i;
 while(scanf("%u %u",&before,&after)==2) {
  memset(&cl,0,sizeof(cl));cl.time=7;cl.items=before;cl.stats[STAT_ITEMS]=after;
''' + loop + r'''
  uint32_t acquired=0;
  for(i=0;i<32;i++) if(cl.item_gettime[i]==7) acquired|=UINT32_C(1)<<i;
  uint32_t sigils=0;
  for(i=0;i<4;i++) if(''' + sigil_condition + r''') sigils|=UINT32_C(1)<<(28+i);
  printf("%u %u %u\n",acquired,(uint32_t)cl.items,sigils);
 }
 return 0;
}
'''
pairs = [(0, 0), (0, 0xffffffff), (0xffffffff, 0), (0x80000000, 0x80000000)]
pairs += [(0, 1 << bit) for bit in range(32)]
pairs += [(1 << bit, 0xffffffff) for bit in range(32)]
pairs += [((i * 0x9e3779b9) & 0xffffffff, (i * 0x85ebca77 + 17) & 0xffffffff)
          for i in range(1000)]
data = ''.join(f'{before} {after}\n' for before, after in pairs).encode()
expected = [(after & ~before, after, after & 0xf0000000) for before, after in pairs]
with tempfile.TemporaryDirectory(prefix='qssm-item-masks-') as temporary:
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
    actual = [tuple(int(value) for value in line.split()) for line in output.splitlines()]
    assert actual == expected, 'acquired-item timestamps or inventory changed'
print(f'PASS: {len(pairs)} item mask transitions, all 32 bits, additions, removals and HUD sigils')
