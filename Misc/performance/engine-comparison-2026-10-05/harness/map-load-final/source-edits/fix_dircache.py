"""Directory cache: forget cached results whenever the engine creates a directory."""


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


edit('Quake/common.c', [(
"""void COM_EndLoadCache (void)
{
	int i;

	if (com_dircache_depth > 1)
	{
		com_dircache_depth--;
		return;
	}
	com_dircache_depth = 0;
	for (i = 0; i < COM_DIRCACHE_SLOTS; i++)
	{
		free (com_dircache[i].path);
		com_dircache[i].path = NULL;
	}
	com_dircache_count = 0;
}""",
"""// Forget every cached directory but stay inside any open load scope. Called
// when a directory is created, so a load that makes one sees it straight away.
void COM_InvalidateLoadCache (void)
{
	int i;

	for (i = 0; i < COM_DIRCACHE_SLOTS; i++)
	{
		free (com_dircache[i].path);
		com_dircache[i].path = NULL;
	}
	com_dircache_count = 0;
}

void COM_EndLoadCache (void)
{
	if (com_dircache_depth > 1)
	{
		com_dircache_depth--;
		return;
	}
	com_dircache_depth = 0;
	COM_InvalidateLoadCache ();
}""")])

edit('Quake/common.h', [(
"""void COM_AbortLoadCache (void);""",
"""void COM_AbortLoadCache (void);
void COM_InvalidateLoadCache (void);""")])
