/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2011 O. Sezer <sezero@users.sourceforge.net>
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// snd_dma.c -- main control for any streaming sound output device

/* FIXME -- spike
** with SDL, the SDL api provides a callback that is called whenever SDL thinks more audio is needed
** if we were to move our mixing into the callback instead, we would obsolete _snd_mixahead and reduce audio latency as a result (instead of having to pre-mix audio just in case).
** this callback can typically also be assumed to be on another thread, so mixing audio there would result in a drop in cpu usage on the main thread, increasing framerates.
** typically quake's audio mixer isn't that expensive, but when you have maps with 1000 static sounds with 8-channel surround sound, things can start to get pricy.
**
** S_Update_ would become a stub, and we'd need to call SDL_LockAudio to block the callback from happening any time we change an audio channel.
** snd_mix.c would also need to be threadsafe with regard to the rest of the code
**
** alternatively, sdl2 provides a different audio api that more closely matches what we currently do, where we would directly submit audio (snd_mixahead would again be obsolete).
*/

#include "quakedef.h"
#include "snd_codec.h"
#include "bgmusic.h"

static void S_Play (void);
static void S_PlayVol (void);
static void S_SoundList (void);
static void S_Update_ (void);
static void GetSoundtime (void);
static void S_CompleteStartup (void);
void S_StopAllSounds (qboolean clear, qboolean keep_statics);
static void S_StopAllSoundsC (void);
void Sound_Toggle_Mute_f (void); // woods

void S_SetUnderwaterIntensity(float intensity); // woods #waterfx (ironwail)

// =======================================================================
// Internal sound data & structures
// =======================================================================

channel_t	*snd_channels;
int		total_channels;
int		max_channels;

static int	snd_blocked = 0;
static qboolean	snd_initialized = false;

static dma_t	sn;
volatile dma_t	*shm = NULL;

vec3_t		listener_origin;
vec3_t		listener_forward;
vec3_t		listener_right;
vec3_t		listener_up;
float		voicevolumescale = 1;	//for audio ducking while speaking

#define	sound_nominal_clip_dist	1500.0 // JPG - changed this from 1000 to 15000 (I'm 99% sure that's what it was in 1.06) woods (put sound back to DOSquake levels!)

int		soundtime;	// sample PAIRS
int		paintedtime;	// sample PAIRS

int		s_rawend;
portable_samplepair_t	s_rawsamples[MAX_RAW_SAMPLES];

static unsigned int SND_HashUInt (unsigned int hash, unsigned int value)
{
	hash ^= value;
	hash *= 0x01000193u;
	return hash;
}

static int SND_DeterministicSkip (int maxskip, int entnum, int entchannel,
		const vec3_t origin, int target_channel)
{
	unsigned int	hash;
	int				i;

	if (maxskip <= 1)
		return 0;

	hash = 0x811c9dc5u;
	hash = SND_HashUInt (hash, (unsigned int)entnum);
	hash = SND_HashUInt (hash, (unsigned int)entchannel);
	hash = SND_HashUInt (hash, (unsigned int)paintedtime);
	hash = SND_HashUInt (hash, (unsigned int)target_channel);
	for (i = 0; i < 3; i++)
		hash = SND_HashUInt (hash, (unsigned int)Q_rint(origin[i] * 8.0f));

	return 1 + (int)(hash % (unsigned int)(maxskip - 1));
}

#define	MAX_SFX		MAX_SOUNDS
static sfx_t	*known_sfx = NULL;	// hunk allocated [MAX_SFX]
static int	num_sfx;

static sfx_t	*ambient_sfx[NUM_AMBIENTS];

static qboolean	sound_started = false;
static cvar_t nosound;

#define SOUND_PREVIEW_ENTNUM       (-7777)
#define SOUND_PREVIEW_ENTCHANNEL   (-2) /* non-spatial, like voice audio */
#define SOUND_PREVIEW_CHANNEL      (NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS)
#define SOUND_NOTIFY_ENTNUM        (-7778)
#define SOUND_NOTIFY_ENTCHANNEL    (-2) /* non-spatial, like voice audio */
#define NOTIFICATION_SOUND_CHANNEL (SOUND_PREVIEW_CHANNEL + 1)
#define FIRST_STATIC_SOUND_CHANNEL (NOTIFICATION_SOUND_CHANNEL + 1)

static sfx_t sound_preview_sfx;
static sound_preview_state_t sound_preview;
static qboolean sound_preview_exclusive;

qboolean S_SoundPreview_ShouldMixChannel(int channel)
{
	return !sound_preview_exclusive || channel == SOUND_PREVIEW_CHANNEL;
}

void S_SoundPreview_SetExclusive(qboolean exclusive)
{
	if (sound_preview_exclusive == exclusive)
		return;
	sound_preview_exclusive = exclusive;

	/* Drop gameplay one-shots and flush already mixed audio at both edges of
	 * audition mode. Looping statics remain registered and restart cleanly. */
	if (sound_started)
		S_StopAllSounds(true, true);
}

static channel_t *S_SoundPreview_Channel(void)
{
	channel_t *channel;

	if (!snd_channels || total_channels <= SOUND_PREVIEW_CHANNEL)
		return NULL;
	channel = &snd_channels[SOUND_PREVIEW_CHANNEL];
	return channel->sfx == &sound_preview_sfx ? channel : NULL;
}

static void S_SoundPreview_StopChannel(void)
{
	channel_t *channel = S_SoundPreview_Channel();
	if (channel)
	{
		channel->sfx = NULL;
		channel->end = 0;
	}
}

static channel_t *S_SoundPreview_StartChannel(sfxcache_t *cache, int position)
{
	channel_t *channel;

	if (!cache || cache->length <= 0 || !snd_channels ||
		total_channels <= SOUND_PREVIEW_CHANNEL)
		return NULL;

	channel = &snd_channels[SOUND_PREVIEW_CHANNEL];
	memset(channel, 0, sizeof(*channel));
	channel->sfx = &sound_preview_sfx;
	channel->pos = CLAMP(0, position, cache->length - 1);
	channel->end = paintedtime + cache->length - channel->pos;
	channel->looping = sound_preview.loop ? SND_LOOP_FORCE : SND_LOOP_DISABLE;
	channel->master_vol = (int)(sound_preview.gain * 255.0f);
	channel->advance_silently = true;
	channel->entnum = SOUND_PREVIEW_ENTNUM;
	channel->entchannel = SOUND_PREVIEW_ENTCHANNEL;
	VectorCopy(listener_origin, channel->origin);
	SND_Spatialize(channel);
	return channel;
}

