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
*/

// Linux hardware descriptions for f_system replies. Kept free of engine
// headers so the parsers can be tested against a fake sysfs on any host.

#include "sysinfo_linux.h"

#if defined(__linux__) || defined(SYSINFO_LINUX_TEST)

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define countof(a) (sizeof(a) / sizeof((a)[0]))

static char sysinfo_root[512];

void SysInfoLinux_SetRoot (const char *root)
{
	snprintf (sysinfo_root, sizeof(sysinfo_root), "%s", root ? root : "");
}

static void copy_string (char *dst, size_t size, const char *src)
{
	if (size)
		snprintf (dst, size, "%s", src);
}

static void trim (char *s)
{
	char *start = s;
	while (*start && isspace ((unsigned char)*start))
		start++;
	size_t length = strlen (start);
	while (length && isspace ((unsigned char)start[length - 1]))
		length--;
	memmove (s, start, length);
	s[length] = 0;
}

static int starts_with_ci (const char *s, const char *prefix)
{
	return !strncasecmp (s, prefix, strlen (prefix));
}

static int contains_ci (const char *s, const char *needle)
{
	size_t length = strlen (needle);
	for (; *s; s++)
		if (!strncasecmp (s, needle, length))
			return 1;
	return 0;
}

static FILE *open_file (const char *path)
{
	char full[1024];
	snprintf (full, sizeof(full), "%s%s", sysinfo_root, path);
	return fopen (full, "rb");
}

// Reads a whole small file. Returns the byte count, or -1 if unreadable.
static long read_file (const char *path, void *buffer, size_t size)
{
	FILE *file = open_file (path);
	if (!file)
		return -1;
	size_t length = fread (buffer, 1, size, file);
	fclose (file);
	return (long)length;
}

// Reads a one-line text attribute, trimmed. Embedded NULs end the string.
static int read_text (const char *path, char *buffer, size_t size)
{
	if (!size)
		return 0;
	long length = read_file (path, buffer, size - 1);
	if (length < 0)
	{
		buffer[0] = 0;
		return 0;
	}
	buffer[length] = 0;
	trim (buffer);
	return buffer[0] != 0;
}

static int collapse_spaces (char *s)
{
	char *out = s;
	for (const char *p = s; *p; p++)
	{
		if (isspace ((unsigned char)*p))
		{
			if (out == s || out[-1] == ' ')
				continue;
			*out++ = ' ';
		}
		else
			*out++ = *p;
	}
	while (out > s && out[-1] == ' ')
		out--;
	*out = 0;
	return out != s;
}

/*
==============================================================================
Processor
==============================================================================
*/

// /proc/cpuinfo on 64-bit ARM names only the implementer and part numbers.
static const char *arm_core_name (unsigned implementer, unsigned part)
{
	static const struct { unsigned implementer, part; const char *name; } cores[] = {
		{0x41, 0xd03, "Cortex-A53"}, {0x41, 0xd04, "Cortex-A35"}, {0x41, 0xd05, "Cortex-A55"},
		{0x41, 0xd07, "Cortex-A57"}, {0x41, 0xd08, "Cortex-A72"}, {0x41, 0xd09, "Cortex-A73"},
		{0x41, 0xd0a, "Cortex-A75"}, {0x41, 0xd0b, "Cortex-A76"}, {0x41, 0xd0d, "Cortex-A77"},
		{0x41, 0xd41, "Cortex-A78"}, {0x41, 0xd44, "Cortex-X1"}, {0x41, 0xd46, "Cortex-A510"},
		{0x41, 0xd47, "Cortex-A710"}, {0x41, 0xd48, "Cortex-X2"}, {0x41, 0xd4d, "Cortex-A715"},
		{0x41, 0xd4e, "Cortex-X3"}, {0x41, 0xd80, "Cortex-A520"}, {0x41, 0xd81, "Cortex-A720"},
		{0x41, 0xd82, "Cortex-X4"}, {0x41, 0xd85, "Cortex-X925"}, {0x41, 0xd87, "Cortex-A725"},
		{0x41, 0xd0c, "Neoverse N1"}, {0x41, 0xd40, "Neoverse V1"}, {0x41, 0xd49, "Neoverse N2"},
		{0x41, 0xd4f, "Neoverse V2"}, {0x51, 0x001, "Qualcomm Oryon"},
	};
	for (size_t i = 0; i < countof (cores); i++)
		if (cores[i].implementer == implementer && cores[i].part == part)
			return cores[i].name;
	return NULL;
}

