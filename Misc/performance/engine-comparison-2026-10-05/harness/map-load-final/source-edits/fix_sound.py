"""Sound reuse fixes: no slot recycling, pak-backed reuse only, sample-rate check."""
import sys
sys.path.insert(0, '.codex-build/map-load-finish-20261006')


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


edit('Quake/q_sound.h', [(
"""	char	name[MAX_QPATH];
	cache_user_t	cache;
	unsigned int	generation;	// map that last looked this sound up
} sfx_t;""",
"""	char	name[MAX_QPATH];
	cache_user_t	cache;
	unsigned int	generation;	// map that last looked this sound up
	const void		*source;	// PACK search path the samples came from, or NULL
} sfx_t;""")])

edit('Quake/snd_dma.c', [
("""#define	MAX_SFX		MAX_SOUNDS""",
"""// Sounds are kept across maps (see S_ClearPrecache), so leave room for one
// full map's precaches on top of the ones kept from earlier maps.
#define	MAX_SFX		(MAX_SOUNDS * 2 + 256)"""),
("""// see if already loaded
	for (i = 0; i < num_sfx; i++)
	{
		if (!strcmp(known_sfx[i].name, name))
		{
			known_sfx[i].generation = sfx_generation;
			return &known_sfx[i];
		}
	}

	if (num_sfx == MAX_SFX)
	{
		// sounds kept from earlier maps give way to this map's
		for (i = 0; i < num_sfx; i++)
			if (known_sfx[i].generation != sfx_generation)
				break;
		if (i == num_sfx)
			Sys_Error ("S_FindName: out of sfx_t");
		sfx = &known_sfx[i];
		if (sfx->cache.data)
			Cache_Free (&sfx->cache);
	}
	else
		sfx = &known_sfx[num_sfx++];

	memset (sfx, 0, sizeof(*sfx));
	q_strlcpy (sfx->name, name, sizeof(sfx->name));
	sfx->generation = sfx_generation;

	return sfx;""",
"""// see if already loaded
	for (i = 0; i < num_sfx; i++)
	{
		sfx = &known_sfx[i];
		if (strcmp(sfx->name, name))
			continue;
		// samples kept from an earlier map are only trusted while the same
		// PACK still provides the file; anything else reloads from disk
		if (sfx->generation != sfx_generation && sfx->cache.data &&
			(!sfx->source || COM_FileSearchPath (va ("sound/%s", name)) != sfx->source))
			Cache_Free (&sfx->cache);
		sfx->generation = sfx_generation;
		return sfx;
	}

	if (num_sfx == MAX_SFX)
		Sys_Error ("S_FindName: out of sfx_t");

	sfx = &known_sfx[i];
	q_strlcpy (sfx->name, name, sizeof(sfx->name));
	sfx->generation = sfx_generation;

	num_sfx++;

	return sfx;"""),
("""Keep known sounds and their cached samples across maps, like alias models.
Cache_Flush (game change, memory pressure) drops the samples and S_LoadSound
reloads them; S_FindName recycles entries no later map has asked for.
*/
void S_ClearPrecache (void)
{
	if (!snd_initialized || !known_sfx)
		return;
	S_SoundPreview_Release();

	S_StopAllSounds (true, false);

	sfx_generation++;
	memset (ambient_sfx, 0, sizeof(ambient_sfx));
}""",
"""Keep known sounds and their cached samples across maps, like alias models.
Cache_Flush (game change, memory pressure) drops the samples and S_LoadSound
reloads them. Entries are never reused for another name while kept, so long-
lived sfx_t pointers stay valid; once more than a map's worth accumulates,
start over as the original code did on every map.
*/
void S_ClearPrecache (void)
{
	int		i;
	sfx_t	*sfx;

	if (!snd_initialized || !known_sfx)
		return;
	S_SoundPreview_Release();

	S_StopAllSounds (true, false);

	sfx_generation++;
	memset (ambient_sfx, 0, sizeof(ambient_sfx));

	if (num_sfx <= MAX_SOUNDS)
		return;
	for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++)
	{
		if (sfx->cache.data)
			Cache_Free (&sfx->cache);
	}
	memset (known_sfx, 0, MAX_SFX * sizeof(*known_sfx));
	num_sfx = 0;
}"""),
])

edit('Quake/snd_mem.c', [
("""// see if still in memory
	sc = (sfxcache_t *) Cache_Check (&s->cache);
	if (sc)
		return sc;""",
"""// see if still in memory, resampled for the current output rate
	sc = (sfxcache_t *) Cache_Check (&s->cache);
	if (sc && sc->speed == shm->speed)
		return sc;
	if (sc)
		Cache_Free (&s->cache);
	s->source = NULL;"""),
("""	sound_filelen = 0;
	data = COM_LoadMallocFile(namebuffer, NULL);
	if (data)
		sound_filelen = com_filesize;""",
"""	sound_filelen = 0;
	data = COM_LoadMallocFile(namebuffer, NULL);
	if (data)
	{
		sound_filelen = com_filesize;
		if (file_from_pak)
			s->source = COM_FileSearchPath (namebuffer);	// lets S_FindName reuse it on later maps
	}"""),
])
