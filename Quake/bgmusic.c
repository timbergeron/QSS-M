/*
 * Background music handling for Quakespasm (adapted from uHexen2)
 * Handles streaming music as raw sound samples and runs the midi driver
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2010-2018 O.Sezer <sezero@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "quakedef.h"
#include "snd_codec.h"
#include "snd_codeci.h"
#include "bgmusic.h"

#define MUSIC_DIRNAME	"music"

qboolean	bgmloop;
cvar_t		bgm_extmusic = {"bgm_extmusic", "1", CVAR_ARCHIVE};


extern qboolean muted; // woods #usermute #mute

static qboolean	no_extmusic= false;
static float	old_volume = -1.0f;

music_handler_t wanted_handlers[] = // woods #musiclist remove static
{
	{ CODECTYPE_VORBIS,BGM_STREAMER,-1,  "ogg", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_OPUS, BGM_STREAMER, -1, "opus", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MP3,  BGM_STREAMER, -1,  "mp3", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_FLAC, BGM_STREAMER, -1, "flac", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_WAV,  BGM_STREAMER, -1,  "wav", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MOD,  BGM_STREAMER, -1,  "it",  MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MOD,  BGM_STREAMER, -1,  "s3m", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MOD,  BGM_STREAMER, -1,  "xm",  MUSIC_DIRNAME, NULL },
	{ CODECTYPE_MOD,  BGM_STREAMER, -1,  "mod", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_UMX,  BGM_STREAMER, -1,  "umx", MUSIC_DIRNAME, NULL },
	{ CODECTYPE_NONE, BGM_NONE,     -1,   NULL,         NULL,  NULL }
};

static music_handler_t *music_handlers = NULL;

#define ANY_CODECTYPE	0xFFFFFFFF
#define CDRIP_TYPES	(CODECTYPE_VORBIS | CODECTYPE_MP3 | CODECTYPE_FLAC | CODECTYPE_WAV | CODECTYPE_OPUS)
#define CDRIPTYPE(x)	(((x) & CDRIP_TYPES) != 0)

static snd_stream_t *bgmstream = NULL;
static snd_stream_t *bgmpreviewstream = NULL;
static bgm_preview_state_t bgmpreview;
static qboolean bgmpreview_resume_background;
static qboolean bgmpreview_resume_cd;
static qboolean bgmpreview_exclusive;

static void BGM_Preview_PauseBackground(void)
{
	if (bgmstream && bgmstream->status == STREAM_PLAY)
	{
		bgmstream->status = STREAM_PAUSE;
		bgmstream->volume = 0.f;
		bgmpreview_resume_background = true;
	}
	if (CDAudio_IsPlaying())
	{
		CDAudio_Pause();
		bgmpreview_resume_cd = true;
	}
	s_rawend = 0;
}



static void BGM_Preview_ResumeBackground(void)
{
	if (bgmpreview_resume_background && bgmstream &&
		bgmstream->status == STREAM_PAUSE)
		BGM_Resume();
	if (bgmpreview_resume_cd)
		CDAudio_Resume();
	bgmpreview_resume_background = false;
	bgmpreview_resume_cd = false;
	s_rawend = 0;
}

static void BGM_Preview_Close(qboolean resume_background)
{
	qboolean had_preview = bgmpreviewstream != NULL;

	if (bgmpreviewstream)
	{
		bgmpreviewstream->status = STREAM_NONE;
		S_CodecCloseStream(bgmpreviewstream);
		bgmpreviewstream = NULL;
	}
	if (had_preview)
		s_rawend = 0;
	if (resume_background && !bgmpreview_exclusive)
		BGM_Preview_ResumeBackground();
	else if (!resume_background && !bgmpreview_exclusive)
	{
		if (bgmpreview_resume_cd)
			CDAudio_Stop();
		bgmpreview_resume_background = false;
		bgmpreview_resume_cd = false;
	}
	bgmpreview.status = BGM_PREVIEW_STOPPED;
}

void BGM_Preview_SetExclusive(qboolean exclusive)
{
	if (bgmpreview_exclusive == exclusive)
		return;
	bgmpreview_exclusive = exclusive;
	if (exclusive)
		BGM_Preview_PauseBackground();
	else if (!bgmpreviewstream)
		BGM_Preview_ResumeBackground();
}

void BGM_Preview_Stop(void)
{
	BGM_Preview_Close(true);
	bgmpreview.error[0] = 0;
}

void BGM_Preview_Release(void)
{
	/* Clear exclusivity before closing so any suspended map stream resumes. */
	bgmpreview_exclusive = false;
	BGM_Preview_Close(true);
	memset(&bgmpreview, 0, sizeof(bgmpreview));
	bgmpreview.gain = 1.0f;
}

