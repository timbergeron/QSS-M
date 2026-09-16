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

#include "quakedef.h"
#include <SDL3/SDL.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static const Uint8 bmp_bytes[] =
{
#include "qs_bmp.h"
};

void PL_SetWindowIcon (void)
{
	SDL_IOStream	*rwop;
	SDL_Surface	*icon;
	Uint32		colorkey;

	rwop = SDL_IOFromConstMem(bmp_bytes, sizeof(bmp_bytes));
	if (rwop == NULL)
		return;
	icon = SDL_LoadBMP_IO(rwop, 1);
	if (icon == NULL)
		return;
	/* make pure magenta (#ff00ff) tranparent */
	colorkey = SDL_MapSurfaceRGB(icon, 255, 0, 255);
	SDL_SetSurfaceColorKey(icon, true, colorkey);
	SDL_SetWindowIcon((SDL_Window*) VID_GetWindow(), icon);
	SDL_DestroySurface(icon);
}

void PL_VID_Shutdown (void)
{
}

#define MAX_CLIPBOARDTXT	MAXCMDLINE	/* 256 */
char *PL_GetClipboardData (void)
{
	char *data = NULL;
	char *cliptext = SDL_GetClipboardText();

	if (cliptext != NULL)
	{
		// Bound the converted output, rather than splitting the UTF-8 source.
		data = (char *) Z_Malloc(MAX_CLIPBOARDTXT);
		UTF8_ToQuake(data, MAX_CLIPBOARDTXT, cliptext);
		SDL_free(cliptext);
	}

	return data;
}

static qboolean PL_ClipboardMayOffer (char **mime_types, size_t num_mime_types, const char *mime_type)
{
	size_t i;

	/* An unknown offer (for example, copied before launch) must be asked. */
	if (!mime_types || num_mime_types == 0)
		return true;
	for (i = 0; i < num_mime_types; ++i)
	{
		if (mime_types[i] && !strcmp(mime_types[i], mime_type))
			return true;
	}
	return false;
}

char **PL_GetClipboardFilePaths (int *count)
{
	/* Preference order.  Only the first representation that yields files is
	 * used, so a file manager offering several never imports twice. */
	static const struct
	{
		const char		*mime_type;
		clipboard_uri_format_t	format;
	} formats[] =
	{
		{ "text/uri-list", CLIPBOARD_URI_LIST },
		{ "x-special/gnome-copied-files", CLIPBOARD_GNOME_COPIED_FILES },
	};
	char		**paths = NULL;
	char		**mime_types;
	size_t		num_mime_types = 0;
	size_t		i;
	int		local_count = 0;
	qboolean	too_many = false;

	mime_types = SDL_GetClipboardMimeTypes(&num_mime_types);
	for (i = 0; i < sizeof(formats) / sizeof(formats[0]) && !paths && !too_many; ++i)
	{
		size_t	size = 0;
		void	*data;

		if (!PL_ClipboardMayOffer(mime_types, num_mime_types, formats[i].mime_type))
			continue;
		data = SDL_GetClipboardData(formats[i].mime_type, &size);
		if (data)
		{
			paths = Clipboard_ParseFileURIs((const char *)data, size, formats[i].format,
				&local_count, &too_many);
			SDL_free(data);
		}
	}
	SDL_free(mime_types);

	if (!paths && !too_many)
	{
		char *cliptext = SDL_GetClipboardText();

		if (cliptext != NULL)
		{
			paths = Clipboard_ParseFileURIs(cliptext, strlen(cliptext), CLIPBOARD_URI_TEXT,
				&local_count, &too_many);
			SDL_free(cliptext);
		}
	}

	if (too_many)
		Con_Printf("Clipboard file list is too large (over %d files or %d KB of paths); nothing was imported.\n",
			CLIPBOARD_MAX_FILES, CLIPBOARD_MAX_PATH_BYTES / 1024);
	if (count)
		*count = local_count;
	return paths;
}

void PL_ErrorDialog (const char *errorMsg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Quake Error", errorMsg, NULL);
}