static void S_SoundPreview_Clear(void)
{
	S_SoundPreview_StopChannel();
	if (sound_preview_sfx.cache.data)
		Cache_Free(&sound_preview_sfx.cache, false);
	memset(&sound_preview_sfx, 0, sizeof(sound_preview_sfx));
	memset(&sound_preview, 0, sizeof(sound_preview));
	sound_preview.gain = 1.0f;
}

void S_SoundPreview_Release(void)
{
	S_SoundPreview_Clear();
	S_SoundPreview_SetExclusive(false);
}

qboolean S_SoundPreview_Play(const char *name, float gain, qboolean loop)
{
	sfxcache_t *cache;
	channel_t *channel;

	/* Starting another preview must preserve the browser's exclusive mode. */
	S_SoundPreview_Clear();
	sound_preview.gain = isfinite(gain) ? CLAMP(0.0f, gain, 1.0f) : 1.0f;
	sound_preview.loop = loop;
	if (!sound_started || nosound.value)
	{
		sound_preview.status = SOUND_PREVIEW_FAILED;
		q_strlcpy(sound_preview.error, "audio unavailable", sizeof(sound_preview.error));
		return false;
	}
	if (!name || !*name || strlen(name) >= sizeof(sound_preview_sfx.name))
	{
		sound_preview.status = SOUND_PREVIEW_FAILED;
		q_strlcpy(sound_preview.error, "invalid sound path", sizeof(sound_preview.error));
		return false;
	}

	q_strlcpy(sound_preview_sfx.name, name, sizeof(sound_preview_sfx.name));
	q_strlcpy(sound_preview.name, name, sizeof(sound_preview.name));
	cache = S_LoadSound(&sound_preview_sfx);
	if (!cache)
	{
		sound_preview.status = SOUND_PREVIEW_FAILED;
		q_strlcpy(sound_preview.error, "load failed", sizeof(sound_preview.error));
		return false;
	}
	sound_preview.length = cache->length;
	sound_preview.rate = cache->speed;
	sound_preview.bits = cache->width * 8;
	sound_preview.source_looped = cache->loopstart >= 0;
	sound_preview.seekable = true;

	channel = S_SoundPreview_StartChannel(cache, 0);
	if (!channel)
	{
		sound_preview.status = SOUND_PREVIEW_FAILED;
		q_strlcpy(sound_preview.error, "no mixer channel", sizeof(sound_preview.error));
		return false;
	}
	sound_preview.status = SOUND_PREVIEW_PLAYING;
	return true;
}

void S_SoundPreview_Stop(void)
{
	S_SoundPreview_StopChannel();
	sound_preview.position = 0;
	sound_preview.error[0] = 0;
	sound_preview.status = SOUND_PREVIEW_STOPPED;
}

void S_SoundPreview_SetPaused(qboolean paused)
{
	channel_t *channel;
	sfxcache_t *cache;

	if (paused)
	{
		if (sound_preview.status != SOUND_PREVIEW_PLAYING)
			return;
		channel = S_SoundPreview_Channel();
		if (!channel)
		{
			sound_preview.position = sound_preview.length;
			sound_preview.status = SOUND_PREVIEW_STOPPED;
			return;
		}
		sound_preview.position = channel->pos;
		S_SoundPreview_StopChannel();
		sound_preview.status = SOUND_PREVIEW_PAUSED;
		return;
	}
	if (sound_preview.status != SOUND_PREVIEW_PAUSED || !sound_preview_sfx.name[0])
		return;
	cache = S_LoadSound(&sound_preview_sfx);
	if (!cache)
	{
		sound_preview.status = SOUND_PREVIEW_FAILED;
		q_strlcpy(sound_preview.error, "reload failed", sizeof(sound_preview.error));
		return;
	}
	channel = S_SoundPreview_StartChannel(cache, sound_preview.position);
	if (!channel)
	{
		sound_preview.status = SOUND_PREVIEW_FAILED;
		q_strlcpy(sound_preview.error, "preview channel unavailable", sizeof(sound_preview.error));
		return;
	}
	sound_preview.status = SOUND_PREVIEW_PLAYING;
}

void S_SoundPreview_SetLoop(qboolean loop)
{
	channel_t *channel;
	sound_preview.loop = loop;
	channel = S_SoundPreview_Channel();
	if (channel)
		channel->looping = loop ? SND_LOOP_FORCE : SND_LOOP_DISABLE;
}

void S_SoundPreview_SetGain(float gain)
{
	channel_t *channel;
	if (!isfinite(gain))
		return;
	sound_preview.gain = CLAMP(0.0f, gain, 1.0f);
	channel = S_SoundPreview_Channel();
	if (channel)
	{
		channel->master_vol = (int)(sound_preview.gain * 255.0f);
		SND_Spatialize(channel);
	}
}

qboolean S_SoundPreview_Seek(double fraction)
{
	qboolean playing;
	sfxcache_t *cache;
	int position;

	if (!isfinite(fraction) || !sound_preview.seekable ||
		(sound_preview.status != SOUND_PREVIEW_PLAYING &&
		 sound_preview.status != SOUND_PREVIEW_PAUSED) || sound_preview.length <= 0)
		return false;

	fraction = CLAMP(0.0, fraction, 1.0);
	position = (int)(fraction * (sound_preview.length - 1) + 0.5);
	playing = sound_preview.status == SOUND_PREVIEW_PLAYING;
	cache = S_LoadSound(&sound_preview_sfx);
	if (!cache)
	{
		sound_preview.status = SOUND_PREVIEW_FAILED;
		q_strlcpy(sound_preview.error, "reload failed", sizeof(sound_preview.error));
		return false;
	}

	sound_preview.position = CLAMP(0, position, cache->length - 1);
	if (playing)
	{
		S_SoundPreview_StopChannel();
		if (!S_SoundPreview_StartChannel(cache, sound_preview.position))
		{
			sound_preview.status = SOUND_PREVIEW_FAILED;
			q_strlcpy(sound_preview.error, "preview channel unavailable", sizeof(sound_preview.error));
			return false;
		}
	}
	return true;
}

void S_SoundPreview_GetState(sound_preview_state_t *state)
{
	channel_t *channel;
	if (!state)
		return;
	channel = S_SoundPreview_Channel();
	if (sound_preview.status == SOUND_PREVIEW_PLAYING)
	{
		if (channel)
			sound_preview.position = channel->pos;
		else
		{
			sound_preview.position = sound_preview.length;
			sound_preview.status = SOUND_PREVIEW_STOPPED;
		}
	}
	*state = sound_preview;
}

