/*
Copyright (C) 2026 QSS-M contributors

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

// clipboard_sdl.c -- shared SDL3 clipboard publication and file-list helpers

#include "quakedef.h"

#include <stdint.h>
#include <string.h>

typedef struct
{
	Uint32			serial;
	const char		*mime_type;
	clipboard_image_t	image;
} clipboard_entry_t;

/*
 * SDL receives a serial number as userdata, never an entry pointer.  SDL 3.2
 * keeps callback state after a failed SDL_SetClipboardData, and a Cocoa
 * pasteboard provider holds userdata past SDL's cleanup, so a late callback
 * must not be able to reach freed memory.  Entries are looked up by serial.
 */
static clipboard_entry_t	*clipboard_owned;	/* published and held by SDL */
static clipboard_entry_t	*clipboard_pending;	/* inside SDL_SetClipboardData */
static Uint32		clipboard_serial;

/*
 * Requests are ordered by tokens taken from SDL's own SDL_GetTicksNS clock, so
 * they can be compared with the timestamp SDL puts on each clipboard event.  A
 * change that was queued before a request is older than it and leaves it alone.
 */
static uint64_t		clipboard_last_token;
static uint64_t		clipboard_latest_request;
static uint64_t		clipboard_superseded_before;
static Uint32		clipboard_echo_serial;	/* awaiting our own Wayland offer */

/*
==============================================================================

IMAGE ENCODING

==============================================================================
*/

clipboard_image_format_t Clipboard_NativeImageFormat (void)
{
#ifdef _WIN32
	/* SDL's Windows backend maps image/bmp to CF_DIB and offers no PNG format. */
	return CLIPBOARD_IMAGE_BMP;
#else
	return CLIPBOARD_IMAGE_PNG;
#endif
}

const char *Clipboard_ImageMimeType (clipboard_image_format_t format)
{
	return format == CLIPBOARD_IMAGE_BMP ? "image/bmp" : "image/png";
}

void Clipboard_ReleaseImage (clipboard_image_t *image)
{
	if (!image)
		return;
	if (image->data && image->release)
		image->release (image->data);
	memset (image, 0, sizeof(*image));
}

static void Clipboard_SetError (char *error, size_t error_size, const char *message)
{
	if (error && error_size)
		q_strlcpy (error, message ? message : "", error_size);
}

static void Clipboard_SDLFree (void *data)
{
	SDL_free (data);
}

/* Writes top-down opaque RGB (or BGR) rows of dst_pitch bytes. */
static void Clipboard_ConvertRows (const byte *pixels, int width, int height,
	clipboard_pixels_t layout, qboolean bottom_up, qboolean bgr, byte *dst,
	size_t dst_pitch)
{
	size_t	src_bpp = layout == CLIPBOARD_PIXELS_BGRA32 ? 4 : 3;
	size_t	src_pitch = (size_t)width * src_bpp;
	int	x, y;

	for (y = 0; y < height; y++)
	{
		const byte *src = pixels + (size_t)(bottom_up ? height - 1 - y : y) * src_pitch;
		byte *out = dst + (size_t)y * dst_pitch;

		for (x = 0; x < width; x++, src += src_bpp, out += 3)
		{
			byte r = layout == CLIPBOARD_PIXELS_BGRA32 ? src[2] : src[0];
			byte b = layout == CLIPBOARD_PIXELS_BGRA32 ? src[0] : src[2];

			out[0] = bgr ? b : r;
			out[1] = src[1];
			out[2] = bgr ? r : b;
		}
	}
}

static qboolean Clipboard_EncodePNG (const byte *pixels, int width, int height,
	clipboard_pixels_t layout, qboolean bottom_up, clipboard_image_t *image,
	char *error, size_t error_size)
{
	const byte	*source = pixels;
	byte		*rgb = NULL;
	byte		*png;
	size_t		png_size;
	qboolean	top_down = !bottom_up;
	qboolean	ok;

	if (layout != CLIPBOARD_PIXELS_RGB24)
	{
		rgb = (byte *) malloc ((size_t)width * (size_t)height * 3);
		if (!rgb)
		{
			Clipboard_SetError (error, error_size, "out of memory");
			return false;
		}
		Clipboard_ConvertRows (pixels, width, height, layout, bottom_up, false,
			rgb, (size_t)width * 3);
		source = rgb;
		top_down = true;
	}

	ok = Image_EncodePNGMemory (source, width, height, 24, top_down, &png,
		&png_size, error, error_size);
	free (rgb);
	if (!ok)
		return false;

	image->format = CLIPBOARD_IMAGE_PNG;
	image->data = png;
	image->size = png_size;
	image->release = Image_FreePNGMemory;
	return true;
}

