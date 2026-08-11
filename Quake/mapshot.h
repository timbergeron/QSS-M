/*
Copyright (C) 2026 QSS-M contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef MAPSHOT_H
#define MAPSHOT_H

/*
mapshot.c -- shared level-screenshot lookup.

Two consumers with different needs share one worker thread and one cache:

  Discord rich presence wants a *URL* to hand to Discord, which fetches the
  image itself.  It can use any format, including the .webp source.

  The in-engine overlays (loading screen, levels menu) want a *drawable pic*,
  so they need the bytes locally and a format the engine can decode.  stb_image
  is built with STBI_ONLY_JPEG here, so only the .jpg sources qualify.

Both resolve calls are non-blocking: they answer from cache when they can and
otherwise start (or leave running) a background job, returning "not yet".  Call
them again on a later frame.  Only one job runs at a time; asking for a
different map aborts the job in flight, which is what makes cursor-driven
previews cheap.
*/

// Values for cl_mapshots.
#define MAPSHOT_MODE_OFF		0	// no mapshots
#define MAPSHOT_MODE_LEVELS		1	// Levels-menu shots; download missing images
#define MAPSHOT_MODE_LOADING		2	// Levels menu plus loading/console shots

extern cvar_t cl_mapshots;
// User bias on top of the automatic gamma compensation in Draw_Levelshot.
// 1 = neutral (shot looks like the source image), lower = dimmer.
extern cvar_t cl_mapshots_brightness;

void Mapshot_Init (void);
void Mapshot_Shutdown (void);
// Join and publish a completed worker result without advancing vote-map state.
void Mapshot_CollectFinished (void);
void Mapshot_Frame (void);

// Drop cached pics and in-memory results.  Call on gamedir changes.
void Mapshot_NewGame (void);

// Normalized gamedir for mapshot keys; empty for id1/qw.  Uses the connected
// server's gamedir when there is one, else com_gamedir.
void Mapshot_CurrentGameDir (char *destination, size_t destination_size);

// Discord path.  Returns true and fills `url` when a remote image is known to
// exist, false while probing or when there is none.
qboolean Mapshot_ResolveURL (const char *map, const char *gamedir,
	char *url, size_t url_size);

// Overlay path.  Returns NULL until a drawable image is on disk and uploaded.
qpic_t *Mapshot_ResolvePic (const char *map);

// True while a drawable lookup is still outstanding, so callers can tell
// "fetching" from "this map has no image" instead of showing nothing for both.
qboolean Mapshot_PicPending (const char *map);

// Start the work for `map` without needing the result yet.  Safe to call every
// frame; cheap once the answer is cached.
void Mapshot_Prefetch (const char *map);

// Loading-plaque consumer.  Local map commands set an explicit destination;
// an advertised vote-map is used as a fallback for remote transitions.
void Mapshot_SetLoadingMap (const char *map);
const char *Mapshot_LoadingMap (void);
void Mapshot_EndLoading (void);

#endif	/* MAPSHOT_H */