cvar_t		bgmvolume = {"bgmvolume", "1", CVAR_ARCHIVE};
cvar_t		sfxvolume = {"volume", "0.7", CVAR_ARCHIVE};

cvar_t		precache = {"precache", "1", CVAR_NONE};
cvar_t		loadas8bit = {"loadas8bit", "0", CVAR_NONE};

cvar_t		sndspeed = {"sndspeed", "11025", CVAR_NONE};
cvar_t		snd_mixspeed = {"snd_mixspeed", "44100", CVAR_ARCHIVE};
cvar_t		snd_surround = {"snd_surround", "1", CVAR_ARCHIVE};

cvar_t		snd_waterfx = {"snd_waterfx", "1", CVAR_ARCHIVE}; // woods #waterfx (ironwail)

#if defined(_WIN32)
#define SND_FILTERQUALITY_DEFAULT "5"
#else
#define SND_FILTERQUALITY_DEFAULT "1"
#endif

cvar_t		snd_filterquality = {"snd_filterquality", SND_FILTERQUALITY_DEFAULT,
								 CVAR_NONE};

static	cvar_t	nosound = {"nosound", "0", CVAR_NONE};
cvar_t	ambient_level = {"ambient_level", "0.3", CVAR_ARCHIVE}; // woods  - remove static
static	cvar_t	ambient_fade = {"ambient_fade", "100", CVAR_NONE};
static	cvar_t	snd_noextraupdate = {"snd_noextraupdate", "0", CVAR_NONE};
static	cvar_t	snd_show = {"snd_show", "0", CVAR_NONE};
static	cvar_t	_snd_mixahead = {"_snd_mixahead", "0.1", CVAR_ARCHIVE};

extern char mute[2]; // woods #usermute #mute

#if defined(USE_SDL2)
static SDL_atomic_t snd_mastervolume_scale = {256};
#else
static volatile int snd_mastervolume_scale = 256;
#endif

int S_GetMasterVolumeScale (void)
{
#if defined(USE_SDL2)
	return SDL_AtomicGet (&snd_mastervolume_scale);
#else
	return snd_mastervolume_scale;
#endif
}

void S_SetMasterVolumeScale (float scale)
{
	int value = (int)(CLAMP (0.0f, scale, 1.0f) * 256.0f);

#if defined(USE_SDL2)
	SDL_AtomicSet (&snd_mastervolume_scale, value);
#else
	snd_mastervolume_scale = value;
#endif
}

static void S_SoundInfo_f (void)
{
	int samplepos;

	if (!sound_started || !shm)
	{
		Con_Printf ("sound system not started\n");
		return;
	}
	SNDDMA_LockBuffer ();
	samplepos = SNDDMA_GetDMAPos ();
	SNDDMA_Submit ();

	Con_Printf("%d bit, %s, %d Hz\n", shm->samplebits,
			(shm->channels == 2) ? "stereo" : "mono", shm->speed);
	Con_Printf("%5d samples\n", shm->samples);
	Con_Printf("%5d samplepos\n", samplepos);
	Con_Printf("%5d submission_chunk\n", shm->submission_chunk);
	Con_Printf("%5d total_channels\n", total_channels);
	Con_Printf("%p dma buffer\n", shm->buffer);
}

static void S_SoundPreview_f(void)
{
	const char *arg;
	sound_preview_state_t state;

	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: sfx_preview <path>|pause|resume|stop|info|loop 0|1|gain 0..1|seek 0..1\n");
		return;
	}
	arg = Cmd_Argv(1);
	if (!q_strcasecmp(arg, "pause"))
		S_SoundPreview_SetPaused(true);
	else if (!q_strcasecmp(arg, "resume"))
		S_SoundPreview_SetPaused(false);
	else if (!q_strcasecmp(arg, "stop"))
		S_SoundPreview_Stop();
	else if (!q_strcasecmp(arg, "loop"))
	{
		if (Cmd_Argc() != 3)
			Con_Printf("usage: sfx_preview loop 0|1\n");
		else
			S_SoundPreview_SetLoop(atof(Cmd_Argv(2)) != 0.0f);
	}
	else if (!q_strcasecmp(arg, "gain"))
	{
		if (Cmd_Argc() != 3)
			Con_Printf("usage: sfx_preview gain 0..1\n");
		else
			S_SoundPreview_SetGain(atof(Cmd_Argv(2)));
	}
	else if (!q_strcasecmp(arg, "seek"))
	{
		if (Cmd_Argc() != 3)
			Con_Printf("usage: sfx_preview seek 0..1\n");
		else if (!S_SoundPreview_Seek(atof(Cmd_Argv(2))))
			Con_Printf("sfx preview is not seekable\n");
	}
	else if (!q_strcasecmp(arg, "info"))
	{
		S_SoundPreview_GetState(&state);
		Con_Printf("sfx preview: %s, %d/%d samples, %d Hz %d-bit, gain %.0f%%, loop %s",
			state.name[0] ? state.name : "none", state.position, state.length,
			state.rate, state.bits, state.gain * 100.0f, state.loop ? "on" : "off");
		if (state.error[0])
			Con_Printf(" (%s)", state.error);
		Con_Printf("\n");
	}
	else
		S_SoundPreview_Play(arg, sound_preview.gain, sound_preview.loop);
}


static void SND_Callback_sfxvolume (cvar_t *var)
{
	scr_volume_display_time = realtime + 1.0;
	SND_InitScaletable ();
	if (!strcmp(mute, "y")) // woods #usermute #mute
		Sound_Toggle_Mute_f();
}

static void SND_Callback_snd_filterquality (cvar_t *var)
{
	if (snd_filterquality.value < 1 || snd_filterquality.value > 5)
	{
		Con_Printf ("snd_filterquality must be between 1 and 5\n");
		Cvar_SetQuick (&snd_filterquality, SND_FILTERQUALITY_DEFAULT);
	}
}

static void SND_Callback_snd_surround (cvar_t *var)
{
	(void) var;

#if defined(USE_SDL2)
	if (sound_started)
		Con_Printf ("snd_surround will take effect after snd_restart\n");
#else
	if (sound_started)
		Con_Printf ("snd_surround requires SDL2\n");
#endif
}

/*
================
S_Startup
================
*/
void S_Startup (void)
{
	if (!snd_initialized)
		return;

	sound_started = SNDDMA_Init(&sn);

	if (!sound_started)
	{
		Con_Printf("Failed initializing sound\n");
	}
	else
	{
		Con_Printf("Audio: %d bit, %s, %d Hz\n", shm->samplebits,
				(shm->channels == 2) ? "stereo" : "mono", shm->speed);
		SNDDMA_LockBuffer ();
		GetSoundtime();
		SNDDMA_Submit ();
	}
	paintedtime = soundtime;
}

