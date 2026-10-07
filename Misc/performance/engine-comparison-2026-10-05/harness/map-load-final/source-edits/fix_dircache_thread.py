"""Directory cache: other threads may create directories; defer invalidation to the main thread."""


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


edit('Quake/common.c', [
("""static com_dircache_t	com_dircache[COM_DIRCACHE_SLOTS];
static int				com_dircache_depth, com_dircache_count;
""",
"""static com_dircache_t	com_dircache[COM_DIRCACHE_SLOTS];
static int				com_dircache_depth, com_dircache_count;
static SDL_AtomicInt	com_dircache_stale;	// a directory was created, possibly on another thread
"""),
("""// Forget every cached directory but stay inside any open load scope. Called
// when a directory is created, so a load that makes one sees it straight away.
void COM_InvalidateLoadCache (void)
{""",
"""// Called by Sys_mkdir from any thread: the main thread drops the cache at its
// next lookup, so a load that creates a directory sees it straight away.
void COM_MarkLoadCacheStale (void)
{
	SDL_SetAtomicInt (&com_dircache_stale, 1);
}

// Forget every cached directory but stay inside any open load scope.
void COM_InvalidateLoadCache (void)
{"""),
("""	if (!com_dircache_depth)
		return Sys_FileType (path);

	slash = strrchr (path, '/');""",
"""	if (!com_dircache_depth)
		return Sys_FileType (path);
	if (SDL_GetAtomicInt (&com_dircache_stale))
	{
		SDL_SetAtomicInt (&com_dircache_stale, 0);
		COM_InvalidateLoadCache ();
	}

	slash = strrchr (path, '/');"""),
])
edit('Quake/common.h', [("""void COM_InvalidateLoadCache (void);""", """void COM_InvalidateLoadCache (void);
void COM_MarkLoadCacheStale (void);""")])
for p in ('Quake/sys_sdl_win.c', 'Quake/sys_sdl_unix.c'):
    edit(p, [("""	COM_InvalidateLoadCache ();	// a load in progress must see the new directory""",
              """	COM_MarkLoadCacheStale ();	// a load in progress must see the new directory""")])
