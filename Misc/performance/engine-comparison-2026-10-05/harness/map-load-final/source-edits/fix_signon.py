import sys
sys.path.insert(0, '.codex-build/map-load-finish-20261006')
p = 'Quake/host.c'
raw = open(p, 'rb').read(); crlf = b'\r\n' in raw
t = raw.decode('latin-1').replace('\r\n', '\n')
old = '''	/* A local client signing on would wait for the next tick at every round
	 * trip, so step the server each frame (with real elapsed time) until the
	 * client is in game. */
	signon_steps = Host_LocalSignon () && accumtime > 0;

	//Run the server+networking (client->server->client), at a different rate from everything else
	if (accumtime >= host_netinterval || signon_steps)
	{
		float realframetime = host_frametime;
		if (host_netinterval)
		{
			host_frametime = signon_steps ? accumtime : q_max(accumtime, host_netinterval);
			accumtime -= host_frametime;'''
new = '''	/* A local client signing on would wait for the next tick at every round
	 * trip. Between ticks, exchange its messages only (no physics, QuakeC
	 * frame or time advance) so the simulation keeps its normal tick budget. */
	signon_steps = Host_LocalSignon () && Host_SignonStepsAllowed ();

	//Run the server+networking (client->server->client), at a different rate from everything else
	if (accumtime < host_netinterval && signon_steps)
	{
		CL_SendCmd ();
		PR_SwitchQCVM(&sv.qcvm);
		SV_RunClientMessages ();
		SV_SendClientMessages ();
		PR_SwitchQCVM(NULL);
	}
	else if (accumtime >= host_netinterval)
	{
		float realframetime = host_frametime;
		if (host_netinterval)
		{
			host_frametime = q_max(accumtime, host_netinterval);
			accumtime -= host_frametime;'''
assert t.count(old) == 1
t = t.replace(old, new)
old2 = '''qboolean Host_LocalSignon (void)'''
new2 = '''/* Message-only signon steps send replies outside the normal tick, so allow
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

qboolean Host_LocalSignon (void)'''
assert t.count(old2) == 1
t = t.replace(old2, new2)
if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin-1'))
print('host ok')
