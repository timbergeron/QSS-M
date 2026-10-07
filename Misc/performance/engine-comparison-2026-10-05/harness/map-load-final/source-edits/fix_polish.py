"""Final-pass polish: comments, shm guard, texmgr layout."""


def edit(p, pairs):
    raw = open(p, 'rb').read(); crlf = b'\r\n' in raw
    t = raw.decode('latin-1').replace('\r\n', '\n')
    for old, new in pairs:
        assert t.count(old) == 1, (p, old[:70])
        t = t.replace(old, new)
    if crlf:
        t = t.replace('\n', '\r\n')
    open(p, 'wb').write(t.encode('latin-1'))
    print(p, 'ok')


edit('Quake/snd_dma.c', [(
"""static unsigned int	sfx_generation;	// bumped per map; older entries may be recycled""",
"""static unsigned int	sfx_generation;	// bumped per map; marks entries kept from earlier maps""")])

edit('Quake/snd_mem.c', [(
"""	if (sc && sc->speed == shm->speed)
		return sc;""",
"""	if (sc && (!shm || sc->speed == shm->speed))
		return sc;""")])

edit('Quake/gl_texmgr.c', [
("""================
TexMgr_NewTexture
================
*/
static gltexture_t *TexMgr_AllocTexture (void)""",
"""================
TexMgr_AllocTexture -- a texture record without a GL name
================
*/
static gltexture_t *TexMgr_AllocTexture (void)"""),
("""	numgltextures++;
	return glt;
}
/* glGenTextures returns values, so a threaded driver must stop and catch up
 * with its worker. Level loads take names from a pool generated in bulk. */
static GLuint	texmgr_names[1024];""",
"""	numgltextures++;
	return glt;
}

/* glGenTextures returns values, so a threaded driver must stop and catch up
 * with its worker. Level loads take names from a pool generated in bulk. */
static GLuint	texmgr_names[1024];"""),
])
