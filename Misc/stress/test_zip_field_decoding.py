"""Check actual ZIP byte-assembly expressions, including every possible top byte."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'Quake/common.c').read_text()
text = text[text.index('static qboolean ZIP_ExtractInternalBody('):text.index('static qboolean ZIP_ExtractInternal(')]
fields = [('sig', 0), ('cd_offset', 16), ('size', 16),
          ('compressed_size', 16), ('local_header_offset', 38), ('external_attrs', 34)]
source = '#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n'
for name, _ in fields:
    match = re.search(r'unsigned int\s+' + name + r'\s*=\s*([^;]+);', text)
    assert match, name
    expression = re.sub(r'\b(?:buffer|header|cd_header)\b', 'input', match.group(1))
    source += f'static unsigned int read_{name}(const unsigned char *input) {{ return {expression}; }}\n'
source += r'''
int main(void) {
    unsigned char storage[43], *input=storage+1; /* unaligned input is intentional */
    uint32_t boundaries[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu,0xff000001u};
    for (unsigned int i=0;i<1262;i++) {
        uint32_t value=i<6 ? boundaries[i] : i<262 ? ((i-6)<<24)|0x123456u : (i-262)*0x9e3779b9u;
'''
for name, offset in fields:
    source += f'''
        memset(storage,0xa5,sizeof(storage));
        for (int byte=0;byte<4;byte++) input[{offset}+byte]=(unsigned char)(value>>(byte*8));
        if (read_{name}(input)!=value) {{ fprintf(stderr,"FAIL {name}: %u\\n",value); return 1; }}
'''
source += r'''
    }
    puts("PASS: 7572 ZIP field decodes, all 256 top bytes, 32-bit boundaries and unaligned input");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-zip-fields-') as temporary:
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
