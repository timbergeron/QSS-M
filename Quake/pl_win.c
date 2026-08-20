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
#include <windows.h>
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#else
#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>
#endif
#else
#include "SDL.h"
#include "SDL_syswm.h"
#endif

static HICON icon;

#define PL_QUIT_HOLD_DURATION_MS 1000
#define PL_QUIT_HOLD_COMPLETION_MS 80
#define PL_QUIT_HOLD_TIMER_PROGRESS 1
#define PL_QUIT_HOLD_TIMER_COMPLETE 2
#define PL_QUIT_HOLD_WIDTH 356
#define PL_QUIT_HOLD_HEIGHT 72
#define PL_QUIT_HOLD_TOP_OFFSET 72
#define PL_QUIT_HOLD_PROGRESS_INTERVAL_MS 10
#define PL_QUIT_HOLD_POINT_COUNT 256

static const WCHAR pl_quit_hold_class[] = L"QSSMQuitHoldOverlay";
static HWND pl_quit_hold_window;
static qboolean pl_quit_hold_active;
static qboolean pl_quit_hold_committed;
static DWORD pl_quit_hold_started;

static HWND PL_GetNativeWindow (void)
{
	SDL_SysWMinfo wminfo;

	SDL_VERSION(&wminfo.version);
#if defined(USE_SDL2)
	if (!VID_GetWindow())
		return NULL;
	if (SDL_GetWindowWMInfo((SDL_Window *)VID_GetWindow(), &wminfo) != SDL_TRUE)
		return NULL;
	return wminfo.info.win.window;
#else
	if (SDL_GetWMInfo(&wminfo) != 1)
		return NULL;
	return wminfo.window;
#endif
}

static UINT PL_GetWindowDPI (HWND hwnd)
{
	typedef UINT (WINAPI *get_dpi_for_window_fn)(HWND);
	union
	{
		FARPROC generic;
		get_dpi_for_window_fn typed;
	} resolver;
	static get_dpi_for_window_fn get_dpi_for_window;
	static qboolean resolved;
	HDC dc;
	HMODULE user32;
	UINT dpi;

	if (!resolved)
	{
		user32 = GetModuleHandleA("user32.dll");
		if (user32)
		{
			resolver.generic = GetProcAddress(user32, "GetDpiForWindow");
			get_dpi_for_window = resolver.typed;
		}
		resolved = true;
	}
	if (get_dpi_for_window)
	{
		dpi = get_dpi_for_window(hwnd);
		if (dpi)
			return dpi;
	}

	dc = GetDC(hwnd);
	dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
	if (dc)
		ReleaseDC(hwnd, dc);
	return dpi ? dpi : 96;
}

static double PL_QuitHoldBorderPerimeter (int width, int height, int radius)
{
	const double horizontal = (width - 5.0) - 2.0 * radius;
	const double vertical = (height - 5.0) - 2.0 * radius;

	return 2.0 * horizontal + 2.0 * vertical + 2.0 * M_PI * radius;
}

