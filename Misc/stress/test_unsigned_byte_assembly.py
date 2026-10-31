"""Check production byte assembly and truncated extended headers with UBSan.

Use --baseline REV to verify that a pre-fix revision reproduces the UB.
"""
from pathlib import Path
import argparse
import os
import re
import shlex
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', metavar='REV', help='expect UBSan failures using this git revision')
baseline = parser.parse_args().baseline


def read(name):
    if baseline:
        return subprocess.check_output(['git', 'show', f'{baseline}:Quake/{name}'],
                                       cwd=ROOT, text=True)
    return (ROOT / 'Quake' / name).read_text()


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


common, light, parse, protocol = map(read, ['common.c', 'gl_rlight.c', 'cl_parse.c', 'protocol.h'])
definitions = '\n'.join(re.findall(r'^#define\s+(?:UF_EXTEND[123]|U_MOREBITS|U_EXTEND[12]|'
                                   r'SU_EXTEND[123]|PROTOCOL_FITZQUAKE|PROTOCOL_RMQ)\b[^\n]*',
                                   protocol, re.M))
definitions += '\n' + '\n'.join(re.findall(r'^#define\s+E5_\w+[^\n]*', parse, re.M))
fte = section(function(parse, 'static unsigned int CLFTE_ReadDelta('),
              '\tbits = MSG_ReadByte();', '\n\tif (cl_shownet')
dp = section(function(parse, 'static void CLDP_ReadDelta('),
             '\tunsigned int bits = MSG_ReadByte();', '\n\tif (bits & (E5_ALLUNSUPPORTED')
fitz = section(function(parse, 'static void CL_ParseUpdate ('),
               '\tif (bits & U_MOREBITS)', '\n\tif (bits & U_LONGENTITY)')