static int read_khz (const char *path)
{
	char text[32];
	if (!read_text (path, text, sizeof(text)))
		return 0;
	char *end;
	long khz = strtol (text, &end, 10);
	return *end || khz < 100000 || khz > 20000000 ? 0 : (int)khz;
}

int SysInfoLinux_CPU (char *name, size_t name_size, int *mhz)
{
	char line[512], model[256] = "";
	const char *arm = NULL;
	unsigned implementer = 0;

	*mhz = 0;
	copy_string (name, name_size, "");
	FILE *file = open_file ("/proc/cpuinfo");
	if (!file)
		return 0;
	while (fgets (line, sizeof(line), file))
	{
		char *value = strchr (line, ':');
		if (!value)
			continue;
		*value++ = 0;
		trim (line);
		trim (value);
		if (!strcmp (line, "model name") && !model[0])
			copy_string (model, sizeof(model), value);
		else if (!strcmp (line, "CPU implementer"))
			implementer = (unsigned)strtoul (value, NULL, 0);
		else if (!strcmp (line, "CPU part"))
		{
			// Big cores follow the little ones, so the last core named wins.
			const char *core = arm_core_name (implementer, (unsigned)strtoul (value, NULL, 0));
			if (core)
				arm = core;
		}
	}
	fclose (file);

	// 32-bit ARM kernels report an architecture string rather than a model.
	if (!model[0] || starts_with_ci (model, "ARMv"))
		copy_string (model, sizeof(model), arm ? arm : "");
	collapse_spaces (model);
	copy_string (name, name_size, model);

	// Prefer the advertised base clock, matching what Windows reports.
	int khz = read_khz ("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency");
	if (!khz)
		khz = read_khz ("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
	*mhz = khz / 1000;
	return name[0] != 0;
}

/*
==============================================================================
Machine model
==============================================================================
*/

static int dmi_junk (const char *s)
{
	static const char *junk[] = {
		"system product name", "system version", "system manufacturer", "default string",
		"not applicable", "not specified", "none", "unknown", "type1productconfigid",
		"all series", "oem", "invalid", "x.x", "n/a", "undefined", "standard",
		"product name", "product version", "base board product name", "base board manufacturer",
		"chassis manufacturer", "123456789", "0123456789", "1234567890",
	};
	if (!*s || contains_ci (s, "to be filled") || contains_ci (s, "o.e.m"))
		return 1;
	for (size_t i = 0; i < countof (junk); i++)
		if (!strcasecmp (s, junk[i]))
			return 1;
	return 0;
}

// Short brand from a DMI vendor such as "ASUSTeK COMPUTER INC.".
static void dmi_vendor (const char *vendor, char *out, size_t size)
{
	static const struct { const char *prefix, *brand; } brands[] = {
		{"ASUSTeK", "ASUS"}, {"LENOVO", "Lenovo"}, {"Hewlett-Packard", "HP"},
		{"Micro-Star", "MSI"}, {"Dell", "Dell"}, {"Gigabyte", "Gigabyte"},
		{"Samsung", "Samsung"}, {"Microsoft", "Microsoft"}, {"Apple", "Apple"},
		{"innotek", ""}, {"Notebook", ""}, {"Intel Corporation", "Intel"},
	};
	copy_string (out, size, "");
	if (dmi_junk (vendor))
		return;
	for (size_t i = 0; i < countof (brands); i++)
	{
		if (starts_with_ci (vendor, brands[i].prefix))
		{
			copy_string (out, size, brands[i].brand);
			return;
		}
	}

	// Drop corporate suffixes: "Framework Computer Inc" -> "Framework".
	static const char *suffixes[] = {
		",", " Inc", " Co.", " Co,", " Corporation", " Corp", " Ltd", " GmbH", " Technology", " International",
		" Computer", " Electronics", " LLC", " AG", " S.A.",
	};
	copy_string (out, size, vendor);
	for (size_t i = 0; i < countof (suffixes); i++)
	{
		size_t length = strlen (suffixes[i]);
		for (char *p = out + 1; *p; p++)
		{
			if (!strncasecmp (p, suffixes[i], length) &&
				(suffixes[i][0] == ',' || !p[length] || !isalpha ((unsigned char)p[length])))
			{
				*p = 0;
				break;
			}
		}
	}
	trim (out);
}

static void join_model (char *model, size_t size, const char *brand, const char *product)
{
	if (!*brand || starts_with_ci (product, brand))
		copy_string (model, size, product);
	else
		snprintf (model, size, "%s %s", brand, product);
}

int SysInfoLinux_MachineModel (char *model, size_t model_size)
{
	char vendor[128], brand[64], product[128], text[128];

	copy_string (model, model_size, "");

	// ARM boards and Apple silicon describe themselves in the device tree.
	if (read_text ("/sys/firmware/devicetree/base/model", text, sizeof(text)) ||
		read_text ("/proc/device-tree/model", text, sizeof(text)))
	{
		collapse_spaces (text);
		copy_string (model, model_size, text);
		return 1;
	}

	read_text ("/sys/class/dmi/id/sys_vendor", vendor, sizeof(vendor));
	dmi_vendor (vendor, brand, sizeof(brand));
	read_text ("/sys/class/dmi/id/product_name", product, sizeof(product));

	if (!strcmp (brand, "Lenovo"))
	{
		// Lenovo stores a machine type in product_name and the marketing name here.
		if (read_text ("/sys/class/dmi/id/product_version", text, sizeof(text)) && !dmi_junk (text))
			copy_string (product, sizeof(product), text);
		else if (read_text ("/sys/class/dmi/id/product_family", text, sizeof(text)) && !dmi_junk (text))
			copy_string (product, sizeof(product), text);
	}
	else if (!strcasecmp (brand, "Valve"))
	{
		if (!strcasecmp (product, "Jupiter"))
			copy_string (product, sizeof(product), "Steam Deck");
		else if (!strcasecmp (product, "Galileo"))
			copy_string (product, sizeof(product), "Steam Deck OLED");
	}

	if (!dmi_junk (product))
	{
		collapse_spaces (product);
		join_model (model, model_size, brand, product);
		return model[0] != 0;
	}

	// Self-built desktops leave the system fields blank; the board is the model.
	read_text ("/sys/class/dmi/id/board_vendor", vendor, sizeof(vendor));
	dmi_vendor (vendor, brand, sizeof(brand));
	if (read_text ("/sys/class/dmi/id/board_name", product, sizeof(product)) && !dmi_junk (product))
	{
		collapse_spaces (product);
		join_model (model, model_size, brand, product);
	}
	return model[0] != 0;
}

/*
==============================================================================
Monitor PPI
==============================================================================
*/

typedef struct
{
	int width, height;
	int ppi;
	char name[14];
} edid_monitor_t;

static int edid_ppi (int width, int height, int width_mm, int height_mm)
{
	// Small values encode an aspect ratio or a projector, not a size.
	if (width <= 0 || height <= 0 || width_mm < 40 || height_mm < 30)
		return 0;
	double pixel_aspect = (double)width / height;
	double size_aspect = (double)width_mm / height_mm;
	if (fabs (pixel_aspect - size_aspect) > 0.2 * pixel_aspect)
		return 0;
	int ppi = (int)(hypot (width, height) * 25.4 / hypot (width_mm, height_mm) + 0.5);
	return ppi >= 50 && ppi <= 900 ? ppi : 0;
}

static int parse_edid (const unsigned char *edid, long length, edid_monitor_t *monitor)
{
	static const unsigned char header[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
	if (length < 128 || memcmp (edid, header, sizeof(header)))
		return 0;
	unsigned char sum = 0;
	for (int i = 0; i < 128; i++)
		sum += edid[i];
	if (sum)
		return 0;

	// The first detailed timing descriptor is the preferred (native) mode.
	const unsigned char *timing = edid + 54;
	if (!(timing[0] | timing[1]))
		return 0;
	memset (monitor, 0, sizeof(*monitor));
	monitor->width = timing[2] | ((timing[4] & 0xf0) << 4);
	monitor->height = timing[5] | ((timing[7] & 0xf0) << 4);
	monitor->ppi = edid_ppi (monitor->width, monitor->height,
		timing[12] | ((timing[14] & 0xf0) << 4), timing[13] | ((timing[14] & 0x0f) << 8));
	if (!monitor->ppi) // fall back to the centimetre screen size
		monitor->ppi = edid_ppi (monitor->width, monitor->height, edid[21] * 10, edid[22] * 10);

	for (int i = 0; i < 4; i++)
	{
		const unsigned char *descriptor = edid + 54 + 18 * i;
		if (descriptor[0] || descriptor[1] || descriptor[2] || descriptor[3] != 0xfc)
			continue;
		int n = 0;
		while (n < 13 && descriptor[5 + n] != 0x0a && descriptor[5 + n] >= ' ' && descriptor[5 + n] < 127)
		{
			monitor->name[n] = (char)descriptor[5 + n];
			n++;
		}
		monitor->name[n] = 0;
		trim (monitor->name);
	}
	return monitor->ppi != 0;
}

static int matches_mode (const edid_monitor_t *monitor, int width, int height)
{
	return width > 0 && height > 0 &&
		((monitor->width == width && monitor->height == height) ||
		 (monitor->width == height && monitor->height == width));
}

// Returns the shared PPI, or 0 if the candidates disagree.
static int agreed_ppi (const edid_monitor_t *const *list, int count)
{
	if (!count)
		return 0;
	for (int i = 1; i < count; i++)
		if (abs (list[i]->ppi - list[0]->ppi) > 1)
			return 0;
	return list[0]->ppi;
}

int SysInfoLinux_DisplayPPI (int width, int height, int alt_width, int alt_height,
	const char *display_name, int display_count)
{
	char path[1024];
	edid_monitor_t monitors[16];
	int count = 0, connected = 0;

	snprintf (path, sizeof(path), "%s/sys/class/drm", sysinfo_root);
	DIR *dir = opendir (path);
	if (!dir)
		return 0;
	for (struct dirent *entry; (entry = readdir (dir)) != NULL; )
	{
		char text[32];
		// Connector directories look like card0-DP-1 or card1-eDP-1.
		if (strncmp (entry->d_name, "card", 4) || !strchr (entry->d_name, '-'))
			continue;
		snprintf (path, sizeof(path), "/sys/class/drm/%s/status", entry->d_name);
		if (!read_text (path, text, sizeof(text)) || strcmp (text, "connected"))
			continue;
		snprintf (path, sizeof(path), "/sys/class/drm/%s/enabled", entry->d_name);
		if (read_text (path, text, sizeof(text)) && strcmp (text, "enabled"))
			continue;
		connected++;

		unsigned char edid[512];
		snprintf (path, sizeof(path), "/sys/class/drm/%s/edid", entry->d_name);
		long length = read_file (path, edid, sizeof(edid));
		if (count < (int)countof (monitors) && parse_edid (edid, length, &monitors[count]))
			count++;
	}
	closedir (dir);

	if (display_count == 1 && connected == 1 && count == 1)
		return monitors[0].ppi;

	const edid_monitor_t *matches[countof (monitors)], *named[countof (monitors)];
	int match_count = 0, named_count = 0;
	for (int i = 0; i < count; i++)
		if (matches_mode (&monitors[i], width, height) || matches_mode (&monitors[i], alt_width, alt_height))
			matches[match_count++] = &monitors[i];
	if (match_count == 1)
		return matches[0]->ppi;

	if (display_name && *display_name)
		for (int i = 0; i < match_count; i++)
			if (matches[i]->name[0] && contains_ci (display_name, matches[i]->name))
				named[named_count++] = matches[i];
	return named_count ? agreed_ppi (named, named_count) : agreed_ppi (matches, match_count);
}

/*
==============================================================================
Input devices
==============================================================================
*/

void SysInfoLinux_CollapseVendor (char *name)
{
	const char *space = strchr (name, ' ');
	if (!space || space == name)
		return;
	size_t word = (size_t)(space - name);
	if (!strncasecmp (name, space + 1, word) && (space[1 + word] == ' ' || !space[1 + word]))
		memmove (name, space + 1, strlen (space + 1) + 1);
}

static void device_base_name (const char *in, char *out, size_t size)
{
	static const char *suffixes[] = {" Mouse", " Keyboard"};
	copy_string (out, size, in);
	collapse_spaces (out);
	SysInfoLinux_CollapseVendor (out);
	size_t length = strlen (out);
	for (size_t i = 0; i < countof (suffixes); i++)
	{
		size_t suffix = strlen (suffixes[i]);
		if (length > suffix && !strcasecmp (out + length - suffix, suffixes[i]))
		{
			out[length - suffix] = 0;
			break;
		}
	}
}

int SysInfoLinux_SameDeviceName (const char *a, const char *b)
{
	char base_a[256], base_b[256];
	device_base_name (a, base_a, sizeof(base_a));
	device_base_name (b, base_b, sizeof(base_b));
	return base_a[0] && !strcasecmp (base_a, base_b);
}

typedef struct
{
	char name[128];
	unsigned bus;
	int kbd, mouse;
	char ev[64], key[512], rel[64];
} input_device_t;

// Kernel bitmaps print as hex longs, most significant word first.
static int bitmap_test (const char *bitmap, int bit)
{
	const int word_bits = (int)(8 * sizeof(unsigned long));
	char copy[512];
	char *words[64];
	int count = 0;
	copy_string (copy, sizeof(copy), bitmap);
	for (char *state = NULL, *word = strtok_r (copy, " ", &state); word && count < (int)countof (words);
		word = strtok_r (NULL, " ", &state))
		words[count++] = word;
	int index = count - 1 - bit / word_bits;
	if (index < 0)
		return 0;
	return (strtoul (words[index], NULL, 16) >> (bit % word_bits)) & 1;
}

static int is_pseudo_device (const char *name)
{
	static const char *pseudo[] = {
		"Consumer Control", "System Control", "Wireless Radio Control", "Virtual",
		"ydotool", "uinput", "XTEST", "AVRCP", "Hotkeys", "WMI",
	};
	for (size_t i = 0; i < countof (pseudo); i++)
		if (contains_ci (name, pseudo[i]))
			return 1;
	return 0;
}

static int is_keyboard (const input_device_t *device)
{
	static const int letters[][2] = {{16, 25}, {30, 38}, {44, 50}}; // KEY_Q..P, A..L, Z..M
	if (!device->kbd || !bitmap_test (device->ev, 20)) // EV_REP
		return 0;
	for (size_t i = 0; i < countof (letters); i++)
		for (int bit = letters[i][0]; bit <= letters[i][1]; bit++)
			if (!bitmap_test (device->key, bit))
				return 0;
	return 1;
}

static int is_mouse (const input_device_t *device)
{
	return device->mouse && bitmap_test (device->rel, 0) && bitmap_test (device->rel, 1); // REL_X, REL_Y
}

// Keeps only external devices; built-in PS/2, I2C and platform devices say little.
static void add_input_device (const input_device_t *device, char names[][128], int *count, int max,
	int keyboard)
{
	if ((device->bus != 0x03 && device->bus != 0x05) || !device->name[0] ||
		is_pseudo_device (device->name) || !(keyboard ? is_keyboard (device) : is_mouse (device)))
		return;
	for (int i = 0; i < *count; i++)
		if (SysInfoLinux_SameDeviceName (names[i], device->name))
			return;
	if (*count < max)
		copy_string (names[*count], sizeof(names[0]), device->name);
	(*count)++;
}

void SysInfoLinux_InputDevices (char *keyboard, size_t keyboard_size, char *mouse, size_t mouse_size)
{
	char line[1024];
	char keyboards[8][128], mice[8][128];
	int keyboard_count = 0, mouse_count = 0;
	input_device_t device;

	copy_string (keyboard, keyboard_size, "");
	copy_string (mouse, mouse_size, "");
	FILE *file = open_file ("/proc/bus/input/devices");
	if (!file)
		return;

	memset (&device, 0, sizeof(device));
	for (int done = 0; !done; )
	{
		done = !fgets (line, sizeof(line), file);
		trim (line);
		if (done || !line[0])
		{
			add_input_device (&device, keyboards, &keyboard_count, (int)countof (keyboards), 1);
			add_input_device (&device, mice, &mouse_count, (int)countof (mice), 0);
			memset (&device, 0, sizeof(device));
			continue;
		}
		if (!strncmp (line, "I: ", 3))
		{
			const char *bus = strstr (line, "Bus=");
			device.bus = bus ? (unsigned)strtoul (bus + 4, NULL, 16) : 0;
		}
		else if (!strncmp (line, "N: Name=", 8))
		{
			char *name = line + 8;
			size_t length = strlen (name);
			if (length >= 2 && name[0] == '"' && name[length - 1] == '"')
			{
				name[length - 1] = 0;
				name++;
			}
			copy_string (device.name, sizeof(device.name), name);
			collapse_spaces (device.name);
		}
		else if (!strncmp (line, "H: Handlers=", 12))
		{
			char handlers[256];
			copy_string (handlers, sizeof(handlers), line + 12);
			for (char *state = NULL, *handler = strtok_r (handlers, " ", &state); handler;
				handler = strtok_r (NULL, " ", &state))
			{
				if (!strcmp (handler, "kbd"))
					device.kbd = 1;
				else if (!strncmp (handler, "mouse", 5))
					device.mouse = 1;
			}
		}
		else if (!strncmp (line, "B: EV=", 6))
			copy_string (device.ev, sizeof(device.ev), line + 6);
		else if (!strncmp (line, "B: KEY=", 7))
			copy_string (device.key, sizeof(device.key), line + 7);
		else if (!strncmp (line, "B: REL=", 7))
			copy_string (device.rel, sizeof(device.rel), line + 7);
	}
	fclose (file);

	// A gaming mouse also exposes a macro keyboard under its own name.
	int keyboard_index = -1;
	for (int i = 0; i < keyboard_count && i < (int)countof (keyboards); i++)
	{
		if (mouse_count == 1 && SysInfoLinux_SameDeviceName (keyboards[i], mice[0]))
			continue;
		if (keyboard_index >= 0)
		{
			keyboard_index = -1; // more than one: not knowable which is in use
			break;
		}
		keyboard_index = i;
	}
	if (keyboard_index >= 0)
	{
		copy_string (keyboard, keyboard_size, keyboards[keyboard_index]);
		SysInfoLinux_CollapseVendor (keyboard);
	}
	if (mouse_count == 1)
	{
		copy_string (mouse, mouse_size, mice[0]);
		SysInfoLinux_CollapseVendor (mouse);
	}
}

/*
==============================================================================
OpenRazer
==============================================================================
*/

int SysInfoLinux_OpenRazerDPI (const char *selected, char *name, size_t name_size, int *dpi_x, int *dpi_y)
{
	const char *driver = "/sys/bus/hid/drivers/razermouse";
	char path[1024], found[128] = "";
	int x = 0, y = 0, matches = 0;

	snprintf (path, sizeof(path), "%s%s", sysinfo_root, driver);
	DIR *dir = opendir (path);
	if (!dir)
		return 0;
	for (struct dirent *entry; (entry = readdir (dir)) != NULL; )
	{
		char device[128], text[64];
		if (entry->d_name[0] == '.' || !strchr (entry->d_name, ':'))
			continue;
		snprintf (path, sizeof(path), "%s/%s/device_type", driver, entry->d_name);
		if (!read_text (path, device, sizeof(device)))
			continue;
		if (selected && *selected && !SysInfoLinux_SameDeviceName (selected, device))
			continue;

		// Only the mouse interface has this attribute; reading it queries the device.
		snprintf (path, sizeof(path), "%s/%s/dpi", driver, entry->d_name);
		if (!read_text (path, text, sizeof(text)))
			continue;
		char *end;
		long read_x = strtol (text, &end, 10), read_y = read_x;
		if (*end == ':')
			read_y = strtol (end + 1, &end, 10);
		if (*end || read_x < 100 || read_x > 100000 || read_y < 100 || read_y > 100000)
			continue;
		if (matches && (strcasecmp (found, device) || read_x != x || read_y != y))
		{
			closedir (dir);
			return 0; // two different mice
		}
		copy_string (found, sizeof(found), device);
		x = (int)read_x;
		y = (int)read_y;
		matches++;
	}
	closedir (dir);

	if (!matches)
		return 0;
	copy_string (name, name_size, found);
	*dpi_x = x;
	*dpi_y = y;
	return 1;
}

#else

typedef int sysinfo_linux_unused_t; // ISO C forbids an empty translation unit

#endif
