"""Bound the uncapped/held local-signon window to 2 seconds; tidy comments."""


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


edit('Quake/host.c', [(
"""/* A local client signing on only waits on its own server, and the screen is
 * frozen for loading, so run those few frames without the FPS cap. */
/* Message-only signon steps send replies outside the normal tick, so allow
 * them only while no other client is in game to receive extra updates. */
static qboolean Host_SignonStepsAllowed (void)
{
	int i;
	client_t *c;

	for (i = 0, c = svs.clients; i < svs.maxclients; i++, c++)
		if (c->active && c->spawned)
			return false;
	return true;
}

qboolean Host_LocalSignon (void)
{
	return sv.active && cls.state == ca_connected && cls.signon < SIGNONS && !cls.demoplayback;
}""",
"""/* Message-only signon steps send replies outside the normal tick, so allow
 * them only while no other client is in game to receive extra updates. */
static qboolean Host_SignonStepsAllowed (void)
{
	int i;
	client_t *c;

	for (i = 0, c = svs.clients; i < svs.maxclients; i++, c++)
		if (c->active && c->spawned)
			return false;
	return true;
}

/* A local client signing on only waits on its own server, so its first two
 * seconds run without the FPS cap or sleeps and with the screen held (see
 * SCR_UpdateScreen). A signon that takes longer, e.g. waiting on a download,
 * goes back to normal frames instead of spinning. */
#define HOST_LOCAL_SIGNON_WINDOW	2.0
static double host_local_signon_start;

qboolean Host_LocalSignon (void)
{
	if (!sv.active || cls.state != ca_connected || cls.signon >= SIGNONS || cls.demoplayback)
	{
		host_local_signon_start = 0;
		return false;
	}
	if (!host_local_signon_start)
		host_local_signon_start = realtime;
	return realtime - host_local_signon_start < HOST_LOCAL_SIGNON_WINDOW;
}""")])

edit('Quake/gl_screen.c', [
("""static double	scr_signon_hold_start;

void SCR_UpdateScreen (void)""",
"""void SCR_UpdateScreen (void)"""),
("""	/* A local server's client signs on within a few frames, and drawing them
	 * only waits on the GL driver; hold the last frame as the loading plaque
	 * does, but never for long. */
	if (Host_LocalSignon ())
	{
		if (!scr_signon_hold_start)
			scr_signon_hold_start = realtime;
		if (realtime - scr_signon_hold_start < 2.0)
			return;
	}
	else
		scr_signon_hold_start = 0;
""",
"""	/* A local server's client signs on within a few frames, and drawing them
	 * only waits on the GL driver; hold the last frame as the loading plaque
	 * does. Host_LocalSignon stops holding after two seconds. */
	if (Host_LocalSignon ())
		return;
"""),
])