/*
snd_restart console command
*/
void S_Restart_f(void)
{
	sfx_t *s;
	size_t i;
	int oldspeed;

	if (!snd_initialized)
		return;
	S_SoundPreview_Release();

	oldspeed = shm ? shm->speed : 0;

	if (sound_started)
	{
		sound_started = 0;
		snd_blocked = 0;
		SNDDMA_Shutdown();
		shm = NULL;
	}

	S_Startup();

	if (!sound_started || !shm)
	{
		Con_Printf("S_Restart_f: Failed to restart sound system\n");
		return;
	}

	if (!snd_channels || !S_CodecIsInitialized ())
	{
		S_CompleteStartup ();
		BGM_RefreshCodecHandlers ();
	}

	paintedtime = soundtime;
	//we changed the sound time and probably the rates too...
	//any timing of sounds will be way off. so lets just kill any currently playing sounds
	//(note that this lazy way of killing them will ensure that looping sounds restart)
	for (i = 0; i < total_channels; i++)
	{
		snd_channels[i].pos = 0;
		snd_channels[i].end = 0;
	}
	s_rawend = 0;	//clear any music too...

	//reload any sounds if their rates changed.
	if (oldspeed != 0 && shm->speed != oldspeed)
	{
		for (i = 0; i < num_sfx; i++)
		{
			s = &known_sfx[i];
			if (s->cache.data)
				Cache_Free(&s->cache, false);
		}
	}
}

void BGM_Volume_Callback_f (cvar_t* var) // woods #usermute #mute
{
	if (!strcmp(mute, "y"))
		Sound_Toggle_Mute_f();
}

static void S_PrecacheAmbientSounds (void)
{
	if (!sound_started || nosound.value)
	{
		ambient_sfx[AMBIENT_WATER] = NULL;
		ambient_sfx[AMBIENT_SKY] = NULL;
		return;
	}

	ambient_sfx[AMBIENT_WATER] = S_PrecacheSound ("ambience/water1.wav");
	ambient_sfx[AMBIENT_SKY] = S_PrecacheSound ("ambience/wind2.wav");
}

static void S_CompleteStartup (void)
{
	if (!sound_started || !shm)
		return;

	if (!snd_channels)
		S_StopAllSounds (true, false);

	S_CodecInit ();
	S_PrecacheAmbientSounds ();
}

/*
================
S_Init
================
*/
void S_Init (void)
{
	int i;

	if (snd_initialized)
	{
		Con_Printf("Sound is already initialized\n");
		return;
	}

	Cvar_RegisterVariable(&nosound);
	Cvar_RegisterVariable(&sfxvolume);
	Cvar_RegisterVariable(&precache);
	Cvar_RegisterVariable(&loadas8bit);
	Cvar_RegisterVariable(&bgmvolume);
	Cvar_SetCallback(&bgmvolume, &BGM_Volume_Callback_f); // woods #usermute #mute
	Cvar_RegisterVariable(&ambient_level);
	Cvar_RegisterVariable(&ambient_fade);
	Cvar_RegisterVariable(&snd_noextraupdate);
	Cvar_RegisterVariable(&snd_show);
	Cvar_RegisterVariable(&_snd_mixahead);
	Cvar_RegisterVariable(&sndspeed);
	Cvar_RegisterVariable(&snd_mixspeed);
	Cvar_RegisterVariable(&snd_surround);
	Cvar_RegisterVariable(&snd_filterquality);
	Cvar_RegisterVariable(&snd_waterfx); // woods #waterfx (ironwail)

	S_Voip_Init();

	if (safemode || COM_CheckParm("-nosound"))
		return;

	Con_Printf("\nSound Initialization\n");

	Cmd_AddCommand("play", S_Play);
	Cmd_AddCommand("play2", S_Play);	//Spike -- a version with attenuation 0.
	Cmd_AddCommand("playvol", S_PlayVol);
	Cmd_AddCommand("stopsound", S_StopAllSoundsC);
	Cmd_AddCommand("soundlist", S_SoundList);
	Cmd_AddCommand("soundinfo", S_SoundInfo_f);
	Cmd_AddCommand("sfx_preview", S_SoundPreview_f);
	Cmd_AddCommand("snd_restart", S_Restart_f);
	Cmd_AddCommand("mute", Sound_Toggle_Mute_f); // woods #usermute

	i = COM_CheckParm("-sndspeed");
	if (i && i < com_argc-1)
	{
		Cvar_SetQuick (&sndspeed, com_argv[i + 1]);
	}

	i = COM_CheckParm("-mixspeed");
	if (i && i < com_argc-1)
	{
		Cvar_SetQuick (&snd_mixspeed, com_argv[i + 1]);
	}

	if (host_parms->memsize < 0x800000)
	{
		Cvar_SetQuick (&loadas8bit, "1");
		Con_Printf ("loading all sounds as 8bit\n");
	}

	Cvar_SetCallback(&sfxvolume, SND_Callback_sfxvolume);
	Cvar_SetCallback(&snd_filterquality, &SND_Callback_snd_filterquality);
	Cvar_SetCallback(&snd_surround, &SND_Callback_snd_surround);

	SND_InitScaletable ();

	known_sfx = (sfx_t *) Hunk_AllocName (MAX_SFX*sizeof(sfx_t), "sfx_t");
	num_sfx = 0;
	sound_preview.gain = 1.0f;

	snd_initialized = true;

	S_Startup ();
	if (sound_started == 0)
		return;

// provides a tick sound until washed clean
//	if (shm->buffer)
//		shm->buffer[4] = shm->buffer[5] = 0x7f;	// force a pop for debugging

	S_CompleteStartup ();
}


// =======================================================================
// Shutdown sound engine
// =======================================================================
void S_Shutdown (void)
{
	qboolean shutdown_backend;

	if (!sound_started && !S_CodecIsInitialized ())
		return;
	S_SoundPreview_Release();

	shutdown_backend = sound_started;
	sound_started = 0;
	snd_blocked = 0;

	S_CodecShutdown();

	if (shutdown_backend)
		SNDDMA_Shutdown();
	shm = NULL;
}


