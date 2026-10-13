"""Check real RGB9_E5/RGB10_A2 lightmap packing under ASan/UBSan."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'Quake/r_brush.c').read_text()
start = text.index('\tcase GL_RGB9_E5:', text.index('void R_BuildLightMapForState ('))
packing = text[start:text.index('\tcase GL_RGBA:', start)]
source = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define q_max(a,b) ((a)>(b)?(a):(b))
#define CLAMP(a,x,b) ((x)<(a)?(a):(x)>(b)?(b):(x))
#define GL_RGB9_E5 1
#define GL_RGB10_A2 2
typedef unsigned char byte;
static uint32_t pack(unsigned *blocklights,int overbright,int format) {
 uint32_t word=0;byte *dest=(byte*)&word;int smax=1,tmax=1,stride=4,i,j;
 unsigned r,g,b,*bl;
 switch(format) {
''' + packing + r'''
 default: abort();
 }
 return word;
}
int main(void) {
 unsigned samples[][3]={{0,0,0},{1,2,3},{32768,16384,8192},{65535,12345,1000},
  {65536,32768,16384},{131072,98304,1},{262144,196608,65536},{33554432,8388608,65536}};
 int high_exponents=0;
 for(int overbright=0;overbright<2;overbright++) for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);i++) {
  uint32_t word=pack(samples[i],overbright,GL_RGB9_E5);
  int exponent=word>>27;double step=ldexp(1.0,exponent-24);
  if(exponent>=16) high_exponents++;
  for(int channel=0;channel<3;channel++) {
   double decoded=((word>>(channel*9))&511)*step;
   double target=samples[i][channel]/(overbright?65536.0:32768.0);
   if(!overbright&&target>1) target=1;
   assert(fabs(decoded-target)<=step+1e-12);
  }
  word=pack(samples[i],overbright,GL_RGB10_A2);assert((word&3)==3);
  for(int channel=0;channel<3;channel++) {
   unsigned decoded=(word>>(22-channel*10))&1023;
   unsigned target=samples[i][channel]>>(overbright?8:7);
   unsigned limit=overbright?1023:255;if(target>limit) target=limit;
   assert(decoded==target);
  }
 }
 assert(high_exponents>0);
 puts("PASS: actual lightmap packing, high exponent/sign bit, RGB roundtrip, rounding, black and saturation");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-lightmap-pack-') as temporary:
    work = Path(temporary)
    (work / 'test.c').write_text(source)
    compiler = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    command = ([*compiler, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}']
               if Path(compiler[0]).stem.lower() == 'cl' else
               [*compiler, '-std=c99', '-Wall', '-Wextra', '-O2', '-fsanitize=address,undefined', 'test.c', '-lm', '-o', str(binary)])
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], cwd=work, check=True,
                   env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