int PL_MessageDialog(const char *title, const char *message,
	const pl_dialog_button_t *buttons, int num_buttons,
	int default_button, int cancel_button)
{
	SDL_MessageBoxButtonData *sdl_buttons;
	SDL_MessageBoxData data;
	int i, selected = cancel_button;

	if (!buttons || num_buttons <= 0)
		return cancel_button;
	sdl_buttons = (SDL_MessageBoxButtonData *)calloc((size_t)num_buttons,
		sizeof(*sdl_buttons));
	if (!sdl_buttons)
		return cancel_button;
	for (i = 0; i < num_buttons; ++i)
	{
		sdl_buttons[i].flags =
			(buttons[i].id == default_button ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) |
			(buttons[i].id == cancel_button ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
		sdl_buttons[i].buttonID = buttons[i].id;
		sdl_buttons[i].text = buttons[i].text;
	}
	memset(&data, 0, sizeof(data));
	data.flags = SDL_MESSAGEBOX_INFORMATION;
	data.title = title;
	data.message = message;
	data.numbuttons = num_buttons;
	data.buttons = sdl_buttons;
	if (!SDL_ShowMessageBox(&data, &selected))
		selected = cancel_button;
	free(sdl_buttons);
	return selected;
}

qboolean PL_ConfirmDialog(const char *title, const char *text)
{
	static const pl_dialog_button_t buttons[] = {{1, "Yes"}, {0, "No"}};
	return PL_MessageDialog(title, text, buttons, 2, 1, 0) == 1;
}

typedef enum
{
	PL_PICKER_UNAVAILABLE = -1,
	PL_PICKER_CANCELLED = 0,
	PL_PICKER_SUCCESS = 1
} pl_picker_result_t;

static pl_picker_result_t PL_RunDirectoryPicker(char *const argv[], qboolean portal,
	char *result, size_t result_size)
{
	int output[2];
	pid_t child;
	ssize_t got;
	size_t total = 0;
	unsigned char buffer[4096];
	int status;
	qboolean read_error = false, truncated = false;

	if (pipe(output) != 0)
		return PL_PICKER_UNAVAILABLE;
	child = fork();
	if (child == 0)
	{
		close(output[0]);
		dup2(output[1], STDOUT_FILENO);
		close(output[1]);
		if (portal)
			setenv("GTK_USE_PORTAL", "1", 1);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(output[1]);
	if (child < 0)
	{
		close(output[0]);
		return PL_PICKER_UNAVAILABLE;
	}
	/* Always drain the child pipe, even if the selected path exceeds our output
	 * buffer, so the picker cannot block before waitpid(). */
	for (;;)
	{
		got = read(output[0], buffer, sizeof(buffer));
		if (got > 0)
		{
			size_t available = total + 1 < result_size ?
				result_size - total - 1 : 0;
			size_t copy = (size_t)got < available ? (size_t)got : available;
			if (copy < (size_t)got)
				truncated = true;
			if (copy > 0)
			{
				memcpy(result + total, buffer, copy);
				total += copy;
			}
			continue;
		}
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0)
			read_error = true;
		break;
	}
	close(output[0]);
	do
		got = waitpid(child, &status, 0);
	while (got < 0 && errno == EINTR);
	if (read_error || truncated || got != child || !WIFEXITED(status))
		return PL_PICKER_UNAVAILABLE;
	if (WEXITSTATUS(status) == 1)
		return PL_PICKER_CANCELLED;
	if (WEXITSTATUS(status) != 0 || total == 0)
		return PL_PICKER_UNAVAILABLE;
	result[total] = '\0';
	while (total > 0 && (result[total - 1] == '\n' || result[total - 1] == '\r'))
		result[--total] = '\0';
	return total > 0 ? PL_PICKER_SUCCESS : PL_PICKER_UNAVAILABLE;
}

qboolean PL_SelectDirectory(const char *title, const char *initial_path,
	char *result, size_t result_size)
{
	char *zenity_argv[7];
	char *kdialog_argv[6];
	char *zenity_title;
	char *zenity_initial = NULL;
	size_t len;
	pl_picker_result_t picker;

	if (!result || result_size == 0)
		return false;
	result[0] = '\0';
	len = strlen(title ? title : "Select Quake Data Folder") + 9;
	zenity_title = (char *)malloc(len);
	if (!zenity_title)
		return false;
	q_snprintf(zenity_title, len, "--title=%s",
		title ? title : "Select Quake Data Folder");
	if (initial_path && initial_path[0])
	{
		len = strlen(initial_path) + 13;
		zenity_initial = (char *)malloc(len);
		if (zenity_initial)
			q_snprintf(zenity_initial, len, "--filename=%s/", initial_path);
	}
	zenity_argv[0] = "zenity";
	zenity_argv[1] = "--file-selection";
	zenity_argv[2] = "--directory";
	zenity_argv[3] = zenity_title;
	zenity_argv[4] = zenity_initial;
	zenity_argv[5] = NULL;
	/* GTK's portal backend is requested first.  Exit 1 is an intentional cancel,
	 * not a reason to open two more picker windows. */
	picker = PL_RunDirectoryPicker(zenity_argv, true, result, result_size);
	if (picker == PL_PICKER_SUCCESS)
	{
		free(zenity_initial);
		free(zenity_title);
		return true;
	}
	if (picker == PL_PICKER_CANCELLED)
	{
		free(zenity_initial);
		free(zenity_title);
		return false;
	}
	picker = PL_RunDirectoryPicker(zenity_argv, false, result, result_size);
	if (picker == PL_PICKER_SUCCESS || picker == PL_PICKER_CANCELLED)
	{
		free(zenity_initial);
		free(zenity_title);
		return picker == PL_PICKER_SUCCESS;
	}
	free(zenity_initial);
	free(zenity_title);
	kdialog_argv[0] = "kdialog";
	kdialog_argv[1] = "--getexistingdirectory";
	kdialog_argv[2] = (char *)(initial_path && initial_path[0] ? initial_path : ".");
	kdialog_argv[3] = "--title";
	kdialog_argv[4] = (char *)(title ? title : "Select Quake Data Folder");
	kdialog_argv[5] = NULL;
	return PL_RunDirectoryPicker(kdialog_argv, false, result, result_size) ==
		PL_PICKER_SUCCESS;
}