qboolean BGM_Preview_Play(const char *filename, float gain, qboolean loop)
{
	music_handler_t *handler;
	const char *ext;
	char path[MAX_QPATH];

	BGM_Preview_Close(true);
	memset(&bgmpreview, 0, sizeof(bgmpreview));
	bgmpreview.gain = isfinite(gain) ? CLAMP(0.0f, gain, 1.0f) : 1.0f;
	bgmpreview.loop = loop;
	if (!filename || !*filename)
		goto invalid;
	q_strlcpy(bgmpreview.name, filename, sizeof(bgmpreview.name));
	ext = COM_FileGetExtension(filename);
	for (handler = music_handlers; handler; handler = handler->next)
		if (handler->is_available && !q_strcasecmp(ext, handler->ext))
			break;
	if (!handler)
		goto unsupported;
	if ((size_t)q_snprintf(path, sizeof(path), "%s/%s", handler->dir, filename) >= sizeof(path))
		goto toolong;
	bgmpreviewstream = S_CodecOpenStreamType(path, handler->type, loop);
	if (!bgmpreviewstream)
		goto failed;
	if (bgmpreviewstream->info.rate <= 0 || bgmpreviewstream->info.rate > 384000 ||
		(bgmpreviewstream->info.width != 1 && bgmpreviewstream->info.width != 2) ||
		(bgmpreviewstream->info.channels != 1 && bgmpreviewstream->info.channels != 2))
	{
		S_CodecCloseStream(bgmpreviewstream);
		bgmpreviewstream = NULL;
		goto invalidstream;
	}

	BGM_Preview_PauseBackground();
	bgmpreview.rate = bgmpreviewstream->info.rate;
	bgmpreview.bits = bgmpreviewstream->info.bits ? bgmpreviewstream->info.bits : bgmpreviewstream->info.width * 8;
	bgmpreview.channels = bgmpreviewstream->info.channels;
	bgmpreview.total_samples = q_max(0, bgmpreviewstream->info.samples);
	bgmpreview.seekable = bgmpreview.total_samples > 0 && S_CodecCanSeekStream(bgmpreviewstream);
	bgmpreview.status = BGM_PREVIEW_PLAYING;
	return true;

invalid:
	q_strlcpy(bgmpreview.error, "invalid music path", sizeof(bgmpreview.error));
	goto error;
unsupported:
	q_strlcpy(bgmpreview.error, "unsupported music format", sizeof(bgmpreview.error));
	goto error;
toolong:
	q_strlcpy(bgmpreview.error, "music path too long", sizeof(bgmpreview.error));
	goto error;
failed:
	q_strlcpy(bgmpreview.error, "stream open failed", sizeof(bgmpreview.error));
	goto error;
invalidstream:
	q_strlcpy(bgmpreview.error, "invalid stream metadata", sizeof(bgmpreview.error));
error:
	bgmpreview.status = BGM_PREVIEW_FAILED;
	return false;
}

void BGM_Preview_SetPaused(qboolean paused)
{
	if (!bgmpreviewstream)
		return;
	if (paused && bgmpreviewstream->status == STREAM_PLAY)
	{
		bgmpreviewstream->status = STREAM_PAUSE;
		bgmpreview.status = BGM_PREVIEW_PAUSED;
	}
	else if (!paused && bgmpreviewstream->status == STREAM_PAUSE)
	{
		bgmpreviewstream->status = STREAM_PLAY;
		bgmpreview.status = BGM_PREVIEW_PLAYING;
	}
}

void BGM_Preview_SetLoop(qboolean loop)
{
	bgmpreview.loop = loop;
	if (bgmpreviewstream)
		bgmpreviewstream->loop = loop;
}

void BGM_Preview_SetGain(float gain)
{
	if (isfinite(gain))
		bgmpreview.gain = CLAMP(0.0f, gain, 1.0f);
}

