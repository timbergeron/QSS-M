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
#include <shlobj.h>
#if !defined(__WATCOMC__)
#include <shobjidl.h>
#endif
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
#define PL_QUIT_HOLD_CANCELLED_DISPLAY_MS 1000
#define PL_QUIT_HOLD_COMPLETION_MS 80
#define PL_QUIT_HOLD_TIMER_PROGRESS 1
#define PL_QUIT_HOLD_TIMER_COMPLETE 2
#define PL_QUIT_HOLD_TIMER_DISMISS 3
#define PL_QUIT_HOLD_WIDTH 356
#define PL_QUIT_HOLD_HEIGHT 72
#define PL_QUIT_HOLD_RADIUS 12
#define PL_QUIT_HOLD_TOP_OFFSET 72
#define PL_QUIT_HOLD_PROGRESS_INTERVAL_MS 10
#define PL_QUIT_HOLD_POINT_COUNT 256

static const WCHAR pl_quit_hold_class[] = L"QSSMQuitHoldOverlay";
static HWND pl_quit_hold_window;
static qboolean pl_quit_hold_active;
static qboolean pl_quit_hold_committed;
static qboolean pl_quit_hold_cancelled;
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
	HDC paint_dc;
	HDC buffer_dc;
	HBITMAP buffer_bitmap;
	HGDIOBJ old_bitmap;
	HBRUSH background;
	HPEN border;
	HFONT font;
	HGDIOBJ old_brush;
	HGDIOBJ old_pen;
	HGDIOBJ old_font;
	UINT dpi;
	int corner_radius;
	int progress_radius;
	float progress;

	dc = BeginPaint(hwnd, &paint);
	GetClientRect(hwnd, &rect);
	paint_dc = dc;
	buffer_dc = CreateCompatibleDC(dc);
	buffer_bitmap = buffer_dc ? CreateCompatibleBitmap(dc,
		rect.right - rect.left, rect.bottom - rect.top) : NULL;
	old_bitmap = buffer_bitmap ? SelectObject(buffer_dc, buffer_bitmap) : NULL;
	if (old_bitmap && old_bitmap != HGDI_ERROR)
		paint_dc = buffer_dc;
	else
	{
		if (buffer_bitmap)
			DeleteObject(buffer_bitmap);
		if (buffer_dc)
			DeleteDC(buffer_dc);
		buffer_bitmap = NULL;
		buffer_dc = NULL;
		old_bitmap = NULL;
	}
	dpi = PL_GetWindowDPI(hwnd);
	corner_radius = MulDiv(PL_QUIT_HOLD_RADIUS, dpi, 96);
	if (corner_radius < 4)
		corner_radius = 4;
	progress_radius = corner_radius - MulDiv(2, dpi, 96);
	if (progress_radius < 3)
		progress_radius = 3;

	background = CreateSolidBrush(RGB(41, 41, 41));
	border = CreatePen(PS_SOLID, 1, RGB(82, 82, 82));
	old_brush = SelectObject(paint_dc, background);
	old_pen = SelectObject(paint_dc, border);
	RoundRect(paint_dc, rect.left, rect.top, rect.right, rect.bottom,
		corner_radius * 2, corner_radius * 2);
	SelectObject(paint_dc, old_pen);
	SelectObject(paint_dc, old_brush);
	DeleteObject(border);
	DeleteObject(background);

	if (pl_quit_hold_committed)
		progress = 1.0f;
	else if (pl_quit_hold_cancelled)
		progress = 0.0f;
	else
		progress = (float)(DWORD)(GetTickCount() - pl_quit_hold_started) /
			(float)PL_QUIT_HOLD_DURATION_MS;
	PL_QuitHoldDrawProgress(paint_dc, &rect, progress, progress_radius);

	font = CreateFontW(-MulDiv(24, dpi, 96), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	old_font = font ? SelectObject(paint_dc, font) : NULL;
	SetBkMode(paint_dc, TRANSPARENT);
	SetTextColor(paint_dc, RGB(255, 255, 255));
	text_rect = rect;
	DrawTextW(paint_dc, L"Hold Ctrl+W to Quit", -1, &text_rect,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	if (old_font)
		SelectObject(paint_dc, old_font);
	if (font)
		DeleteObject(font);
	if (buffer_dc)
	{
		BitBlt(dc, rect.left, rect.top, rect.right - rect.left,
			rect.bottom - rect.top, buffer_dc, 0, 0, SRCCOPY);
		SelectObject(buffer_dc, old_bitmap);
		DeleteObject(buffer_bitmap);
		DeleteDC(buffer_dc);
	}
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
				pl_quit_hold_cancelled = false;
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
		if (wparam == PL_QUIT_HOLD_TIMER_DISMISS && pl_quit_hold_cancelled)
		{
			KillTimer(hwnd, PL_QUIT_HOLD_TIMER_DISMISS);
			pl_quit_hold_cancelled = false;
			ShowWindow(hwnd, SW_HIDE);
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
		pl_quit_hold_cancelled = false;
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
	pl_quit_hold_cancelled = false;
	if (pl_quit_hold_window && IsWindow(pl_quit_hold_window))
	{
		KillTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_PROGRESS);
		KillTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_COMPLETE);
		KillTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_DISMISS);
		ShowWindow(pl_quit_hold_window, SW_HIDE);
	}
}

static void PL_QuitHoldCancelAfterDelay (void)
{
	if (!pl_quit_hold_active || !pl_quit_hold_window || !IsWindow(pl_quit_hold_window))
		return;

	pl_quit_hold_active = false;
	pl_quit_hold_committed = false;
	pl_quit_hold_cancelled = true;
	KillTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_PROGRESS);
	KillTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_COMPLETE);
	InvalidateRect(pl_quit_hold_window, NULL, FALSE);
	UpdateWindow(pl_quit_hold_window);
	if (!SetTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_DISMISS,
		PL_QUIT_HOLD_CANCELLED_DISPLAY_MS, NULL))
		PL_QuitHoldCancel();
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
			PL_QuitHoldCancelAfterDelay();
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
	radius = MulDiv(PL_QUIT_HOLD_RADIUS, dpi, 96);
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

	if (pl_quit_hold_cancelled)
		KillTimer(pl_quit_hold_window, PL_QUIT_HOLD_TIMER_DISMISS);
	pl_quit_hold_active = true;
	pl_quit_hold_committed = false;
	pl_quit_hold_cancelled = false;
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

#if !defined(__WATCOMC__)
#define PL_MESSAGE_DIALOG_CLASS L"QSSMMessageDialog"
#define PL_MESSAGE_TEXT_ID 4099
#define PL_MESSAGE_BUTTON_ID_BASE 4100
#define PL_MESSAGE_MIN_WIDTH 420
#define PL_MESSAGE_MAX_WIDTH 820
#define PL_SEARCH_PROGRESS_CLASS L"QSSMSearchProgress"
#define PL_SEARCH_CANCEL_BUTTON_ID 4200

typedef struct
{
	HWND window;
	HWND owner;
	HWND message_window;
	HWND *button_windows;
	wchar_t **button_text;
	int *button_ids;
	int num_buttons;
	int default_button;
	int cancel_button;
	int selected_button;
	wchar_t *title;
	wchar_t *message;
	HFONT font;
	HICON information_icon;
	HBRUSH message_background;
	RECT message_rect;
	int separator_y;
	UINT dpi;
	qboolean dark;
	qboolean owner_disabled;
} pl_message_dialog_state_t;

