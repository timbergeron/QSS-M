"""Print NetQuake demo protocol, map, and precache lists (models/sounds) from the first serverinfo."""
import struct, sys
def info(path):
    b = open(path, 'rb').read()
    i = b.index(b'\n') + 1
    blocks = 0; frames = 0; out = None
    while i + 16 <= len(b):
        n = struct.unpack_from('<i', b, i)[0]; i += 16
        msg = b[i:i + n]; i += n; blocks += 1
        if out is None and 11 in msg[:64]:
            j = msg.index(11) + 1
            proto = struct.unpack_from('<i', msg, j)[0]; j += 4
            flags = None
            if proto in (999,):
                flags = struct.unpack_from('<i', msg, j)[0]; j += 4
            maxc, gametype = msg[j], msg[j + 1]; j += 2
            def s():
                nonlocal j
                k = msg.index(0, j); v = msg[j:k].decode('latin-1'); j = k + 1; return v
            level = s(); models = []; sounds = []
            while True:
                v = s()
                if not v: break
                models.append(v)
            while True:
                v = s()
                if not v: break
                sounds.append(v)
            out = dict(protocol=proto, flags=flags, maxclients=maxc, gametype=gametype, level=level, models=models, sounds=sounds)
    out['blocks'] = blocks
    return out
for p in sys.argv[1:]:
    d = info(p)
    print(p, {k: v for k, v in d.items() if k not in ('models', 'sounds')})
    print('  models', len(d['models']), d['models'][:3], '...', [m for m in d['models'] if not m.startswith('*')][:60])
    print('  sounds', len(d['sounds']), d['sounds'][:80])