qboolean BGM_Preview_Seek(double fraction)
{
	int64_t sample;

	if (!isfinite(fraction) || !bgmpreviewstream || !bgmpreview.seekable ||
		bgmpreview.total_samples <= 0)
		return false;
	fraction = CLAMP(0.0, fraction, 1.0);
	sample = (int64_t)(fraction * (bgmpreview.total_samples - 1) + 0.5);
	if (S_CodecSeekStream(bgmpreviewstream, sample) != 0)
		return false;
	bgmpreview.position_samples = sample;
	s_rawend = 0;
	return true;
}

void BGM_Preview_GetState(bgm_preview_state_t *state)
{
	int64_t queued;

	if (!state)
		return;
	*state = bgmpreview;
	if (!bgmpreviewstream || state->rate <= 0 || !shm || shm->speed <= 0)
		return;

	/* position_samples tracks source frames decoded into the raw ring. Convert
	 * the mixer-rate frames that are queued but not heard back to source-rate
	 * frames before exposing the playhead to the UI. */
	queued = q_max(0, s_rawend - paintedtime);
	queued = (int64_t)(queued * ((double)state->rate / shm->speed) + 0.5);
	state->position_samples -= queued;
	if (state->loop && state->total_samples > 0)
	{
		state->position_samples %= state->total_samples;
		if (state->position_samples < 0)
			state->position_samples += state->total_samples;
	}
	else
		state->position_samples = CLAMP(0, state->position_samples, state->total_samples);
}

static void BGM_Play_f (void)
{
	if (Cmd_Argc() == 2) {
		BGM_Play (Cmd_Argv(1));
	}
	else {
		if (bgmstream)
		{
			char path[MAX_QPATH];
			COM_StripExtension (COM_SkipPath (bgmstream->name), path, sizeof (path));
			Con_Printf ("Playing %s, use 'music <musicfile>' to change\n", path);
		}
		else
			Con_Printf ("music <musicfile>\n");
	}
}

static void BGM_Pause_f (void)
{
	BGM_Pause ();
}

static void BGM_Resume_f (void)
{
	BGM_Resume ();
}

static void BGM_Loop_f (void)
{
	if (Cmd_Argc() == 2) {
		if (q_strcasecmp(Cmd_Argv(1),  "0") == 0 ||
		    q_strcasecmp(Cmd_Argv(1),"off") == 0)
			bgmloop = false;
		else if (q_strcasecmp(Cmd_Argv(1), "1") == 0 ||
			 q_strcasecmp(Cmd_Argv(1),"on") == 0)
			bgmloop = true;
		else if (q_strcasecmp(Cmd_Argv(1),"toggle") == 0)
			bgmloop = !bgmloop;

		if (bgmstream) bgmstream->loop = bgmloop;
	}

	if (bgmloop)
		Con_Printf("Music will be looped\n");
	else
		Con_Printf("Music will not be looped\n");
}

static void BGM_Stop_f (void)
{
	BGM_Stop();
}

static void BGM_Preview_f(void)
{
	const char *arg;
	bgm_preview_state_t state;

	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: music_preview <path>|pause|resume|stop|info|loop 0|1|gain 0..1|seek 0..1\n");
		return;
	}
	arg = Cmd_Argv(1);
	if (!q_strcasecmp(arg, "pause"))
		BGM_Preview_SetPaused(true);
	else if (!q_strcasecmp(arg, "resume"))
		BGM_Preview_SetPaused(false);
	else if (!q_strcasecmp(arg, "stop"))
		BGM_Preview_Stop();
	else if (!q_strcasecmp(arg, "loop"))
	{
		if (Cmd_Argc() != 3)
			Con_Printf("usage: music_preview loop 0|1\n");
		else
			BGM_Preview_SetLoop(atof(Cmd_Argv(2)) != 0.0f);
	}
	else if (!q_strcasecmp(arg, "gain"))
	{
		if (Cmd_Argc() != 3)
			Con_Printf("usage: music_preview gain 0..1\n");
		else
			BGM_Preview_SetGain(atof(Cmd_Argv(2)));
	}
	else if (!q_strcasecmp(arg, "seek"))
	{
		if (Cmd_Argc() != 3)
			Con_Printf("usage: music_preview seek 0..1\n");
		else if (!BGM_Preview_Seek(atof(Cmd_Argv(2))))
			Con_Printf("music preview is not seekable\n");
	}
	else if (!q_strcasecmp(arg, "info"))
	{
		BGM_Preview_GetState(&state);
		Con_Printf("music preview: %s, %lld/%lld samples, %d Hz %d-bit %dch, gain %.0f%%, loop %s",
			state.name[0] ? state.name : "none", (long long)state.position_samples,
			(long long)state.total_samples, state.rate, state.bits, state.channels,
			state.gain * 100.0f, state.loop ? "on" : "off");
		if (state.error[0])
			Con_Printf(" (%s)", state.error);
		Con_Printf("\n");
	}
	else
		BGM_Preview_Play(arg, bgmpreview.gain, bgmpreview.loop);
}

