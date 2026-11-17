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

#ifndef SYSINFO_LINUX_H
#define SYSINFO_LINUX_H

#include <stddef.h>

// Linux hardware descriptions for f_system replies. These read only
// world-readable procfs and sysfs files and never start processes.
// Each returns nonzero only when it found an unambiguous answer.

// Tests point this at a fake filesystem root. NULL restores "/".
void SysInfoLinux_SetRoot (const char *root);

// Raw processor name and nominal clock in MHz (0 if unknown).
int SysInfoLinux_CPU (char *name, size_t name_size, int *mhz);

// Board or system model, e.g. "Framework Laptop 13" or "Raspberry Pi 5 Model B".
int SysInfoLinux_MachineModel (char *model, size_t model_size);

// Physical PPI of the connected monitor whose EDID native mode matches
// width x height (or alt_width x alt_height). display_name disambiguates
// matching monitors, and display_count 1 accepts the only connected one.
int SysInfoLinux_DisplayPPI (int width, int height, int alt_width, int alt_height,
	const char *display_name, int display_count);

// The single external USB or Bluetooth keyboard and mouse, for backends
// such as Wayland that do not report device names.
void SysInfoLinux_InputDevices (char *keyboard, size_t keyboard_size, char *mouse, size_t mouse_size);

// Current DPI from the OpenRazer kernel driver. selected, if nonempty,
// must name the device; name receives the driver's device name.
int SysInfoLinux_OpenRazerDPI (const char *selected, char *name, size_t name_size, int *dpi_x, int *dpi_y);

// Compare device names, ignoring case, a doubled vendor prefix such as
// "Razer Razer", and a kernel " Mouse" interface suffix.
int SysInfoLinux_SameDeviceName (const char *a, const char *b);

// Collapse a doubled leading vendor word in place ("Razer Razer X" -> "Razer X").
void SysInfoLinux_CollapseVendor (char *name);

#endif
