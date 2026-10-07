"""GL name pools survive vid_restart when the context does, and reset when it doesn't.

Compile the real GL_GenBufferName/GL_ResetBufferNames and the texture pool's
reset condition from TexMgr_ReloadImages against a stub GL. A retained context
must keep every pooled name (none leak); a new context must regenerate.
Run with CC, or from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
rmisc = (ROOT / "Quake/gl_rmisc.c").read_text(encoding="utf-8")
start = rmisc.index("static GLuint\tgl_buffernames[256];")
end = rmisc.index("\n/*", rmisc.index("void GL_ResetBufferNames", start))
buffers = rmisc[start:end]
texmgr = (ROOT / "Quake/gl_texmgr.c").read_text(encoding="utf-8")
reset = re.search(r"\tif \(texmgr_names_context != SDL_GL_GetCurrentContext \(\)\)\s*\n\s*texmgr_numnames = 0;[^\n]*", texmgr)
assert reset, "texture pool reset condition not found"

source = r'''
#include <stdio.h>
#include <stdlib.h>
typedef unsigned int GLuint;
typedef int GLsizei;
typedef int qboolean;
#define countof(x) (sizeof(x)/sizeof((x)[0]))
#define CHECK(c,m) do { if (!(c)) { puts("FAIL: " m); exit(1); } } while (0)
static qboolean gl_vbo_able = 1;
static void *ctx = (void *)1;
static int gen_calls;
static GLuint next_name = 1;
static void *SDL_GL_GetCurrentContext (void) { return ctx; }
static void GL_GenBuffersFunc (GLsizei n, GLuint *p) { int i; gen_calls++; for (i = 0; i < n; i++) p[i] = next_name++; }
''' + buffers + r'''
static GLuint texmgr_names[1024];
static int texmgr_numnames;
static void *texmgr_names_context;
static void texture_reload (void)
{
''' + reset.group(0) + r'''
}
int main (void)
{
	GLuint a, b;
	GL_ResetBufferNames ();                       /* first context */
	CHECK (gen_calls == 1 && gl_numbuffernames == 256, "first context fills the pool");
	a = GL_GenBufferName ();
	GL_ResetBufferNames ();                       /* vid_restart, same context */
	CHECK (gen_calls == 1 && gl_numbuffernames == 255, "retained context keeps pooled names");
	b = GL_GenBufferName ();
	CHECK (a != b, "names stay unique across the restart");
	ctx = (void *)2;                              /* vid_restart that recreated the context */
	GL_ResetBufferNames ();
	CHECK (gen_calls == 2 && gl_numbuffernames == 256, "new context regenerates the pool");
	gl_vbo_able = 0; ctx = (void *)3;
	GL_ResetBufferNames ();
	CHECK (gl_numbuffernames == 0, "no VBO support leaves the pool empty");

	texmgr_numnames = 500; texmgr_names_context = (void *)3;
	texture_reload ();
	CHECK (texmgr_numnames == 500, "texture pool kept when the context survives");
	ctx = (void *)4;
	texture_reload ();
	CHECK (texmgr_numnames == 0, "texture pool dropped for a new context");
	puts ("PASS: buffer and texture name pools keep names across a context-preserving vid_restart and reset for a new context");
	return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-name-pools-") as tmp:
    work = Path(tmp)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        args = [*cc, "/nologo", "/W3", "/O2", "test.c", f"/Fe:{binary}"]
    else:
        args = [*cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-fsanitize=address,undefined", "test.c", "-o", str(binary)]
    subprocess.run(args, cwd=work, check=True)
    subprocess.run([str(binary)], cwd=work, check=True)