static POINT PL_QuitHoldBorderPoint (double distance, int width, int height, int radius)
{
	POINT point;
	double left = 2.0;
	double top = 2.0;
	double right = width - 3.0;
	double bottom = height - 3.0;
	double center = (left + right) * 0.5;
	double straight;
	double arc = M_PI * radius * 0.5;
	double angle;

	straight = right - radius - center;
	if (distance <= straight)
	{
		point.x = (LONG)(center + distance + 0.5);
		point.y = (LONG)top;
		return point;
	}
	distance -= straight;
	if (distance <= arc)
	{
		angle = -M_PI * 0.5 + distance / radius;
		point.x = (LONG)(right - radius + cos(angle) * radius + 0.5);
		point.y = (LONG)(top + radius + sin(angle) * radius + 0.5);
		return point;
	}
	distance -= arc;
	straight = bottom - top - 2.0 * radius;
	if (distance <= straight)
	{
		point.x = (LONG)right;
		point.y = (LONG)(top + radius + distance + 0.5);
		return point;
	}
	distance -= straight;
	if (distance <= arc)
	{
		angle = distance / radius;
		point.x = (LONG)(right - radius + cos(angle) * radius + 0.5);
		point.y = (LONG)(bottom - radius + sin(angle) * radius + 0.5);
		return point;
	}
	distance -= arc;
	straight = right - left - 2.0 * radius;
	if (distance <= straight)
	{
		point.x = (LONG)(right - radius - distance + 0.5);
		point.y = (LONG)bottom;
		return point;
	}
	distance -= straight;
	if (distance <= arc)
	{
		angle = M_PI * 0.5 + distance / radius;
		point.x = (LONG)(left + radius + cos(angle) * radius + 0.5);
		point.y = (LONG)(bottom - radius + sin(angle) * radius + 0.5);
		return point;
	}
	distance -= arc;
	straight = bottom - top - 2.0 * radius;
	if (distance <= straight)
	{
		point.x = (LONG)left;
		point.y = (LONG)(bottom - radius - distance + 0.5);
		return point;
	}
	distance -= straight;
	if (distance <= arc)
	{
		angle = M_PI + distance / radius;
		point.x = (LONG)(left + radius + cos(angle) * radius + 0.5);
		point.y = (LONG)(top + radius + sin(angle) * radius + 0.5);
		return point;
	}
	distance -= arc;
	point.x = (LONG)(left + radius + distance + 0.5);
	point.y = (LONG)top;
	return point;
}

static void PL_QuitHoldDrawProgress (HDC dc, const RECT *rect, float progress, int radius)
{
	POINT points[PL_QUIT_HOLD_POINT_COUNT];
	HPEN glow_pen;
	HPEN progress_pen;
	HGDIOBJ old_pen;
	double width = rect->right - rect->left;
	double height = rect->bottom - rect->top;
	double perimeter;
	int count;
	int i;

	if (progress <= 0.0f)
		return;
	if (progress > 1.0f)
		progress = 1.0f;

	perimeter = PL_QuitHoldBorderPerimeter((int)width, (int)height, radius);
	count = 2 + (int)((PL_QUIT_HOLD_POINT_COUNT - 2) * progress);
	for (i = 0; i < count; ++i)
	{
		double fraction = count > 1 ? (double)i / (double)(count - 1) : 0.0;
		points[i] = PL_QuitHoldBorderPoint(perimeter * progress * fraction,
			(int)width, (int)height, radius);
	}

	glow_pen = CreatePen(PS_SOLID, progress >= 1.0f ? 5 : 4, RGB(120, 120, 120));
	progress_pen = CreatePen(PS_SOLID, progress >= 1.0f ? 3 : 2, RGB(245, 245, 245));
	if (!glow_pen || !progress_pen)
	{
		if (progress_pen)
			DeleteObject(progress_pen);
		if (glow_pen)
			DeleteObject(glow_pen);
		return;
	}
	old_pen = SelectObject(dc, glow_pen);
	Polyline(dc, points, count);
	SelectObject(dc, progress_pen);
	Polyline(dc, points, count);
	SelectObject(dc, old_pen);
	DeleteObject(progress_pen);
	DeleteObject(glow_pen);
}