// =======================================================================
// Load a sound
// =======================================================================
/*
==================
S_FindName

==================
*/
static sfx_t *S_FindName (const char *name)
{
	int		i;
	sfx_t	*sfx;

	if (!name)
		Sys_Error ("S_FindName: NULL");

	if (strlen(name) >= MAX_QPATH)
		Sys_Error ("Sound name too long: %s", name);

// see if already loaded
	for (i = 0; i < num_sfx; i++)
	{
		if (!strcmp(known_sfx[i].name, name))
		{
			return &known_sfx[i];
		}
	}

	if (num_sfx == MAX_SFX)
		Sys_Error ("S_FindName: out of sfx_t");

	sfx = &known_sfx[i];
	q_strlcpy (sfx->name, name, sizeof(sfx->name));

	num_sfx++;

	return sfx;
}


/*
==================
S_TouchSound

==================
*/
void S_TouchSound (const char *name)
{
	sfx_t	*sfx;

	if (!sound_started)
		return;

	sfx = S_FindName (name);
	Cache_Check (&sfx->cache);
}

/*
==================
S_PrecacheSound

==================
*/
sfx_t *S_PrecacheSound (const char *name)
{
	sfx_t	*sfx;

	if (!sound_started || nosound.value)
		return NULL;

	sfx = S_FindName (name);

// cache it in
	if (precache.value)
		S_LoadSound (sfx);

	return sfx;
}


//=============================================================================

/*
=================
SND_PickChannel

picks a channel based on priorities, empty slots, number of channels
=================
*/
channel_t *SND_PickChannel (int entnum, int entchannel)
{
	int	ch_idx;
	int	first_to_die;
	int	life_left;

// Check for replacement sound, or find the best one to replace
	first_to_die = -1;
	life_left = 0x7fffffff;
	for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++)
	{
		if (entchannel != 0		// channel 0 never overrides
			&& snd_channels[ch_idx].entnum == entnum
			&& (snd_channels[ch_idx].entchannel == entchannel || entchannel == -1) )
		{	// always override sound from same entity
			first_to_die = ch_idx;
			break;
		}

		// don't let monster sounds override player sounds
		if (snd_channels[ch_idx].entnum == cl.viewentity && entnum != cl.viewentity && snd_channels[ch_idx].sfx)
			continue;

		if (snd_channels[ch_idx].end - paintedtime < life_left)
		{
			life_left = snd_channels[ch_idx].end - paintedtime;
			first_to_die = ch_idx;
		}
	}

	if (first_to_die == -1)
		return NULL;

	return &snd_channels[first_to_die];
}

/*
=================
SND_Spatialize

spatializes a channel
=================
*/
void SND_Spatialize (channel_t *ch)
{
	vec_t	dot;
	vec_t	dist;
	vec_t	lscale, rscale, scale;
	vec3_t	source_vec;

	if (ch->entchannel == -2)
	{
		ch->leftvol = ch->master_vol;	//voip comes out full volume
		ch->rightvol = ch->master_vol;
		return;
	}
// anything coming from the view entity will always be full volume
	if (ch->entnum == cl.viewentity)
	{
		ch->leftvol = ch->master_vol * voicevolumescale;
		ch->rightvol = ch->master_vol * voicevolumescale;
		return;
	}

// calculate stereo seperation and distance attenuation
	VectorSubtract(ch->origin, listener_origin, source_vec);
	dist = VectorNormalize(source_vec) * ch->dist_mult;
	dot = DotProduct(listener_right, source_vec);

	if (shm->channels == 1)
	{
		rscale = 1.0;
		lscale = 1.0;
	}
	else
	{
		rscale = 1.0 + dot;
		lscale = 1.0 - dot;
	}

// add in distance effect
	scale = (1.0 - dist) * rscale;
	ch->rightvol = (int) (ch->master_vol * scale * voicevolumescale);
	if (ch->rightvol < 0)
		ch->rightvol = 0;

	scale = (1.0 - dist) * lscale;
	ch->leftvol = (int) (ch->master_vol * scale * voicevolumescale);
	if (ch->leftvol < 0)
		ch->leftvol = 0;
}


// =======================================================================
// Start a sound effect
// =======================================================================

void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation)
{
	channel_t	*target_chan, *check;
	sfxcache_t	*sc;
	sfx_t* old_sfx; // woods (iw) #democontrols
	vec3_t		old_origin; // woods (iw) #democontrols
	float		old_vol; // woods (iw) #democontrols
	float		old_atten; // woods (iw) #democontrols
	int			ch_idx; // woods (iw) #democontrols
	int			skip; // woods (iw) #democontrols

	if (fabsf(cls.demospeed) > 8) // woods (iw) #democontrols
		return;

	if (!sound_started)
		return;

	if (!sfx)
		return;

	if (nosound.value)
		return;

// pick a channel to play on
	target_chan = SND_PickChannel(entnum, entchannel);
	if (!target_chan)
		return;

	// keep track of the old sound playing on this channel (for demo rewinding) // woods (iw) #democontrols
	old_sfx = NULL;
	VectorCopy(origin, old_origin);
	old_vol = fvol;
	old_atten = attenuation;
	if (entnum > 0 && entchannel > 0 && target_chan->entnum == entnum && target_chan->entchannel == entchannel)
	{
		old_sfx = target_chan->sfx;
		VectorCopy(target_chan->origin, old_origin);
		old_vol = target_chan->master_vol;
		old_atten = target_chan->dist_mult * sound_nominal_clip_dist;
	}

// spatialize
	memset (target_chan, 0, sizeof(*target_chan));
	VectorCopy(origin, target_chan->origin);
	target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
	target_chan->master_vol = (int) (fvol * 255);
	target_chan->entnum = entnum;
	target_chan->entchannel = entchannel;
	SND_Spatialize(target_chan);

	if (!target_chan->leftvol && !target_chan->rightvol)
		return;		// not audible at all

// new channel
	sc = S_LoadSound (sfx);
	if (!sc)
	{
		target_chan->sfx = NULL;
		return;		// couldn't load the sound's data
	}

	// if this is a looping sound and we're not rewinding, keep track of the previous sound playing // woods (iw) #democontrols
// on the same ent/channel so that when we do rewind past this frame we start playing it instead
	if (cls.demoplayback && cls.demospeed > 0.f && sc->loopstart != -1)
		CL_AddDemoRewindSound(entnum, entchannel, old_sfx, old_origin, old_vol, old_atten);

	target_chan->sfx = sfx;
	target_chan->pos = 0.0;
	target_chan->end = paintedtime + sc->length;

// if an identical sound has also been started this frame, offset the pos
// a bit to keep it from just making the first one louder
	check = &snd_channels[NUM_AMBIENTS];
	for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++, check++)
	{
		if (check == target_chan)
			continue;
		if (check->sfx == sfx && !check->pos)
		{
			/*
			skip = rand () % (int)(0.1 * shm->speed);
			if (skip >= target_chan->end)
				skip = target_chan->end - 1;
			*/
			/* LordHavoc: fixed skip calculations */
			skip = 0.1 * shm->speed; /* 0.1 * sc->speed */
			if (skip > sc->length)
				skip = sc->length;
			if (skip > 0)
				skip = SND_DeterministicSkip(skip, entnum, entchannel,
					origin, (int)(target_chan - snd_channels));
			target_chan->pos += skip;
			target_chan->end -= skip;
			break;
		}
	}
}

