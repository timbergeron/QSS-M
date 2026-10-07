import struct, sys
def names(p):
    b = open(p, 'rb').read(12); _, ofs, ln = struct.unpack('<4sii', b)
    f = open(p, 'rb'); f.seek(ofs); d = f.read(ln)
    return {d[i:i+56].split(b'\0')[0].decode('latin-1').lower() for i in range(0, ln, 64)}
