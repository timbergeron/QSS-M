"""sub.py FILE  < pairs.py  -- apply exact replacements, preserving CRLF/LF."""
import sys
p = sys.argv[1]
raw = open(p, 'rb').read()
crlf = raw.count(b'\r\n') > raw.count(b'\n') // 2
t = raw.decode('latin-1').replace('\r\n', '\n')
ns = {}
exec(sys.stdin.read(), ns)
for old, new in ns['PAIRS']:
    n = t.count(old)
    if n != 1:
        sys.exit('%s: expected 1 match, got %d for:\n%s' % (p, n, old))
    t = t.replace(old, new)
if crlf:
    t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin-1'))
print(p, 'ok', 'CRLF' if crlf else 'LF')