struct pl_search_progress_s
{
	HWND window;
	HWND owner;
	HWND cancel_button;
	HFONT font;
	wchar_t *title;
	wchar_t phase[256];
	wchar_t location[1024];
	wchar_t count[128];
	UINT dpi;
	qboolean dark;
	qboolean cancelled;
	qboolean owner_disabled;
	qboolean quit_pending;
	int quit_code;
	DWORD last_visual_update;
};

static wchar_t *PL_MessageWideString (const char *text)
{
	UINT codepage = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;
	int length;
	wchar_t *wide;

	if (!text)
		text = "";
	length = MultiByteToWideChar(codepage, flags, text, -1, NULL, 0);
	if (length <= 0)
	{
		codepage = CP_ACP;
		flags = 0;
		length = MultiByteToWideChar(codepage, flags, text, -1, NULL, 0);
	}
	if (length <= 0)
		return NULL;
	wide = (wchar_t *)calloc((size_t)length, sizeof(*wide));
	if (!wide)
		return NULL;
	if (!MultiByteToWideChar(codepage, flags, text, -1, wide, length))
	{
		free(wide);
		return NULL;
	}
	return wide;
}

static qboolean PL_MessageSystemUsesDarkMode (void)
{
	static const wchar_t personalize[] =
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
	HIGHCONTRASTW high_contrast;
	HKEY key;
	DWORD value = 1;
	DWORD type = 0;
	DWORD size = sizeof(value);

	memset(&high_contrast, 0, sizeof(high_contrast));
	high_contrast.cbSize = sizeof(high_contrast);
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast),
		&high_contrast, 0) && (high_contrast.dwFlags & HCF_HIGHCONTRASTON))
		return false;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, personalize, 0, KEY_QUERY_VALUE,
		&key) != ERROR_SUCCESS)
		return false;
	if (RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type,
		(LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
		value = 1;
	RegCloseKey(key);
	return value == 0;
}

static HBRUSH PL_MessageCreateBackgroundBrush (qboolean dark)
{
	return CreateSolidBrush(dark ? RGB(32, 32, 32) :
		GetSysColor(COLOR_WINDOW));
}

static void PL_MessageApplyDarkTitleBar (HWND window, qboolean dark)
{
	typedef HRESULT (WINAPI *dwm_set_window_attribute_fn)(HWND, DWORD,
		LPCVOID, DWORD);
	union
	{
		FARPROC generic;
		dwm_set_window_attribute_fn typed;
	} resolver;
	HMODULE dwmapi;
	dwm_set_window_attribute_fn set_attribute;
	BOOL enabled = dark ? TRUE : FALSE;
	HRESULT result;

	dwmapi = LoadLibraryW(L"dwmapi.dll");
	if (!dwmapi)
		return;
	resolver.generic = GetProcAddress(dwmapi, "DwmSetWindowAttribute");
	set_attribute = resolver.typed;
	if (set_attribute)
	{
		result = set_attribute(window, 20, &enabled, sizeof(enabled));
		if (FAILED(result))
			(void)set_attribute(window, 19, &enabled, sizeof(enabled));
	}
	FreeLibrary(dwmapi);
}

static HFONT PL_MessageCreateFont (UINT dpi)
{
	typedef BOOL (WINAPI *system_parameters_info_for_dpi_fn)(UINT, UINT,
		PVOID, UINT, UINT);
	union
	{
		FARPROC generic;
		system_parameters_info_for_dpi_fn typed;
	} resolver;
	NONCLIENTMETRICSW metrics;
	HFONT font = NULL;
	HMODULE user32 = GetModuleHandleW(L"user32.dll");

	memset(&metrics, 0, sizeof(metrics));
	metrics.cbSize = sizeof(metrics);
	resolver.generic = user32 ? GetProcAddress(user32,
		"SystemParametersInfoForDpi") : NULL;
	if (resolver.typed && resolver.typed(SPI_GETNONCLIENTMETRICS,
		sizeof(metrics), &metrics, 0, dpi))
		font = CreateFontIndirectW(&metrics.lfMessageFont);
	else if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
		&metrics, 0))
		font = CreateFontIndirectW(&metrics.lfMessageFont);
	if (!font)
		font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL,
			FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	return font;
}

static HICON PL_MessageLoadInformationIcon (UINT dpi)
{
	return (HICON)LoadImageW(NULL,
		MAKEINTRESOURCEW(LOWORD((ULONG_PTR)IDI_INFORMATION)), IMAGE_ICON,
		MulDiv(32, dpi, 96), MulDiv(32, dpi, 96), LR_DEFAULTCOLOR);
}

static int PL_MessageScale (const pl_message_dialog_state_t *state, int value)
{
	return MulDiv(value, state->dpi, 96);
}

static void PL_MessageGetWorkArea (HWND owner, RECT *work_area)
{
	MONITORINFO monitor_info;
	HMONITOR monitor;
	POINT point;

	if (owner)
		monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
	else
	{
		GetCursorPos(&point);
		monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
	}
	memset(&monitor_info, 0, sizeof(monitor_info));
	monitor_info.cbSize = sizeof(monitor_info);
	if (monitor && GetMonitorInfoW(monitor, &monitor_info))
		*work_area = monitor_info.rcWork;
	else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, work_area, 0))
		SetRect(work_area, 0, 0, GetSystemMetrics(SM_CXSCREEN),
			GetSystemMetrics(SM_CYSCREEN));
}

static int PL_MessageFindButton (const pl_message_dialog_state_t *state,
	int button_id)
{
	int i;
	for (i = 0; i < state->num_buttons; ++i)
		if (state->button_ids[i] == button_id)
			return i;
	return -1;
}

static void PL_MessageChooseButton (pl_message_dialog_state_t *state,
	int index)
{
	if (!state || index < 0 || index >= state->num_buttons)
		return;
	state->selected_button = state->button_ids[index];
	if (state->window)
		DestroyWindow(state->window);
}

static void PL_MessagePaint (pl_message_dialog_state_t *state)
{
	PAINTSTRUCT paint;
	HDC dc;
	RECT client;
	RECT icon_rect;
	HBRUSH background;
	HPEN separator;
	HPEN old_pen;
	COLORREF background_color;
	COLORREF separator_color;
	int icon_size;

	dc = BeginPaint(state->window, &paint);
	GetClientRect(state->window, &client);
	background_color = state->dark ? RGB(32, 32, 32) :
		GetSysColor(COLOR_WINDOW);
	separator_color = state->dark ? RGB(68, 68, 68) :
		GetSysColor(COLOR_3DLIGHT);
	background = CreateSolidBrush(background_color);
	if (background)
	{
		FillRect(dc, &client, background);
		DeleteObject(background);
	}
	icon_size = PL_MessageScale(state, 32);
	SetRect(&icon_rect, PL_MessageScale(state, 24), PL_MessageScale(state, 24),
		PL_MessageScale(state, 24) + icon_size,
		PL_MessageScale(state, 24) + icon_size);
	if (state->information_icon)
		DrawIconEx(dc, icon_rect.left, icon_rect.top, state->information_icon,
			icon_size, icon_size, 0, NULL, DI_NORMAL);
	separator = CreatePen(PS_SOLID, 1, separator_color);
	if (separator)
	{
		old_pen = SelectObject(dc, separator);
		MoveToEx(dc, 0, state->separator_y, NULL);
		LineTo(dc, client.right, state->separator_y);
		SelectObject(dc, old_pen);
		DeleteObject(separator);
	}
	EndPaint(state->window, &paint);
}