static void PL_QuitHoldPaint (HWND hwnd)
{
	PAINTSTRUCT paint;
	RECT rect;
	RECT text_rect;
	HDC dc;
	HBRUSH background;
	HPEN border;
	HFONT font;
	HGDIOBJ old_brush;
	HGDIOBJ old_pen;
	HGDIOBJ old_font;
	UINT dpi;
	int radius;
	float progress;

	dc = BeginPaint(hwnd, &paint);
	GetClientRect(hwnd, &rect);
	dpi = PL_GetWindowDPI(hwnd);
	radius = MulDiv(5, dpi, 96);
	if (radius < 3)
		radius = 3;

	background = CreateSolidBrush(RGB(41, 41, 41));
	border = CreatePen(PS_SOLID, 1, RGB(82, 82, 82));
	old_brush = SelectObject(dc, background);
	old_pen = SelectObject(dc, border);
	RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
	SelectObject(dc, old_pen);
	SelectObject(dc, old_brush);
	DeleteObject(border);
	DeleteObject(background);

	if (pl_quit_hold_committed)
		progress = 1.0f;
	else
		progress = (float)(DWORD)(GetTickCount() - pl_quit_hold_started) /
			(float)PL_QUIT_HOLD_DURATION_MS;
	PL_QuitHoldDrawProgress(dc, &rect, progress, radius);

	font = CreateFontW(-MulDiv(24, dpi, 96), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	old_font = font ? SelectObject(dc, font) : NULL;
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	text_rect = rect;
	DrawTextW(dc, L"Hold Ctrl+W to Quit", -1, &text_rect,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	if (old_font)
		SelectObject(dc, old_font);
	if (font)
		DeleteObject(font);
	EndPaint(hwnd, &paint);
}

static void PL_QuitHoldPushQuit (void)
{
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	event.type = SDL_QUIT;
	if (SDL_PushEvent(&event) != 1)
		Cbuf_AddText("quit\n");
}

static LRESULT CALLBACK PL_QuitHoldWindowProc (HWND hwnd, UINT message,
	WPARAM wparam, LPARAM lparam)
{
	(void)lparam;

	switch (message)
	{
	case WM_TIMER:
		if (wparam == PL_QUIT_HOLD_TIMER_PROGRESS && pl_quit_hold_active)
		{
			if ((DWORD)(GetTickCount() - pl_quit_hold_started) >= PL_QUIT_HOLD_DURATION_MS)
			{
				pl_quit_hold_active = false;
				pl_quit_hold_committed = true;
				KillTimer(hwnd, PL_QUIT_HOLD_TIMER_PROGRESS);
				if (!SetTimer(hwnd, PL_QUIT_HOLD_TIMER_COMPLETE,
					PL_QUIT_HOLD_COMPLETION_MS, NULL))
				{
					pl_quit_hold_committed = false;
					ShowWindow(hwnd, SW_HIDE);
					PL_QuitHoldPushQuit();
				}
			}
			InvalidateRect(hwnd, NULL, FALSE);
			UpdateWindow(hwnd);
			return 0;
		}
		if (wparam == PL_QUIT_HOLD_TIMER_COMPLETE && pl_quit_hold_committed)
		{
			KillTimer(hwnd, PL_QUIT_HOLD_TIMER_COMPLETE);
			pl_quit_hold_committed = false;
			ShowWindow(hwnd, SW_HIDE);
			PL_QuitHoldPushQuit();
			return 0;
		}
		break;
	case WM_PAINT:
		PL_QuitHoldPaint(hwnd);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_DESTROY:
		pl_quit_hold_active = false;
		pl_quit_hold_committed = false;
		pl_quit_hold_window = NULL;
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, message, wparam, lparam);
}

static qboolean PL_QuitHoldCreateWindow (HWND anchor)
{
	WNDCLASSEXW window_class;
	HINSTANCE instance = GetModuleHandle(NULL);

	if (pl_quit_hold_window && IsWindow(pl_quit_hold_window))
		return true;

	memset(&window_class, 0, sizeof(window_class));
	window_class.cbSize = sizeof(window_class);
	window_class.lpfnWndProc = PL_QuitHoldWindowProc;
	window_class.hInstance = instance;
	window_class.lpszClassName = pl_quit_hold_class;
	if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;

	pl_quit_hold_window = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
		pl_quit_hold_class, L"", WS_POPUP, 0, 0, 0, 0, anchor, NULL, instance, NULL);
	if (!pl_quit_hold_window)
		return false;
	if (!SetLayeredWindowAttributes(pl_quit_hold_window, 0, 235, LWA_ALPHA))
	{
		DestroyWindow(pl_quit_hold_window);
		pl_quit_hold_window = NULL;
		return false;
	}
	return true;
}

static void PL_QuitHoldCancel (void)
{
	pl_quit_hold_active = false;
	pl_quit_hold_committed = false;
	if (pl_quit_hold_window && IsWindow(pl_quit_hold_window))
	{
		KillTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_PROGRESS);
		KillTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_COMPLETE);
		ShowWindow(pl_quit_hold_window, SW_HIDE);
	}
}