static void BGM_Jump_f (void)
{
	if (Cmd_Argc() != 2) {
		Con_Printf ("music_jump <ordernum>\n");
	}
	else if (bgmstream) {
		S_CodecJumpToOrder(bgmstream, atoi(Cmd_Argv(1)));
	}
}

void BGM_RefreshCodecHandlers (void)
{
	music_handler_t *handlers = NULL;
	int i;

	music_handlers = NULL;

	for (i = 0; wanted_handlers[i].type != CODECTYPE_NONE; i++)
	{
		wanted_handlers[i].next = NULL;

		switch (wanted_handlers[i].player)
		{
		case BGM_MIDIDRV:
		/* not supported in quake */
			break;
		case BGM_STREAMER:
			wanted_handlers[i].is_available =
				S_CodecIsAvailable(wanted_handlers[i].type);
			break;
		case BGM_NONE:
		default:
			break;
		}
		if (wanted_handlers[i].is_available != -1)
		{
			if (handlers)
			{
				handlers->next = &wanted_handlers[i];
				handlers = handlers->next;
			}
			else
			{
				music_handlers = &wanted_handlers[i];
				handlers = music_handlers;
			}
		}
	}
}

qboolean BGM_Init (void)
{
	Cvar_RegisterVariable(&bgm_extmusic);
	Cmd_AddCommand("music", BGM_Play_f);
	Cmd_AddCommand("music_pause", BGM_Pause_f);
	Cmd_AddCommand("music_resume", BGM_Resume_f);
	Cmd_AddCommand("music_loop", BGM_Loop_f);
	Cmd_AddCommand("music_stop", BGM_Stop_f);
	Cmd_AddCommand("music_preview", BGM_Preview_f);
	Cmd_AddCommand("music_jump", BGM_Jump_f);

	if (COM_CheckParm("-noextmusic") != 0)
		no_extmusic = true;

	bgmloop = true;
	bgmpreview.gain = 1.0f;

	BGM_RefreshCodecHandlers ();

	return true;
}

void BGM_Shutdown (void)
{
	BGM_Preview_Release();
	BGM_Stop();
/* sever our connections to
 * midi_drv and snd_codec */
	music_handlers = NULL;
}

static void BGM_Play_noext (const char *filename, unsigned int allowed_types)
{
	char tmp[MAX_QPATH];
	music_handler_t *handler;

	handler = music_handlers;
	while (handler)
	{
		if (! (handler->type & allowed_types))
		{
			handler = handler->next;
			continue;
		}
		if (!handler->is_available)
		{
			handler = handler->next;
			continue;
		}
		q_snprintf(tmp, sizeof(tmp), "%s/%s.%s",
			   handler->dir, filename, handler->ext);
		switch (handler->player)
		{
		case BGM_MIDIDRV:
		/* not supported in quake */
			break;
		case BGM_STREAMER:
			bgmstream = S_CodecOpenStreamType(tmp, handler->type, bgmloop);
			if (bgmstream)
				return;		/* success */
			break;
		case BGM_NONE:
		default:
			break;
		}
		handler = handler->next;
	}

	Con_Printf("Couldn't handle music file %s\n", filename);
}

