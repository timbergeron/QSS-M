"""GL name pools: keep pooled names across vid_restart when the context survives."""


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


edit('Quake/gl_rmisc.c', [
("""static GLuint	gl_buffernames[256];
static int		gl_numbuffernames;

GLuint GL_GenBufferName (void)
{
	if (!gl_numbuffernames)
	{
		GL_GenBuffersFunc (countof(gl_buffernames), gl_buffernames);
		gl_numbuffernames = countof(gl_buffernames);
	}
	return gl_buffernames[--gl_numbuffernames];
}

void GL_ResetBufferNames (void)
{
	gl_numbuffernames = 0;
	if (gl_vbo_able)
	{
		GL_GenBuffersFunc (countof(gl_buffernames), gl_buffernames);
		gl_numbuffernames = countof(gl_buffernames);
	}
}""",
"""static GLuint	gl_buffernames[256];
static int		gl_numbuffernames;
static void		*gl_buffernames_context;	// the context the pooled names belong to

GLuint GL_GenBufferName (void)
{
	if (!gl_numbuffernames)
	{
		GL_GenBuffersFunc (countof(gl_buffernames), gl_buffernames);
		gl_numbuffernames = countof(gl_buffernames);
		gl_buffernames_context = SDL_GL_GetCurrentContext ();
	}
	return gl_buffernames[--gl_numbuffernames];
}

// vid_restart usually keeps the context, and its pooled names with it; only a
// new context invalidates them (they can't be deleted there: the same numbers
// may already name live objects).
void GL_ResetBufferNames (void)
{
	if (gl_numbuffernames && gl_buffernames_context == SDL_GL_GetCurrentContext ())
		return;
	gl_numbuffernames = 0;
	if (gl_vbo_able)
	{
		GL_GenBuffersFunc (countof(gl_buffernames), gl_buffernames);
		gl_numbuffernames = countof(gl_buffernames);
		gl_buffernames_context = SDL_GL_GetCurrentContext ();
	}
}"""),
])

edit('Quake/gl_texmgr.c', [
("""static GLuint	texmgr_names[1024];
static int		texmgr_numnames;

static GLuint TexMgr_GenName (void)
{
	if (!texmgr_numnames)
	{
		glGenTextures (countof(texmgr_names), texmgr_names);
		texmgr_numnames = countof(texmgr_names);
	}
	return texmgr_names[--texmgr_numnames];
}""",
"""static GLuint	texmgr_names[1024];
static int		texmgr_numnames;
static void		*texmgr_names_context;	// the context the pooled names belong to

static GLuint TexMgr_GenName (void)
{
	if (!texmgr_numnames)
	{
		glGenTextures (countof(texmgr_names), texmgr_names);
		texmgr_numnames = countof(texmgr_names);
		texmgr_names_context = SDL_GL_GetCurrentContext ();
	}
	return texmgr_names[--texmgr_numnames];
}"""),
("""	texmgr_numnames = 0;	// pooled names belonged to the old context""",
"""	if (texmgr_names_context != SDL_GL_GetCurrentContext ())
		texmgr_numnames = 0;	// pooled names belonged to the old context"""),
])
