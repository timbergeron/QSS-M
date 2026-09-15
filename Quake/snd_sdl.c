/*
 * snd_sdl.c - SDL audio driver for Hexen II: Hammer of Thyrion (uHexen2)
 * based on implementations found in the quakeforge and ioquake3 projects.
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2005-2012 O.Sezer <sezero@users.sourceforge.net>
 * Copyright (C) 2010-2014 QuakeSpasm developers
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
 */

#include "quakedef.h"

#include <SDL3/SDL.h>

static int	buffersize;

#define SND_MIX_CHANNELS 2

/* The engine's ring holds this many device periods of interleaved stereo. */
#define SND_RING_PERIODS 10

/* The engine always mixes in stereo. When the output device has more
 * speakers (e.g. 4.0/5.1/7.1), we up-mix stereo to that layout in the
 * stream callback so every speaker is driven. */
static int	device_channels = 2;

static SDL_AudioStream	*sdl_stream;
static Uint8		*sdl_scratch;		/* one callback chunk of device frames */
static int			sdl_scratch_bytes;
static SDL_AtomicInt	sdl_stream_failed;	/* set by the callback, reported by the main thread */
static int			snd_playback_frames;	/* the playback period request, restored after other opens */
static char			sdl_devicename[128];

static int SND_Scaled16 (int sample, int scale)
{
	return (sample * scale) >> 8;
}


static int SND_ScaledU8 (int sample, int scale)
{
	int centered = sample - 128;

	return CLAMP (0, ((centered * scale) >> 8) + 128, 255);
}

static int SND_ScaledS8 (int sample, int scale)
{
	return CLAMP (-128, (sample * scale) >> 8, 127);
}

static void SND_CopyScaled (Uint8 *dst, const Uint8 *src, int len)
{
	int scale = S_GetMasterVolumeScale ();

	if (scale >= 256)
	{
		memcpy (dst, src, len);
		return;
	}
	if (scale <= 0)
	{
		memset (dst, (shm->samplebits == 8 && !shm->signed8) ? 128 : 0, len);
		return;
	}

	if (shm->samplebits == 16)
	{
		const short *in = (const short *)src;
		short *out = (short *)dst;
		int samples = len / (int)sizeof(*out);
		int i;

		for (i = 0; i < samples; i++)
			out[i] = (short)SND_Scaled16 (in[i], scale);
	}
	else if (!shm->signed8)
	{
		int i;

		for (i = 0; i < len; i++)
			dst[i] = (Uint8)SND_ScaledU8 (src[i], scale);
	}
	else
	{
		const signed char *in = (const signed char *)src;
		signed char *out = (signed char *)dst;
		int i;

		for (i = 0; i < len; i++)
			out[i] = (signed char)SND_ScaledS8 (in[i], scale);
	}
}

/* Expand one stereo frame (l,r) into a device frame and advance the output
 * pointer. Channel order matches SDL's default layouts:
 *   1: mono
 *   2: FL FR
 *   3: FL FR LFE
 *   4: FL FR BL BR
 *   5: FL FR LFE BL BR
 *   6: FL FR FC LFE SL SR
 *   7: FL FR FC LFE BC SL SR
 *   8: FL FR FC LFE BL BR SL SR
 * Unknown counts get L/R in the first two channels and silence elsewhere. */
static short *SND_Upmix16 (short *out, int l, int r, int chans)
{
	int	c = (l + r) / 2;
	switch (chans)
	{
	case 1: *out++ = (short) c; break;
	case 2: *out++ = (short) l; *out++ = (short) r; break;
	case 3: *out++ = (short) l; *out++ = (short) r; *out++ = 0; break;
	case 4: *out++ = (short) l; *out++ = (short) r; *out++ = (short) l; *out++ = (short) r; break;
	case 5: *out++ = (short) l; *out++ = (short) r; *out++ = 0; *out++ = (short) l; *out++ = (short) r; break;
	case 6: *out++ = (short) l; *out++ = (short) r; *out++ = (short) c; *out++ = 0; *out++ = (short) l; *out++ = (short) r; break;
	case 7: *out++ = (short) l; *out++ = (short) r; *out++ = (short) c; *out++ = 0; *out++ = (short) c; *out++ = (short) l; *out++ = (short) r; break;
	case 8: *out++ = (short) l; *out++ = (short) r; *out++ = (short) c; *out++ = 0; *out++ = (short) l; *out++ = (short) r; *out++ = (short) l; *out++ = (short) r; break;
	default:
		{
			int i;
			*out++ = (short) l;
			if (chans > 1) *out++ = (short) r;
			for (i = 2; i < chans; i++) *out++ = 0;
		}
		break;
	}
	return out;
}

