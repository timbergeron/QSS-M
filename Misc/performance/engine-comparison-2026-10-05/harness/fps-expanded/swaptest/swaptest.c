/* Minimal GL present microbenchmark: SDL2 or SDL3 (loaded at runtime), 800x600 window,
 * swap interval 0, clear + optional N draw calls per frame, report mean swap/frame time.
 * usage: swaptest.exe <SDL2.dll|SDL3.dll> <2|3> [frames] [quads]
 */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

typedef int (*init_t)(unsigned);
typedef void *(*cw2_t)(const char *, int, int, int, int, unsigned);
typedef void *(*cw3_t)(const char *, int, int, unsigned long long);
typedef void *(*ctx_t)(void *);
typedef int (*si_t)(int);
typedef int (*swap_t)(void *);
typedef int (*attr_t)(int, int);
typedef int (*poll_t)(void *);

static double now (void) { LARGE_INTEGER c, f; QueryPerformanceCounter (&c); QueryPerformanceFrequency (&f); return (double)c.QuadPart / f.QuadPart; }

int main (int argc, char **argv)
{
	HMODULE sdl = LoadLibraryA (argv[1]);
	int ver = atoi (argv[2]), frames = argc > 3 ? atoi (argv[3]) : 6000, quads = argc > 4 ? atoi (argv[4]) : 0;
	void *win, *ctx; char ev[256];
	double t0, tswap = 0, start;
	int i, q;
	if (!sdl) { printf ("no dll\n"); return 1; }
	((init_t)GetProcAddress (sdl, "SDL_Init")) (ver == 2 ? 0x20u : 0x20u); /* VIDEO */
	if (ver == 2)
		win = ((cw2_t)GetProcAddress (sdl, "SDL_CreateWindow")) ("swaptest", 100, 100, 800, 600, 0x2u /* OPENGL */);
	else
		win = ((cw3_t)GetProcAddress (sdl, "SDL_CreateWindow")) ("swaptest", 800, 600, 0x2ull /* OPENGL */);
	ctx = ((ctx_t)GetProcAddress (sdl, "SDL_GL_CreateContext")) (win);
	((si_t)GetProcAddress (sdl, "SDL_GL_SetSwapInterval")) (0);
	start = now ();
	for (i = 0; i < frames; i++)
	{
		while (((poll_t)GetProcAddress (sdl, "SDL_PollEvent")) (ev)) {}
		glClearColor (0.2f, 0.1f, 0.1f, 1); glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		for (q = 0; q < quads; q++)
		{
			glBegin (GL_QUADS); glVertex2f (-0.5f, -0.5f); glVertex2f (0.5f, -0.5f); glVertex2f (0.5f, 0.5f); glVertex2f (-0.5f, 0.5f); glEnd ();
		}
		t0 = now ();
		((swap_t)GetProcAddress (sdl, "SDL_GL_SwapWindow")) (win);
		tswap += now () - t0;
	}
	printf ("SDL%d quads %d: %.1f us/frame total, %.1f us/frame in swap (%.0f fps)\n", ver, quads,
		(now () - start) * 1e6 / frames, tswap * 1e6 / frames, frames / (now () - start));
	return 0;
}
