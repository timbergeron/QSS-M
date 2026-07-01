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

#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif

static int	buffersize;

#define SND_MIX_CHANNELS 2

/* The engine always mixes in stereo. When the SDL callback exposes more
 * speakers (e.g. 4.0/5.1/7.1), we up-mix stereo to that layout in the
 * callback so every speaker is driven. */
static int	device_channels = 2;

#if defined(USE_SDL2)
static SDL_AudioDeviceID	sdl_audiodevice;
#endif

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
	int scale = snd_mastervolume_scale;

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

#if defined(USE_SDL2)
static int SND_GetPreferredOutputChannels (void)
{
	SDL_AudioSpec spec;
	int	channels = SND_MIX_CHANNELS;

	SDL_zero(spec);
#if SDL_VERSION_ATLEAST(2, 24, 0)
	if (SDL_GetDefaultAudioInfo(NULL, &spec, SDL_FALSE) == 0 &&
		spec.channels > channels)
	{
		channels = spec.channels;
	}
#endif
#if SDL_VERSION_ATLEAST(2, 0, 16)
	SDL_zero(spec);
	if (SDL_GetNumAudioDevices(SDL_FALSE) > 0 &&
		SDL_GetAudioDeviceSpec(0, SDL_FALSE, &spec) == 0 &&
		spec.channels > channels)
	{
		channels = spec.channels;
	}
#endif

	return channels;
}
#endif

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
		int scale = snd_mastervolume_scale;
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

qboolean SNDDMA_Init (dma_t *dma)
{
	SDL_AudioSpec desired;
	int		tmp, val;
	char	drivername[128];
	const char	*surround_status;
#if defined(USE_SDL2)
	SDL_AudioSpec obtained;
	int		allowed_changes;
#endif

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
	{
		Con_Printf("Couldn't init SDL audio: %s\n", SDL_GetError());
		return false;
	}

	/* Set up the desired format */
	desired.freq = snd_mixspeed.value;
	desired.format = (loadas8bit.value) ? AUDIO_U8 : AUDIO_S16SYS;
	desired.channels = SND_MIX_CHANNELS;
#if defined(USE_SDL2)
	if (snd_surround.value > 0)
		desired.channels = SND_GetPreferredOutputChannels();
#endif
	if (desired.freq <= 11025)
		desired.samples = 256;
	else if (desired.freq <= 22050)
		desired.samples = 512;
	else if (desired.freq <= 44100)
		desired.samples = 1024;
	else if (desired.freq <= 56000)
		desired.samples = 2048; /* for 48 kHz */
	else
		desired.samples = 4096; /* for 96 kHz */
	desired.callback = paint_audio;
	desired.userdata = NULL;

#if defined(USE_SDL2)
	/* Keep the engine mix buffer stereo. If snd_surround is enabled, request
	 * the default output's preferred channel count and up-mix to SDL's
	 * callback layout. Set snd_surround 0 and snd_restart to force stereo. */
	allowed_changes = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
		SDL_AUDIO_ALLOW_SAMPLES_CHANGE;
	if (snd_surround.value > 0)
		allowed_changes |= SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
	sdl_audiodevice = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained,
		allowed_changes);
	if (sdl_audiodevice == 0)
	{
		Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}
	desired.freq = obtained.freq;
	desired.samples = obtained.samples;
	device_channels = obtained.channels;
	if (snd_surround.value <= 0 || device_channels < 1)
		device_channels = SND_MIX_CHANNELS;
	surround_status = (snd_surround.value > 0) ? "on" : "off";
#else
	/* Open the audio device. SDL 1.2 guarantees the requested stereo format
	 * (it converts internally), so no up-mixing is performed here. */
	if (SDL_OpenAudio(&desired, NULL) == -1)
	{
		Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}
	device_channels = desired.channels;
	surround_status = "unsupported";
#endif

	memset ((void *) dma, 0, sizeof(dma_t));
	shm = dma;

	/* Fill the audio DMA information block. The engine always mixes in
	 * stereo; the callback up-mixes to device_channels when they differ. */
	shm->samplebits = (desired.format & 0xFF); /* first byte of format is bits */
	shm->signed8 = (desired.format == AUDIO_S8);
	shm->speed = desired.freq;
	shm->channels = SND_MIX_CHANNELS;
	tmp = (desired.samples * shm->channels) * 10;
	if (tmp & (tmp - 1))
	{	/* make it a power of two */
		val = 1;
		while (val < tmp)
			val <<= 1;

		tmp = val;
	}
	shm->samples = tmp;
	shm->samplepos = 0;
	shm->submission_chunk = 1;

	Con_Printf ("SDL audio spec  : %d Hz, %d samples, %d mix channels (callback: %d ch, surround: %s)\n",
			desired.freq, desired.samples, shm->channels, device_channels,
			surround_status);
#if defined(USE_SDL2)
	{
		const char *driver = SDL_GetCurrentAudioDriver();
		const char *device = SDL_GetAudioDeviceName(0, SDL_FALSE);
		q_snprintf(drivername, sizeof(drivername), "%s - %s",
			driver != NULL ? driver : "(UNKNOWN)",
			device != NULL ? device : "(UNKNOWN)");
	}
#else
	if (SDL_AudioDriverName(drivername, sizeof(drivername)) == NULL)
		strcpy(drivername, "(UNKNOWN)");
#endif
	buffersize = shm->samples * (shm->samplebits / 8);
	Con_Printf ("SDL audio driver: %s, %d bytes buffer\n", drivername, buffersize);

	shm->buffer = (unsigned char *) calloc (1, buffersize);
	if (!shm->buffer)
	{
#if defined(USE_SDL2)
		SDL_CloseAudioDevice(sdl_audiodevice);
		sdl_audiodevice = 0;
#else
		SDL_CloseAudio();
#endif
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		shm = NULL;
		Con_Printf ("Failed allocating memory for SDL audio\n");
		return false;
	}

#if defined(USE_SDL2)
	SDL_PauseAudioDevice(sdl_audiodevice, 0);
#else
	SDL_PauseAudio(0);
#endif

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
#if defined(USE_SDL2)
		SDL_CloseAudioDevice(sdl_audiodevice);
		sdl_audiodevice = 0;
#else
		SDL_CloseAudio();
#endif
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		if (shm->buffer)
			free (shm->buffer);
		shm->buffer = NULL;
		shm = NULL;
	}
}

void SNDDMA_LockBuffer (void)
{
#if defined(USE_SDL2)
	SDL_LockAudioDevice (sdl_audiodevice);
#else
	SDL_LockAudio ();
#endif
}

void SNDDMA_Submit (void)
{
#if defined(USE_SDL2)
	SDL_UnlockAudioDevice (sdl_audiodevice);
#else
	SDL_UnlockAudio ();
#endif
}

void SNDDMA_BlockSound (void)
{
#if defined(USE_SDL2)
	SDL_PauseAudioDevice (sdl_audiodevice, 1);
#else
	SDL_PauseAudio (1);
#endif
}

void SNDDMA_UnblockSound (void)
{
#if defined(USE_SDL2)
	SDL_PauseAudioDevice (sdl_audiodevice, 0);
#else
	SDL_PauseAudio (0);
#endif
}

