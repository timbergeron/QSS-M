/*
 * Background music handling for Quakespasm (adapted from uHexen2)
 * Handles streaming music as raw sound samples and runs the midi driver
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2010-2012 O.Sezer <sezero@users.sourceforge.net>
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

#ifndef _BGMUSIC_H_
#define _BGMUSIC_H_

extern qboolean	bgmloop;
extern cvar_t	bgm_extmusic;

qboolean BGM_Init (void);
void BGM_Shutdown (void);

void BGM_Play (const char *filename);
void BGM_Stop (void);
void BGM_Update (void);
void BGM_Pause (void);
void BGM_Resume (void);

void BGM_PlayCDtrack (byte track, qboolean looping);

typedef enum
{
	BGM_PREVIEW_STOPPED,
	BGM_PREVIEW_PLAYING,
	BGM_PREVIEW_PAUSED,
	BGM_PREVIEW_FAILED
} bgm_preview_status_t;

typedef struct
{
	bgm_preview_status_t status;
	char name[MAX_QPATH];
	char error[96];
	int64_t position_samples;
	int64_t total_samples;
	int rate;
	int bits;
	int channels;
	qboolean loop;
	qboolean seekable;
	float gain;
} bgm_preview_state_t;

qboolean BGM_Preview_Play(const char *filename, float gain, qboolean loop);
void BGM_Preview_Stop(void);
void BGM_Preview_Release(void);
void BGM_Preview_SetPaused(qboolean paused);
void BGM_Preview_SetLoop(qboolean loop);
void BGM_Preview_SetGain(float gain);
qboolean BGM_Preview_Seek(double fraction);
void BGM_Preview_GetState(bgm_preview_state_t *state);
void BGM_Preview_SetExclusive(qboolean exclusive);

typedef enum _bgm_player // woods moved from bgmusic.c #musiclist
{
    BGM_NONE = -1,
    BGM_MIDIDRV = 1,
    BGM_STREAMER
} bgm_player_t;

typedef struct music_handler_s // woods moved from bgmusic.c #musiclist
{
    unsigned int    type;           /* 1U << n (see snd_codec.h)    */
    bgm_player_t    player;         /* Enumerated bgm player type   */
    int             is_available;   /* -1 means not present         */
    const char* ext;           /* Expected file extension      */
    const char* dir;           /* Where to look for music file */
    struct music_handler_s* next;
} music_handler_t;

extern music_handler_t wanted_handlers[]; // woods #musiclist

#endif	/* _BGMUSIC_H_ */