client = section(function(parse, 'static void CL_ParseClientdata ('),
                 '\tbits = (unsigned short)MSG_ReadShort', '\n\tif (cl.protocol')
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned char byte;
#define true 1
static struct {byte *data; int cursize;} net_message;
static int msg_readcount, msg_badread;
struct rctx_s {byte *data; int ofs, size;};
static struct {int protocol;} cl;
''' + definitions + '\n'
source += '\n'.join(function(common, signature) for signature in
                    ['int MSG_ReadByte (void)', 'int MSG_ReadShort (void)', 'int LongSwap (int l)'])
source += '\n' + function(light, 'static byte ReadByte(')
source += '\n' + function(light, 'static int ReadInt(')
source += '\nstatic unsigned int fte_header(void) {unsigned int bits;\n' + fte + 'return bits;}\n'
source += '\nstatic unsigned int dp_header(void) {\n' + dp + r'''
 assert((unsigned int)E5_EXTEND4 == UINT32_C(0x80000000));
 return bits;
}
'''
source += '\nstatic unsigned int fitz_header(void) {unsigned int bits = MSG_ReadByte(); int i;\n'
source += fitz + 'return bits;}\n'
source += '\nstatic unsigned int client_header(void) {unsigned int bits;\n' + client + r'''
 assert((unsigned int)SU_EXTEND3 == UINT32_C(0x80000000));
 return bits;
}
int main(void) {
 unsigned int value, result; int kind, length;
 while (scanf("%d %u %d", &kind, &value, &length) == 3) {
  byte data[4];
  for (int i = 0; i < 4; i++) data[i] = value >> (8 * i);
  net_message.data = data; net_message.cursize = length;
  msg_readcount = msg_badread = 0;
  cl.protocol = kind == 6 ? PROTOCOL_RMQ : PROTOCOL_FITZQUAKE;
  switch (kind) {
   case 0: result = LongSwap((int)value); break;
   case 1: {
    struct rctx_s ctx = {data, 0, length};
    result = ReadInt(&ctx); msg_readcount = ctx.ofs;
    break;
   }
   case 2: result = fte_header(); break;
   case 3: result = dp_header(); break;
   case 4: result = fitz_header(); break;
   case 5: result = client_header(); break;
   case 6: result = fitz_header(); break;
   default: return 2;
  }
  printf("%u %d %d\n", result, msg_readcount, msg_badread);
 }
 return ferror(stdin) ? 3 : 0;
}
'''

values = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0xff000001]
values += [byte << shift for shift in (0, 8, 16, 24) for byte in range(256)]
values += [(i * 0x9e3779b9) & 0xffffffff for i in range(1000)]
names = ['LongSwap', 'light-grid ReadInt', 'FTE header', 'DP header', 'Fitz header',
         'clientdata header', 'RMQ header']
with tempfile.TemporaryDirectory(prefix='qssm-unsigned-bytes-') as temporary:
    work = Path(temporary)
    (work / 'test.c').write_text(source)
    compiler = shlex.split(os.environ.get('CC', 'cl' if os.name == 'nt' else 'cc'))
    binary = work / ('test.exe' if os.name == 'nt' else 'test')
    msvc = Path(compiler[0]).stem.lower() == 'cl'
    if baseline and msvc:
        raise SystemExit('--baseline requires a compiler supporting UBSan')
    command = ([*compiler, '/nologo', '/W3', '/O2', 'test.c', f'/Fe:{binary}'] if msvc else
               [*compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', 'test.c', '-o', str(binary)])
    subprocess.run(command, cwd=work, check=True)
    for kind, name in enumerate(names):
        cases, expected = [], []
        for value in values:
            # Force every extension byte to be present; vary all remaining bits.
            value |= {2: 0x00808080, 3: 0x00808080, 4: 0x00808001,
                      5: 0x00808000, 6: 0x00808001}.get(kind, 0)
            cases.append((value, 4))
            result = int.from_bytes(struct.pack('<I', value), 'big') if kind == 0 else value
            expected.append((result, 0 if kind == 0 else 4, 0))
        # Trailing payload must remain unread when an extension flag is clear.
        short_headers = {
            2: [(0xa5a51234, 1), (0xa5a51280, 2), (0xa5128080, 3)],
            3: [(0xa5a51234, 1), (0xa5a51280, 2), (0xa5128080, 3)],
            4: [(0xa5a51234, 1), (0xa5a51201, 2), (0xa5128001, 3)],
            5: [(0xa5a51234, 2), (0xa5128034, 3)],
            6: [(0xa5a51234, 1), (0xa5a51201, 2), (0xa5128001, 3)],
        }
        for value, consumed in short_headers.get(kind, []):
            cases.append((value, 4))
            expected.append((value & ((1 << (8 * consumed)) - 1), consumed, 0))
        if kind:
            for length in range(4):
                value = 0xa5c3b781  # Sets all extension flags, with distinct bytes.
                cases.append((value, length))
                available = 0 if kind == 5 and length < 2 else length
                low_mask = (1 << (8 * available)) - 1
                if kind == 1:  # The light-grid reader pads missing bytes with zero.
                    expected.append((value & low_mask, 4, 0))
                elif kind == 5 and length == 1:
                    # A failed short leaves the lone byte for the next extension read.
                    expected.append((0xff00ffff | ((value & 0xff) << 16), 1, 1))
                else:  # Packet readers retain their -1 sentinel and bad-read flag.
                    expected.append(((value & low_mask) | (0xffffffff ^ low_mask), available, 1))
        data = ''.join(f'{kind} {value} {length}\n' for value, length in cases).encode()
        result = subprocess.run([str(binary)], input=data, capture_output=True,
                                env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
        if baseline:
            assert result.returncode and b'runtime error:' in result.stderr, (name, result.stderr)
            print(f'REPRODUCED: {name}: {result.stderr.decode().splitlines()[0]}')
        else:
            assert result.returncode == 0, (name, result.stderr.decode())
            actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
            assert actual == expected, f'{name}: decoded values, cursor or bad-read behavior changed'
            print(f'PASS: {name}: {len(cases)} byte-pattern, extension and truncation cases')
