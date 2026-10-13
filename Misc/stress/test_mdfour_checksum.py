"""Check real engine MD4/checksum results against OpenSSL under ASan/UBSan.

Requires a C compiler and OpenSSL with its legacy MD4 provider.
"""
from pathlib import Path
import os
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = r'''
#include <stdio.h>
#include <stdlib.h>
#include "mdfour.c"
int main(void) {
 unsigned char input[16384],digest[16];size_t length=fread(input,1,sizeof(input),stdin);
 if(ferror(stdin)||!feof(stdin)) return 2;
 mdfour(digest,input,length);
 for(int i=0;i<16;i++) printf("%02x",digest[i]);
 printf(" %08x\n",Com_BlockChecksum(input,length));return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-md4-') as temporary:
    work = Path(temporary)
    (work / 'mdfour.c').write_bytes((ROOT / 'Quake/mdfour.c').read_bytes())
    (work / 'test.c').write_text(source)
    compiler = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    command = ([*compiler, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}']
               if Path(compiler[0]).stem.lower() == 'cl' else
               [*compiler, '-std=c99', '-Wall', '-Wextra', '-O2', '-fsanitize=address,undefined', 'test.c', '-o', str(binary)])
    subprocess.run(command, cwd=work, check=True)
    inputs = [b'', b'a', b'abc', b'message digest', bytes(range(256)), b'\xff' * 256]
    inputs += [bytes((i * 197 + 129) % 256 for i in range(length))
               for length in [3, 4, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129, 255, 256, 4096, 4097]]
    environment = {**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
    for data in inputs:
        reference = subprocess.check_output(['openssl', 'dgst', '-md4', '-provider', 'legacy', '-binary'], input=data)
        digest, checksum = subprocess.check_output([str(binary)], input=data, env=environment).decode().split()
        words = struct.unpack('=4I', reference)
        expected = words[0] ^ words[1] ^ words[2] ^ words[3]
        assert digest == reference.hex() and int(checksum, 16) == expected, (len(data), digest, checksum, reference.hex())
print(f'PASS: {len(inputs)} MD4/checksum reference comparisons, high-bit bytes, padding/block boundaries and empty input')
