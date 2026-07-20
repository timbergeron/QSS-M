/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// sound.h -- client sound i/o functions

#ifndef __QUAKE_SOUND__
#define __QUAKE_SOUND__

/* !!! if this is changed, it must be changed in asm_i386.h too !!! */
typedef struct
{
	int left;
	int right;
} portable_samplepair_t;

typedef struct sfx_s
{
	char	name[MAX_QPATH];
	cache_user_t	cache;
} sfx_t;

/* !!! if this is changed, it must be changed in asm_i386.h too !!! */
typedef struct
{
	int	length;
	int	loopstart;
	int	speed;
	int	width;
	int	stereo;
	byte	data[1];	/* variable sized	*/
} sfxcache_t;

typedef struct
{
	int	channels;
	int	samples;		/* mono samples in buffer			*/
	int	submission_chunk;	/* don't mix less than this #			*/
	int	samplepos;		/* in mono samples				*/
	int	samplebits;
	int	signed8;		/* device opened for S8 format? (e.g. Amiga AHI) */
	int	speed;
	unsigned char	*buffer;
} dma_t;

/* !!! if this is changed, it must be changed in asm_i386.h too !!! */
typedef struct
{
	sfx_t	*sfx;			/* sfx number					*/
	int	leftvol;		/* 0-255 volume					*/
	int	rightvol;		/* 0-255 volume					*/
	int	end;			/* end time in global paintsamples		*/
	int	pos;			/* sample position in sfx			*/
	int	looping;		/* SND_LOOP_* policy, NEVER a sample position	*/
	int	entnum;			/* to allow overriding a specific sound		*/
	int	entchannel;
	vec3_t	origin;			/* origin of sound effect			*/
	vec_t	dist_mult;		/* distance multiplier (attenuation/clipK)	*/
	int	master_vol;		/* 0-255 master volume				*/
	qboolean advance_silently;	/* keep timeline moving at zero channel gain	*/
} channel_t;

/* channel_t.looping is a policy enum despite its legacy name. Zero must keep
 * the historical sfxcache_t.loopstart behavior for memset-initialized channels;
 * never store a loop sample offset in this field. */
#define SND_LOOP_DEFAULT 0
#define SND_LOOP_FORCE   1
#define SND_LOOP_DISABLE (-1)

typedef enum
{
	SOUND_PREVIEW_STOPPED,
	SOUND_PREVIEW_PLAYING,
	SOUND_PREVIEW_PAUSED,
	SOUND_PREVIEW_FAILED
} sound_preview_status_t;

typedef struct
{
	sound_preview_status_t status;
	char name[MAX_QPATH];
	char error[96];
	int position;
	int length;
	int rate;
	int bits;
	qboolean source_looped;
	qboolean loop;
	qboolean seekable;
	float gain;
} sound_preview_state_t;

#define WAV_FORMAT_PCM	1

typedef struct
{
	int	rate;
	int	width;
	int	channels;
	int	loopstart;
	int	samples;
	qofs_t	dataofs;		/* chunk starts this many bytes from file start	*/
} wavinfo_t;

void S_Init (void);
void S_Startup (void);
void S_Shutdown (void);
void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation);
void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation);
void S_StopSound (int entnum, int entchannel);
void S_StopAllSounds (qboolean clear, qboolean keep_statics);
void S_ClearBuffer (void);
void S_Update (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up);
void S_ExtraUpdate (void);
int S_GetMasterVolumeScale (void);
void S_SetMasterVolumeScale (float scale);
void S_ClearPrecache (void);
void S_BeginPrecaching (void);
void S_EndPrecaching (void);
qboolean S_SoundPreview_Play (const char *name, float gain, qboolean loop);
void S_SoundPreview_Stop (void);
void S_SoundPreview_SetPaused (qboolean paused);
void S_SoundPreview_SetLoop (qboolean loop);
void S_SoundPreview_SetGain (float gain);
qboolean S_SoundPreview_Seek (double fraction);
void S_SoundPreview_GetState (sound_preview_state_t *state);
void S_SoundPreview_Release (void);
void S_SoundPreview_SetExclusive (qboolean exclusive);
qboolean S_SoundPreview_ShouldMixChannel (int channel);

qboolean S_BlockSound (void);
void S_UnblockSound (void);

sfx_t *S_PrecacheSound (const char *sample);
void S_TouchSound (const char *sample);
void S_PaintChannels (int endtime);
void S_InitPaintChannels (void);
float S_GetLoFreqLevel (void);
float S_GetHiFreqLevel (void);
void S_ClearFilteredLevels (void);

/* picks a channel based on priorities, empty slots, number of channels */
channel_t *SND_PickChannel (int entnum, int entchannel);

/* spatializes a channel */
void SND_Spatialize (channel_t *ch);

/* music stream support */
void S_RawSamples(int samples, int rate, int width, int channels, byte * data, float volume);
				/* Expects data in signed 16 bit, or unsigned 8 bit format. */

/* initializes cycling through a DMA buffer and returns information on it */
qboolean SNDDMA_Init(dma_t *dma);

/* gets the current DMA position */
int SNDDMA_GetDMAPos(void);

/* shutdown the DMA xfer. */
void SNDDMA_Shutdown(void);

/* validates & locks the dma buffer */
void SNDDMA_LockBuffer(void);

/* unlocks the dma buffer / sends sound to the device */
void SNDDMA_Submit(void);

/* blocks sound output upon window focus loss */
void SNDDMA_BlockSound(void);

/* unblocks the output upon window focus gain */
void SNDDMA_UnblockSound(void);

/* ====================================================================
 * User-setable variables
 * ====================================================================
 */

//#define	MAX_CHANNELS		1024 // spike -- made this obsolete. ericw -- was 512 /* johnfitz -- was 128 */
#define	MAX_DYNAMIC_CHANNELS	128 /* johnfitz -- was 8   */

extern	channel_t	*snd_channels;
/* 0 to NUM_AMBIENTS - 1 = water, wind, etc.
 * NUM_AMBIENTS to NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS - 1 = entity sounds
 * The audio browser reserves the following channel.
 * Remaining channels up to total_channels are static sounds.
 */

extern	volatile dma_t	*shm;

extern	int		max_channels;
extern	int		total_channels;
extern	int		soundtime;
extern	int		paintedtime;
extern	int		s_rawend;

extern float voicevolumescale;

extern	vec3_t		listener_origin;
extern	vec3_t		listener_forward;
extern	vec3_t		listener_right;
extern	vec3_t		listener_up;

extern	cvar_t		sndspeed;
extern	cvar_t		snd_mixspeed;
extern	cvar_t		snd_surround;
extern	cvar_t		snd_filterquality;
extern	cvar_t		sfxvolume;
extern	cvar_t		loadas8bit;
#define	MAX_RAW_SAMPLES	8192
extern	portable_samplepair_t	s_rawsamples[MAX_RAW_SAMPLES];

extern	cvar_t		bgmvolume;

void S_LocalSound (const char *name);
sfxcache_t *S_LoadSound (sfx_t *s);

wavinfo_t GetWavinfo (const char *name, byte *wav, qofs_t wavlength);

void SND_InitScaletable (void);

#endif	/* __QUAKE_SOUND__ */
