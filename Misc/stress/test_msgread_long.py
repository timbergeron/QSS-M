"""Verify actual MSG_ReadLong signed values and truncation guards with sanitizers."""
from pathlib import Path
import os
import re
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'Quake/common.c').read_text()
match = re.search(r'int MSG_ReadLong \(void\)\s*\{', text)
assert match
end, depth = match.end(), 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
function = text[match.start():end]
source = r'''
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char byte;
#define true 1
static struct {byte *data;int cursize;} net_message;
static int msg_readcount,msg_badread;
''' + function + r'''
int main(void) {
 byte bytes[4];size_t length;
 while((length=fread(bytes,1,4,stdin))==4) {
  net_message.data=bytes;net_message.cursize=4;msg_readcount=msg_badread=0;
  printf("%d\n",MSG_ReadLong());
  if(msg_readcount!=4||msg_badread) return 2;
 }
 if(length||ferror(stdin)) return 3;
 for(int size=0;size<4;size++) {
  net_message.data=bytes;net_message.cursize=size;msg_readcount=msg_badread=0;
  if(MSG_ReadLong()!=-1||!msg_badread||msg_readcount) return 4;
 }
 return 0;
}
'''
values = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0xff000001]
values += [(byte << 24) | 0x123456 for byte in range(256)]
values += [(i * 0x9e3779b9) & 0xffffffff for i in range(1000)]
data = b''.join(struct.pack('<I', value) for value in values)
expected = [struct.unpack('<i', struct.pack('<I', value))[0] for value in values]
with tempfile.TemporaryDirectory(prefix='qssm-msg-long-') as temporary:
    work = Path(temporary)
    (work / 'test.c').write_text(source)
    compiler = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    command = ([*compiler, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}']
               if Path(compiler[0]).stem.lower() == 'cl' else
               [*compiler, '-std=c99', '-Wall', '-Wextra', '-O2', '-fsanitize=address,undefined', 'test.c', '-o', str(binary)])
    subprocess.run(command, cwd=work, check=True)
    result = subprocess.check_output([str(binary)], input=data,
                                    env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
    assert [int(value) for value in result.split()] == expected, 'signed packet values changed'
print(f'PASS: {len(values)} signed packet values, all 256 high bytes, boundaries and truncated inputs')