static qboolean Clipboard_EncodeBMP (const byte *pixels, int width, int height,
	clipboard_pixels_t layout, qboolean bottom_up, clipboard_image_t *image,
	char *error, size_t error_size)
{
	SDL_Surface	*surface;
	SDL_IOStream	*io;
	SDL_PropertiesID	props;
	Sint64		size;
	void		*data;

	/* SDL pads the pitch to whole 4-byte BMP rows. SDL_SaveBMP_IO writes
	   biSizeImage as height * pitch, and Windows copies exactly that many
	   pixel bytes into CF_DIB, so an unpadded pitch truncates odd widths. */
	surface = SDL_CreateSurface (width, height, SDL_PIXELFORMAT_BGR24);
	if (!surface)
	{
		Clipboard_SetError (error, error_size, SDL_GetError ());
		return false;
	}
	Clipboard_ConvertRows (pixels, width, height, layout, bottom_up, true,
		(byte *)surface->pixels, (size_t)surface->pitch);

	io = SDL_IOFromDynamicMem ();
	if (!io || !SDL_SaveBMP_IO (surface, io, false))
	{
		Clipboard_SetError (error, error_size, SDL_GetError ());
		if (io)
			SDL_CloseIO (io);
		SDL_DestroySurface (surface);
		return false;
	}
	SDL_DestroySurface (surface);

	size = SDL_GetIOSize (io);
	props = SDL_GetIOProperties (io);
	data = props ? SDL_GetPointerProperty (props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL) : NULL;
	/* Clearing the pointer transfers the buffer to us; SDL_free releases it. */
	if (!data || size <= 0 ||
		!SDL_SetPointerProperty (props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL))
	{
		Clipboard_SetError (error, error_size, "couldn't take BMP data");
		SDL_CloseIO (io);
		return false;
	}
	SDL_CloseIO (io);

	image->format = CLIPBOARD_IMAGE_BMP;
	image->data = (byte *)data;
	image->size = (size_t)size;
	image->release = Clipboard_SDLFree;
	return true;
}

qboolean Clipboard_EncodeImage (const byte *pixels, int width, int height,
	clipboard_pixels_t layout, qboolean bottom_up, clipboard_image_format_t format,
	clipboard_image_t *image, char *error, size_t error_size)
{
	Clipboard_SetError (error, error_size, "");
	if (!image)
	{
		Clipboard_SetError (error, error_size, "invalid image parameters");
		return false;
	}
	memset (image, 0, sizeof(*image));

	if (!pixels || width <= 0 || height <= 0 ||
		(layout != CLIPBOARD_PIXELS_RGB24 && layout != CLIPBOARD_PIXELS_BGRA32))
	{
		Clipboard_SetError (error, error_size, "invalid image parameters");
		return false;
	}
	if (width > CLIPBOARD_MAX_IMAGE_SIDE || height > CLIPBOARD_MAX_IMAGE_SIDE ||
		(size_t)width * (size_t)height *
			(layout == CLIPBOARD_PIXELS_BGRA32 ? 4 : 3) > CLIPBOARD_MAX_IMAGE_BYTES)
	{
		Clipboard_SetError (error, error_size, "image is too large for the clipboard");
		return false;
	}

	if (format == CLIPBOARD_IMAGE_BMP)
		return Clipboard_EncodeBMP (pixels, width, height, layout, bottom_up,
			image, error, error_size);
	return Clipboard_EncodePNG (pixels, width, height, layout, bottom_up,
		image, error, error_size);
}

/*
==============================================================================

PUBLICATION

==============================================================================
*/

static clipboard_entry_t *Clipboard_FindEntry (Uint32 serial)
{
	if (clipboard_owned && clipboard_owned->serial == serial)
		return clipboard_owned;
	if (clipboard_pending && clipboard_pending->serial == serial)
		return clipboard_pending;
	return NULL;
}

static void Clipboard_FreeEntry (clipboard_entry_t *entry)
{
	if (!entry)
		return;
	Clipboard_ReleaseImage (&entry->image);
	free (entry);
}

static const void * SDLCALL Clipboard_ImageData (void *userdata, const char *mime_type, size_t *size)
{
	clipboard_entry_t *entry = Clipboard_FindEntry ((Uint32)(uintptr_t)userdata);

	if (size)
		*size = 0;
	if (!entry || !entry->image.data || !size || !mime_type ||
		strcmp (mime_type, entry->mime_type))
		return NULL;

	*size = entry->image.size;
	return entry->image.data;
}