void BGM_Play (const char *filename)
{
	char tmp[MAX_QPATH];
	const char *ext;
	music_handler_t *handler;

	BGM_Preview_Close(false);
	BGM_Stop();

	if (music_handlers == NULL)
		return;

	if (!filename || !*filename)
	{
		Con_DPrintf("null music file name\n");
		return;
	}

	ext = COM_FileGetExtension(filename);
	if (! *ext)	/* try all things */
	{
		BGM_Play_noext(filename, ANY_CODECTYPE);
		if (bgmpreview_exclusive)
			BGM_Preview_PauseBackground();
		return;
	}

	handler = music_handlers;
	while (handler)
	{
		if (handler->is_available &&
		    !q_strcasecmp(ext, handler->ext))
			break;
		handler = handler->next;
	}
	if (!handler)
	{
		Con_Printf("Unhandled extension for %s\n", filename);
		return;
	}
	q_snprintf(tmp, sizeof(tmp), "%s/%s", handler->dir, filename);
	switch (handler->player)
	{
	case BGM_MIDIDRV:
	/* not supported in quake */
		break;
	case BGM_STREAMER:
		bgmstream = S_CodecOpenStreamType(tmp, handler->type, bgmloop);
		if (bgmstream)
		{
			if (bgmpreview_exclusive)
				BGM_Preview_PauseBackground();
			return;		/* success */
		}
		break;
	case BGM_NONE:
	default:
		break;
	}

	Con_Printf("Couldn't handle music file %s\n", filename);
}

void BGM_PlayCDtrack (byte track, qboolean looping)
{
/* instead of searching by the order of music_handlers, do so by
 * the order of searchpath priority: the file from the searchpath
 * with the highest path_id is most likely from our own gamedir
 * itself. This way, if a mod has track02 as a *.mp3 file, which
 * is below *.ogg in the music_handler order, the mp3 will still
 * have priority over track02.ogg from, say, id1.
 */
	char tmp[MAX_QPATH];
	const char *ext;
	unsigned int path_id, prev_id, type;
	music_handler_t *handler;

	/* if replaying the same track, just resume playing instead of stopping and restarting*/
	BGM_Preview_Close(false);
	if (bgmstream)
	{
		q_snprintf (tmp, sizeof (tmp), "%s/track%02d.%s", MUSIC_DIRNAME, track, bgmstream->codec->ext);
		if (strcmp (tmp, bgmstream->name) == 0)
		{
			if (!bgmpreview_exclusive)
				BGM_Resume ();
			else
				BGM_Preview_PauseBackground();
			return;
		}
	}

	BGM_Stop();
	if (CDAudio_Play(track, looping) == 0)
	{
		if (bgmpreview_exclusive)
			BGM_Preview_PauseBackground();
		return;			/* success */
	}

	if (music_handlers == NULL)
		return;

	if (no_extmusic || !bgm_extmusic.value)
		return;

	prev_id = 0;
	type = 0;
	ext  = NULL;
	handler = music_handlers;
	while (handler)
	{
		if (! handler->is_available)
			goto _next;
	//	if (! CDRIPTYPE(handler->type))
	//		goto _next;
		q_snprintf(tmp, sizeof(tmp), "%s/track%02d.%s",
				MUSIC_DIRNAME, (int)track, handler->ext);
		if (! COM_FileExists(tmp, &path_id))
			goto _next;
		if (path_id > prev_id)
		{
			prev_id = path_id;
			type = handler->type;
			ext = handler->ext;
		}
	_next:
		handler = handler->next;
	}
	if (ext == NULL)
	{
		if (track != 0) // woods
			Con_Printf("Couldn't find a cdrip for track %d\n", (int)track);
		else
			Con_DPrintf("Skipped invalid track 0 request\n");
	}
	else
	{
		q_snprintf(tmp, sizeof(tmp), "%s/track%02d.%s",
				MUSIC_DIRNAME, (int)track, ext);
		bgmstream = S_CodecOpenStreamType(tmp, type, bgmloop);
		if (! bgmstream)
			Con_Printf("Couldn't handle music file %s\n", tmp);
		else if (bgmpreview_exclusive)
			BGM_Preview_PauseBackground();
	}
}

void BGM_Stop (void)
{
	BGM_Preview_Close(false);
	if (bgmpreview_exclusive)
	{
		if (bgmpreview_resume_cd)
			CDAudio_Stop();
		bgmpreview_resume_background = false;
		bgmpreview_resume_cd = false;
	}
	if (bgmstream)
	{
		bgmstream->status = STREAM_NONE;
		S_CodecCloseStream(bgmstream);
		bgmstream = NULL;
		s_rawend = 0;
	}
}