void PL_ControlWEvent (int down)
{
	HWND anchor;
	RECT anchor_rect;
	HRGN region;
	UINT dpi;
	int width;
	int height;
	int offset;
	int radius;
	int x;
	int y;

	if (!down)
	{
		if (!pl_quit_hold_committed)
			PL_QuitHoldCancel();
		return;
	}
	if (pl_quit_hold_active || pl_quit_hold_committed)
		return;

	anchor = PL_GetNativeWindow();
	if (!anchor || !PL_QuitHoldCreateWindow(anchor))
		return;

	dpi = PL_GetWindowDPI(anchor);
	width = MulDiv(PL_QUIT_HOLD_WIDTH, dpi, 96);
	height = MulDiv(PL_QUIT_HOLD_HEIGHT, dpi, 96);
	offset = MulDiv(PL_QUIT_HOLD_TOP_OFFSET, dpi, 96);
	radius = MulDiv(6, dpi, 96);
	if (!GetWindowRect(anchor, &anchor_rect))
		return;
	x = anchor_rect.left + ((anchor_rect.right - anchor_rect.left) - width) / 2;
	y = anchor_rect.top + offset;
	region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
	if (!region)
		return;
	if (!SetWindowRgn(pl_quit_hold_window, region, FALSE))
	{
		DeleteObject(region);
		return;
	}

	pl_quit_hold_active = true;
	pl_quit_hold_committed = false;
	pl_quit_hold_started = GetTickCount();
	if (!SetWindowPos(pl_quit_hold_window, HWND_TOPMOST, x, y, width, height,
		SWP_NOACTIVATE | SWP_SHOWWINDOW))
	{
		PL_QuitHoldCancel();
		return;
	}
	InvalidateRect(pl_quit_hold_window, NULL, FALSE);
	UpdateWindow(pl_quit_hold_window);
	if (!SetTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_PROGRESS,
		PL_QUIT_HOLD_PROGRESS_INTERVAL_MS, NULL))
		PL_QuitHoldCancel();
}

void PL_SetWindowIcon (void)
{
	HINSTANCE handle;
	HWND hwnd;

	handle = GetModuleHandle(NULL);
	icon = LoadIcon(handle, "icon");

	if (!icon)
		return;	/* no icon in the exe */

	hwnd = PL_GetNativeWindow();
	if (!hwnd)
		return;	/* wrong SDL version */
#ifdef _WIN64
	SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR) icon);
#else
	SetClassLong(hwnd, GCL_HICON, (LONG) icon);
#endif
}

void PL_VID_Shutdown (void)
{
	PL_QuitHoldCancel();
	if (pl_quit_hold_window && IsWindow(pl_quit_hold_window))
		DestroyWindow(pl_quit_hold_window);
	pl_quit_hold_window = NULL;
	if (icon)
		DestroyIcon(icon);
	icon = NULL;
}

#define MAX_CLIPBOARDTXT	MAXCMDLINE	/* 256 */
char *PL_GetClipboardData (void)
{
	char *data = NULL;
	char *cliptext;

	if (OpenClipboard(NULL) != 0)
	{
		HANDLE hClipboardData;

		if ((hClipboardData = GetClipboardData(CF_TEXT)) != NULL)
		{
			cliptext = (char *) GlobalLock(hClipboardData);
			if (cliptext != NULL)
			{
				size_t size = GlobalSize(hClipboardData) + 1;
			/* this is intended for simple small text copies
			 * such as an ip address, etc:  do chop the size
			 * here, otherwise we may experience Z_Malloc()
			 * failures and all other not-oh-so-fun stuff. */
				size = q_min((size_t)(MAX_CLIPBOARDTXT), size);
				data = (char *) Z_Malloc((int)size);
				q_strlcpy (data, cliptext, size);
				GlobalUnlock (hClipboardData);
			}
		}
		CloseClipboard ();
	}
	return data;
}