void S_StopSound (int entnum, int entchannel)
{
	int	i;

	// svc_stopsound reaches us straight off the wire, and snd_channels is
	// only allocated once sound is running -- with -nosound, or when the
	// audio device fails to open, this would dereference NULL
	if (!sound_started || !snd_channels)
		return;

	for (i = NUM_AMBIENTS; i < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; i++) // woods
	{
		if (snd_channels[i].entnum == entnum
			&& snd_channels[i].entchannel == entchannel)
		{
			snd_channels[i].end = 0;
			snd_channels[i].sfx = NULL;
			return;
		}
	}
}

void S_StopAllSounds (qboolean clear, qboolean keep_statics)
{
	int	i;

	if (!sound_started)
		return;

	if (!keep_statics)
	{
		total_channels = FIRST_STATIC_SOUND_CHANNEL;	// ambience, dynamics, preview, and notifications; no statics
		if (max_channels != total_channels + 64)
		{	//shrink it if needed
			max_channels = total_channels + 64;
			free(snd_channels);
			snd_channels = malloc(sizeof(channel_t) * max_channels);
		}
		memset(snd_channels, 0, max_channels * sizeof(channel_t));
	}
	else
	{
		for (i = 0; i < total_channels; i++)
		{
			sfxcache_t *sc = NULL;

			if (snd_channels[i].sfx)
				sc = S_LoadSound (snd_channels[i].sfx);

			if (i < FIRST_STATIC_SOUND_CHANNEL || !sc || sc->loopstart == -1)
				memset (&snd_channels[i], 0, sizeof (channel_t));
			else
			{
				snd_channels[i].pos = 0;
				snd_channels[i].end = paintedtime + sc->length;
			}
		}
	}

	if (clear)
		S_ClearBuffer ();
}

static void S_StopAllSoundsC (void)
{
	S_StopAllSounds (true, false);
}

void S_ClearBuffer (void)
{
	int		clear;

	if (!sound_started || !shm)
		return;

	SNDDMA_LockBuffer ();
	if (! shm->buffer)
	{
		SNDDMA_Submit ();
		return;
	}

	s_rawend = 0;

	if (shm->samplebits == 8 && !shm->signed8)
		clear = 0x80;
	else
		clear = 0;

	memset (shm->buffer, clear, shm->samples * shm->samplebits / 8);
	memset (s_rawsamples, 0, sizeof (s_rawsamples));
	S_ClearFilteredLevels ();

	SNDDMA_Submit ();
}


/*
=================
S_StaticSound
=================
*/
void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
	channel_t	*ss;
	sfxcache_t		*sc;

	if (!sound_started)	// likewise driven by svc_spawnstaticsound
		return;

	if (!sfx)
		return;

	if (total_channels == max_channels)
	{
		int nm = max_channels+64;
		ss = realloc(snd_channels, sizeof(*ss)*nm);
		if (!ss)
		{
			Con_Printf ("unable to increase max_channels\n");
			return;
		}
		snd_channels = ss;
		memset(snd_channels+max_channels, 0, sizeof(*ss)*(nm-max_channels));
		max_channels = nm;
	}

	ss = &snd_channels[total_channels];
	total_channels++;

	sc = S_LoadSound (sfx);
	if (!sc)
		return;

	if (sc->loopstart == -1)
	{
		Con_Printf ("Sound %s not looped\n", sfx->name);
		return;
	}

	ss->sfx = sfx;
	VectorCopy (origin, ss->origin);
	ss->master_vol = (int)vol;
	ss->dist_mult = (attenuation / 64) / sound_nominal_clip_dist;
	ss->end = paintedtime + sc->length;

	SND_Spatialize (ss);
}


//=============================================================================

/*
===================
S_UnderwaterIntensityForContents // woods #waterfx (ironwail)
===================
*/
static float S_UnderwaterIntensityForContents(int contents)
{
	switch (contents)
	{
	case CONTENTS_WATER:
	case CONTENTS_SLIME:
	case CONTENTS_LAVA:
		return 1.f;
	default:
		return 0.f;
	}
}

/*
===================
S_UpdateAmbientSounds
===================
*/
static void S_UpdateAmbientSounds (void)
{
	mleaf_t			*l;
	int				ambient_channel;
	channel_t		*chan;
	float			vol;
	static float	levels[NUM_AMBIENTS];

// no ambients when disconnected
	if (cls.state != ca_connected || cls.signon != SIGNONS)
	{
		memset (levels, 0, sizeof (levels));
		S_SetUnderwaterIntensity(0.f); // woods #waterfx (ironwail)
		return;
	}

// calc ambient sound levels
	if (!cl.worldmodel || cl.worldmodel->needload)
	{
		memset (levels, 0, sizeof (levels));
		S_SetUnderwaterIntensity(0.f); // woods #waterfx (ironwail)
		return;
	}

	l = Mod_PointInLeaf (listener_origin, cl.worldmodel);
	S_SetUnderwaterIntensity(l ? S_UnderwaterIntensityForContents(l->contents) : 0.f); // woods #waterfx (ironwail)
	if (!l || !ambient_level.value)
	{
		for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++)
			snd_channels[ambient_channel].sfx = NULL;
		memset (levels, 0, sizeof (levels));
		return;
	}

	if (!ambient_sfx[AMBIENT_WATER] || !ambient_sfx[AMBIENT_SKY])
		S_PrecacheAmbientSounds ();

	for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++)
	{
		chan = &snd_channels[ambient_channel];
		chan->sfx = ambient_sfx[ambient_channel];

		vol = (int) (ambient_level.value * l->ambient_sound_level[ambient_channel]);
		if (vol < 8.f)
			vol = 0.f;
		else if (vol > 255.f)
			vol = 255.f;

	// don't adjust volume too fast
		if (levels[ambient_channel] < vol)
		{
			levels[ambient_channel] += host_frametime * ambient_fade.value;
			if (levels[ambient_channel] > vol)
				levels[ambient_channel] = vol;
		}
		else if (levels[ambient_channel] > vol)
		{
			levels[ambient_channel] -= host_frametime * ambient_fade.value;
			if (levels[ambient_channel] < vol)
				levels[ambient_channel] = vol;
		}

		chan->leftvol = chan->rightvol = chan->master_vol = (int) levels[ambient_channel];
	}
}