/* 8-bit unsigned: silence is 128. */
static unsigned char *SND_Upmix8 (unsigned char *out, int l, int r, int chans)
{
	int	c = (l + r) / 2;
	switch (chans)
	{
	case 1: *out++ = c; break;
	case 2: *out++ = l; *out++ = r; break;
	case 3: *out++ = l; *out++ = r; *out++ = 128; break;
	case 4: *out++ = l; *out++ = r; *out++ = l; *out++ = r; break;
	case 5: *out++ = l; *out++ = r; *out++ = 128; *out++ = l; *out++ = r; break;
	case 6: *out++ = l; *out++ = r; *out++ = c; *out++ = 128; *out++ = l; *out++ = r; break;
	case 7: *out++ = l; *out++ = r; *out++ = c; *out++ = 128; *out++ = c; *out++ = l; *out++ = r; break;
	case 8: *out++ = l; *out++ = r; *out++ = c; *out++ = 128; *out++ = l; *out++ = r; *out++ = l; *out++ = r; break;
	default:
		{
			int i;
			*out++ = l;
			if (chans > 1) *out++ = r;
			for (i = 2; i < chans; i++) *out++ = 128;
		}
		break;
	}
	return out;
}

static void SDLCALL paint_audio (void *unused, Uint8 *stream, int len)
{
	int	pos, tobufend;
	int	len1, len2;
	int	silence;

	if (!shm)
	{	/* shouldn't happen, but just in case */
		memset(stream, 0, len);
		return;
	}

	silence = (shm->samplebits == 8 && !shm->signed8) ? 128 : 0;
	if (device_channels <= 0)
	{
		memset(stream, silence, len);
		return;
	}

	/* Fast path: device layout matches our mix buffer, just copy the ring. */
	if (device_channels == shm->channels)
	{
		pos = (shm->samplepos * (shm->samplebits / 8));
		if (pos >= buffersize)
			shm->samplepos = pos = 0;

		tobufend = buffersize - pos;  /* bytes to buffer's end. */
		len1 = len;
		len2 = 0;

		if (len1 > tobufend)
		{
			len1 = tobufend;
			len2 = len - len1;
		}

		SND_CopyScaled(stream, shm->buffer + pos, len1);

		if (len2 <= 0)
		{
			shm->samplepos += (len1 / (shm->samplebits / 8));
		}
		else
		{	/* wraparound? */
			SND_CopyScaled(stream + len1, shm->buffer, len2);
			shm->samplepos = (len2 / (shm->samplebits / 8));
		}

		if (shm->samplepos >= shm->samples)
			shm->samplepos = 0;
		return;
	}

	/* Up-mix path: read stereo frames from the ring buffer and expand each
	 * one to the device's channel layout. samplepos counts interleaved
	 * (stereo) samples, advancing by 2 per consumed frame, matching the
	 * fast path so S_GetDMAPos stays consistent. */
	{
		int	bps = shm->samplebits / 8;
		int	in_samples = buffersize / bps;	/* total samples in the ring */
		int	frame_bytes = device_channels * bps;
		int	out_frames = len / frame_bytes;
		int scale = S_GetMasterVolumeScale ();
		int	f, sp, l, r;

		memset(stream, silence, len);
		if (in_samples < SND_MIX_CHANNELS)
			return;

		if (shm->samplebits == 16)
		{
			const short	*in = (const short *) shm->buffer;
			short		*out = (short *) stream;
			for (f = 0; f < out_frames; f++)
			{
				sp = shm->samplepos;
				if (sp < 0 || sp >= in_samples)
					sp = 0;
				l = in[sp];
				r = in[(sp + 1) % in_samples];
				if (scale < 256)
				{
					l = (scale <= 0) ? 0 : SND_Scaled16 (l, scale);
					r = (scale <= 0) ? 0 : SND_Scaled16 (r, scale);
				}
				out = SND_Upmix16 (out, l, r, device_channels);
				sp += SND_MIX_CHANNELS;
				if (sp >= in_samples)
					sp -= in_samples;
				shm->samplepos = sp;
			}
		}
		else	/* 8-bit unsigned */
		{
			const unsigned char	*in = shm->buffer;
			unsigned char		*out = stream;
			for (f = 0; f < out_frames; f++)
			{
				sp = shm->samplepos;
				if (sp < 0 || sp >= in_samples)
					sp = 0;
				l = in[sp];
				r = in[(sp + 1) % in_samples];
				if (scale < 256)
				{
					l = (scale <= 0) ? 128 : SND_ScaledU8 (l, scale);
					r = (scale <= 0) ? 128 : SND_ScaledU8 (r, scale);
				}
				out = SND_Upmix8 (out, l, r, device_channels);
				sp += SND_MIX_CHANNELS;
				if (sp >= in_samples)
					sp -= in_samples;
				shm->samplepos = sp;
			}
		}

		if (shm->samplepos >= in_samples)
			shm->samplepos = 0;
	}
}