static void SDLCALL Clipboard_ImageCleanup (void *userdata)
{
	Uint32 serial = (Uint32)(uintptr_t)userdata;

	if (clipboard_owned && clipboard_owned->serial == serial)
	{
		Clipboard_FreeEntry (clipboard_owned);
		clipboard_owned = NULL;
	}
	else if (clipboard_pending && clipboard_pending->serial == serial)
	{
		Clipboard_FreeEntry (clipboard_pending);
		clipboard_pending = NULL;
	}
}

static uint64_t Clipboard_NextToken (void)
{
	uint64_t now = (uint64_t)SDL_GetTicksNS ();

	if (now <= clipboard_last_token)
		now = clipboard_last_token + 1;
	clipboard_last_token = now;
	return now;
}

static void Clipboard_SupersedeBefore (uint64_t when)
{
	if (when > clipboard_superseded_before)
		clipboard_superseded_before = when;
	if (when > clipboard_last_token)
		clipboard_last_token = when;
}

/*
 * SDL 3.4 marks its own Wayland offers and filters them out; older SDL hands
 * our own selection back to us as an external change.  Only there is an echo
 * possible, and only while SDL still holds our image: another client taking
 * the selection cancels our data source first, which runs our cleanup.
 */
static qboolean Clipboard_WaylandEchoPossible (void)
{
	const char *driver = SDL_GetCurrentVideoDriver ();

	return driver && !strcmp (driver, "wayland") &&
		SDL_GetVersion () < SDL_VERSIONNUM(3, 4, 0);
}

static qboolean Clipboard_EventOffersOnly (const SDL_ClipboardEvent *event, const char *mime_type)
{
	return event->num_mime_types == 1 && event->mime_types &&
		event->mime_types[0] && !strcmp (event->mime_types[0], mime_type);
}

void Clipboard_HandleEvent (const union SDL_Event *event)
{
	if (!event || event->type != SDL_EVENT_CLIPBOARD_UPDATE || event->clipboard.owner)
		return;

	if (clipboard_echo_serial && clipboard_owned &&
		clipboard_owned->serial == clipboard_echo_serial &&
		Clipboard_EventOffersOnly (&event->clipboard, clipboard_owned->mime_type))
	{
		/* our own offer, echoed back before anyone else took the clipboard */
		clipboard_echo_serial = 0;
		return;
	}

	clipboard_echo_serial = 0;
	Clipboard_SupersedeBefore (event->clipboard.timestamp ?
		(uint64_t)event->clipboard.timestamp : Clipboard_NextToken ());
}

/* Handles clipboard changes SDL has seen but the input loop has not yet read. */
static void Clipboard_ProcessPendingEvents (void)
{
	SDL_Event event;

	SDL_PumpEvents ();
	while (SDL_PeepEvents (&event, 1, SDL_GETEVENT, SDL_EVENT_CLIPBOARD_UPDATE,
		SDL_EVENT_CLIPBOARD_UPDATE) > 0)
		Clipboard_HandleEvent (&event);
}

static qboolean Clipboard_VideoDriverIs (const char *name)
{
	const char *driver = SDL_GetCurrentVideoDriver ();

	return driver && !strcmp (driver, name);
}

/* Adjusts ownership to how the backend took a successful publication. */
static void Clipboard_SettlePublication (clipboard_entry_t *entry)
{
	size_t size;

	if (Clipboard_VideoDriverIs ("windows"))
	{
		/* Windows copied the image into CF_DIB inside SDL_SetClipboardData
		   and never asks again, so keeping ours would double the memory. */
		Clipboard_ReleaseImage (&entry->image);
	}
	else if (Clipboard_VideoDriverIs ("cocoa"))
	{
		/* Cocoa only promises the data, and the promise dies with the
		   process.  Reading it back makes AppKit store it on the pasteboard
		   now, as the old native export did, so it survives a quit or crash. */
		SDL_free (SDL_GetClipboardData (entry->mime_type, &size));
	}
	clipboard_echo_serial = Clipboard_WaylandEchoPossible () ? entry->serial : 0;
}

uint64_t Clipboard_BeginImageRequest (void)
{
	clipboard_latest_request = Clipboard_NextToken ();
	return clipboard_latest_request;
}

qboolean Clipboard_SetText (const char *text)
{
	/* A text copy is newer than any image still being encoded. */
	Clipboard_SupersedeBefore (Clipboard_NextToken ());
	return SDL_SetClipboardText (text ? text : "");
}