/*
===================
S_RawSamples		(from QuakeII)

Streaming music support. Byte swapping
of data must be handled by the codec.
Expects data in signed 16 bit, or unsigned
8 bit format.
===================
*/
void S_RawSamples (int samples, int rate, int width, int channels, byte *data, float volume)
{
	int i;
	int src, dst;
	float scale;
	int intVolume;

	if (s_rawend < paintedtime)
		s_rawend = paintedtime;

	scale = (float) rate / shm->speed;
	intVolume = (int) (256 * volume);

	if (channels == 2 && width == 2)
	{
		for (i = 0; ; i++)
		{
			src = i * scale;
			if (src >= samples)
				break;
			dst = s_rawend & (MAX_RAW_SAMPLES - 1);
			s_rawend++;
			s_rawsamples [dst].left = ((short *) data)[src * 2] * intVolume;
			s_rawsamples [dst].right = ((short *) data)[src * 2 + 1] * intVolume;
		}
	}
	else if (channels == 1 && width == 2)
	{
		for (i = 0; ; i++)
		{
			src = i * scale;
			if (src >= samples)
				break;
			dst = s_rawend & (MAX_RAW_SAMPLES - 1);
			s_rawend++;
			s_rawsamples [dst].left = ((short *) data)[src] * intVolume;
			s_rawsamples [dst].right = ((short *) data)[src] * intVolume;
		}
	}
	else if (channels == 2 && width == 1)
	{
		intVolume *= 256;

		for (i = 0; ; i++)
		{
			src = i * scale;
			if (src >= samples)
				break;
			dst = s_rawend & (MAX_RAW_SAMPLES - 1);
			s_rawend++;
		//	s_rawsamples [dst].left = ((signed char *) data)[src * 2] * intVolume;
		//	s_rawsamples [dst].right = ((signed char *) data)[src * 2 + 1] * intVolume;
			s_rawsamples [dst].left = (((byte *) data)[src * 2] - 128) * intVolume;
			s_rawsamples [dst].right = (((byte *) data)[src * 2 + 1] - 128) * intVolume;
		}
	}
	else if (channels == 1 && width == 1)
	{
		intVolume *= 256;

		for (i = 0; ; i++)
		{
			src = i * scale;
			if (src >= samples)
				break;
			dst = s_rawend & (MAX_RAW_SAMPLES - 1);
			s_rawend++;
		//	s_rawsamples [dst].left = ((signed char *) data)[src] * intVolume;
		//	s_rawsamples [dst].right = ((signed char *) data)[src] * intVolume;
			s_rawsamples [dst].left = (((byte *) data)[src] - 128) * intVolume;
			s_rawsamples [dst].right = (((byte *) data)[src] - 128) * intVolume;
		}
	}
}

/*
============
S_Update

Called once each time through the main loop
============
*/
void S_Update (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
	int			i, j;
	int			total;
	channel_t	*ch;
	channel_t	*combine;

	if (!sound_started || (snd_blocked > 0))
		return;

	VectorCopy(origin, listener_origin);
	VectorCopy(forward, listener_forward);
	VectorCopy(right, listener_right);
	VectorCopy(up, listener_up);

// update general area ambient sound sources
	S_UpdateAmbientSounds ();

	combine = NULL;

// update spatialization for static and dynamic sounds
	ch = snd_channels + NUM_AMBIENTS;
	for (i = NUM_AMBIENTS; i < total_channels; i++, ch++)
	{
		if (!ch->sfx)
			continue;
		SND_Spatialize(ch);	// respatialize channel
		if (!ch->leftvol && !ch->rightvol)
			continue;

	// try to combine static sounds with a previous channel of the same
	// sound effect so we don't mix five torches every frame

		if (i >= FIRST_STATIC_SOUND_CHANNEL)
		{
		// see if it can just use the last one
			if (combine && combine->sfx == ch->sfx)
			{
				combine->leftvol += ch->leftvol;
				combine->rightvol += ch->rightvol;
				ch->leftvol = ch->rightvol = 0;
				continue;
			}
		// search for one
			combine = snd_channels + FIRST_STATIC_SOUND_CHANNEL;
			for (j = FIRST_STATIC_SOUND_CHANNEL; j < i; j++, combine++)
			{
				if (combine->sfx == ch->sfx)
					break;
			}

			if (j == total_channels)
			{
				combine = NULL;
			}
			else
			{
				if (combine != ch)
				{
					combine->leftvol += ch->leftvol;
					combine->rightvol += ch->rightvol;
					ch->leftvol = ch->rightvol = 0;
				}
				continue;
			}
		}
	}

//
// debugging output
//
	if (snd_show.value)
	{
		total = 0;
		ch = snd_channels;
		for (i = 0; i < total_channels; i++, ch++)
		{
			if (ch->sfx && (ch->leftvol || ch->rightvol) )
			{
				sfxcache_t *sc = (sfxcache_t *) Cache_Check (&ch->sfx->cache);
				if (snd_show.value >= 2.f)
					Con_SafePrintf ("L:%3i R:%3i | ENT:%5i CH:%3i | %s%s\n",
						ch->leftvol, ch->rightvol, ch->entnum, ch->entchannel, ch->sfx->name, sc && sc->loopstart >= 0 ? " [L]" : "");
				total++;
			}
		}

		Con_Printf ("----(%i)----\n", total);
	}

// add raw data from streamed samples
//	BGM_Update();	// moved to the main loop just before S_Update ()

// mix some sound
	S_Update_();
}

static void GetSoundtime (void)
{
	int		samplepos;
	static	int		buffers;
	static	int		oldsamplepos;
	int		fullsamples;

	fullsamples = shm->samples / shm->channels;

// it is possible to miscount buffers if it has wrapped twice between
// calls to S_Update.  Oh well.
	samplepos = SNDDMA_GetDMAPos();

	if (samplepos < oldsamplepos)
	{
		buffers++;	// buffer wrapped

		if (paintedtime > 0x40000000)
		{	// time to chop things off to avoid 32 bit limits
			buffers = 0;
			paintedtime = fullsamples;
			S_StopAllSounds (true, true);
		}
	}
	oldsamplepos = samplepos;

	soundtime = buffers*fullsamples + samplepos/shm->channels;
}