/*
================
SND_StreamCallback

SDL asks for arbitrary byte counts, which may exceed the ring. Paint whole
device frames in chunks no larger than the preallocated scratch buffer, which
never holds more than one ring's worth of frames, so paint_audio's two-copy
path cannot run past the ring. SDL holds the stream lock during the callback.
================
*/
static void SDLCALL SND_StreamCallback (void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	int	frame_bytes;

	(void)userdata;
	(void)total_amount;

	if (!shm || !sdl_scratch || device_channels <= 0)
		return;

	frame_bytes = device_channels * (shm->samplebits / 8);
	while (additional_amount > 0)
	{
		/* sdl_scratch_bytes is a whole number of frames, so rounding up fits */
		int	chunk = q_min (additional_amount, sdl_scratch_bytes);

		chunk = (chunk + frame_bytes - 1) / frame_bytes * frame_bytes;
		paint_audio (NULL, sdl_scratch, chunk);
		if (!SDL_PutAudioStreamData (stream, sdl_scratch, chunk))
		{
			SDL_CompareAndSwapAtomicInt (&sdl_stream_failed, 0, 1);
			return;
		}
		additional_amount -= chunk;
	}
}

/*
================
SND_PeriodFrames

Engine-owned device period for a mix rate, independent of what the device
reports back.
================
*/
static int SND_PeriodFrames (int rate)
{
	if (rate <= 11025)
		return 256;
	if (rate <= 22050)
		return 512;
	/* Keep the common 44.1/48 kHz rates near the same 21-23 ms period. */
	if (rate <= 48000)
		return 1024;
	if (rate <= 56000)
		return 2048;
	return 4096; /* for 96 kHz */
}

/*
================
SND_RingSamples

Interleaved stereo samples in the engine ring: SND_RING_PERIODS periods,
rounded up to a power of two.
================
*/
static int SND_RingSamples (int period_frames)
{
	int	samples = period_frames * SND_MIX_CHANNELS * SND_RING_PERIODS;
	int	val = 1;

	while (val < samples)
		val <<= 1;
	return val;
}