void BGM_Pause (void)
{
	if (bgmpreviewstream || bgmpreview_exclusive)
		bgmpreview_resume_background = false;
	if (bgmstream)
	{
		if (bgmstream->status == STREAM_PLAY)
		{
			bgmstream->status = STREAM_PAUSE;
			bgmstream->volume = 0.f;
		}
	}
}

void BGM_Resume (void)
{
	if (bgmpreview_exclusive)
		return;
	if (bgmstream)
	{
		if (bgmstream->status == STREAM_PAUSE)
			bgmstream->status = STREAM_PLAY;
	}
}

static qboolean BGM_UpdateStream(snd_stream_t *stream, qboolean loop, float gain,
	qboolean preview)
{
	qboolean did_rewind = false;
	int	res;	/* Number of bytes read. */
	int	bufferSamples;
	int	fileSamples;
	int	fileBytes;
	byte	raw[16384];

	if (muted || stream->status != STREAM_PLAY) // woods #usermute #mute
		return true;

	/* don't bother playing anything if musicvolume is 0 */
	if (bgmvolume.value <= 0)
		return true;

	/* see how many samples should be copied into the raw buffer */
	if (s_rawend < paintedtime)
		s_rawend = paintedtime;

	while (s_rawend < paintedtime + MAX_RAW_SAMPLES)
	{
		bufferSamples = MAX_RAW_SAMPLES - (s_rawend - paintedtime);

		/* ramp up volume after stream was paused */
		if (stream->volume < 1.f)
		{
			stream->volume += bufferSamples / (stream->info.rate * 1.f);
			stream->volume = q_min (1.f, stream->volume);
		}

		/* decide how much data needs to be read from the file */
		fileSamples = bufferSamples * stream->info.rate / shm->speed;
		if (!fileSamples)
			return true;

		/* our max buffer size */
		fileBytes = fileSamples * (stream->info.width * stream->info.channels);
		if (fileBytes > (int) sizeof(raw))
		{
			fileBytes = (int) sizeof(raw);
			fileSamples = fileBytes /
					  (stream->info.width * stream->info.channels);
		}

		/* Read */
		res = S_CodecReadStream(stream, fileBytes, raw);
		if (res < fileBytes)
		{
			fileBytes = res;
			fileSamples = res / (stream->info.width * stream->info.channels);
		}

		if (res > 0)	/* data: add to raw buffer */
		{
			S_RawSamples(fileSamples, stream->info.rate,
							stream->info.width,
							stream->info.channels,
							raw, bgmvolume.value * stream->volume * gain);
			if (preview)
				bgmpreview.position_samples += fileSamples;
			did_rewind = false;
		}
		else if (res == 0)	/* EOF */
		{
			if (loop)
			{
				if (did_rewind)
				{
					Con_Printf("Stream keeps returning EOF.\n");
					return false;
				}

				res = S_CodecRewindStream(stream);
				if (res != 0)
				{
					Con_Printf("Stream seek error (%i), stopping.\n", res);
					return false;
				}
				if (preview)
					bgmpreview.position_samples = 0;
				did_rewind = true;
			}
			else
			{
				return false;
			}
		}
		else	/* res < 0: some read error */
		{
			Con_Printf("Stream read error (%i), stopping.\n", res);
			return false;
		}
	}
	return true;
}

void BGM_Update (void)
{
	if (old_volume != bgmvolume.value)
	{
		if (bgmvolume.value < 0)
			Cvar_SetQuick (&bgmvolume, "0");
		else if (bgmvolume.value > 1)
			Cvar_SetQuick (&bgmvolume, "1");
		old_volume = bgmvolume.value;
	}
	if (bgmpreviewstream)
	{
		if (!BGM_UpdateStream(bgmpreviewstream, bgmpreview.loop, bgmpreview.gain, true))
			BGM_Preview_Close(true);
	}
	else if (bgmstream && !bgmpreview_exclusive)
	{
		if (!BGM_UpdateStream(bgmstream, bgmloop, 1.0f, false))
			BGM_Stop();
	}
}