static void PL_MessageDrawButton (pl_message_dialog_state_t *state,
	const DRAWITEMSTRUCT *draw)
{
	RECT rect = draw->rcItem;
	RECT text_rect;
	HBRUSH background;
	HPEN border;
	HPEN old_pen;
	HBRUSH old_brush;
	HFONT old_font;
	COLORREF background_color;
	COLORREF border_color;
	COLORREF text_color;
	int index;
	qboolean is_default;

	index = (int)draw->CtlID - PL_MESSAGE_BUTTON_ID_BASE;
	if (index < 0 || index >= state->num_buttons)
		return;
	is_default = state->button_ids[index] == state->default_button;
	if (state->dark)
	{
		background_color = (draw->itemState & ODS_SELECTED) ? RGB(72, 72, 72) :
			(draw->itemState & ODS_HOTLIGHT) ? RGB(58, 58, 58) : RGB(45, 45, 45);
		border_color = is_default ? RGB(92, 155, 255) : RGB(105, 105, 105);
		text_color = RGB(245, 245, 245);
	}
	else
	{
		background_color = (draw->itemState & ODS_SELECTED) ?
			GetSysColor(COLOR_3DSHADOW) : GetSysColor(COLOR_BTNFACE);
		border_color = is_default ? GetSysColor(COLOR_HIGHLIGHT) :
			GetSysColor(COLOR_3DSHADOW);
		text_color = GetSysColor(COLOR_BTNTEXT);
	}
	background = CreateSolidBrush(background_color);
	border = CreatePen(PS_SOLID, is_default ? 2 : 1, border_color);
	old_brush = background ? SelectObject(draw->hDC, background) : NULL;
	old_pen = border ? SelectObject(draw->hDC, border) : NULL;
	Rectangle(draw->hDC, rect.left, rect.top, rect.right, rect.bottom);
	if (old_pen)
		SelectObject(draw->hDC, old_pen);
	if (old_brush)
		SelectObject(draw->hDC, old_brush);
	if (border)
		DeleteObject(border);
	if (background)
		DeleteObject(background);
	text_rect = rect;
	if (draw->itemState & ODS_SELECTED)
		OffsetRect(&text_rect, 1, 1);
	old_font = state->font ? SelectObject(draw->hDC, state->font) : NULL;
	SetBkMode(draw->hDC, TRANSPARENT);
	SetTextColor(draw->hDC, text_color);
	DrawTextW(draw->hDC, state->button_text[index], -1, &text_rect,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	if (draw->itemState & ODS_FOCUS)
	{
		InflateRect(&rect, -PL_MessageScale(state, 4),
			-PL_MessageScale(state, 4));
		DrawFocusRect(draw->hDC, &rect);
	}
	if (old_font)
		SelectObject(draw->hDC, old_font);
}

static void PL_MessageHandleDPIChange(pl_message_dialog_state_t *state,
	UINT dpi, const RECT *suggested);

static LRESULT CALLBACK PL_MessageWindowProc (HWND window, UINT message,
	WPARAM wparam, LPARAM lparam)
{
	pl_message_dialog_state_t *state =
		(pl_message_dialog_state_t *)GetWindowLongPtrW(window, GWLP_USERDATA);

	if (message == WM_NCCREATE)
	{
		CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
		state = (pl_message_dialog_state_t *)create->lpCreateParams;
		SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
		state->window = window;
	}
	if (!state)
		return DefWindowProcW(window, message, wparam, lparam);

	switch (message)
	{
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
		PL_MessagePaint(state);
		return 0;
	case WM_DRAWITEM:
		if (wparam >= PL_MESSAGE_BUTTON_ID_BASE &&
			wparam < (WPARAM)(PL_MESSAGE_BUTTON_ID_BASE + state->num_buttons))
		{
			PL_MessageDrawButton(state, (const DRAWITEMSTRUCT *)lparam);
			return TRUE;
		}
		break;
	case WM_CTLCOLORSTATIC:
		if ((HWND)lparam == state->message_window)
		{
			HDC dc = (HDC)wparam;
			COLORREF background = state->dark ? RGB(32, 32, 32) :
				GetSysColor(COLOR_WINDOW);
			SetTextColor(dc, state->dark ? RGB(240, 240, 240) :
				GetSysColor(COLOR_WINDOWTEXT));
			SetBkColor(dc, background);
			return (LRESULT)state->message_background;
		}
		break;
	case WM_COMMAND:
		if (HIWORD(wparam) == BN_CLICKED &&
			LOWORD(wparam) >= PL_MESSAGE_BUTTON_ID_BASE &&
			LOWORD(wparam) < PL_MESSAGE_BUTTON_ID_BASE + state->num_buttons)
		{
			PL_MessageChooseButton(state,
				LOWORD(wparam) - PL_MESSAGE_BUTTON_ID_BASE);
			return 0;
		}
		break;
	case WM_DPICHANGED:
		PL_MessageHandleDPIChange(state, HIWORD(wparam),
			(const RECT *)lparam);
		return 0;
	case WM_SETTINGCHANGE:
	case WM_THEMECHANGED:
	{
		int i;
		HBRUSH background;
		state->dark = PL_MessageSystemUsesDarkMode();
		background = PL_MessageCreateBackgroundBrush(state->dark);
		if (background)
		{
			if (state->message_background)
				DeleteObject(state->message_background);
			state->message_background = background;
		}
		PL_MessageApplyDarkTitleBar(window, state->dark);
		InvalidateRect(window, NULL, TRUE);
		if (state->message_window)
			InvalidateRect(state->message_window, NULL, TRUE);
		for (i = 0; i < state->num_buttons; ++i)
			if (state->button_windows[i])
				InvalidateRect(state->button_windows[i], NULL, TRUE);
		return 0;
	}
	case WM_CLOSE:
		state->selected_button = state->cancel_button;
		DestroyWindow(window);
		return 0;
	case WM_DESTROY:
		state->window = NULL;
		return 0;
	default:
		break;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

static qboolean PL_MessageRegisterClass (void)
{
	static qboolean registered;
	WNDCLASSEXW window_class;

	if (registered)
		return true;
	memset(&window_class, 0, sizeof(window_class));
	window_class.cbSize = sizeof(window_class);
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	window_class.lpfnWndProc = PL_MessageWindowProc;
	window_class.hInstance = GetModuleHandleW(NULL);
	window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
	window_class.lpszClassName = PL_MESSAGE_DIALOG_CLASS;
	if (!RegisterClassExW(&window_class) &&
		GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;
	registered = true;
	return true;
}

static qboolean PL_MessageMeasureAndPosition (pl_message_dialog_state_t *state,
	RECT *window_rect, int *button_widths)
{
	const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN;
	const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
	RECT work_area;
	RECT text_rect;
	RECT adjusted;
	HDC dc;
	HFONT old_font;
	SIZE text_size;
	int margin;
	int icon_space;
	int gap;
	int button_height;
	int button_total = 0;
	int available_client_width;
	int max_client_width;
	int client_width;
	int client_height;
	int content_height;
	int x;
	int y;
	int i;

	PL_MessageGetWorkArea(state->owner, &work_area);
	margin = PL_MessageScale(state, 24);
	icon_space = PL_MessageScale(state, 48);
	gap = PL_MessageScale(state, 8);
	button_height = PL_MessageScale(state, 32);
	available_client_width = work_area.right - work_area.left -
		PL_MessageScale(state, 16);
	max_client_width = work_area.right - work_area.left - PL_MessageScale(state, 48);
	if (max_client_width > PL_MessageScale(state, PL_MESSAGE_MAX_WIDTH))
		max_client_width = PL_MessageScale(state, PL_MESSAGE_MAX_WIDTH);
	if (max_client_width < PL_MessageScale(state, PL_MESSAGE_MIN_WIDTH))
		max_client_width = work_area.right - work_area.left - PL_MessageScale(state, 16);

	dc = GetDC(NULL);
	if (!dc)
		return false;
	old_font = state->font ? SelectObject(dc, state->font) : NULL;
	for (i = 0; i < state->num_buttons; ++i)
	{
		if (!GetTextExtentPoint32W(dc, state->button_text[i],
			(int)wcslen(state->button_text[i]), &text_size))
			text_size.cx = 0;
		button_widths[i] = text_size.cx + PL_MessageScale(state, 28);
		if (button_widths[i] < PL_MessageScale(state, 80))
			button_widths[i] = PL_MessageScale(state, 80);
		button_total += button_widths[i];
		if (i > 0)
			button_total += gap;
	}
	if (button_total + margin * 2 > available_client_width)
	{
		if (old_font)
			SelectObject(dc, old_font);
		ReleaseDC(NULL, dc);
		return false;
	}
	if (button_total + margin * 2 > max_client_width)
		max_client_width = button_total + margin * 2;
	SetRect(&text_rect, 0, 0,
		max_client_width - margin * 2 - icon_space, 0);
	DrawTextW(dc, state->message, -1, &text_rect,
		DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX |
		DT_EDITCONTROL);
	client_width = text_rect.right + margin * 2 + icon_space;
	if (client_width < button_total + margin * 2)
		client_width = button_total + margin * 2;
	if (client_width < PL_MessageScale(state, PL_MESSAGE_MIN_WIDTH))
		client_width = PL_MessageScale(state, PL_MESSAGE_MIN_WIDTH);
	if (client_width > max_client_width)
		client_width = max_client_width;
	SetRect(&text_rect, 0, 0, client_width - margin * 2 - icon_space, 0);
	DrawTextW(dc, state->message, -1, &text_rect,
		DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX |
		DT_EDITCONTROL);
	if (old_font)
		SelectObject(dc, old_font);
	ReleaseDC(NULL, dc);

	content_height = text_rect.bottom;
	if (content_height < PL_MessageScale(state, 32))
		content_height = PL_MessageScale(state, 32);
	client_height = margin + content_height + PL_MessageScale(state, 24) + 1 +
		PL_MessageScale(state, 12) + button_height + PL_MessageScale(state, 16);
	SetRect(&adjusted, 0, 0, client_width, client_height);
	if (!AdjustWindowRectEx(&adjusted, style, FALSE, ex_style))
		return false;
	x = work_area.left + ((work_area.right - work_area.left) -
		(adjusted.right - adjusted.left)) / 2;
	y = work_area.top + ((work_area.bottom - work_area.top) -
		(adjusted.bottom - adjusted.top)) / 2;
	SetRect(window_rect, x, y, x + adjusted.right - adjusted.left,
		y + adjusted.bottom - adjusted.top);
	SetRect(&state->message_rect, margin + icon_space, margin,
		client_width - margin, margin + text_rect.bottom);
	state->separator_y = margin + content_height + PL_MessageScale(state, 24);
	return true;
}

static void PL_MessagePositionButtons (pl_message_dialog_state_t *state,
	const int *button_widths)
{
	RECT client;
	int margin = PL_MessageScale(state, 24);
	int gap = PL_MessageScale(state, 8);
	int height = PL_MessageScale(state, 32);
	int y;
	int x;
	int i;

	GetClientRect(state->window, &client);
	y = state->separator_y + PL_MessageScale(state, 12);
	x = client.right - margin;
	for (i = state->num_buttons - 1; i >= 0; --i)
	{
		x -= button_widths[i];
		SetWindowPos(state->button_windows[i], NULL, x, y, button_widths[i],
			height, SWP_NOZORDER | SWP_NOACTIVATE);
		x -= gap;
	}
}

static void PL_MessageHandleDPIChange(pl_message_dialog_state_t *state,
	UINT dpi, const RECT *suggested)
{
	RECT measured;
	HFONT font;
	HFONT old_font;
	HICON icon;
	HICON old_icon;
	int *button_widths;
	UINT old_dpi;
	int i;
	int x;
	int y;

	if (!state || !state->window || !dpi || dpi == state->dpi)
		return;
	font = PL_MessageCreateFont(dpi);
	button_widths = (int *)calloc((size_t)state->num_buttons,
		sizeof(*button_widths));
	if (!font || !button_widths)
	{
		if (font) DeleteObject(font);
		free(button_widths);
		return;
	}
	old_dpi = state->dpi;
	old_font = state->font;
	state->dpi = dpi;
	state->font = font;
	if (!PL_MessageMeasureAndPosition(state, &measured, button_widths))
	{
		state->dpi = old_dpi;
		state->font = old_font;
		DeleteObject(font);
		free(button_widths);
		return;
	}
	x = suggested ? suggested->left : measured.left;
	y = suggested ? suggested->top : measured.top;
	SetWindowPos(state->window, NULL, x, y,
		measured.right - measured.left, measured.bottom - measured.top,
		SWP_NOZORDER | SWP_NOACTIVATE);
	if (state->message_window)
	{
		SetWindowPos(state->message_window, NULL, state->message_rect.left,
			state->message_rect.top,
			state->message_rect.right - state->message_rect.left,
			state->message_rect.bottom - state->message_rect.top,
			SWP_NOZORDER | SWP_NOACTIVATE);
		SendMessageW(state->message_window, WM_SETFONT, (WPARAM)font, FALSE);
	}
	for (i = 0; i < state->num_buttons; ++i)
		if (state->button_windows[i])
			SendMessageW(state->button_windows[i], WM_SETFONT,
				(WPARAM)font, FALSE);
	PL_MessagePositionButtons(state, button_widths);
	icon = PL_MessageLoadInformationIcon(dpi);
	if (icon)
	{
		old_icon = state->information_icon;
		state->information_icon = icon;
		if (old_icon) DestroyIcon(old_icon);
	}
	if (old_font) DeleteObject(old_font);
	free(button_widths);
	InvalidateRect(state->window, NULL, TRUE);
}

static void PL_MessageFreeState (pl_message_dialog_state_t *state)
{
	int i;
	if (!state)
		return;
	if (state->information_icon)
		DestroyIcon(state->information_icon);
	if (state->message_background)
		DeleteObject(state->message_background);
	if (state->font)
		DeleteObject(state->font);
	for (i = 0; i < state->num_buttons; ++i)
		free(state->button_text ? state->button_text[i] : NULL);
	free(state->button_windows);
	free(state->button_text);
	free(state->button_ids);
	free(state->title);
	free(state->message);
}

static qboolean PL_ShowNativeMessageDialog (const char *title,
	const char *message, const pl_dialog_button_t *buttons, int num_buttons,
	int default_button, int cancel_button, int *selected)
{
	const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN;
	const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
	pl_message_dialog_state_t state;
	RECT window_rect;
	int *button_widths = NULL;
	HINSTANCE instance = GetModuleHandleW(NULL);
	MSG event;
	int default_index;
	int cancel_index;
	int i;
	int get_message_result;
	qboolean repost_quit = false;
	int quit_code = 0;
	qboolean shown = false;

	if (num_buttons <= 0 || num_buttons > 32 || !PL_MessageRegisterClass())
		return false;
	memset(&state, 0, sizeof(state));
	state.owner = PL_GetNativeWindow();
	state.num_buttons = num_buttons;
	state.default_button = default_button;
	state.cancel_button = cancel_button;
	state.selected_button = cancel_button;
	state.dpi = PL_GetWindowDPI(state.owner);
	state.dark = PL_MessageSystemUsesDarkMode();
	state.message_background = PL_MessageCreateBackgroundBrush(state.dark);
	state.title = PL_MessageWideString(title);
	state.message = PL_MessageWideString(message);
	state.button_windows = (HWND *)calloc((size_t)num_buttons,
		sizeof(*state.button_windows));
	state.button_text = (wchar_t **)calloc((size_t)num_buttons,
		sizeof(*state.button_text));
	state.button_ids = (int *)calloc((size_t)num_buttons,
		sizeof(*state.button_ids));
	button_widths = (int *)calloc((size_t)num_buttons, sizeof(*button_widths));
	if (!state.title || !state.message || !state.message_background || !state.button_windows ||
		!state.button_text || !state.button_ids || !button_widths)
		goto done;
	for (i = 0; i < num_buttons; ++i)
	{
		state.button_text[i] = PL_MessageWideString(buttons[i].text);
		state.button_ids[i] = buttons[i].id;
		if (!state.button_text[i])
			goto done;
	}
	state.font = PL_MessageCreateFont(state.dpi);
	state.information_icon = PL_MessageLoadInformationIcon(state.dpi);
	if (!state.font || !PL_MessageMeasureAndPosition(&state, &window_rect,
		button_widths))
		goto done;
	state.window = CreateWindowExW(ex_style, PL_MESSAGE_DIALOG_CLASS,
		state.title, style, window_rect.left, window_rect.top,
		window_rect.right - window_rect.left, window_rect.bottom - window_rect.top,
		state.owner, NULL, instance, &state);
	if (!state.window)
		goto done;
	state.message_window = CreateWindowExW(0, L"STATIC", state.message,
		WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_EDITCONTROL,
		state.message_rect.left, state.message_rect.top,
		state.message_rect.right - state.message_rect.left,
		state.message_rect.bottom - state.message_rect.top,
		state.window, (HMENU)(INT_PTR)PL_MESSAGE_TEXT_ID, instance, NULL);
	if (!state.message_window)
	{
		DestroyWindow(state.window);
		goto done;
	}
	SendMessageW(state.message_window, WM_SETFONT, (WPARAM)state.font, FALSE);
	for (i = 0; i < num_buttons; ++i)
	{
		state.button_windows[i] = CreateWindowExW(0, L"BUTTON",
			state.button_text[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
			0, 0, 0, 0, state.window,
			(HMENU)(INT_PTR)(PL_MESSAGE_BUTTON_ID_BASE + i), instance, NULL);
		if (!state.button_windows[i])
		{
			DestroyWindow(state.window);
			goto done;
		}
		SendMessageW(state.button_windows[i], WM_SETFONT,
			(WPARAM)state.font, FALSE);
	}
	PL_MessagePositionButtons(&state, button_widths);
	PL_MessageApplyDarkTitleBar(state.window, state.dark);
	if (state.owner && IsWindowEnabled(state.owner))
	{
		EnableWindow(state.owner, FALSE);
		state.owner_disabled = true;
	}
	ShowWindow(state.window, SW_SHOWNORMAL);
	UpdateWindow(state.window);
	SetForegroundWindow(state.window);
	default_index = PL_MessageFindButton(&state, default_button);
	if (default_index < 0)
		default_index = 0;
	cancel_index = PL_MessageFindButton(&state, cancel_button);
	SetFocus(state.button_windows[default_index]);
	shown = true;

	while (state.window && IsWindow(state.window))
	{
		get_message_result = GetMessageW(&event, NULL, 0, 0);
		if (get_message_result <= 0)
		{
			if (get_message_result == 0)
			{
				repost_quit = true;
				quit_code = (int)event.wParam;
			}
			if (state.window && IsWindow(state.window))
				DestroyWindow(state.window);
			break;
		}
		if ((event.hwnd == state.window || IsChild(state.window, event.hwnd)) &&
			event.message == WM_KEYDOWN)
		{
			if (event.wParam == VK_RETURN)
			{
				int focused_index;
				HWND focused = GetFocus();
				for (focused_index = 0; focused_index < state.num_buttons;
					++focused_index)
					if (state.button_windows[focused_index] == focused)
						break;
				PL_MessageChooseButton(&state,
					focused_index < state.num_buttons ? focused_index : default_index);
				continue;
			}
			if (event.wParam == VK_ESCAPE)
			{
				if (cancel_index >= 0)
					PL_MessageChooseButton(&state, cancel_index);
				else if (state.window)
					DestroyWindow(state.window);
				continue;
			}
			if (event.wParam == VK_TAB)
			{
				HWND next = GetNextDlgTabItem(state.window, GetFocus(),
					(GetKeyState(VK_SHIFT) & 0x8000) != 0);
				if (next)
					SetFocus(next);
				continue;
			}
		}
		TranslateMessage(&event);
		DispatchMessageW(&event);
	}
	if (state.owner_disabled)
	{
		EnableWindow(state.owner, TRUE);
		SetActiveWindow(state.owner);
	}
	if (repost_quit)
		PostQuitMessage(quit_code);
	*selected = state.selected_button;

done:
	if (state.window && IsWindow(state.window))
		DestroyWindow(state.window);
	free(button_widths);
	PL_MessageFreeState(&state);
	return shown;
}

static int PL_SearchProgressScale (const pl_search_progress_t *progress,
	int value)
{
	return MulDiv(value, progress->dpi, 96);
}

static void PL_SearchProgressSetText (wchar_t *destination, size_t capacity,
	const char *text)
{
	wchar_t *wide = PL_MessageWideString(text ? text : "");
	if (!capacity)
	{
		free(wide);
		return;
	}
	if (wide)
	{
		wcsncpy(destination, wide, capacity - 1);
		destination[capacity - 1] = L'\0';
		free(wide);
	}
	else
		destination[0] = L'\0';
}

static void PL_SearchProgressTrackRect (pl_search_progress_t *progress,
	RECT *track)
{
	RECT client;
	int margin = PL_SearchProgressScale(progress, 24);
	int top = PL_SearchProgressScale(progress, 122);
	GetClientRect(progress->window, &client);
	SetRect(track, margin, top, client.right - margin,
		top + PL_SearchProgressScale(progress, 8));
}

static void PL_SearchProgressPaint (pl_search_progress_t *progress)
{
	PAINTSTRUCT paint;
	HDC target = BeginPaint(progress->window, &paint);
	HDC memory = NULL;
	HDC dc = target;
	HBITMAP bitmap = NULL;
	HBITMAP old_bitmap = NULL;
	RECT client, text_rect, track, segment, clipped;
	HBRUSH brush;
	HPEN separator, old_pen;
	HFONT old_font;
	COLORREF background_color = progress->dark ? RGB(32, 32, 32) :
		GetSysColor(COLOR_WINDOW);
	COLORREF text_color = progress->dark ? RGB(240, 240, 240) :
		GetSysColor(COLOR_WINDOWTEXT);
	COLORREF secondary_color = progress->dark ? RGB(185, 185, 185) :
		GetSysColor(COLOR_GRAYTEXT);
	COLORREF track_color = progress->dark ? RGB(62, 62, 62) : RGB(218, 218, 218);
	COLORREF accent_color = progress->dark ? RGB(74, 137, 230) :
		GetSysColor(COLOR_HIGHLIGHT);
	int margin = PL_SearchProgressScale(progress, 24);
	int separator_y = PL_SearchProgressScale(progress, 158);
	int segment_width, travel, segment_x;
	DWORD cycle;

	GetClientRect(progress->window, &client);
	memory = CreateCompatibleDC(target);
	if (memory)
	{
		bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
		if (bitmap)
		{
			old_bitmap = (HBITMAP)SelectObject(memory, bitmap);
			dc = memory;
		}
	}
	brush = CreateSolidBrush(background_color);
	if (brush)
	{
		FillRect(dc, &client, brush);
		DeleteObject(brush);
	}
	old_font = progress->font ? SelectObject(dc, progress->font) : NULL;
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, text_color);
	SetRect(&text_rect, margin, PL_SearchProgressScale(progress, 22),
		client.right - margin, PL_SearchProgressScale(progress, 48));
	DrawTextW(dc, progress->phase, -1, &text_rect,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	SetTextColor(dc, secondary_color);
	SetRect(&text_rect, margin, PL_SearchProgressScale(progress, 56),
		client.right - margin, PL_SearchProgressScale(progress, 80));
	DrawTextW(dc, progress->location, -1, &text_rect,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
	SetRect(&text_rect, margin, PL_SearchProgressScale(progress, 86),
		client.right - margin, PL_SearchProgressScale(progress, 110));
	DrawTextW(dc, progress->count, -1, &text_rect,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	if (old_font)
		SelectObject(dc, old_font);

	PL_SearchProgressTrackRect(progress, &track);
	brush = CreateSolidBrush(track_color);
	if (brush)
	{
		FillRect(dc, &track, brush);
		DeleteObject(brush);
	}
	segment_width = (track.right - track.left) / 5;
	if (segment_width < PL_SearchProgressScale(progress, 48))
		segment_width = PL_SearchProgressScale(progress, 48);
	travel = track.right - track.left + segment_width;
	cycle = GetTickCount() % 1400u;
	segment_x = track.left - segment_width + (int)((uint64_t)cycle *
		(uint64_t)travel / 1400u);
	SetRect(&segment, segment_x, track.top, segment_x + segment_width,
		track.bottom);
	if (IntersectRect(&clipped, &track, &segment))
	{
		brush = CreateSolidBrush(accent_color);
		if (brush)
		{
			FillRect(dc, &clipped, brush);
			DeleteObject(brush);
		}
	}
	separator = CreatePen(PS_SOLID, 1, progress->dark ? RGB(68, 68, 68) :
		GetSysColor(COLOR_3DLIGHT));
	if (separator)
	{
		old_pen = SelectObject(dc, separator);
		MoveToEx(dc, 0, separator_y, NULL);
		LineTo(dc, client.right, separator_y);
		SelectObject(dc, old_pen);
		DeleteObject(separator);
	}
	if (dc == memory)
	{
		BitBlt(target, paint.rcPaint.left, paint.rcPaint.top,
			paint.rcPaint.right - paint.rcPaint.left,
			paint.rcPaint.bottom - paint.rcPaint.top, memory,
			paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
		SelectObject(memory, old_bitmap);
		DeleteObject(bitmap);
	}
	if (memory)
		DeleteDC(memory);
	EndPaint(progress->window, &paint);
}

static void PL_SearchProgressDrawButton (pl_search_progress_t *progress,
	const DRAWITEMSTRUCT *draw)
{
	RECT rect = draw->rcItem;
	RECT text_rect = rect;
	HBRUSH background;
	HPEN border, old_pen;
	HBRUSH old_brush;
	HFONT old_font;
	COLORREF background_color;
	COLORREF border_color;
	COLORREF text_color;

	if (progress->dark)
	{
		background_color = (draw->itemState & ODS_SELECTED) ? RGB(72, 72, 72) :
			(draw->itemState & ODS_HOTLIGHT) ? RGB(58, 58, 58) : RGB(45, 45, 45);
		border_color = (draw->itemState & ODS_FOCUS) ? RGB(92, 155, 255) :
			RGB(105, 105, 105);
		text_color = RGB(245, 245, 245);
	}
	else
	{
		background_color = (draw->itemState & ODS_SELECTED) ?
			GetSysColor(COLOR_3DSHADOW) : GetSysColor(COLOR_BTNFACE);
		border_color = (draw->itemState & ODS_FOCUS) ?
			GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_3DSHADOW);
		text_color = GetSysColor(COLOR_BTNTEXT);
	}
	background = CreateSolidBrush(background_color);
	border = CreatePen(PS_SOLID, (draw->itemState & ODS_FOCUS) ? 2 : 1,
		border_color);
	old_brush = background ? SelectObject(draw->hDC, background) : NULL;
	old_pen = border ? SelectObject(draw->hDC, border) : NULL;
	Rectangle(draw->hDC, rect.left, rect.top, rect.right, rect.bottom);
	if (old_pen) SelectObject(draw->hDC, old_pen);
	if (old_brush) SelectObject(draw->hDC, old_brush);
	if (border) DeleteObject(border);
	if (background) DeleteObject(background);
	if (draw->itemState & ODS_SELECTED)
		OffsetRect(&text_rect, 1, 1);
	old_font = progress->font ? SelectObject(draw->hDC, progress->font) : NULL;
	SetBkMode(draw->hDC, TRANSPARENT);
	SetTextColor(draw->hDC, text_color);
	DrawTextW(draw->hDC, L"Cancel", -1, &text_rect,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	if (draw->itemState & ODS_FOCUS)
	{
		InflateRect(&rect, -PL_SearchProgressScale(progress, 4),
			-PL_SearchProgressScale(progress, 4));
		DrawFocusRect(draw->hDC, &rect);
	}
	if (old_font) SelectObject(draw->hDC, old_font);
}

static void PL_SearchProgressCancel (pl_search_progress_t *progress)
{
	progress->cancelled = true;
	if (progress->window && IsWindow(progress->window))
		DestroyWindow(progress->window);
}

static void PL_SearchProgressHandleDPIChange(pl_search_progress_t *progress,
	UINT dpi, const RECT *suggested)
{
	const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN;
	const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
	RECT adjusted;
	RECT client;
	HFONT font;
	HFONT old_font;
	UINT old_dpi;
	int x;
	int y;

	if (!progress || !progress->window || !dpi || dpi == progress->dpi)
		return;
	font = PL_MessageCreateFont(dpi);
	if (!font)
		return;
	old_dpi = progress->dpi;
	old_font = progress->font;
	progress->dpi = dpi;
	progress->font = font;
	SetRect(&adjusted, 0, 0, PL_SearchProgressScale(progress, 560),
		PL_SearchProgressScale(progress, 216));
	if (!AdjustWindowRectEx(&adjusted, style, FALSE, ex_style))
	{
		progress->dpi = old_dpi;
		progress->font = old_font;
		DeleteObject(font);
		return;
	}
	x = suggested ? suggested->left : 0;
	y = suggested ? suggested->top : 0;
	SetWindowPos(progress->window, NULL, x, y,
		adjusted.right - adjusted.left, adjusted.bottom - adjusted.top,
		SWP_NOZORDER | SWP_NOACTIVATE);
	GetClientRect(progress->window, &client);
	if (progress->cancel_button)
	{
		SendMessageW(progress->cancel_button, WM_SETFONT, (WPARAM)font, FALSE);
		SetWindowPos(progress->cancel_button, NULL,
			client.right - PL_SearchProgressScale(progress, 24 + 92),
			PL_SearchProgressScale(progress, 172),
			PL_SearchProgressScale(progress, 92),
			PL_SearchProgressScale(progress, 32),
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (old_font) DeleteObject(old_font);
	InvalidateRect(progress->window, NULL, TRUE);
}

static LRESULT CALLBACK PL_SearchProgressWindowProc (HWND window, UINT message,
	WPARAM wparam, LPARAM lparam)
{
	pl_search_progress_t *progress =
		(pl_search_progress_t *)GetWindowLongPtrW(window, GWLP_USERDATA);
	if (message == WM_NCCREATE)
	{
		CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
		progress = (pl_search_progress_t *)create->lpCreateParams;
		SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)progress);
		progress->window = window;
	}
	if (!progress)
		return DefWindowProcW(window, message, wparam, lparam);
	switch (message)
	{
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
		PL_SearchProgressPaint(progress);
		return 0;
	case WM_DRAWITEM:
		if (wparam == PL_SEARCH_CANCEL_BUTTON_ID)
		{
			PL_SearchProgressDrawButton(progress,
				(const DRAWITEMSTRUCT *)lparam);
			return TRUE;
		}
		break;
	case WM_COMMAND:
		if (LOWORD(wparam) == PL_SEARCH_CANCEL_BUTTON_ID &&
			HIWORD(wparam) == BN_CLICKED)
		{
			PL_SearchProgressCancel(progress);
			return 0;
		}
		break;
	case WM_TIMER:
	{
		RECT track;
		PL_SearchProgressTrackRect(progress, &track);
		InvalidateRect(window, &track, FALSE);
		return 0;
	}
	case WM_DPICHANGED:
		PL_SearchProgressHandleDPIChange(progress, HIWORD(wparam),
			(const RECT *)lparam);
		return 0;
	case WM_SETTINGCHANGE:
	case WM_THEMECHANGED:
		progress->dark = PL_MessageSystemUsesDarkMode();
		PL_MessageApplyDarkTitleBar(window, progress->dark);
		InvalidateRect(window, NULL, TRUE);
		if (progress->cancel_button)
			InvalidateRect(progress->cancel_button, NULL, TRUE);
		return 0;
	case WM_CLOSE:
		PL_SearchProgressCancel(progress);
		return 0;
	case WM_DESTROY:
		KillTimer(window, 1);
		progress->window = NULL;
		return 0;
	default:
		break;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

static qboolean PL_SearchProgressRegisterClass (void)
{
	static qboolean registered;
	WNDCLASSEXW window_class;
	if (registered) return true;
	memset(&window_class, 0, sizeof(window_class));
	window_class.cbSize = sizeof(window_class);
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	window_class.lpfnWndProc = PL_SearchProgressWindowProc;
	window_class.hInstance = GetModuleHandleW(NULL);
	window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
	window_class.lpszClassName = PL_SEARCH_PROGRESS_CLASS;
	if (!RegisterClassExW(&window_class) &&
		GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;
	registered = true;
	return true;
}

pl_search_progress_t *PL_SearchProgressBegin (const char *title,
	const char *phase)
{
	const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN;
	const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
	pl_search_progress_t *progress;
	RECT work_area, adjusted;
	int client_width, client_height, x, y;
	HINSTANCE instance = GetModuleHandleW(NULL);

	if (!PL_SearchProgressRegisterClass())
		return NULL;
	progress = (pl_search_progress_t *)calloc(1, sizeof(*progress));
	if (!progress) return NULL;
	progress->owner = PL_GetNativeWindow();
	progress->dpi = PL_GetWindowDPI(progress->owner);
	progress->dark = PL_MessageSystemUsesDarkMode();
	progress->title = PL_MessageWideString(title);
	progress->font = PL_MessageCreateFont(progress->dpi);
	PL_SearchProgressSetText(progress->phase,
		sizeof(progress->phase) / sizeof(progress->phase[0]), phase);
	PL_SearchProgressSetText(progress->location,
		sizeof(progress->location) / sizeof(progress->location[0]),
		"Preparing search...");
	wcsncpy(progress->count, L"0 folders checked",
		sizeof(progress->count) / sizeof(progress->count[0]) - 1);
	if (!progress->title || !progress->font)
		goto fail;
	PL_MessageGetWorkArea(progress->owner, &work_area);
	client_width = PL_SearchProgressScale(progress, 560);
	client_height = PL_SearchProgressScale(progress, 216);
	SetRect(&adjusted, 0, 0, client_width, client_height);
	if (!AdjustWindowRectEx(&adjusted, style, FALSE, ex_style))
		goto fail;
	x = work_area.left + ((work_area.right - work_area.left) -
		(adjusted.right - adjusted.left)) / 2;
	y = work_area.top + ((work_area.bottom - work_area.top) -
		(adjusted.bottom - adjusted.top)) / 2;
	progress->window = CreateWindowExW(ex_style, PL_SEARCH_PROGRESS_CLASS,
		progress->title, style, x, y, adjusted.right - adjusted.left,
		adjusted.bottom - adjusted.top, progress->owner, NULL, instance, progress);
	if (!progress->window)
		goto fail;
	progress->cancel_button = CreateWindowExW(0, L"BUTTON", L"Cancel",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
		progress->window, (HMENU)(INT_PTR)PL_SEARCH_CANCEL_BUTTON_ID,
		instance, NULL);
	if (!progress->cancel_button)
		goto fail;
	SendMessageW(progress->cancel_button, WM_SETFONT, (WPARAM)progress->font,
		FALSE);
	SetWindowPos(progress->cancel_button, NULL,
		client_width - PL_SearchProgressScale(progress, 24 + 92),
		PL_SearchProgressScale(progress, 172),
		PL_SearchProgressScale(progress, 92),
		PL_SearchProgressScale(progress, 32), SWP_NOZORDER | SWP_NOACTIVATE);
	PL_MessageApplyDarkTitleBar(progress->window, progress->dark);
	if (progress->owner && IsWindowEnabled(progress->owner))
	{
		EnableWindow(progress->owner, FALSE);
		progress->owner_disabled = true;
	}
	SetTimer(progress->window, 1, 33, NULL);
	ShowWindow(progress->window, SW_SHOWNORMAL);
	UpdateWindow(progress->window);
	SetForegroundWindow(progress->window);
	return progress;

fail:
	if (progress->window && IsWindow(progress->window))
		DestroyWindow(progress->window);
	if (progress->font) DeleteObject(progress->font);
	free(progress->title);
	free(progress);
	return NULL;
}

qboolean PL_SearchProgressUpdate (pl_search_progress_t *progress,
	const char *phase, const char *location, unsigned long long directories)
{
	MSG event;
	DWORD now;
	qboolean refresh;
	if (!progress || progress->cancelled || !progress->window)
		return false;
	now = GetTickCount();
	refresh = phase != NULL || directories < 2 ||
		(DWORD)(now - progress->last_visual_update) >= 50;
	if (phase)
		PL_SearchProgressSetText(progress->phase,
			sizeof(progress->phase) / sizeof(progress->phase[0]), phase);
	if (location && refresh)
		PL_SearchProgressSetText(progress->location,
			sizeof(progress->location) / sizeof(progress->location[0]), location);
	if (refresh)
	{
		_snwprintf(progress->count,
			sizeof(progress->count) / sizeof(progress->count[0]) - 1,
			L"%llu folder%s checked", directories,
			directories == 1 ? L"" : L"s");
		progress->count[sizeof(progress->count) / sizeof(progress->count[0]) - 1] =
			L'\0';
		progress->last_visual_update = now;
		InvalidateRect(progress->window, NULL, FALSE);
	}
	while (PeekMessageW(&event, NULL, 0, 0, PM_REMOVE))
	{
		if (event.message == WM_QUIT)
		{
			progress->quit_pending = true;
			progress->quit_code = (int)event.wParam;
			PL_SearchProgressCancel(progress);
			break;
		}
		if ((event.hwnd == progress->window ||
			IsChild(progress->window, event.hwnd)) &&
			event.message == WM_KEYDOWN && event.wParam == VK_ESCAPE)
		{
			PL_SearchProgressCancel(progress);
			continue;
		}
		if (!progress->window || !IsDialogMessageW(progress->window, &event))
		{
			TranslateMessage(&event);
			DispatchMessageW(&event);
		}
	}
	if (progress->window)
		UpdateWindow(progress->window);
	return !progress->cancelled && progress->window != NULL;
}

void PL_SearchProgressEnd (pl_search_progress_t *progress)
{
	if (!progress) return;
	if (progress->window && IsWindow(progress->window))
		DestroyWindow(progress->window);
	if (progress->owner_disabled)
	{
		EnableWindow(progress->owner, TRUE);
		SetActiveWindow(progress->owner);
	}
	if (progress->quit_pending)
		PostQuitMessage(progress->quit_code);
	if (progress->font) DeleteObject(progress->font);
	free(progress->title);
	free(progress);
}
#endif

static int PL_SDLMessageDialog(const char *title, const char *message,
	const pl_dialog_button_t *buttons, int num_buttons,
	int default_button, int cancel_button)
{
	SDL_MessageBoxButtonData *sdl_buttons;
	SDL_MessageBoxData data;
	int i, selected = cancel_button;

	sdl_buttons = (SDL_MessageBoxButtonData *)calloc((size_t)num_buttons,
		sizeof(*sdl_buttons));
	if (!sdl_buttons)
		return cancel_button;
	for (i = 0; i < num_buttons; ++i)
	{
		sdl_buttons[i].flags =
			(buttons[i].id == default_button ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) |
			(buttons[i].id == cancel_button ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
		sdl_buttons[i].buttonid = buttons[i].id;
		sdl_buttons[i].text = buttons[i].text;
	}
	memset(&data, 0, sizeof(data));
	data.flags = SDL_MESSAGEBOX_INFORMATION;
	data.title = title;
	data.message = message;
	data.numbuttons = num_buttons;
	data.buttons = sdl_buttons;
	if (SDL_ShowMessageBox(&data, &selected) < 0)
		selected = cancel_button;
	free(sdl_buttons);
	return selected;
}

void PL_ErrorDialog(const char *errorMsg)
{
	MessageBox (NULL, errorMsg, "Quake Error",
			MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
}

int PL_MessageDialog(const char *title, const char *message,
	const pl_dialog_button_t *buttons, int num_buttons,
	int default_button, int cancel_button)
{
	int selected = cancel_button;

	if (!buttons || num_buttons <= 0)
		return cancel_button;
#if !defined(__WATCOMC__)
	if (PL_ShowNativeMessageDialog(title, message, buttons, num_buttons,
		default_button, cancel_button, &selected))
		return selected;
#endif
	return PL_SDLMessageDialog(title, message, buttons, num_buttons,
		default_button, cancel_button);
}

qboolean PL_ConfirmDialog(const char *title, const char *text)
{
	static const pl_dialog_button_t buttons[] = {{1, "Yes"}, {0, "No"}};
	return PL_MessageDialog(title, text, buttons, 2, 1, 0) == 1;
}

qboolean PL_SelectDirectory(const char *title, const char *initial_path,
	char *result, size_t result_size)
{
	qboolean success = false;
	qboolean use_legacy = true;
	HRESULT initialized;

	if (!result || result_size == 0)
		return false;
	result[0] = '\0';
	initialized = CoInitialize(NULL);

#if !defined(__WATCOMC__)
	{
		IFileDialog *dialog = NULL;
		if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL,
			CLSCTX_INPROC_SERVER, &IID_IFileDialog, (void **)&dialog)))
		{
			DWORD options = 0;
			qboolean configured = false;
			if (initial_path && initial_path[0])
			{
				int needed = MultiByteToWideChar(CP_UTF8, 0, initial_path, -1,
					NULL, 0);
				wchar_t *winitial = needed > 0 ?
					(wchar_t *)malloc((size_t)needed * sizeof(*winitial)) : NULL;
				IShellItem *folder = NULL;
				if (winitial && MultiByteToWideChar(CP_UTF8, 0, initial_path, -1,
					winitial, needed) > 0 &&
					SUCCEEDED(SHCreateItemFromParsingName(winitial, NULL,
						&IID_IShellItem, (void **)&folder)))
				{
					dialog->lpVtbl->SetFolder(dialog, folder);
					folder->lpVtbl->Release(folder);
				}
				free(winitial);
			}
			if (SUCCEEDED(dialog->lpVtbl->GetOptions(dialog, &options)) &&
				SUCCEEDED(dialog->lpVtbl->SetOptions(dialog,
					options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM)))
			{
				configured = true;
				if (title)
				{
					wchar_t wtitle[512];
					if (MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle,
						(int)(sizeof(wtitle) / sizeof(wtitle[0]))) > 0)
						dialog->lpVtbl->SetTitle(dialog, wtitle);
				}
			}
			if (configured)
			{
				HRESULT shown = dialog->lpVtbl->Show(dialog, NULL);
				if (SUCCEEDED(shown))
				{
					IShellItem *item = NULL;
					use_legacy = false;
					if (SUCCEEDED(dialog->lpVtbl->GetResult(dialog, &item)))
					{
						PWSTR path = NULL;
						if (SUCCEEDED(item->lpVtbl->GetDisplayName(item,
							SIGDN_FILESYSPATH, &path)))
						{
							int needed = WideCharToMultiByte(CP_UTF8, 0, path, -1,
								NULL, 0, NULL, NULL);
							if (needed > 0 && (size_t)needed <= result_size &&
								WideCharToMultiByte(CP_UTF8, 0, path, -1, result,
									(int)result_size, NULL, NULL) > 0)
								success = true;
							CoTaskMemFree(path);
						}
						item->lpVtbl->Release(item);
					}
				}
				else if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
					use_legacy = false;
			}
			dialog->lpVtbl->Release(dialog);
		}
	}
#endif

	if (use_legacy)
	{
		BROWSEINFOW browse;
		LPITEMIDLIST item;
		wchar_t path[MAX_PATH];
		memset(&browse, 0, sizeof(browse));
		{
			static wchar_t wtitle[512];
			browse.lpszTitle = L"Select the folder containing Quake data";
			if (title && MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle,
				(int)(sizeof(wtitle) / sizeof(wtitle[0]))) > 0)
				browse.lpszTitle = wtitle;
		}
		browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		item = SHBrowseForFolderW(&browse);
		if (item)
		{
			if (SHGetPathFromIDListW(item, path) &&
				WideCharToMultiByte(CP_UTF8, 0, path, -1, result,
					(int)result_size, NULL, NULL) > 0)
				success = true;
			CoTaskMemFree(item);
		}
	}

	if (SUCCEEDED(initialized))
		CoUninitialize();
	return success;
}