/*
================
SND_OpenAudioStream

Open an audio stream while requesting a device period of sample_frames.
SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES is best effort, and a value from the
environment still wins over this default-priority request. The playback
request is restored afterwards, so opening a capture stream never changes
later playback opens. Main thread only.
================
*/
SDL_AudioStream *SND_OpenAudioStream (SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
	SDL_AudioStreamCallback callback, void *userdata, int sample_frames)
{
	SDL_AudioStream	*stream;
	char	frames[16];

	q_snprintf (frames, sizeof(frames), "%d", sample_frames);
	SDL_SetHintWithPriority (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames, SDL_HINT_DEFAULT);
	stream = SDL_OpenAudioDeviceStream (device, spec, callback, userdata);

	if (snd_playback_frames > 0)
	{
		q_snprintf (frames, sizeof(frames), "%d", snd_playback_frames);
		SDL_SetHintWithPriority (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames, SDL_HINT_DEFAULT);
	}
	else
	{
		SDL_ResetHint (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
	}
	return stream;
}

/* The stream plays on the default output; name the physical device behind it. */
static void SND_UpdateDeviceName (void)
{
	/* SDL before 3.2.14 misinterprets logical device IDs here and can crash.
	 * Check the loaded runtime, since Linux supports SDL 3.2.12. */
	const char	*name = (sdl_stream && SDL_GetVersion () >= SDL_VERSIONNUM (3, 2, 14)) ?
		SDL_GetAudioDeviceName (SDL_GetAudioStreamDevice (sdl_stream)) : NULL;

	q_strlcpy (sdl_devicename, (name && *name) ? name : "System default", sizeof(sdl_devicename));
}

static int SND_GetPreferredOutputChannels (SDL_AudioDeviceID device)
{
	SDL_AudioSpec	spec;
	int	frames;

	if (SDL_GetAudioDeviceFormat (device, &spec, &frames) && spec.channels > SND_MIX_CHANNELS)
		return spec.channels;
	return SND_MIX_CHANNELS;
}

static void SND_FreeBuffers (void)
{
	if (shm)
	{
		free (shm->buffer);
		shm->buffer = NULL;
	}
	free (sdl_scratch);
	sdl_scratch = NULL;
	sdl_scratch_bytes = 0;
}

qboolean SNDDMA_Init (dma_t *dma)
{
	SDL_AudioSpec	spec;
	SDL_AudioSpec	device_spec;
	int		period_frames, device_frames;
	char	drivername[128];
	const char	*surround_status;

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		Con_Printf("Couldn't init SDL audio: %s\n", SDL_GetError());
		return false;
	}

	/* The stream's input format is what the callback produces: the engine's
	 * sample format and rate, with the device's channel count when surround
	 * is enabled. SDL converts it to whatever the device actually uses. */
	spec.freq = snd_mixspeed.value;
	spec.format = (loadas8bit.value) ? SDL_AUDIO_U8 : SDL_AUDIO_S16;
	spec.channels = SND_MIX_CHANNELS;
	if (snd_surround.value > 0)
		spec.channels = SND_GetPreferredOutputChannels (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
	device_channels = spec.channels;
	surround_status = (snd_surround.value > 0) ? "on" : "off";
	period_frames = SND_PeriodFrames (spec.freq);

	memset ((void *) dma, 0, sizeof(dma_t));
	shm = dma;

	/* Fill the audio DMA information block. The engine always mixes in
	 * stereo; the callback up-mixes to device_channels when they differ. */
	shm->samplebits = SDL_AUDIO_BITSIZE (spec.format);
	shm->signed8 = (spec.format == SDL_AUDIO_S8);
	shm->speed = spec.freq;
	shm->channels = SND_MIX_CHANNELS;
	shm->samples = SND_RingSamples (period_frames);
	shm->samplepos = 0;
	shm->submission_chunk = 1;

	/* Set up the ring and scratch storage before the stream can call back. */
	buffersize = shm->samples * (shm->samplebits / 8);
	shm->buffer = (unsigned char *) malloc (buffersize);
	sdl_scratch_bytes = q_min (period_frames, shm->samples / SND_MIX_CHANNELS) *
		device_channels * (shm->samplebits / 8);
	sdl_scratch = (Uint8 *) malloc (sdl_scratch_bytes);
	if (!shm->buffer || !sdl_scratch)
	{
		SND_FreeBuffers ();
		shm = NULL;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		Con_Printf ("Failed allocating memory for SDL audio\n");
		return false;
	}
	memset (shm->buffer, SDL_GetSilenceValueForFormat (spec.format), buffersize);

	SDL_SetAtomicInt (&sdl_stream_failed, 0);
	snd_playback_frames = period_frames;
	sdl_stream = SND_OpenAudioStream (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, SND_StreamCallback, NULL, period_frames);
	if (!sdl_stream)
	{
		Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
		snd_playback_frames = 0;
		SDL_ResetHint (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
		SND_FreeBuffers ();
		shm = NULL;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}
	SND_UpdateDeviceName ();

	Con_Printf ("SDL audio spec  : %d Hz, %d samples, %d mix channels (callback: %d ch, surround: %s)\n",
			shm->speed, period_frames, shm->channels, device_channels,
			surround_status);
	/* Diagnostics only: the ring and rate never follow what the device reports. */
	if (SDL_GetAudioDeviceFormat (SDL_GetAudioStreamDevice (sdl_stream), &device_spec, &device_frames))
		Con_Printf ("SDL audio device: %d Hz, %d ch, %s, %d sample frames\n",
				device_spec.freq, device_spec.channels,
				SDL_GetAudioFormatName (device_spec.format), device_frames);
	{
		const char *driver = SDL_GetCurrentAudioDriver();
		const char *device = SNDDMA_GetDeviceName();
		q_snprintf(drivername, sizeof(drivername), "%s - %s",
			driver != NULL ? driver : "(UNKNOWN)",
			device != NULL ? device : "(UNKNOWN)");
	}
	Con_Printf ("SDL audio driver: %s, %d bytes buffer\n", drivername, buffersize);

	if (!SDL_ResumeAudioStreamDevice (sdl_stream))
	{
		Con_Printf ("Couldn't start SDL audio: %s\n", SDL_GetError());
		SNDDMA_Shutdown ();
		return false;
	}

	return true;
}

int SNDDMA_GetDMAPos (void)
{
	return shm->samplepos;
}

void SNDDMA_Shutdown (void)
{
	if (shm)
	{
		Con_Printf ("Shutting down SDL sound\n");
		/* Destroying the stream stops its callback; free what it reads afterwards. */
		SDL_DestroyAudioStream (sdl_stream);
		sdl_stream = NULL;
		snd_playback_frames = 0;
		SDL_ResetHint (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
		sdl_devicename[0] = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		SND_FreeBuffers ();
		shm = NULL;
	}
}

void SNDDMA_LockBuffer (void)
{
	SDL_LockAudioStream (sdl_stream);
}

void SNDDMA_Submit (void)
{
	SDL_UnlockAudioStream (sdl_stream);
	/* The callback cannot print; report its first failure here, once. */
	if (SDL_CompareAndSwapAtomicInt (&sdl_stream_failed, 1, 2))
		Con_Warning ("SDL audio stream rejected mixed audio; sound may drop out\n");
}

void SNDDMA_BlockSound (void)
{
	SDL_PauseAudioStreamDevice (sdl_stream);
}

void SNDDMA_UnblockSound (void)
{
	if (!SDL_ResumeAudioStreamDevice (sdl_stream))
		Con_Warning ("Couldn't resume SDL audio: %s\n", SDL_GetError());
}

/*
================
SNDDMA_DeviceChanged

The default-output stream follows device changes on its own, converting our
frames for whatever device is current, so nothing is reopened here. With
surround on, refresh the up-mix layout to the current device's channel count;
the scratch buffer is resized outside the stream lock. Main thread only.
================
*/
void SNDDMA_DeviceChanged (void)
{
	SDL_AudioSpec	spec;
	Uint8	*scratch;
	int	bps, frames, channels, bytes;

	if (!shm || !sdl_stream)
		return;
	SND_UpdateDeviceName ();
	if (snd_surround.value <= 0)
		return;

	channels = SND_GetPreferredOutputChannels (SDL_GetAudioStreamDevice (sdl_stream));
	if (channels == device_channels)
		return;

	bps = shm->samplebits / 8;
	frames = sdl_scratch_bytes / (device_channels * bps);
	bytes = frames * channels * bps;
	scratch = (Uint8 *) malloc (bytes);
	if (!scratch)
		return;

	spec.format = (shm->samplebits == 8) ? (shm->signed8 ? SDL_AUDIO_S8 : SDL_AUDIO_U8) : SDL_AUDIO_S16;
	spec.channels = channels;
	spec.freq = shm->speed;

	SDL_LockAudioStream (sdl_stream);
	if (SDL_SetAudioStreamFormat (sdl_stream, &spec, NULL))
	{
		Uint8 *old = sdl_scratch;

		sdl_scratch = scratch;
		sdl_scratch_bytes = bytes;
		device_channels = channels;
		scratch = old;
	}
	SDL_UnlockAudioStream (sdl_stream);
	free (scratch);

	Con_DPrintf ("SDL audio: up-mixing to %d channels for %s\n", device_channels, sdl_devicename);
}

const char *SNDDMA_GetDeviceName (void)
{
	if (sdl_stream)
		SND_UpdateDeviceName ();
	return sdl_devicename[0] ? sdl_devicename : "System default";
}