clipboard_publish_t Clipboard_PublishImage (uint64_t request, clipboard_image_t *image,
	char *error, size_t error_size)
{
	const char		*mime_types[1];
	clipboard_entry_t	*entry;
	qboolean		ok;

	Clipboard_SetError (error, error_size, "");
	if (!image || !image->data || !image->size)
	{
		Clipboard_ReleaseImage (image);
		Clipboard_SetError (error, error_size, "no image data");
		return CLIPBOARD_FAILED;
	}
	if (!SDL_WasInit (SDL_INIT_VIDEO))
	{
		Clipboard_ReleaseImage (image);
		Clipboard_SetError (error, error_size, "video is not initialized");
		return CLIPBOARD_FAILED;
	}

	Clipboard_ProcessPendingEvents ();
	if (request != clipboard_latest_request || request <= clipboard_superseded_before)
	{
		Clipboard_ReleaseImage (image);
		return CLIPBOARD_SUPERSEDED;
	}

	entry = (clipboard_entry_t *) calloc (1, sizeof(*entry));
	if (!entry)
	{
		Clipboard_ReleaseImage (image);
		Clipboard_SetError (error, error_size, "out of memory");
		return CLIPBOARD_FAILED;
	}
	entry->image = *image;
	memset (image, 0, sizeof(*image));
	if (++clipboard_serial == 0)
		clipboard_serial = 1;
	entry->serial = clipboard_serial;
	entry->mime_type = Clipboard_ImageMimeType (entry->image.format);
	mime_types[0] = entry->mime_type;

	/* SDL releases the previous owner through its cleanup callback here. */
	clipboard_pending = entry;
	ok = SDL_SetClipboardData (Clipboard_ImageData, Clipboard_ImageCleanup,
		(void *)(uintptr_t)entry->serial, mime_types, 1);
	if (!ok)
	{
		Clipboard_SetError (error, error_size, SDL_GetError ());
		/* SDL may still hold this serial; a later cleanup finds nothing. */
		if (clipboard_pending == entry)
		{
			clipboard_pending = NULL;
			Clipboard_FreeEntry (entry);
		}
		return CLIPBOARD_FAILED;
	}

	if (clipboard_pending == entry)
	{
		if (clipboard_owned)
			Clipboard_FreeEntry (clipboard_owned);
		clipboard_owned = entry;
		clipboard_pending = NULL;
		Clipboard_SettlePublication (entry);
	}
	return CLIPBOARD_PUBLISHED;
}

void Clipboard_Shutdown (void)
{
	/* nothing still encoding may publish after this */
	Clipboard_SupersedeBefore (Clipboard_NextToken ());
}

/*
==============================================================================

FILE LISTS

==============================================================================
*/

qboolean PL_AddClipboardFilePath (char ***paths, int *count, int *capacity, char *path)
{
	char	**new_paths;
	size_t	needed;
	size_t	max_paths;
	int	new_capacity;

	if (!path || !*path)
	{
		if (path)
			Z_Free(path);
		return false;
	}
	if (!paths || !count || !capacity || *count < 0 || *capacity < *count)
	{
		Z_Free(path);
		return false;
	}
	if (*count > 0 && !*paths)
	{
		Z_Free(path);
		return false;
	}
	if (!*paths)
		*capacity = 0;

	needed = (size_t)*count + 1;
	max_paths = (size_t)Q_MAXINT / sizeof(*new_paths);
	if (needed > max_paths)
	{
		Z_Free(path);
		return false;
	}

	if (*count >= *capacity)
	{
		new_capacity = *capacity > 0 ? *capacity : 4;
		while ((size_t)new_capacity < needed)
		{
			if ((size_t)new_capacity > max_paths / 2)
			{
				new_capacity = (int)max_paths;
				break;
			}
			new_capacity *= 2;
		}

		new_paths = (char **) Z_Malloc(new_capacity * (int)sizeof(*new_paths));
		if (*paths)
		{
			memcpy(new_paths, *paths, *count * sizeof(*new_paths));
			Z_Free(*paths);
		}
		*paths = new_paths;
		*capacity = new_capacity;
	}

	(*paths)[*count] = path;
	++(*count);
	return true;
}

void PL_FreeClipboardFilePaths (char **paths, int count)
{
	int i;

	if (!paths)
		return;
	for (i = 0; i < count; ++i)
	{
		if (paths[i])
			Z_Free(paths[i]);
	}
	Z_Free(paths);
}

char *PL_GetClipboardFilePath (void)
{
	char **paths;
	char *data = NULL;
	int count = 0;

	paths = PL_GetClipboardFilePaths(&count);
	if (paths && count > 0)
	{
		data = paths[0];
		paths[0] = NULL;
	}
	PL_FreeClipboardFilePaths(paths, count);
	return data;
}