void S_ExtraUpdate (void)
{
	if (snd_noextraupdate.value)
		return;		// don't pollute timings
	S_Update_();
}

static void S_Update_ (void)
{
	unsigned int	endtime;
	int		samps;

	if (!sound_started || (snd_blocked > 0))
		return;

	SNDDMA_LockBuffer ();
	if (! shm->buffer)
	{
		SNDDMA_Submit ();
		return;
	}

// Updates DMA time
	GetSoundtime();

// check to make sure that we haven't overshot
	if (paintedtime < soundtime)
	{
	//	Con_Printf ("S_Update_ : overflow\n");
		paintedtime = soundtime;
	}

// mix ahead of current position
	endtime = soundtime + (unsigned int)(_snd_mixahead.value * shm->speed);
	samps = shm->samples >> (shm->channels - 1);
	endtime = q_min(endtime, (unsigned int)(soundtime + samps));

	S_PaintChannels (endtime);

	SNDDMA_Submit ();
}

qboolean S_BlockSound (void)
{
/* FIXME: do we really need the blocking at the
 * driver level?
 */
	if (sound_started && snd_blocked == 0)
	{
		snd_blocked = 1;
		S_ClearBuffer ();
		if (shm)
			SNDDMA_BlockSound();
		return true;
	}
	return false;
}

void S_UnblockSound (void)
{
	if (!sound_started || !snd_blocked)
		return;
	if (snd_blocked == 1)
	{
		S_ClearBuffer ();
		snd_blocked = 0;
		SNDDMA_UnblockSound();
	}
}

/*
===============================================================================

console functions

===============================================================================
*/

static void S_Play (void)
{
	static int hash = 345;
	int		i;
	char	name[256];
	sfx_t	*sfx;
	float	attenuation = !strcmp(Cmd_Argv(0), "play2")?0:1.0;

	i = 1;
	while (i < Cmd_Argc())
	{
		q_strlcpy(name, Cmd_Argv(i), sizeof(name));
		if (!strrchr(Cmd_Argv(i), '.'))
		{
			q_strlcat(name, ".wav", sizeof(name));
		}
		sfx = S_PrecacheSound(name);
		S_StartSound(hash++, 0, sfx, listener_origin, 1.0, attenuation);
		i++;
	}
}

static void S_PlayVol (void)
{
	static int hash = 543;
	int		i;
	float	vol;
	char	name[256];
	sfx_t	*sfx;

	i = 1;
	while (i < Cmd_Argc())
	{
		q_strlcpy(name, Cmd_Argv(i), sizeof(name));
		if (!strrchr(Cmd_Argv(i), '.'))
		{
			q_strlcat(name, ".wav", sizeof(name));
		}
		sfx = S_PrecacheSound(name);
		vol = atof(Cmd_Argv(i + 1));
		S_StartSound(hash++, 0, sfx, listener_origin, vol, 1.0);
		i += 2;
	}
}

static void S_SoundList (void)
{
	int		i;
	sfx_t	*sfx;
	sfxcache_t	*sc;
	int		size, total;

	total = 0;
	for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++)
	{
		sc = (sfxcache_t *) Cache_Check (&sfx->cache);
		if (!sc)
			continue;
		size = sc->length*sc->width*(sc->stereo + 1);
		total += size;
		if (sc->loopstart >= 0)
			Con_SafePrintf ("L"); //johnfitz -- was Con_Printf
		else
			Con_SafePrintf (" "); //johnfitz -- was Con_Printf
		Con_SafePrintf("(%2db) %6i : %s\n", sc->width*8, size, sfx->name); //johnfitz -- was Con_Printf
	}
	Con_Printf ("%i sounds, %i bytes\n", num_sfx, total); //johnfitz -- added count
}

qboolean CompleteSoundList (const char* partial, void* unused) // woods #iwtabcomplete
{
	int		i;
	sfx_t* sfx;

	if (Cmd_Argc() != 2)
		return false;

	for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++)
	{
		const char* name;
		name = sfx->name;
		if (*name)
			Con_AddToTabList (name, partial, NULL, NULL);
	}

	return true;
}

void S_LocalSound (const char *name)
{
	sfx_t	*sfx;

	if (nosound.value)
		return;
	if (!sound_started)
		return;

	sfx = S_PrecacheSound (name);
	if (!sfx)
	{
		Con_Printf ("S_LocalSound: can't cache %s\n", name);
		return;
	}
	S_StartSound (cl.viewentity, -1, sfx, vec3_origin, 1, 1);
}

/* Plays UI feedback without consuming or replacing a gameplay sound channel. */
void S_NotificationSound (const char *name)
{
	channel_t	*channel;
	sfxcache_t	*cache;
	sfx_t		*sfx;

	if (nosound.value || !sound_started || !snd_channels ||
		total_channels <= NOTIFICATION_SOUND_CHANNEL)
		return;

	sfx = S_PrecacheSound (name);
	if (!sfx)
	{
		Con_Printf ("S_NotificationSound: can't cache %s\n", name);
		return;
	}
	cache = S_LoadSound (sfx);
	if (!cache || cache->length <= 0)
		return;

	channel = &snd_channels[NOTIFICATION_SOUND_CHANNEL];
	memset (channel, 0, sizeof(*channel));
	channel->sfx = sfx;
	channel->end = paintedtime + cache->length;
	channel->looping = SND_LOOP_DISABLE;
	channel->master_vol = 255;
	channel->entnum = SOUND_NOTIFY_ENTNUM;
	channel->entchannel = SOUND_NOTIFY_ENTCHANNEL;
	VectorCopy (vec3_origin, channel->origin);
	SND_Spatialize (channel);
}

void S_NotificationSound_Copy (void)
{
	S_NotificationSound (COM_FileExists ("sound/qssm/copy.wav", NULL) ?
		"qssm/copy.wav" : "player/tornoff2.wav");
}


void S_ClearPrecache (void)
{
	int		i;
	sfx_t	*sfx;

	if (!snd_initialized || !known_sfx)
		return;
	S_SoundPreview_Release();

	S_StopAllSounds (true, false);

	for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++)
	{
		if (sfx->cache.data)
			Cache_Free (&sfx->cache, false);
	}

	memset (known_sfx, 0, MAX_SFX * sizeof(*known_sfx));
	num_sfx = 0;
	memset (ambient_sfx, 0, sizeof(ambient_sfx));
}


void S_BeginPrecaching (void)
{
}


void S_EndPrecaching (void)
{
}