static char *PL_CopyClipboardPathA(const char *path, size_t bytes)
{
	char *data;

	if (!path || bytes == 0 || bytes >= (size_t)Q_MAXINT)
		return NULL;

	data = (char *) Z_Malloc((int)bytes + 1);
	memcpy(data, path, bytes);
	data[bytes] = '\0';
	return data;
}

static char *PL_CopyClipboardPathW(const WCHAR *path, size_t chars)
{
	char	*data;
	int	size;

	if (!path || chars == 0 || chars >= (size_t)Q_MAXINT)
		return NULL;

	size = WideCharToMultiByte(CP_UTF8, 0, path, (int)chars, NULL, 0, NULL, NULL);
	if (size <= 0 || size >= Q_MAXINT)
		return NULL;

	data = (char *) Z_Malloc(size + 1);
	if (WideCharToMultiByte(CP_UTF8, 0, path, (int)chars, data, size, NULL, NULL) == size)
	{
		data[size] = '\0';
		return data;
	}

	Z_Free(data);
	return NULL;
}

char **PL_GetClipboardFilePaths (int *count)
{
	typedef struct
	{
		DWORD pFiles;
		POINT pt;
		BOOL fNC;
		BOOL fWide;
	} pl_dropfiles_t;

	char **paths = NULL;
	int local_count = 0;
	int local_capacity = 0;

	if (OpenClipboard(NULL) != 0)
	{
		HANDLE hClipboardData;

		if ((hClipboardData = GetClipboardData(CF_HDROP)) != NULL)
		{
			pl_dropfiles_t *drop = (pl_dropfiles_t *) GlobalLock(hClipboardData);
			SIZE_T drop_size = GlobalSize(hClipboardData);

			if (drop != NULL && drop->pFiles >= sizeof(pl_dropfiles_t) && drop->pFiles < drop_size)
			{
				const char *files = (const char *)drop + drop->pFiles;
				SIZE_T bytes_remaining = drop_size - drop->pFiles;

				if (drop->fWide)
				{
					const WCHAR *wpath = (const WCHAR *)files;
					size_t max_chars = bytes_remaining / sizeof(WCHAR);
					size_t pos = 0;

					while (pos < max_chars && wpath[pos])
					{
						size_t start = pos;

						while (pos < max_chars && wpath[pos])
							pos++;
						if (pos >= max_chars)
							break;
						if (pos > start)
						{
							char *path = PL_CopyClipboardPathW(wpath + start, pos - start);
							if (path)
								PL_AddClipboardFilePath(&paths, &local_count, &local_capacity, path);
						}
						pos++;
					}
				}
				else
				{
					size_t pos = 0;

					while (pos < bytes_remaining && files[pos])
					{
						size_t start = pos;

						while (pos < bytes_remaining && files[pos])
							pos++;
						if (pos >= bytes_remaining)
							break;
						if (pos > start)
						{
							char *path = PL_CopyClipboardPathA(files + start, pos - start);
							if (path)
								PL_AddClipboardFilePath(&paths, &local_count, &local_capacity, path);
						}
						pos++;
					}
				}
			}
			if (drop != NULL)
				GlobalUnlock(hClipboardData);
		}
		CloseClipboard ();
	}

	if (count)
		*count = local_count;
	return paths;
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

void PL_ErrorDialog(const char *errorMsg)
{
	MessageBox (NULL, errorMsg, "Quake Error",
			MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
}

qboolean PL_ConfirmDialog(const char *title, const char *text)
{
	int result = MessageBox(NULL, text, title,
		MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION | MB_DEFBUTTON1);
	return (result == IDYES) ? true : false;
}