static int Clipboard_HexValue (int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/*
Decodes one local file URI (file:///path, file://localhost/path or file:/path)
into a Z_Malloc'ed path.  Bytes are kept as they are, so paths stay in the
filesystem's encoding.  Remote hosts, malformed escapes, NUL bytes, controls,
queries and fragments are refused rather than guessed at.
*/
static char *Clipboard_DecodeFileURI (const char *uri, size_t length)
{
	const char	*src;
	const char	*end = uri + length;
	char		*path;
	char		*dst;

	if (length < 6 || q_strncasecmp (uri, "file:", 5))
		return NULL;

	src = uri + 5;
	if (end - src >= 2 && src[0] == '/' && src[1] == '/')
	{
		const char *host = src + 2;
		const char *slash = (const char *) memchr (host, '/', (size_t)(end - host));

		if (!slash)
			return NULL;
		if (slash != host &&
			!(slash - host == 9 && !q_strncasecmp (host, "localhost", 9)))
			return NULL;
		src = slash;
	}
	if (src >= end || *src != '/' || (size_t)(end - src) >= (size_t)MAX_OSPATH * 3)
		return NULL;

	path = (char *) Z_Malloc ((int)(end - src) + 1);
	dst = path;
	while (src < end)
	{
		unsigned char c = (unsigned char)*src;

		if (c == '%')
		{
			int hi = src + 2 < end ? Clipboard_HexValue ((unsigned char)src[1]) : -1;
			int lo = hi >= 0 ? Clipboard_HexValue ((unsigned char)src[2]) : -1;

			if (lo < 0 || (hi == 0 && lo == 0))
				goto fail;
			*dst++ = (char)((hi << 4) | lo);
			src += 3;
			continue;
		}
		if (c < 32 || c == 127 || c == '?' || c == '#')
			goto fail;
		*dst++ = (char)c;
		src++;
	}
	*dst = '\0';
	if (dst - path >= MAX_OSPATH)
		goto fail;
	return path;

fail:
	Z_Free (path);
	return NULL;
}

static qboolean Clipboard_HasPath (char **paths, int count, const char *path)
{
	int i;

	for (i = 0; i < count; i++)
	{
		if (!strcmp (paths[i], path))
			return true;
	}
	return false;
}

char **Clipboard_ParseFileURIs (const char *data, size_t size, clipboard_uri_format_t format,
	int *count, qboolean *too_many)
{
	char		**paths = NULL;
	const char	*line;
	const char	*end;
	int		local_count = 0;
	int		capacity = 0;
	size_t		path_bytes = 0;
	qboolean	header_pending = format == CLIPBOARD_GNOME_COPIED_FILES;

	if (count)
		*count = 0;
	if (too_many)
		*too_many = false;
	if (!data || !count)
		return NULL;

	/* some owners count a terminating NUL in the payload */
	while (size > 0 && data[size - 1] == '\0')
		size--;
	if (size == 0 || size > CLIPBOARD_MAX_URI_BYTES)
		return NULL;

	end = data + size;
	for (line = data; line < end; )
	{
		const char	*start = line;
		const char	*stop;
		char		*path;

		while (line < end && *line != '\n' && *line != '\r')
			line++;
		stop = line;
		while (line < end && (*line == '\n' || *line == '\r'))
			line++;

		while (start < stop && (*start == ' ' || *start == '\t'))
			start++;
		while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t'))
			stop--;

		if (header_pending)
		{
			/* GNOME: "copy" or "cut", then one URI per line.  Both import
			   copies; the source files are never moved. */
			header_pending = false;
			if (!(stop - start == 4 && !memcmp (start, "copy", 4)) &&
				!(stop - start == 3 && !memcmp (start, "cut", 3)))
				break;
			continue;
		}

		if (start == stop || *start == '#')
			continue;
		if (memchr (start, '\0', (size_t)(stop - start)))
			continue;

		path = Clipboard_DecodeFileURI (start, (size_t)(stop - start));
		if (!path)
			continue;
		if (Clipboard_HasPath (paths, local_count, path))
		{
			Z_Free (path);
			continue;
		}
		path_bytes += strlen (path) + 1;
		if (local_count >= CLIPBOARD_MAX_FILES || path_bytes > CLIPBOARD_MAX_PATH_BYTES)
		{
			Z_Free (path);
			PL_FreeClipboardFilePaths (paths, local_count);
			if (too_many)
				*too_many = true;
			return NULL;
		}
		PL_AddClipboardFilePath (&paths, &local_count, &capacity, path);
	}

	if (local_count == 0)
	{
		PL_FreeClipboardFilePaths (paths, local_count);
		return NULL;
	}
	*count = local_count;
	return paths;
}
