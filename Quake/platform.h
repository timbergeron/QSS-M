/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
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

#ifndef _QUAKE_PLATFORM_H
#define _QUAKE_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* platform dependent way to set the window icon */
void PL_SetWindowIcon(void);

/* platform dependent cleanup */
void PL_VID_Shutdown (void);

/* retrieve text from the clipboard (returns Z_Malloc()'ed data) */
char *PL_GetClipboardData (void);

/* retrieve a file path from the clipboard (returns Z_Malloc()'ed data) */
char *PL_GetClipboardFilePath (void);

/* retrieve file paths from the clipboard (returns Z_Malloc()'ed array/data).
 * PL_GetClipboardFilePaths is per platform; the helpers live in clipboard_sdl.c. */
qboolean PL_AddClipboardFilePath (char ***paths, int *count, int *capacity, char *path);
char **PL_GetClipboardFilePaths (int *count);
void PL_FreeClipboardFilePaths (char **paths, int count);

/*
 * Shared SDL clipboard layer (clipboard_sdl.c).
 *
 * Clipboard_EncodeImage and Clipboard_ReleaseImage may run on any thread.
 * Everything else must run on the main thread, which SDL requires for
 * clipboard publication.
 */
typedef enum
{
	CLIPBOARD_PIXELS_RGB24,
	CLIPBOARD_PIXELS_BGRA32	/* alpha is ignored: clipboard images are opaque */
} clipboard_pixels_t;

typedef enum
{
	CLIPBOARD_IMAGE_PNG,
	CLIPBOARD_IMAGE_BMP
} clipboard_image_format_t;

typedef struct
{
	clipboard_image_format_t format;
	byte	*data;
	size_t	size;
	void	(*release) (void *data);	/* frees data */
} clipboard_image_t;

typedef enum
{
	CLIPBOARD_PUBLISHED,
	CLIPBOARD_SUPERSEDED,	/* a newer copy replaced this request */
	CLIPBOARD_FAILED
} clipboard_publish_t;

#define CLIPBOARD_MAX_IMAGE_SIDE	16384
/* source pixels for one image; conversion and encoding add to this again */
#define CLIPBOARD_MAX_IMAGE_BYTES	((size_t)256 * 1024 * 1024)

/* the format this platform's SDL backend exchanges with other applications */
clipboard_image_format_t Clipboard_NativeImageFormat (void);
const char *Clipboard_ImageMimeType (clipboard_image_format_t format);
/* Converts pixels to a top-down, opaque image and encodes it. */
qboolean Clipboard_EncodeImage (const byte *pixels, int width, int height,
	clipboard_pixels_t layout, qboolean bottom_up, clipboard_image_format_t format,
	clipboard_image_t *image, char *error, size_t error_size);
void Clipboard_ReleaseImage (clipboard_image_t *image);

/* Image copies finish after encoding.  A request is only published while no
 * newer image request, engine text copy or external clipboard change exists. */
uint64_t Clipboard_BeginImageRequest (void);
/* Takes ownership of image->data whatever the result. */
clipboard_publish_t Clipboard_PublishImage (uint64_t request, clipboard_image_t *image,
	char *error, size_t error_size);
/* All engine text copies go through this, so they supersede pending images. */
qboolean Clipboard_SetText (const char *text);
union SDL_Event;
void Clipboard_HandleEvent (const union SDL_Event *event);
void Clipboard_Shutdown (void);

typedef enum
{
	CLIPBOARD_URI_LIST,		/* text/uri-list */
	CLIPBOARD_GNOME_COPIED_FILES,	/* x-special/gnome-copied-files */
	CLIPBOARD_URI_TEXT		/* plain text holding file: URI lines */
} clipboard_uri_format_t;

#define CLIPBOARD_MAX_URI_BYTES		(1024 * 1024)
#define CLIPBOARD_MAX_FILES		1024
/* decoded paths live in the zone, which is only a few megabytes */
#define CLIPBOARD_MAX_PATH_BYTES	(256 * 1024)

/* Parses local file URIs into Z_Malloc()'ed filesystem paths.  Returns NULL
 * when nothing usable is found; *too_many is set when the list was refused for
 * exceeding CLIPBOARD_MAX_FILES files or CLIPBOARD_MAX_PATH_BYTES of paths. */
char **Clipboard_ParseFileURIs (const char *data, size_t size, clipboard_uri_format_t format,
	int *count, qboolean *too_many);

/* show an error dialog */
void PL_ErrorDialog(const char *text);

#if defined(__APPLE__) || defined(PLATFORM_OSX) || defined(PLATFORM_MAC)
/* Forward a raw engine Command-Q transition to the native hold-to-quit UI. */
void PL_CommandQEvent(int down);
#endif

typedef struct
{
	int id;
	const char *text;
} pl_dialog_button_t;

/* Startup-safe native dialogs.  PL_MessageDialog returns a button id. */
int PL_MessageDialog(const char *title, const char *message,
	const pl_dialog_button_t *buttons, int num_buttons,
	int default_button, int cancel_button);
qboolean PL_SelectDirectory(const char *title, const char *initial_path,
	char *result, size_t result_size);

#if defined(_WIN32) && !defined(__WATCOMC__)
typedef struct pl_search_progress_s pl_search_progress_t;
/* Modeless startup-safe status UI. Update pumps messages and returns false
 * after the user cancels or closes the window. */
pl_search_progress_t *PL_SearchProgressBegin(const char *title,
	const char *phase);
qboolean PL_SearchProgressUpdate(pl_search_progress_t *progress,
	const char *phase, const char *location, unsigned long long directories);
void PL_SearchProgressEnd(pl_search_progress_t *progress);
#endif

/* Compatibility wrapper for older callers. */
qboolean PL_ConfirmDialog(const char *title, const char *text);

#if defined(_WIN32)
/* Forward a raw engine Ctrl-W transition to the native hold-to-quit UI. */
void PL_ControlWEvent(int down);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _QUAKE_PLATFORM_H */
