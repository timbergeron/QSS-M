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
#include <errno.h>
#include <string.h>
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif

#if defined(USE_SDL2)
static void IN_ClearDropBatch(void);
#endif

#ifdef __APPLE__
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hidsystem/event_status_driver.h>

// HID Raw Mouse Input System
static SDL_Thread *hid_thread = NULL;
static SDL_mutex *hid_mouse_mutex = NULL;
static SDL_mutex *hid_start_mutex = NULL;
static SDL_cond *hid_start_cond = NULL;
static IOHIDManagerRef hid_manager = NULL;
static CFRunLoopRef hid_runloop = NULL;
static int hid_mouse_x = 0;
static int hid_mouse_y = 0;
static qboolean hid_mouse_active = false;

static void HID_InputCallback(void *unused, IOReturn result, void *sender, IOHIDValueRef value)
{
	if (!hid_mouse_active || !hid_mouse_mutex || !value) return;
	
	IOHIDElementRef elem = IOHIDValueGetElement(value);
	if (!elem) return;
	
	uint32_t page = IOHIDElementGetUsagePage(elem);
	uint32_t usage = IOHIDElementGetUsage(elem);
	int32_t val = (int32_t)IOHIDValueGetIntegerValue(value);

	if (page == kHIDPage_GenericDesktop) {
		switch (usage) {
			case kHIDUsage_GD_X:
				if (SDL_LockMutex(hid_mouse_mutex) == 0) {
					hid_mouse_x += val;
					SDL_UnlockMutex(hid_mouse_mutex);
				}
				break;
			case kHIDUsage_GD_Y:
				if (SDL_LockMutex(hid_mouse_mutex) == 0) {
					hid_mouse_y += val;
					SDL_UnlockMutex(hid_mouse_mutex);
				}
				break;
			default:
				break;
		}
	}
}

static int HID_MouseThread(void *inarg)
{
	CFMutableDictionaryRef mice = NULL;
	CFNumberRef pageRef = NULL;
	CFNumberRef usageRef = NULL;
	CFRunLoopRef runloop = NULL;
	
	if (!hid_start_mutex) {
		return -1;
	}
	
	SDL_LockMutex(hid_start_mutex);

	hid_manager = IOHIDManagerCreate(kCFAllocatorSystemDefault, kIOHIDOptionsTypeNone);
	if (!hid_manager) {
		goto cleanup_and_signal;
	}

	// Create device matching dictionary for mice
	mice = CFDictionaryCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (!mice) {
		goto cleanup_and_signal;
	}
	
	UInt32 page = kHIDPage_GenericDesktop;
	UInt32 usage = kHIDUsage_GD_Mouse;
	pageRef = CFNumberCreate(kCFAllocatorSystemDefault, kCFNumberIntType, &page);
	usageRef = CFNumberCreate(kCFAllocatorSystemDefault, kCFNumberIntType, &usage);
	
	if (!pageRef || !usageRef) {
		goto cleanup_and_signal;
	}
	
	CFDictionarySetValue(mice, CFSTR(kIOHIDDeviceUsagePageKey), pageRef);
	CFDictionarySetValue(mice, CFSTR(kIOHIDDeviceUsageKey), usageRef);
	CFRelease(pageRef);
	CFRelease(usageRef);
	pageRef = NULL;
	usageRef = NULL;

	IOHIDManagerSetDeviceMatching(hid_manager, mice);
	CFRelease(mice);
	mice = NULL;
	
	IOHIDManagerRegisterInputValueCallback(hid_manager, HID_InputCallback, NULL);
	
	runloop = CFRunLoopGetCurrent();
	if (!runloop) {
		goto cleanup_and_signal;
	}
	
	IOHIDManagerScheduleWithRunLoop(hid_manager, runloop, kCFRunLoopDefaultMode);

	// This may fail if the process running does not have 'Input Monitoring' permissions granted.
	IOReturn ret = IOHIDManagerOpen(hid_manager, kIOHIDOptionsTypeNone);
	if (ret != kIOReturnSuccess) {
		IOHIDManagerUnscheduleFromRunLoop(hid_manager, runloop, kCFRunLoopDefaultMode);
		if (ret == kIOReturnNotPermitted) {
			// Immediate signal for permission denial - no need to wait
			goto cleanup_and_signal;
		}
		goto cleanup_and_signal;
	}

	hid_runloop = runloop;
	
	// Signal success and unlock
	SDL_CondSignal(hid_start_cond);
	SDL_UnlockMutex(hid_start_mutex);

	CFRunLoopRun();

	// Cleanup when run loop exits
	IOHIDManagerClose(hid_manager, kIOHIDOptionsTypeNone);
	IOHIDManagerUnscheduleFromRunLoop(hid_manager, runloop, kCFRunLoopDefaultMode);
	CFRelease(hid_manager);
	hid_manager = NULL;
	hid_runloop = NULL;

	return 0;

cleanup_and_signal:
	// Cleanup resources
	if (pageRef) CFRelease(pageRef);
	if (usageRef) CFRelease(usageRef);
	if (mice) CFRelease(mice);
	if (hid_manager) {
		if (runloop) {
			IOHIDManagerUnscheduleFromRunLoop(hid_manager, runloop, kCFRunLoopDefaultMode);
		}
		CFRelease(hid_manager);
		hid_manager = NULL;
	}
	
	hid_runloop = NULL;
	
	// Signal failure and unlock
	SDL_CondSignal(hid_start_cond);
	SDL_UnlockMutex(hid_start_mutex);
	
	return -1;
}

static qboolean HID_MouseInit(void)
{
	if (hid_mouse_active) return true;

	hid_mouse_x = 0;
	hid_mouse_y = 0;

	hid_start_mutex = SDL_CreateMutex();
	hid_start_cond = SDL_CreateCond();
	hid_mouse_mutex = SDL_CreateMutex();

	if (!hid_start_mutex || !hid_start_cond || !hid_mouse_mutex) {
		Con_DPrintf("HID Mouse: Failed to create mutexes\n");
		return false;
	}

	SDL_LockMutex(hid_start_mutex);
	hid_thread = SDL_CreateThread(HID_MouseThread, "HID_MouseThread", NULL);
	
	if (!hid_thread) {
		SDL_UnlockMutex(hid_start_mutex);
		SDL_DestroyMutex(hid_start_mutex);
		SDL_DestroyMutex(hid_mouse_mutex);
		SDL_DestroyCond(hid_start_cond);
		return false;
	}

	// Wait for HID thread to initialize with timeout (5 seconds)
	Uint32 start_time = SDL_GetTicks();
	int wait_result = 0;
	while ((SDL_GetTicks() - start_time) < 5000) {
		wait_result = SDL_CondWaitTimeout(hid_start_cond, hid_start_mutex, 1000);
		if (wait_result == 0 && hid_runloop) break; // Success - thread is ready
		if (wait_result == 0 && !hid_runloop) break; // Signal received but failed - exit immediately
		if (wait_result == SDL_MUTEX_TIMEDOUT) continue; // Timeout, try again
		break; // Error
	}
	
	SDL_UnlockMutex(hid_start_mutex);

	SDL_DestroyMutex(hid_start_mutex);
	SDL_DestroyCond(hid_start_cond);
	hid_start_mutex = NULL;
	hid_start_cond = NULL;

	if (wait_result != 0 || !hid_runloop) {
		Con_DPrintf("HID Mouse: Failed to initialize - falling back to SDL mouse\n");
		if (hid_thread) {
			SDL_WaitThread(hid_thread, NULL);
			hid_thread = NULL;
		}
		if (hid_mouse_mutex) {
			SDL_DestroyMutex(hid_mouse_mutex);
			hid_mouse_mutex = NULL;
		}
		return false;
	}

	hid_mouse_active = true;
	return true;
}

static void HID_MouseShutdown(void)
{
	if (!hid_mouse_active) return;

	hid_mouse_active = false;

	if (hid_runloop) {
		CFRunLoopStop(hid_runloop);
		hid_runloop = NULL;
	}

	if (hid_thread) {
		// Fast shutdown - wait for thread to ensure IOHIDManagerClose runs
		// The CFRunLoopStop above will cause it to exit cleanly
		SDL_WaitThread(hid_thread, NULL);   /* guarantees IOHIDManagerClose ran */
		hid_thread = NULL;
	}

	if (hid_mouse_mutex) {
		SDL_DestroyMutex(hid_mouse_mutex);
		hid_mouse_mutex = NULL;
	}

	Con_DPrintf("HID Mouse: Raw mouse input shutdown\n");
}

static void HID_MouseGetMovement(int *m_x, int *m_y)
{
	if (!hid_mouse_active || !hid_mouse_mutex) {
		*m_x = 0;
		*m_y = 0;
		return;
	}

	SDL_LockMutex(hid_mouse_mutex);
	*m_x = hid_mouse_x;
	*m_y = hid_mouse_y;
	hid_mouse_x = 0;
	hid_mouse_y = 0;
	SDL_UnlockMutex(hid_mouse_mutex);
}

#endif // __APPLE__

char	afk_name[16]; // woods #smartafk
char	normalname[20]; // woods #smartafk
char	normalname2[32]; // woods #smartafk

extern	char mute[2]; // woods for mute to memory #usermute

qboolean windowhasfocus = true;	//just in case sdl fails to tell us... // woods #pong -- remove static
static qboolean	textmode;
static keydevice_t lastactivetype = KD_NONE;
extern qboolean	bind_grab;	//from the menu code, so that we regrab the mouse in order to pass inputs through

static cvar_t in_debugkeys = {"in_debugkeys", "0", CVAR_NONE};

void Sound_Toggle_Mute_On_f(void); // woods #mute -- adapted from Fitzquake Mark V
void Sound_Toggle_Mute_Off_f(void); // woods #mute -- adapted from Fitzquake Mark V
void BGM_Pause(void); // woods #mute - music
void BGM_Resume(void); // woods #mute - music

void Host_Name_Backup_f(void); // woods #smartafk
void Host_Name_Load_Backup_f(void); // woods #smartafk

qboolean IsOneVsOneMatch (void); // woods #detectmatch
void IN_ObsFragsClick (int mouse_x, int mouse_y); // woods #eyemouse
extern cvar_t cl_bottomcolor; // woods

#ifdef __APPLE__
/* Mouse acceleration needs to be disabled on OS X */
#define MACOS_X_ACCELERATION_HACK
#endif

#ifdef MACOS_X_ACCELERATION_HACK
#include <IOKit/IOTypes.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <dlfcn.h>
#include <CoreGraphics/CoreGraphics.h>

static double originalMouseSpeed = -1.0;

typedef kern_return_t (*IN_IOHIDGetAccelerationWithKeyFunc)(io_connect_t handle, CFStringRef key, double *acceleration);
typedef kern_return_t (*IN_IOHIDSetAccelerationWithKeyFunc)(io_connect_t handle, CFStringRef key, double acceleration);

static kern_return_t IN_IOHIDGetAccelerationWithKey(io_connect_t handle, CFStringRef key, double *acceleration)
{
	static IN_IOHIDGetAccelerationWithKeyFunc func = NULL;
	static qboolean tried = false;

	if (!tried)
	{
		func = (IN_IOHIDGetAccelerationWithKeyFunc)dlsym(RTLD_DEFAULT, "IOHIDGetAccelerationWithKey");
		tried = true;
	}

	if (!func)
		return kIOReturnUnsupported;

	return func(handle, key, acceleration);
}

static kern_return_t IN_IOHIDSetAccelerationWithKey(io_connect_t handle, CFStringRef key, double acceleration)
{
	static IN_IOHIDSetAccelerationWithKeyFunc func = NULL;
	static qboolean tried = false;

	if (!tried)
	{
		func = (IN_IOHIDSetAccelerationWithKeyFunc)dlsym(RTLD_DEFAULT, "IOHIDSetAccelerationWithKey");
		tried = true;
	}

	if (!func)
		return kIOReturnUnsupported;

	return func(handle, key, acceleration);
}

static io_connect_t IN_GetIOHandle(void)
{
	io_connect_t iohandle = MACH_PORT_NULL;
	io_service_t iohidsystem = MACH_PORT_NULL;
	kern_return_t status;

	iohidsystem = IORegistryEntryFromPath(MACH_PORT_NULL, kIOServicePlane ":/IOResources/IOHIDSystem");
	if (!iohidsystem)
		return 0;

	status = IOServiceOpen(iohidsystem, mach_task_self(), kIOHIDParamConnectType, &iohandle);
	IOObjectRelease(iohidsystem);

	return iohandle;
}

static void SetMouseAccelCG(double accel)
{
	typedef void (*CGSetRefFn)(int, double);
	static CGSetRefFn fn = NULL;
	if (!fn)
		fn = (CGSetRefFn)dlsym(RTLD_DEFAULT, "CGEventSourceSetAcceleration");
	if (fn) {
		/* source = -1 == combined "local" events */
		fn(kCGEventSourceStateCombinedSessionState, accel);
	}
}

static void IN_ReenableOSXMouseAccel_AtExit(void)
{
	printf("IN_ReenableOSXMouseAccel_AtExit called\n");
	if (originalMouseSpeed != -1)
	{
		printf("atexit: originalMouseSpeed=%g\n", originalMouseSpeed);
		io_connect_t mouseDev = IN_GetIOHandle();
		if (mouseDev != 0)
		{
			double accel = (originalMouseSpeed == -1) ? 0.0 : originalMouseSpeed;
			
			if (IN_IOHIDSetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), accel) != kIOReturnSuccess)
			{
				printf("RESTORE-FAIL %g\n", accel);
				/* try the CG shim instead */
				SetMouseAccelCG(accel);
			}
			else
			{
				printf("Restored accel %g (atexit)\n", accel);
			}
			IOServiceClose(mouseDev);
		}
		originalMouseSpeed = -1;
	}
	else
	{
		printf("atexit: originalMouseSpeed was already -1\n");
	}
}

static void IN_ReenableOSXMouseAccel (void)
{
	Con_DPrintf("IN_ReenableOSXMouseAccel called\n");
	io_connect_t mouseDev = IN_GetIOHandle();
	if (mouseDev != 0)
	{
		/* NEW: Restore to default (0.0) if originalMouseSpeed is -1 (save failed) */
		double accel = (originalMouseSpeed == -1) ? 0.0 : originalMouseSpeed;
		
		if (IN_IOHIDSetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), accel) != kIOReturnSuccess)
		{
			Con_DPrintf("RESTORE-FAIL %g\n", accel);
			/* try the CG shim instead */
			SetMouseAccelCG(accel);
		}
		else
		{
			Con_DPrintf("Restored accel %g\n", accel);
		}
		IOServiceClose(mouseDev);
	}
	else
	{
		Con_DPrintf("WARNING: Could not re-enable mouse acceleration (failed at IO_GetIOHandle).\n");
	}
	/* keep the cached value in case we lose/regain focus again
	   before quitting, but guard against runaway loops           */
	static int restoreCount = 0;
	if (++restoreCount > 2)        /* paranoia */
		originalMouseSpeed = -1;
}

static void IN_ReenableOSXMouseAccelForFocus (void)
{
	Con_DPrintf("IN_ReenableOSXMouseAccelForFocus called\n");
	io_connect_t mouseDev = IN_GetIOHandle();
	if (mouseDev != 0)
	{
		/* NEW: Restore to default (0.0) if originalMouseSpeed is -1 (save failed) */
		double accel = (originalMouseSpeed == -1) ? 0.0 : originalMouseSpeed;
		
		if (IN_IOHIDSetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), accel) != kIOReturnSuccess)
		{
			Con_DPrintf("RESTORE-FAIL %g\n", accel);
			/* try the CG shim instead */
			SetMouseAccelCG(accel);
		}
		else
		{
			Con_DPrintf("Restored accel %g (for focus)\n", accel);
		}
		IOServiceClose(mouseDev);
	}
	else
	{
		Con_DPrintf("WARNING: Could not re-enable mouse acceleration (failed at IO_GetIOHandle).\n");
	}
	/* DON'T reset originalMouseSpeed here - we need it for subsequent focus events and final quit */
}

#endif /* MACOS_X_ACCELERATION_HACK */

// SDL2 Game Controller cvars
cvar_t	joy_deadzone_look = { "joy_deadzone_look", "0.175", CVAR_ARCHIVE };
cvar_t	joy_deadzone_move = { "joy_deadzone_move", "0.175", CVAR_ARCHIVE };
cvar_t	joy_outer_threshold_look = { "joy_outer_threshold_look", "0.02", CVAR_ARCHIVE };
cvar_t	joy_outer_threshold_move = { "joy_outer_threshold_move", "0.02", CVAR_ARCHIVE };
cvar_t	joy_deadzone_trigger = { "joy_deadzone_trigger", "0.2", CVAR_ARCHIVE };
cvar_t	joy_sensitivity_yaw = { "joy_sensitivity_yaw", "240", CVAR_ARCHIVE };
cvar_t	joy_sensitivity_pitch = { "joy_sensitivity_pitch", "130", CVAR_ARCHIVE };
cvar_t	joy_invert = { "joy_invert", "0", CVAR_ARCHIVE };
cvar_t	joy_exponent = { "joy_exponent", "2", CVAR_ARCHIVE };
cvar_t	joy_exponent_move = { "joy_exponent_move", "2", CVAR_ARCHIVE };
cvar_t	joy_swapmovelook = { "joy_swapmovelook", "0", CVAR_ARCHIVE };
cvar_t	joy_flick = { "joy_flick", "0", CVAR_ARCHIVE };
cvar_t	joy_flick_time = { "joy_flick_time", "0.125", CVAR_ARCHIVE };
cvar_t	joy_flick_recenter = { "joy_flick_recenter", "0.0", CVAR_ARCHIVE };
cvar_t	joy_flick_deadzone = { "joy_flick_deadzone", "0.9", CVAR_ARCHIVE };
cvar_t	joy_flick_noise_thresh = { "joy_flick_noise_thresh", "2.0", CVAR_ARCHIVE };
cvar_t	joy_flick_adjust_speed = { "joy_flick_adjust_speed", "30.0", CVAR_ARCHIVE };
cvar_t	joy_rumble = { "joy_rumble", "0.3", CVAR_ARCHIVE };
cvar_t	joy_rumble_triggers = { "joy_rumble_triggers", "0", CVAR_ARCHIVE };
cvar_t	joy_touchpad = { "joy_touchpad", "1", CVAR_ARCHIVE };
cvar_t	joy_enable = { "joy_enable", "1", CVAR_ARCHIVE };
cvar_t	joy_device = { "joy_device", "0", CVAR_ARCHIVE };
cvar_t	joy_always_active = { "joy_always_active", "0", CVAR_ARCHIVE };

cvar_t gyro_enable = { "gyro_enable", "1", CVAR_ARCHIVE };
cvar_t gyro_mode = { "gyro_mode", "2", CVAR_ARCHIVE };
cvar_t gyro_turning_axis = { "gyro_turning_axis", "0", CVAR_ARCHIVE };
cvar_t gyro_yawsensitivity = { "gyro_yawsensitivity", "2.5", CVAR_ARCHIVE };
cvar_t gyro_pitchsensitivity = { "gyro_pitchsensitivity", "2.5", CVAR_ARCHIVE };
cvar_t gyro_calibration_x = { "gyro_calibration_x", "0", CVAR_ARCHIVE };
cvar_t gyro_calibration_y = { "gyro_calibration_y", "0", CVAR_ARCHIVE };
cvar_t gyro_calibration_z = { "gyro_calibration_z", "0", CVAR_ARCHIVE };
cvar_t gyro_noise_thresh = { "gyro_noise_thresh", "1.5", CVAR_ARCHIVE };

#if defined(USE_SDL2)
static SDL_JoystickID joy_active_instanceid = -1;
static int joy_active_device = -1;
static SDL_GameController *joy_active_controller = NULL;
static gamepadtype_t joy_active_type = GAMEPAD_NONE;
static char joy_active_name[256];
static qboolean joy_has_rumble = false;
static qboolean joy_has_trigger_rumble = false;
static qboolean joy_has_touchpad = false;
static gamepadpower_t joy_power = GAMEPAD_POWER_UNKNOWN;
static qboolean joy_warned_low_power = false;
static qboolean joy_warned_empty_power = false;

static void IN_LoadControllerMappings(void);
static qboolean IN_UseController(int device_index);
static void IN_SetupJoystick(void);
static qboolean IN_RemapJoystick(void);
static void IN_ReloadControllerMappings_f(void);
static void Joy_Device_f(cvar_t *cvar);
static void Joy_Device_Completion_f(cvar_t *cvar, const char *partial);
static void Joy_Flick_f(cvar_t *cvar);
void IN_GyroActionDown(void);
void IN_GyroActionUp(void);
#endif

static qboolean	no_mouse = false;
static qboolean	wheel_block_mouse2 = false;
static qboolean demoscrub_hover = false;
static double demoscrub_hover_until = 0.0;
static qboolean demoscrub_was_eligible = false;
static qboolean demoscrub_cursor_was_visible = false;
#if defined(USE_SDL2)
#if SDL_VERSION_ATLEAST(2, 0, 4)
static qboolean demoscrub_mouse_captured = false;
#endif
#endif

static int buttonremap[] =
{
	K_MOUSE1,
	K_MOUSE3,	/* right button		*/
	K_MOUSE2,	/* middle button	*/
#if !defined(USE_SDL2)	/* mousewheel up/down not counted as buttons in SDL2 */
	K_MWHEELUP,
	K_MWHEELDOWN,
#endif
	K_MOUSE4,
	K_MOUSE5
};

/* SDL accumulates high-resolution wheel motion into the integer x/y fields.
 * Preserve the number of completed vertical steps when translating them to
 * Quake's digital wheel keys. */
static void IN_EmitWheelKeySteps(int steps)
{
	while (steps > 0)
	{
		Key_Event(K_MWHEELUP, true);
		Key_Event(K_MWHEELUP, false);
		steps--;
	}

	while (steps < 0)
	{
		Key_Event(K_MWHEELDOWN, true);
		Key_Event(K_MWHEELDOWN, false);
		steps++;
	}
}

/* total accumulated mouse movement since last frame */
static int	total_dx, total_dy = 0;
static float gyro_yaw = 0.f, gyro_pitch = 0.f, gyro_raw_mag = 0.f;
static float gyro_center_frac = 0.f, gyro_center_amount = 0.f;

#define GYRO_CALIBRATION_SAMPLES 300
static float gyro_accum[3] = {0.f, 0.f, 0.f};
static unsigned int updates_countdown = 0;

static qboolean gyro_present = false;
static qboolean gyro_button_pressed = false;
static double joy_rumble_test_end = 0.0;

static struct
{
	float yaw;
	float pitch;
	float yaw_delta;
	float prev_lerp_frac;
	float prev_angle;
	float prev_scale;
} flick;

static Uint32 obs_cursor_last_move = 0; // ms timestamp of last motion / click -- woods #eyemouse
static qboolean obs_cursor_hidden = false; // SDL_ShowCursor() state we forced -- woods #eyemouse
#define OBS_CURSOR_IDLE_MS 2000 // 2 seconds -- woods #eyemouse

#if 1
static void IN_BeginIgnoringMouseEvents(void){}
static void IN_EndIgnoringMouseEvents(void){}
#else
static int SDLCALL IN_FilterMouseEvents (const SDL_Event *event)
{
	switch (event->type)
	{
	case SDL_MOUSEMOTION:
	// case SDL_MOUSEBUTTONDOWN:
	// case SDL_MOUSEBUTTONUP:
		return 0;
	}

	return 1;
}

#if defined(USE_SDL2)
static int SDLCALL IN_SDL2_FilterMouseEvents (void *userdata, SDL_Event *event)
{
	return IN_FilterMouseEvents (event);
}
#endif

static void IN_BeginIgnoringMouseEvents(void)
{
#if defined(USE_SDL2)
	SDL_EventFilter currentFilter = NULL;
	void *currentUserdata = NULL;
	SDL_GetEventFilter(&currentFilter, &currentUserdata);

	if (currentFilter != IN_SDL2_FilterMouseEvents)
		SDL_SetEventFilter(IN_SDL2_FilterMouseEvents, NULL);
#else
	if (SDL_GetEventFilter() != IN_FilterMouseEvents)
		SDL_SetEventFilter(IN_FilterMouseEvents);
#endif
}

static void IN_EndIgnoringMouseEvents(void)
{
#if defined(USE_SDL2)
	SDL_EventFilter currentFilter;
	void *currentUserdata;
	if (SDL_GetEventFilter(&currentFilter, &currentUserdata) == SDL_TRUE)
		SDL_SetEventFilter(NULL, NULL);
#else
	if (SDL_GetEventFilter() != NULL)
		SDL_SetEventFilter(NULL);
#endif
}
#endif

#ifdef MACOS_X_ACCELERATION_HACK
cvar_t in_disablemacosxmouseaccel = {"in_disablemacosxmouseaccel", "2", CVAR_ARCHIVE}; // woods - remove static

static void IN_DisableOSXMouseAccel (void)
{
	io_connect_t mouseDev = IN_GetIOHandle();
	if (mouseDev != 0)
	{
		kern_return_t kr = IN_IOHIDGetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), &originalMouseSpeed);

		/* Fallback: if we lack permission try CGEventSource */
		if (kr != kIOReturnSuccess) {
			double cgSpeed = 0.0;
			// Load the private CGSGetMouseAcceleration function dynamically
			typedef OSStatus (*CGSGetMouseAcceleration_t)(double *accel);
			static CGSGetMouseAcceleration_t fn = NULL;
			if (!fn)
				fn = (CGSGetMouseAcceleration_t)dlsym(RTLD_DEFAULT, "CGSGetMouseAcceleration");

			if (fn && fn(&cgSpeed) == kCGErrorSuccess) {
				originalMouseSpeed = cgSpeed;
				Con_DPrintf("Saved accel %g (via CGS fallback)\n", originalMouseSpeed);
				kr = kIOReturnSuccess; // Treat as success for the rest of the function
}
			// Final fallback for macOS 15+ where CGS symbol is removed
			else if (!fn) {
				if (IN_IOHIDGetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), &originalMouseSpeed) == kIOReturnSuccess) {
					Con_DPrintf("Saved accel %g (via IOKit fallback)\n", originalMouseSpeed);
					kr = kIOReturnSuccess;
				}
			}
		} else {
			Con_DPrintf("Saved accel %g\n", originalMouseSpeed);
		}

		if (kr == kIOReturnSuccess)
{
			if (IN_IOHIDSetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), -1.0) != kIOReturnSuccess)
			{
				Cvar_Set("in_disablemacosxmouseaccel", "0");
				Con_DPrintf("WARNING: Could not disable mouse acceleration (failed at IOHIDSetAccelerationWithKey).\n");
			}
			else
			{
				Con_DPrintf("Disabled accel (set to -1.0)\n");
				/* NEW: guarantee we clean up even on crashy exits */
				static qboolean registered = false;
				if (!registered) {
					Con_DPrintf("Registering atexit handler\n");
					atexit(IN_ReenableOSXMouseAccel_AtExit);
					registered = true;
		}
			}
		}
		else
		{
			Cvar_Set("in_disablemacosxmouseaccel", "0");
			Con_DPrintf("WARNING: Could not disable mouse acceleration (failed at IOHIDGetAccelerationWithKey and CGS fallback).\n");
		}
		IOServiceClose(mouseDev);
	}
	else
	{
		Cvar_Set("in_disablemacosxmouseaccel", "0");
		Con_DPrintf("WARNING: Could not disable mouse acceleration (failed at IO_GetIOHandle).\n");
	}
}

static void IN_RefreshOriginalAccel(void)
{
	io_connect_t mouseDev = IN_GetIOHandle();
	if (!mouseDev) return;
	double accel;
	if (IN_IOHIDGetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), &accel) == kIOReturnSuccess)
		originalMouseSpeed = accel;
	IOServiceClose(mouseDev);
}

static void IN_DisableOSXMouseAccelOnly (void)
{
	Con_DPrintf("IN_DisableOSXMouseAccelOnly called\n");
	io_connect_t mouseDev = IN_GetIOHandle();
	if (mouseDev != 0)
	{
		if (IN_IOHIDSetAccelerationWithKey(mouseDev, CFSTR(kIOHIDMouseAccelerationType), -1.0) != kIOReturnSuccess)
		{
			Con_DPrintf("WARNING: Could not disable mouse acceleration (IOHIDSetAccelerationWithKey failed).\n");
		}
		else
		{
			Con_DPrintf("Re-disabled accel (set to -1.0)\n");
		}
		IOServiceClose(mouseDev);
	}
	else
	{
		Con_DPrintf("WARNING: Could not disable mouse acceleration (failed at IO_GetIOHandle).\n");
	}
}
#endif /* MACOS_X_ACCELERATION_HACK */

#if 0
static void IN_Activate (void)
{
	if (no_mouse)
		return;

#ifdef MACOS_X_ACCELERATION_HACK
	/* Save the status of mouse acceleration */
	if (originalMouseSpeed == -1 && in_disablemacosxmouseaccel.value)
		IN_DisableOSXMouseAccel();
#endif

#if defined(USE_SDL2)
#ifdef __APPLE__
	{
		// Work around https://github.com/sezero/quakespasm/issues/48
		int width, height;
		SDL_GetWindowSize((SDL_Window*) VID_GetWindow(), &width, &height);
		SDL_WarpMouseInWindow((SDL_Window*) VID_GetWindow(), width / 2, height / 2);
	}
#endif

	if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0)
	{
		Con_Printf("WARNING: SDL_SetRelativeMouseMode(SDL_TRUE) failed.\n");
	}
#else
	if (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_ON)
	{
		SDL_WM_GrabInput(SDL_GRAB_ON);
		if (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_ON)
			Con_Printf("WARNING: SDL_WM_GrabInput(SDL_GRAB_ON) failed.\n");
	}

	if (SDL_ShowCursor(SDL_QUERY) != SDL_DISABLE)
	{
		SDL_ShowCursor(SDL_DISABLE);
		if (SDL_ShowCursor(SDL_QUERY) != SDL_DISABLE)
			Con_Printf("WARNING: SDL_ShowCursor(SDL_DISABLE) failed.\n");
	}
#endif

	IN_EndIgnoringMouseEvents();

	total_dx = 0;
	total_dy = 0;
}

static void IN_Deactivate (qboolean free_cursor)
{
	if (no_mouse)
		return;

#ifdef MACOS_X_ACCELERATION_HACK
	if (originalMouseSpeed != -1)
		IN_ReenableOSXMouseAccel();
#endif

	if (free_cursor)
	{
#if defined(USE_SDL2)
		SDL_SetRelativeMouseMode(SDL_FALSE);
#else
		if (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_OFF)
		{
			SDL_WM_GrabInput(SDL_GRAB_OFF);
			if (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_OFF)
				Con_Printf("WARNING: SDL_WM_GrabInput(SDL_GRAB_OFF) failed.\n");
		}

		if (SDL_ShowCursor(SDL_QUERY) != SDL_ENABLE)
		{
			SDL_ShowCursor(SDL_ENABLE);
			if (SDL_ShowCursor(SDL_QUERY) != SDL_ENABLE)
				Con_Printf("WARNING: SDL_ShowCursor(SDL_ENABLE) failed.\n");
		}
#endif
	}

	/* discard all mouse events when input is deactivated */
	if (cl.qcvm.extfuncs.CSQC_InputEvent && free_cursor)
		IN_EndIgnoringMouseEvents();
	else
		IN_BeginIgnoringMouseEvents();
}
#endif

extern qboolean	qeintermission; // woods #qeintermission
extern qboolean crxintermission; // woods #crxintermission

qboolean CL_IsActiveObserver (void) // woods #eyemouse
{
	return (cl.modtype == 1 && cl.eyecam && !qeintermission && !crxintermission && !cl.intermission);
}

static qboolean IN_DemoScrubEligible(void)
{
	scr_demobar_rect_t rect;

	if (key_dest != key_game || !cls.demoplayback)
		return false;
	if (!SCR_GetDemoBarRect(&rect) || !rect.eligible)
		return false;

	return scr_demobar_timeout.value >= 0.f || rect.visible || CL_DemoScrubActive();
}

static qboolean IN_DemoScrubBarVisible(void)
{
	scr_demobar_rect_t rect;

	if (key_dest != key_game)
		return false;
	return SCR_GetDemoBarRect(&rect) && rect.eligible && rect.visible;
}

static qboolean IN_DemoScrubExpireHover(void)
{
	if (demoscrub_hover && realtime >= demoscrub_hover_until)
	{
		demoscrub_hover = false;
		return true;
	}
	return false;
}

static qboolean IN_DemoScrubPollHoverActive(void)
{
	if (IN_DemoScrubExpireHover())
		return false;
	return demoscrub_hover;
}

static qboolean IN_DemoScrubCursorVisible(void)
{
	return IN_DemoScrubBarVisible() || CL_DemoScrubActive() || IN_DemoScrubPollHoverActive();
}

static void IN_DemoScrubSetHover(qboolean active)
{
	if (active)
	{
		demoscrub_hover = true;
		demoscrub_hover_until = realtime + 0.25;
	}
	else
	{
		demoscrub_hover = false;
		demoscrub_hover_until = 0.0;
	}
}

static qboolean IN_DemoScrubWindowToPct(int win_x, int win_y, float *pct, qboolean *inside_hit)
{
	scr_demobar_rect_t rect;
	int canvas_x, canvas_y;

	if (pct)
		*pct = 0.f;
	if (inside_hit)
		*inside_hit = false;

	if (!SCR_GetDemoBarRect(&rect) || !rect.eligible)
		return false;
	if (!Draw_WindowToCanvas(rect.canvas, win_x, win_y, &canvas_x, &canvas_y))
		return false;

	if (rect.seek_max_x > rect.seek_min_x && pct)
		*pct = CLAMP(0.f, (canvas_x - rect.seek_min_x) * 100.f / (float)(rect.seek_max_x - rect.seek_min_x), 100.f);

	if (inside_hit)
	{
		*inside_hit =
			canvas_x >= rect.hit_x1 && canvas_x <= rect.hit_x2 &&
			canvas_y >= rect.hit_y1 && canvas_y <= rect.hit_y2;
	}

	return true;
}

static void IN_DemoScrubSeedHover(void)
{
	int x, y;
	float pct;
	qboolean inside_hit;
	qboolean was_hover = IN_DemoScrubPollHoverActive();

	if (!IN_DemoScrubEligible())
	{
		IN_DemoScrubSetHover(false);
		return;
	}

	SDL_GetMouseState(&x, &y);
	if (IN_DemoScrubWindowToPct(x, y, &pct, &inside_hit) && inside_hit)
		IN_DemoScrubSetHover(true);
	else if (!was_hover)
		IN_DemoScrubSetHover(false);
}

static qboolean IN_DemoScrubHandleButton(const SDL_Event *event)
{
	float pct;
	float display_pct;
	qboolean inside_hit;

	if (event->button.button != SDL_BUTTON_LEFT)
		return false;

	if (event->button.state == SDL_PRESSED && CL_DemoScrubActive())
		return true;

	if (event->button.state == SDL_RELEASED)
	{
		if (!CL_DemoScrubActive())
			return false;

		if (!IN_DemoScrubWindowToPct(event->button.x, event->button.y, &pct, &inside_hit))
		{
			if (!CL_DemoScrub_GetDisplayPercent(&display_pct))
				display_pct = 0.f;
			pct = display_pct;
		}

		CL_DemoScrub_End(pct);
		IN_UpdateGrabs();
		return true;
	}

	if (event->button.state != SDL_PRESSED || !IN_DemoScrubEligible())
		return false;

	if (!IN_DemoScrubWindowToPct(event->button.x, event->button.y, &pct, &inside_hit) || !inside_hit)
		return false;

	if (!CL_DemoScrub_Begin(pct))
		return false;

	SCR_ShowDemoBar();
	IN_DemoScrubSetHover(true);
	IN_UpdateGrabs();
	return true;
}

static qboolean IN_DemoScrubHandleMotion(const SDL_Event *event)
{
	float pct;
	float display_pct;
	qboolean inside_hit;
	qboolean was_hover = IN_DemoScrubPollHoverActive();

	if (CL_DemoScrubActive())
	{
		if (!IN_DemoScrubWindowToPct(event->motion.x, event->motion.y, &pct, &inside_hit))
		{
			if (!CL_DemoScrub_GetDisplayPercent(&display_pct))
				display_pct = 0.f;
			pct = display_pct;
		}

		CL_DemoScrub_Update(pct);
		SCR_ShowDemoBar();
		return true;
	}

	if (!IN_DemoScrubEligible())
	{
		if (was_hover)
		{
			IN_DemoScrubSetHover(false);
			IN_UpdateGrabs();
		}
		return false;
	}

	if (IN_DemoScrubWindowToPct(event->motion.x, event->motion.y, &pct, &inside_hit) && inside_hit)
	{
		IN_DemoScrubSetHover(true);
		SCR_ShowDemoBar();
		if (!was_hover)
			IN_UpdateGrabs();
	}
	else if (was_hover && IN_DemoScrubExpireHover())
		IN_UpdateGrabs();

	return false;
}

#if defined(USE_SDL2)
static qboolean IN_DemoScrubHandleWheel(const SDL_Event *event)
{
	int x, y;
	float pct;
	qboolean inside_hit = false;
	qboolean hover_active;
	float seconds;

	if (CL_DemoScrubActive())
		return true;

	if (!IN_DemoScrubEligible())
		return false;

	SDL_GetMouseState(&x, &y);
	if (IN_DemoScrubWindowToPct(x, y, &pct, &inside_hit) && inside_hit)
		IN_DemoScrubSetHover(true);

	hover_active = IN_DemoScrubPollHoverActive();
	if (!inside_hit && !hover_active)
		return false;

	seconds = (float)event->wheel.y;
	if (seconds == 0.0f)
		return false;

	if (!CL_DemoSeekRelativeSeconds(seconds))
		return false;

	SCR_ShowDemoBar();
	IN_UpdateGrabs();
	return true;
}
#endif

static void IN_DemoScrubRefreshCursor(void)
{
	const qboolean eligible = IN_DemoScrubEligible();
	const qboolean cursor_visible = IN_DemoScrubCursorVisible();

	if (eligible != demoscrub_was_eligible ||
		cursor_visible != demoscrub_cursor_was_visible)
		IN_UpdateGrabs();
}

void IN_DemoScrubCapture(qboolean capture)
{
#if defined(USE_SDL2)
#if SDL_VERSION_ATLEAST(2, 0, 4)
	if (capture == demoscrub_mouse_captured)
		return;
	if (SDL_CaptureMouse(capture ? SDL_TRUE : SDL_FALSE) != 0)
	{
		Con_DPrintf("WARNING: SDL_CaptureMouse(%s) failed: %s\n",
			capture ? "true" : "false", SDL_GetError());
		return;
	}
	demoscrub_mouse_captured = capture;
#else
	Q_UNUSED(capture);
#endif
#else
	Q_UNUSED(capture);
#endif
}

static void IN_UpdateGrabs_Internal(qboolean forecerelease)
{
	qboolean wantcursor;	//we're trying to get a cursor here...
	qboolean freemouse;		//the OS should have a free cursor too...
	qboolean needevents;	//whether we want to receive events still

	qboolean pong_active = Pong_Enabled() && !cls.demoplayback &&
		(cl.paused || cl.match_pause_time > 0) && key_dest == key_game; // woods #pong active?
	qboolean gamecodecursor = (key_dest == key_game && cl.qcvm.cursorforced) || (key_dest == key_menu && cls.menu_qcvm.cursorforced);
	qboolean demoscrub_eligible = IN_DemoScrubEligible();
	qboolean demoscrub_cursor;

	if (demoscrub_eligible && !demoscrub_was_eligible)
		IN_DemoScrubSeedHover();
	else if (!demoscrub_eligible && demoscrub_was_eligible)
		IN_DemoScrubSetHover(false);
	demoscrub_was_eligible = demoscrub_eligible;
	demoscrub_cursor = IN_DemoScrubCursorVisible();
	demoscrub_cursor_was_visible = demoscrub_cursor;

	wantcursor = (key_dest == key_console || key_dest == key_message)
	          || ((key_dest == key_game && CL_IsActiveObserver() && !obs_cursor_hidden)
	              || (key_dest == key_menu&&!bind_grab))
	          || gamecodecursor || demoscrub_cursor || !windowhasfocus;
	
	if (pong_active) // woods #pong
		wantcursor = false;
	
	freemouse = wantcursor || gamecodecursor || demoscrub_eligible || CL_DemoScrubActive() || (key_dest == key_game && CL_IsActiveObserver()); // woods #mousemenu - keep free mouse mode even when cursor is hidden

	if (pong_active) // woods #pong
		freemouse = true;

	needevents = (!wantcursor) || key_dest == key_game || key_dest == key_console || key_dest == key_message; // woods #conselection

	if (isDedicated)
		return;

	if (forecerelease)
		needevents = freemouse = wantcursor = true;

#ifdef MACOS_X_ACCELERATION_HACK
	if (needevents)
	{	/* Save the status of mouse acceleration */
		if (originalMouseSpeed == -1 && in_disablemacosxmouseaccel.value == 1)
			IN_DisableOSXMouseAccel();
	}
	else if (originalMouseSpeed != -1)
		IN_ReenableOSXMouseAccel();
	
	// Handle cvar change while focused
	if (!needevents && originalMouseSpeed != -1 && !in_disablemacosxmouseaccel.value)
		IN_ReenableOSXMouseAccel();
#endif

#if defined(USE_SDL2)
	// freemouse controls grab/relative mode; wantcursor controls visibility.
	if (freemouse)
	{
		if (SDL_GetRelativeMouseMode())
		{
			if (SDL_SetRelativeMouseMode(SDL_FALSE) != 0)
				Con_Printf("WARNING: SDL_SetRelativeMouseMode(SDL_FALSE) failed.\n");
		}
	}
	else
	{
		if (!SDL_GetRelativeMouseMode())
		{
			if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0)
				Con_Printf("WARNING: SDL_SetRelativeMouseMode(SDL_TRUE) failed.\n");
		}
	}

	if (wantcursor)
	{
		if (key_dest != key_console && key_dest != key_message)
		{
			VID_UpdateCursor(); // menu/game cursor
		}
		SDL_ShowCursor(SDL_ENABLE);
	}
	else
	{
		SDL_ShowCursor(SDL_DISABLE);
		VID_UpdateCursor();
	}
#else
	if (freemouse)
	{
		if (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_OFF)
		{
			SDL_WM_GrabInput(SDL_GRAB_OFF);
			if (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_OFF)
				Con_Printf("WARNING: SDL_WM_GrabInput(SDL_GRAB_OFF) failed.\n");
		}

		if (SDL_ShowCursor(SDL_QUERY) != SDL_ENABLE)
		{
			SDL_ShowCursor(SDL_ENABLE);
			if (SDL_ShowCursor(SDL_QUERY) != SDL_ENABLE)
				Con_Printf("WARNING: SDL_ShowCursor(SDL_ENABLE) failed.\n");
		}
	}
	else
	{
		if (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_ON)
		{
			SDL_WM_GrabInput(SDL_GRAB_ON);
			if (SDL_WM_GrabInput(SDL_GRAB_QUERY) != SDL_GRAB_ON)
				Con_Printf("WARNING: SDL_WM_GrabInput(SDL_GRAB_ON) failed.\n");
		}

		if (SDL_ShowCursor(SDL_QUERY) != SDL_DISABLE)
		{
			SDL_ShowCursor(SDL_DISABLE);
			if (SDL_ShowCursor(SDL_QUERY) != SDL_DISABLE)
				Con_Printf("WARNING: SDL_ShowCursor(SDL_DISABLE) failed.\n");
		}
	}
#endif

	if (needevents)
		IN_EndIgnoringMouseEvents();
	else
		IN_BeginIgnoringMouseEvents();
}
void IN_UpdateGrabs(void)
{
	IN_UpdateGrabs_Internal(false);
}

// Console command to show mouse input status
static void IN_MouseInfo_f(void)
{
	Con_Printf("Mouse Input Status:\n");
	Con_Printf("  SDL Mouse Events: %s\n", no_mouse ? "Disabled" : "Enabled");
	Con_Printf("  Window Focus: %s\n", windowhasfocus ? "Yes" : "No");
	
#if defined(USE_SDL2)
	// SDL2 mouse state information=
	const char* drv = SDL_GetCurrentVideoDriver();
	const char* warp = SDL_GetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP);
	Con_Printf("  SDL Video Driver: %s\n", drv ? drv : "(unknown)");

	if (!drv)
		Con_Printf("  Relative Mode Path: backend default (raw)\n");
	else if (!strcmp(drv, "windows"))
		Con_Printf("  Relative Mode Path: %s (Win RAWINPUT)\n",
			(warp && warp[0] == '1') ? "Warp fallback - accelerated"
			: "Raw");
	else if (!strcmp(drv, "x11"))
		Con_Printf("  Relative Mode Path: %s (X11)\n",
			(warp && warp[0] == '1') ? "Warp fallback - accelerated"
			: "XI2 raw");
	else if (!strcmp(drv, "wayland"))
		Con_Printf("  Relative Mode Path: Wayland zwp_relative_pointer (raw)\n");
	else
		Con_Printf("  Relative Mode Path: backend default (raw)\n");

	Con_Printf("  Relative Mouse Mode: %s\n", SDL_GetRelativeMouseMode() ? "Enabled" : "Disabled");
	Con_Printf("  Cursor Visibility: %s\n", SDL_ShowCursor(SDL_QUERY) ? "Visible" : "Hidden");
	
	// Mouse position information
	int mx, my;
	SDL_GetMouseState(&mx, &my);
	Con_Printf("  Mouse Position: %d, %d\n", mx, my);
	
	// Mouse button state
	Uint32 buttons = SDL_GetMouseState(NULL, NULL);
	Con_Printf("  Mouse Buttons: L:%s M:%s R:%s\n", 
		(buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) ? "Down" : "Up",
		(buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? "Down" : "Up",
		(buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? "Down" : "Up");
#else
	// SDL1 mouse state information
	int grab_state = SDL_WM_GrabInput(SDL_GRAB_QUERY);
	Con_Printf("  Mouse Grab: %s\n", 
		(grab_state == SDL_GRAB_ON) ? "Enabled" : "Disabled");
	Con_Printf("  Cursor Visibility: %s\n", SDL_ShowCursor(SDL_QUERY) ? "Visible" : "Hidden");
#endif
	
	// Platform-specific information
#ifdef __APPLE__
	Con_Printf("  HID Raw Input: %s\n", hid_mouse_active ? "Active" : "Inactive");
	Con_Printf("  Mouse Acceleration: %s\n", (in_disablemacosxmouseaccel.value == 1 && !hid_mouse_active) ? "Disabled" : "System Default");
	
	if (hid_mouse_active) {
		Con_Printf("  Input Method: HID Direct (True Raw Input)\n");
	} else if (in_disablemacosxmouseaccel.value == 1) {
		Con_Printf("  Input Method: SDL with Acceleration Disabled\n");
	} else {
		Con_Printf("  Input Method: SDL with System Acceleration\n");
	}
#elif defined(_WIN32)
	Con_Printf("  Input Method: SDL with System Settings\n");
	Con_Printf("  Key Filter: %s\n", "Active"); // Windows key filtering is active
#else
	Con_Printf("  Input Method: SDL with System Settings\n");
	Con_Printf("  Platform: Linux/Unix\n");
#endif
	
	// Game-specific mouse state
	Con_Printf("  Observer Cursor: %s\n", obs_cursor_hidden ? "Auto-hidden" : "Normal");
	Con_Printf("  Text Input Mode: %s\n", textmode ? "Enabled" : "Disabled");
	Con_Printf("  Bind Grab Mode: %s\n", bind_grab ? "Active" : "Inactive");
	
	// Mouse movement accumulation
	Con_Printf("  Movement Delta: %d, %d\n", total_dx, total_dy);
	
	// Mouse button mapping
	Con_Printf("  Button Mapping: L=Mouse1, R=Mouse3, M=Mouse2\n");
	Con_Printf("  Extended Buttons: Mouse4, Mouse5 supported\n");
}

void IN_StartupJoystick (void)
{
#if defined(USE_SDL2)
	if (COM_CheckParm("-nojoy"))
		return;

#if SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 22)
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 23, 2)
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 25, 1)
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS3, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 26, 0)
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 18)
	SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT, "1");
#endif

	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == -1 )
	{
		Con_Warning("could not initialize SDL Game Controller\n");
		return;
	}

	IN_LoadControllerMappings();
	IN_SetupJoystick();
#endif
}

void IN_ShutdownJoystick (void)
{
#if defined(USE_SDL2)
	IN_UseController(-1);
	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
#endif
}

qboolean IN_HasGamepad (void)
{
#if defined(USE_SDL2)
	return joy_active_controller != NULL;
#else
	return false;
#endif
}

const char *IN_GetGamepadName (void)
{
#if defined(USE_SDL2)
	return joy_active_controller ? joy_active_name : NULL;
#else
	return NULL;
#endif
}

gamepadtype_t IN_GetGamepadType (void)
{
#if defined(USE_SDL2)
	return joy_active_type;
#else
	return GAMEPAD_NONE;
#endif
}

qboolean IN_HasRumble (void)
{
#if defined(USE_SDL2)
	return joy_has_rumble;
#else
	return false;
#endif
}

qboolean IN_HasTriggerRumble (void)
{
#if defined(USE_SDL2) && SDL_VERSION_ATLEAST(2, 0, 18)
	return joy_has_trigger_rumble;
#else
	return false;
#endif
}

qboolean IN_HasTouchpad (void)
{
#if defined(USE_SDL2) && SDL_VERSION_ATLEAST(2, 0, 14)
	return joy_has_touchpad;
#else
	return false;
#endif
}

gamepadpower_t IN_GetGamepadPower (void)
{
#if defined(USE_SDL2)
	return joy_power;
#else
	return GAMEPAD_POWER_UNKNOWN;
#endif
}

#if defined(USE_SDL2)
static gamepadpower_t IN_TranslateGamepadPower (SDL_JoystickPowerLevel level)
{
	switch (level)
	{
	case SDL_JOYSTICK_POWER_EMPTY:	return GAMEPAD_POWER_EMPTY;
	case SDL_JOYSTICK_POWER_LOW:		return GAMEPAD_POWER_LOW;
	case SDL_JOYSTICK_POWER_MEDIUM:	return GAMEPAD_POWER_MEDIUM;
	case SDL_JOYSTICK_POWER_FULL:		return GAMEPAD_POWER_FULL;
	case SDL_JOYSTICK_POWER_WIRED:	return GAMEPAD_POWER_WIRED;
	default:						return GAMEPAD_POWER_UNKNOWN;
	}
}

static void IN_UpdateGamepadPower (SDL_JoystickPowerLevel level, qboolean notify)
{
	qboolean warn_empty;
	qboolean warn_low;

	joy_power = IN_TranslateGamepadPower(level);
	if (joy_power == GAMEPAD_POWER_MEDIUM || joy_power == GAMEPAD_POWER_FULL ||
		joy_power == GAMEPAD_POWER_WIRED)
	{
		joy_warned_low_power = false;
		joy_warned_empty_power = false;
	}

	warn_empty = notify && joy_power == GAMEPAD_POWER_EMPTY && !joy_warned_empty_power;
	warn_low = notify && joy_power == GAMEPAD_POWER_LOW && !joy_warned_low_power;
	if (warn_empty || warn_low)
	{
		Con_Warning("%s battery is %s\n", joy_active_name[0] ? joy_active_name : "Gamepad",
			joy_power == GAMEPAD_POWER_EMPTY ? "empty" : "low");
		joy_warned_low_power = true;
		if (warn_empty)
			joy_warned_empty_power = true;
	}
}
#endif

void IN_TestRumble (void)
{
#if defined(USE_SDL2) && SDL_VERSION_ATLEAST(2, 0, 9)
	if (joy_active_controller && (joy_has_rumble || joy_has_trigger_rumble))
	{
		joy_rumble_test_end = Sys_DoubleTime() + 0.2;
		if (joy_has_rumble)
			SDL_GameControllerRumble(joy_active_controller, 0x6000, 0xffff, 200);
#if SDL_VERSION_ATLEAST(2, 0, 18)
		if (joy_has_trigger_rumble)
			SDL_GameControllerRumbleTriggers(joy_active_controller, 0xb000, 0xffff, 200);
#endif
	}
#endif
}

qboolean IN_HasGyro (void)
{
#if defined(USE_SDL2)
	return gyro_present;
#else
	return false;
#endif
}

float IN_GetRawGyroMagnitude (void)
{
#if defined(USE_SDL2)
	return gyro_present ? gyro_raw_mag : 0.f;
#else
	return 0.f;
#endif
}

#if defined(USE_SDL2)
static float IN_GetControllerAxis (SDL_GameControllerAxis axis)
{
	if (!joy_active_controller)
		return 0.f;

	return SDL_GameControllerGetAxis(joy_active_controller, axis) / 32768.0f;
}
#endif

void IN_GetRawLookAxis (float *x, float *y)
{
#if defined(USE_SDL2)
	if (x)
		*x = IN_GetControllerAxis(joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_RIGHTX);
	if (y)
		*y = IN_GetControllerAxis(joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_RIGHTY);
#else
	if (x)
		*x = 0.f;
	if (y)
		*y = 0.f;
#endif
}

void IN_GetRawMoveAxis (float *x, float *y)
{
#if defined(USE_SDL2)
	if (x)
		*x = IN_GetControllerAxis(joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX);
	if (y)
		*y = IN_GetControllerAxis(joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY);
#else
	if (x)
		*x = 0.f;
	if (y)
		*y = 0.f;
#endif
}

void IN_GetRawTriggerAxis (float *left, float *right)
{
#if defined(USE_SDL2)
	if (left)
		*left = CLAMP(0.f, IN_GetControllerAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT), 1.f);
	if (right)
		*right = CLAMP(0.f, IN_GetControllerAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 1.f);
#else
	if (left)
		*left = 0.f;
	if (right)
		*right = 0.f;
#endif
}

float IN_GetRawLookMagnitude (void)
{
	float x, y;

	IN_GetRawLookAxis(&x, &y);
	return sqrtf(x * x + y * y);
}

float IN_GetRawMoveMagnitude (void)
{
	float x, y;

	IN_GetRawMoveAxis(&x, &y);
	return sqrtf(x * x + y * y);
}

float IN_GetRawTriggerMagnitude (void)
{
	float left, right;

	IN_GetRawTriggerAxis(&left, &right);
	return q_max(left, right);
}

void IN_StartGyroCalibration (void)
{
#if defined(USE_SDL2) && SDL_VERSION_ATLEAST(2, 0, 9)
	if (joy_has_rumble)
		SDL_GameControllerRumble(joy_active_controller, 0, 0, 100);
#if SDL_VERSION_ATLEAST(2, 0, 18)
	if (joy_has_trigger_rumble)
		SDL_GameControllerRumbleTriggers(joy_active_controller, 0, 0, 100);
#endif
#endif

	gyro_accum[0] = 0.f;
	gyro_accum[1] = 0.f;
	gyro_accum[2] = 0.f;
	updates_countdown = GYRO_CALIBRATION_SAMPLES;
	Con_Printf("Calibrating, please wait...\n");
}

qboolean IN_IsCalibratingGyro (void)
{
	return updates_countdown != 0;
}

float IN_GetGyroCalibrationProgress (void)
{
	return (GYRO_CALIBRATION_SAMPLES - updates_countdown) / (float)GYRO_CALIBRATION_SAMPLES;
}

int IN_GetLastActiveDeviceType (void)
{
	return lastactivetype;
}

static qboolean IN_UpdateGyroCalibration (const float newsample[3])
{
	if (!updates_countdown)
		return false;

	gyro_accum[0] += newsample[0];
	gyro_accum[1] += newsample[1];
	gyro_accum[2] += newsample[2];

	updates_countdown--;
	if (!updates_countdown)
	{
		const float inversesamples = 1.f / GYRO_CALIBRATION_SAMPLES;

		Cvar_SetValue("gyro_calibration_x", gyro_accum[0] * inversesamples);
		Cvar_SetValue("gyro_calibration_y", gyro_accum[1] * inversesamples);
		Cvar_SetValue("gyro_calibration_z", gyro_accum[2] * inversesamples);

		Con_Printf("Calibration results:\n X=%f Y=%f Z=%f\n",
			gyro_calibration_x.value, gyro_calibration_y.value, gyro_calibration_z.value);
		Con_Printf("Calibration finished\n");
		return false;
	}

	return true;
}

static float IN_FilterGyroSample (float prev, float cur)
{
	float thresh = DEG2RAD(gyro_noise_thresh.value);
	float d = fabs(cur - prev);

	if (d < thresh)
	{
		d /= thresh;
		cur = LERP(prev, cur, 0.01f + 0.99f * d * d);
	}

	return cur;
}

void IN_Init (void)
{
	textmode = Key_TextEntry();

#if !defined(USE_SDL2)
	SDL_EnableUNICODE (textmode);
	if (SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL) == -1)
		Con_Printf("Warning: SDL_EnableKeyRepeat() failed.\n");
#else
	if (textmode)
		SDL_StartTextInput();
	else
		SDL_StopTextInput();
#endif
	if (safemode || COM_CheckParm("-nomouse"))
	{
		no_mouse = true;
		/* discard all mouse events when input is deactivated */
		IN_BeginIgnoringMouseEvents();
	}

	obs_cursor_last_move = SDL_GetTicks(); // woods -- initialize observer cursor state #eyemouse
	obs_cursor_hidden = false; // woods -- initialize observer cursor state #eyemouse

#ifdef MACOS_X_ACCELERATION_HACK
	Cvar_RegisterVariable(&in_disablemacosxmouseaccel);
#endif
	Cvar_RegisterVariable(&in_debugkeys);
	Cvar_RegisterVariable(&joy_sensitivity_yaw);
	Cvar_RegisterVariable(&joy_sensitivity_pitch);
	Cvar_RegisterVariable(&joy_deadzone_look);
	Cvar_RegisterVariable(&joy_deadzone_move);
	Cvar_RegisterVariable(&joy_outer_threshold_look);
	Cvar_RegisterVariable(&joy_outer_threshold_move);
	Cvar_RegisterVariable(&joy_deadzone_trigger);
	Cvar_RegisterVariable(&joy_invert);
	Cvar_RegisterVariable(&joy_exponent);
	Cvar_RegisterVariable(&joy_exponent_move);
	Cvar_RegisterVariable(&joy_swapmovelook);
	Cvar_RegisterVariable(&joy_flick);
	Cvar_SetCallback(&joy_flick, Joy_Flick_f);
	Cvar_RegisterVariable(&joy_flick_time);
	Cvar_RegisterVariable(&joy_flick_recenter);
	Cvar_RegisterVariable(&joy_flick_deadzone);
	Cvar_RegisterVariable(&joy_flick_noise_thresh);
	Cvar_RegisterVariable(&joy_flick_adjust_speed);
	Cvar_RegisterVariable(&joy_rumble);
	Cvar_RegisterVariable(&joy_rumble_triggers);
	Cvar_RegisterVariable(&joy_touchpad);
	Cvar_RegisterVariable(&joy_enable);
	Cvar_RegisterVariable(&joy_device);
	Cvar_SetCallback(&joy_device, Joy_Device_f);
	Cvar_SetCompletion(&joy_device, Joy_Device_Completion_f);
	Cvar_RegisterVariable(&joy_always_active);
	Cvar_RegisterVariable(&gyro_enable);
	Cvar_RegisterVariable(&gyro_mode);
	Cvar_RegisterVariable(&gyro_turning_axis);
	Cvar_RegisterVariable(&gyro_yawsensitivity);
	Cvar_RegisterVariable(&gyro_pitchsensitivity);
	Cvar_RegisterVariable(&gyro_calibration_x);
	Cvar_RegisterVariable(&gyro_calibration_y);
	Cvar_RegisterVariable(&gyro_calibration_z);
	Cvar_RegisterVariable(&gyro_noise_thresh);

	Cmd_AddCommand("in_mouseinfo", IN_MouseInfo_f);
	Cmd_AddCommand("+gyroaction", IN_GyroActionDown);
	Cmd_AddCommand("-gyroaction", IN_GyroActionUp);
	Cmd_AddCommand("gamecontrollerdb_reload", IN_ReloadControllerMappings_f);

	IN_UpdateGrabs();
	IN_StartupJoystick();
#if defined(_WIN32) // woods #disablecaps via ironwail
	Sys_ActivateKeyFilter(true);
#endif

#ifdef __APPLE__
	// HID Raw Mouse Input
	if (in_disablemacosxmouseaccel.value == 2 && !no_mouse) {
		if (HID_MouseInit()) {
			Con_DPrintf("HID Raw Mouse: Enabled (use 'in_disablemacosxmouseaccel 1' to disable)\n");
		} else {
			Con_DPrintf("HID Raw Mouse: Failed to initialize - using SDL mouse\n");
}
	}
#endif
}

void IN_Shutdown (void)
{
	Con_DPrintf("IN_Shutdown called\n");
#if defined(USE_SDL2)
	IN_ClearDropBatch();
#endif
#if defined(_WIN32) // woods #disablecaps via ironwail
	Sys_ActivateKeyFilter(false);
#endif
	IN_UpdateGrabs();
	IN_ShutdownJoystick();
#ifdef __APPLE__
	HID_MouseShutdown();
#endif
#ifdef MACOS_X_ACCELERATION_HACK
	Con_DPrintf("IN_Shutdown: calling IN_ReenableOSXMouseAccel\n");
	if (originalMouseSpeed != -1)
		IN_ReenableOSXMouseAccel();
#endif
}

extern cvar_t cl_maxpitch; /* johnfitz -- variable pitch clamping */
extern cvar_t cl_minpitch; /* johnfitz -- variable pitch clamping */
extern cvar_t v_centerspeed;

extern cvar_t scr_fov; // woods #zoom (ironwail)

static float IN_FovScale (void)
{
	return tan(DEG2RAD(r_refdef.basefov) * 0.5f) / tan(DEG2RAD(scr_fov.value) * 0.5f);
}

static float IN_RecenterEasing (float frac)
{
	return frac * frac;
}

void IN_MouseMotion(int dx, int dy, int wx, int wy)
{
	if (!windowhasfocus)
		dx = dy = 0;	//don't change view angles etc while unfocused.
	vid.cursorpos[0] = wx;
	vid.cursorpos[1] = wy;

	if (cl.paused || cl.match_pause_time > 0) // if the game is paused in any way (regular or match pause) #pong
	{
		Pong_MouseMove(wx, wy);
	}

	else if (key_dest == key_menu && cls.menu_qcvm.extfuncs.Menu_InputEvent)
	{
		PR_SwitchQCVM(&cls.menu_qcvm);
		if (qcvm->cursorforced)
		{
			float s;
			s = q_min((float)glwidth / 320.0, (float)glheight / 200.0);
			s = CLAMP (1.0, scr_menuscale.value, s);
			wx /= s;
			wy /= s;

			G_FLOAT(OFS_PARM0) = CSIE_MOUSEABS;
			G_VECTORSET(OFS_PARM1, wx, wy, 0);	//x
			G_VECTORSET(OFS_PARM2, wy, 0, 0);	//y
			G_VECTORSET(OFS_PARM3, 0, 0, 0);	//devid
		}
		else if (dx||dy)
		{
			G_FLOAT(OFS_PARM0) = CSIE_MOUSEDELTA;
			G_VECTORSET(OFS_PARM1, dx, dy, 0);	//x
			G_VECTORSET(OFS_PARM2, dy, 0, 0);	//y
			G_VECTORSET(OFS_PARM3, 0, 0, 0);	//devid
		}
		PR_ExecuteProgram(qcvm->extfuncs.Menu_InputEvent);
		if (G_FLOAT(OFS_RETURN) || qcvm->cursorforced)
			dx = dy = 0;	//if the qc says it handled it, swallow the movement.
		PR_SwitchQCVM(NULL);
	}
	else if (key_dest != key_game && key_dest != key_message)
		dx = dy = 0;
	else if (cl.qcvm.extfuncs.CSQC_InputEvent)
	{
		PR_SwitchQCVM(&cl.qcvm);
		if (qcvm->cursorforced)
		{
			float s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
			wx /= s;
			wy /= s;

			G_FLOAT(OFS_PARM0) = CSIE_MOUSEABS;
			G_VECTORSET(OFS_PARM1, wx, wy, 0);	//x
			G_VECTORSET(OFS_PARM2, wy, 0, 0);	//y
			G_VECTORSET(OFS_PARM3, 0, 0, 0);	//devid
		}
		else
		{
			G_FLOAT(OFS_PARM0) = CSIE_MOUSEDELTA;
			G_VECTORSET(OFS_PARM1, dx, dy, 0);	//x
			G_VECTORSET(OFS_PARM2, dy, 0, 0);	//y
			G_VECTORSET(OFS_PARM3, 0, 0, 0);	//devid
		}
		PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_InputEvent);
		if (G_FLOAT(OFS_RETURN) || qcvm->cursorforced)
			dx = dy = 0;	//if the qc says it handled it, swallow the movement.
		PR_SwitchQCVM(NULL);
	}
	if (cls.state != ca_connected || cls.signon != SIGNONS)
	{
		total_dx = 0;
		total_dy = 0;
		return;
	}
	total_dx += dx;
	total_dy += dy;
}

#if defined(USE_SDL2)
typedef struct joyaxis_s
{
	float x;
	float y;
} joyaxis_t;

typedef struct joy_buttonstate_s
{
	qboolean buttondown[SDL_CONTROLLER_BUTTON_MAX];
} joybuttonstate_t;

typedef struct axisstate_s
{
	float axisvalue[SDL_CONTROLLER_AXIS_MAX]; // normalized to +-1
} joyaxisstate_t;

static joybuttonstate_t joy_buttonstate;
static joyaxisstate_t joy_axisstate;
static joyaxisstate_t joy_csqc_axisstate;
static qboolean joy_axis_consumed[SDL_CONTROLLER_AXIS_MAX];
static dprograms_t *joy_csqc_progs;
static func_t joy_csqc_inputevent;

static double joy_buttontimer[SDL_CONTROLLER_BUTTON_MAX];
static int joy_buttonkey[SDL_CONTROLLER_BUTTON_MAX];
static double joy_emulatedkeytimer[6];

#define JOY_CSQC_AXIS_EPSILON (1.0f / 256.0f)

#ifdef __WATCOMC__ /* OW1.9 doesn't have powf() / sqrtf() */
#define powf pow
#define sqrtf sqrt
#endif

static int IN_KeyForControllerButton(SDL_GameControllerButton button);

/*
================
IN_AxisMagnitude

Returns the vector length of the given joystick axis
================
*/
static vec_t IN_AxisMagnitude(joyaxis_t axis)
{
	vec_t magnitude = sqrtf((axis.x * axis.x) + (axis.y * axis.y));
	return magnitude;
}

static void IN_ResetFlickState(void)
{
	memset(&flick, 0, sizeof(flick));
}

static void IN_ReleaseJoystickKeys(void)
{
	int i;

	for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
	{
		if (joy_buttonstate.buttondown[i])
		{
			int key = joy_buttonkey[i] ? joy_buttonkey[i] : IN_KeyForControllerButton((SDL_GameControllerButton)i);
			if (key > 0)
				Key_Event(key, false);
		}
	}

	// These arrow keycodes are shared with the physical keyboard, so only
	// release them here when joystick menu emulation actually generated them.
	if (joy_emulatedkeytimer[0] != 0.0)
		Key_Event(K_LEFTARROW, false);
	if (joy_emulatedkeytimer[1] != 0.0)
		Key_Event(K_RIGHTARROW, false);
	if (joy_emulatedkeytimer[2] != 0.0)
		Key_Event(K_UPARROW, false);
	if (joy_emulatedkeytimer[3] != 0.0)
		Key_Event(K_DOWNARROW, false);
	Key_Event(K_LTRIGGER, false);
	Key_Event(K_RTRIGGER, false);
}

static void IN_ResetJoystickState(void)
{
	IN_ReleaseJoystickKeys();
	memset(&joy_buttonstate, 0, sizeof(joy_buttonstate));
	memset(&joy_axisstate, 0, sizeof(joy_axisstate));
	memset(&joy_csqc_axisstate, 0, sizeof(joy_csqc_axisstate));
	memset(joy_axis_consumed, 0, sizeof(joy_axis_consumed));
	joy_csqc_progs = NULL;
	joy_csqc_inputevent = 0;
	memset(joy_buttontimer, 0, sizeof(joy_buttontimer));
	memset(joy_buttonkey, 0, sizeof(joy_buttonkey));
	memset(joy_emulatedkeytimer, 0, sizeof(joy_emulatedkeytimer));
}

/*
================
IN_ApplyEasing

assumes axis values are in [-1, 1] and the vector magnitude has been clamped at 1.
Raises the axis values to the given exponent, keeping signs.
================
*/
static joyaxis_t IN_ApplyEasing(joyaxis_t axis, float exponent)
{
	joyaxis_t result = {0};
	vec_t eased_magnitude;
	vec_t magnitude = IN_AxisMagnitude(axis);
	
	if (magnitude == 0)
		return result;
	
	eased_magnitude = powf(magnitude, exponent);
	
	result.x = axis.x * (eased_magnitude / magnitude);
	result.y = axis.y * (eased_magnitude / magnitude);
	return result;
}

/*
================
IN_ApplyDeadzone

in: raw joystick axis values converted to floats in +-1
out: applies a circular inner deadzone and a circular outer threshold and clamps the magnitude at 1
     (my 360 controller is slightly non-circular and the stick travels further on the diagonals)

deadzone is expected to satisfy 0 < deadzone < 1 - outer_threshold
outer_threshold is expected to satisfy 0 < outer_threshold < 1 - deadzone

from https://github.com/jeremiah-sypult/Quakespasm-Rift
and adapted from http://www.third-helix.com/2013/04/12/doing-thumbstick-dead-zones-right.html
================
*/
static joyaxis_t IN_ApplyDeadzone(joyaxis_t axis, float deadzone, float outer_threshold)
{
	joyaxis_t result = {0};
	vec_t magnitude = IN_AxisMagnitude(axis);
	
	if ( magnitude > deadzone ) {
		// rescale the magnitude so deadzone becomes 0, and 1-outer_threshold becomes 1
		const vec_t new_magnitude = q_min(1.0, (magnitude - deadzone) / (1.0 - deadzone - outer_threshold));
		const vec_t scale = new_magnitude / magnitude;
		result.x = axis.x * scale;
		result.y = axis.y * scale;
	}
	
	return result;
}

static joyaxis_t IN_GetLookAxis(joyaxisstate_t *state)
{
	joyaxis_t axis;

	axis.x = state->axisvalue[joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_RIGHTX];
	axis.y = state->axisvalue[joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_RIGHTY];
	return axis;
}

static joyaxis_t IN_GetMoveAxis(joyaxisstate_t *state)
{
	joyaxis_t axis;

	axis.x = state->axisvalue[joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX];
	axis.y = state->axisvalue[joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY];
	return axis;
}

static qboolean IN_JoyActive(void)
{
	return joy_active_controller != NULL && (joy_always_active.value || lastactivetype == KD_GAMEPAD);
}

/*
================
IN_KeyForControllerButton
================
*/
static int IN_KeyForControllerButton(SDL_GameControllerButton button)
{
	switch (button)
	{
		case SDL_CONTROLLER_BUTTON_A: return K_ABUTTON;
		case SDL_CONTROLLER_BUTTON_B: return K_BBUTTON;
		case SDL_CONTROLLER_BUTTON_X: return K_XBUTTON;
		case SDL_CONTROLLER_BUTTON_Y: return K_YBUTTON;
		/* View/Back opens search only in eligible native menus. Disabling menu
		 * search restores its legacy K_TAB mapping everywhere. */
		case SDL_CONTROLLER_BUTTON_BACK: return M_MenuSearch_UseGamepadBack() ? K_BACK : K_TAB;
		case SDL_CONTROLLER_BUTTON_START: return K_ESCAPE;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK: return K_LTHUMB;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return K_RTHUMB;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return K_LSHOULDER;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return K_RSHOULDER;
		case SDL_CONTROLLER_BUTTON_DPAD_UP: return K_DPAD_UP;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return K_DPAD_DOWN;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return K_DPAD_LEFT;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return K_DPAD_RIGHT;
#if SDL_VERSION_ATLEAST(2, 0, 14)
		case SDL_CONTROLLER_BUTTON_MISC1: return K_MISC1;
		case SDL_CONTROLLER_BUTTON_PADDLE1: return K_PADDLE1;
		case SDL_CONTROLLER_BUTTON_PADDLE2: return K_PADDLE2;
		case SDL_CONTROLLER_BUTTON_PADDLE3: return K_PADDLE3;
		case SDL_CONTROLLER_BUTTON_PADDLE4: return K_PADDLE4;
		case SDL_CONTROLLER_BUTTON_TOUCHPAD: return K_TOUCHPAD;
#endif
		default: return 0;
	}
}

/*
================
IN_JoyKeyEvent

Sends a Key_Event if a unpressed -> pressed or pressed -> unpressed transition occurred,
and generates key repeats if the button is held down.

Adapted from DarkPlaces by lordhavoc
================
*/
static void IN_JoyKeyEvent(qboolean wasdown, qboolean isdown, int key, double *timer)
{
	static const double repeatdelay = 0.5;
	static const double repeatrate = 32.0;

	// we can't use `realtime` for key repeats because it is not monotomic
	const double currenttime = Sys_DoubleTime();
	
	if (wasdown)
	{
		if (isdown)
		{
			if (currenttime >= *timer)
			{
				*timer = currenttime + 1.0 / repeatrate;
				lastactivetype = KD_GAMEPAD;
				Key_Event(key, true);
			}
		}
		else
		{
			*timer = 0;
			lastactivetype = KD_GAMEPAD;
			Key_Event(key, false);
		}
	}
	else
	{
		if (isdown)
		{
			*timer = currenttime + repeatdelay;
			lastactivetype = KD_GAMEPAD;
			Key_Event(key, true);
		}
	}
}

static qboolean IN_CSQCAxisEvent(int axis, float value)
{
	qboolean consumed;

	if (!cl.qcvm.progs || !cl.qcvm.extfuncs.CSQC_InputEvent)
		return false;

	PR_SwitchQCVM(&cl.qcvm);
	G_FLOAT(OFS_PARM0) = CSIE_JOYAXIS;
	// Keep the SDL game-controller axis order here; joy_swapmovelook only
	// changes the engine's movement/look mapping, not the CSQC axis identity.
	G_FLOAT(OFS_PARM1) = (float)axis;
	G_FLOAT(OFS_PARM2) = value;
	// QSS-M exposes one active controller to CSQC.  Keep its logical device
	// ID consistent with the existing keyboard and mouse input events instead
	// of exposing an SDL enumeration or instance ID to QC.
	G_FLOAT(OFS_PARM3) = 0.0f;
	PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_InputEvent);
	consumed = G_FLOAT(OFS_RETURN) != 0;
	PR_SwitchQCVM(NULL);

	return consumed;
}

static void IN_JoyTriggerKeyEvent(qboolean consumed, qboolean wasdown, qboolean isdown, int key, double *timer)
{
	if (consumed)
	{
		if (*timer != 0.0)
		{
			Key_Event(key, false);
			*timer = 0.0;
		}
		return;
	}

	// If QC consumed the preceding press, do not synthesize an unmatched
	// release when it stops consuming the axis at the neutral position.
	if (wasdown && !isdown && *timer == 0.0)
		return;

	IN_JoyKeyEvent(wasdown, isdown, key, timer);
}

static void IN_UpdateCSQCAxisEvents(const joyaxisstate_t *newaxisstate)
{
	int i;
	func_t inputevent = cl.qcvm.extfuncs.CSQC_InputEvent;

	if (joy_csqc_progs != cl.qcvm.progs || joy_csqc_inputevent != inputevent)
	{
		memset(&joy_csqc_axisstate, 0, sizeof(joy_csqc_axisstate));
		memset(joy_axis_consumed, 0, sizeof(joy_axis_consumed));
		joy_csqc_progs = cl.qcvm.progs;
		joy_csqc_inputevent = inputevent;
	}

	if (key_dest != key_game || !inputevent)
	{
		// Do not carry a gameplay baseline across menu/console input.  If an
		// axis is held while returning to the game, the next frame should report
		// its current value to CSQC rather than treating it as unchanged.
		memset(&joy_csqc_axisstate, 0, sizeof(joy_csqc_axisstate));
		memset(joy_axis_consumed, 0, sizeof(joy_axis_consumed));
		return;
	}

	for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
	{
		// CSQC may change the input destination or reload itself from inside
		// the callback.  Do not dispatch the remaining axes under that new
		// state, and do not retain consumption results from the old state.
		if (key_dest != key_game || cl.qcvm.progs != joy_csqc_progs ||
			cl.qcvm.extfuncs.CSQC_InputEvent != inputevent)
		{
			memset(joy_axis_consumed, 0, sizeof(joy_axis_consumed));
			break;
		}

		float value = newaxisstate->axisvalue[i];
		float last = joy_csqc_axisstate.axisvalue[i];

		// SDL axis values jitter slightly even while the stick is stationary.
		// Report meaningful changes, but always report the return to exact zero.
		if (fabsf(value - last) < JOY_CSQC_AXIS_EPSILON &&
			!(value == 0.0f && last != 0.0f))
			continue;

		// Update before entering QC so a reentrant IN_Commands call cannot
		// dispatch the same axis event again.  Treat the axis as consumed while
		// QC is running so reentrant input processing cannot synthesize a key
		// event before the callback's return value is known.  Keep that
		// provisional value through the callback: a reentrant pass can still
		// reach trigger emulation even when its axis loop emits nothing.
		joy_csqc_axisstate.axisvalue[i] = value;
		joy_axis_consumed[i] = true;
		joy_axis_consumed[i] = IN_CSQCAxisEvent(i, value);
	}
}

static void IN_LoadControllerMappingsFromDir(const char *dir)
{
	char controllerdb[MAX_OSPATH];
	int nummappings;

	if (!dir || !*dir)
		return;

	q_snprintf(controllerdb, sizeof(controllerdb), "%s/gamecontrollerdb.txt", dir);
	if (!(Sys_FileType(controllerdb) & FS_ENT_FILE))
		return;
	nummappings = SDL_GameControllerAddMappingsFromFile(controllerdb);
	if (nummappings < 0)
		Con_Warning("couldn't load controller mappings from %s: %s\n",
			controllerdb, SDL_GetError());
	else if (nummappings > 0)
		Con_Printf("%d mappings loaded from %s\n", nummappings, controllerdb);
}

static void IN_LoadControllerSearchPathMappings(searchpath_t *search)
{
	searchpath_t *prev;

	if (!search)
		return;

	// SDL replaces an existing GUID mapping with the last one loaded. Walk the
	// Quake search path from lowest to highest priority so mod/user mappings win.
	IN_LoadControllerSearchPathMappings(search->next);

	if (search->pack || !search->filename[0])
		return;
	if (!q_strcasecmp(search->filename, com_basedir))
		return;

	// Only load the highest-priority occurrence of a repeated directory.
	for (prev = com_searchpaths; prev != search; prev = prev->next)
	{
		if (!prev->pack && !q_strcasecmp(prev->filename, search->filename))
			return;
	}

	IN_LoadControllerMappingsFromDir(search->filename);
}

static void IN_LoadControllerMappings(void)
{
	const char *userdir = host_parms ? host_parms->userdir : NULL;

	// This also backs gamecontrollerdb_reload for picking up mappings after searchpath changes.
	IN_LoadControllerMappingsFromDir(com_basedir);
	IN_LoadControllerSearchPathMappings(com_searchpaths);

	// A loose database in the user root is the final, highest-priority override.
	if (userdir && *userdir && q_strcasecmp(userdir, com_basedir))
		IN_LoadControllerMappingsFromDir(userdir);
}

static void IN_ClearActiveControllerState(void)
{
	joy_active_instanceid = -1;
	joy_active_device = -1;
	joy_active_type = GAMEPAD_NONE;
	joy_active_name[0] = '\0';
	gyro_present = false;
	gyro_yaw = 0.f;
	gyro_pitch = 0.f;
	gyro_raw_mag = 0.f;
	gyro_center_frac = 0.f;
	gyro_center_amount = 0.f;
	joy_has_rumble = false;
	joy_has_trigger_rumble = false;
	joy_has_touchpad = false;
	joy_power = GAMEPAD_POWER_UNKNOWN;
	joy_warned_low_power = false;
	joy_warned_empty_power = false;
	joy_rumble_test_end = 0.0;
	gyro_button_pressed = false;
	updates_countdown = 0;
	IN_ResetFlickState();
	IN_ResetJoystickState();
}

static void IN_CloseActiveController(qboolean announce)
{
	if (!joy_active_controller)
		return;

#if SDL_VERSION_ATLEAST(2, 0, 9)
	if (joy_has_rumble)
		SDL_GameControllerRumble(joy_active_controller, 0, 0, 100);
#endif
#if SDL_VERSION_ATLEAST(2, 0, 18)
	if (joy_has_trigger_rumble)
		SDL_GameControllerRumbleTriggers(joy_active_controller, 0, 0, 100);
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
	if (SDL_GameControllerHasLED(joy_active_controller))
		SDL_GameControllerSetLED(joy_active_controller, 0, 0, 0);
#endif

	if (announce)
		Con_Printf("Gamepad removed: %s\n", joy_active_name);
	SDL_GameControllerClose(joy_active_controller);
	joy_active_controller = NULL;
	IN_ClearActiveControllerState();
}

static void IN_RefreshActiveControllerInfo(void)
{
	const char *controllername;

	if (!joy_active_controller)
		return;

	controllername = SDL_GameControllerName(joy_active_controller);
	q_strlcpy(joy_active_name, controllername ? controllername : "[Unknown gamepad]",
		sizeof(joy_active_name));

#if SDL_VERSION_ATLEAST(2, 0, 12)
	switch (SDL_GameControllerGetType(joy_active_controller))
	{
	default:
	case SDL_CONTROLLER_TYPE_XBOX360:
	case SDL_CONTROLLER_TYPE_XBOXONE:
		joy_active_type = GAMEPAD_XBOX;
		break;

	case SDL_CONTROLLER_TYPE_PS3:
	case SDL_CONTROLLER_TYPE_PS4:
#if SDL_VERSION_ATLEAST(2, 0, 14)
	case SDL_CONTROLLER_TYPE_PS5:
#endif
		joy_active_type = GAMEPAD_PLAYSTATION;
		break;

	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
		joy_active_type = GAMEPAD_NINTENDO;
		break;
	}
#else
	joy_active_type = GAMEPAD_XBOX;
#endif

	joy_has_touchpad = false;
#if SDL_VERSION_ATLEAST(2, 0, 14)
	if (SDL_GameControllerHasLED(joy_active_controller))
		SDL_GameControllerSetLED(joy_active_controller, 80, 20, 0);
	joy_has_touchpad = SDL_GameControllerGetNumTouchpads(joy_active_controller) > 0;
	if (SDL_GameControllerHasSensor(joy_active_controller, SDL_SENSOR_GYRO) &&
		!SDL_GameControllerSetSensorEnabled(joy_active_controller, SDL_SENSOR_GYRO, SDL_TRUE))
	{
		gyro_present = true;
	}
	else
#endif
	{
		gyro_present = false;
		gyro_yaw = 0.f;
		gyro_pitch = 0.f;
		gyro_raw_mag = 0.f;
		gyro_center_frac = 0.f;
		gyro_center_amount = 0.f;
		updates_countdown = 0;
	}

	joy_has_rumble = false;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	joy_has_rumble = SDL_GameControllerHasRumble(joy_active_controller);
#elif SDL_VERSION_ATLEAST(2, 0, 9)
	joy_has_rumble = SDL_GameControllerRumble(joy_active_controller, 0, 0, 0) == 0;
#endif
	joy_has_trigger_rumble = false;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	joy_has_trigger_rumble = SDL_GameControllerHasRumbleTriggers(joy_active_controller);
#endif
	IN_UpdateGamepadPower(SDL_JoystickCurrentPowerLevel(
		SDL_GameControllerGetJoystick(joy_active_controller)), joy_enable.value != 0.f);
}

static void IN_ReloadControllerMappings_f(void)
{
	int desired_device;
	SDL_JoystickID desired_instanceid;
	qboolean had_active_controller;

	if (!(SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER))
	{
		Con_Printf("Controller subsystem is not initialized\n");
		return;
	}

	desired_device = (int)joy_device.value;
	desired_instanceid = joy_active_instanceid;
	had_active_controller = joy_active_controller != NULL;

	if (had_active_controller)
		IN_CloseActiveController(false);

	IN_LoadControllerMappings();
	Con_Printf("Controller mappings reloaded\n");

	if (had_active_controller)
	{
		int i, count = SDL_NumJoysticks();

		for (i = 0; i < count; i++)
		{
			if (SDL_JoystickGetDeviceInstanceID(i) == desired_instanceid)
			{
				desired_device = i;
				break;
			}
		}
		Cvar_SetValueQuick(&joy_device, desired_device);
	}

	IN_SetupJoystick();
}

static qboolean IN_UseController(int device_index)
{
	SDL_GameController *gamecontroller;

	if (device_index == joy_active_device && joy_active_controller &&
		SDL_GameControllerGetAttached(joy_active_controller) &&
		device_index >= 0 && device_index < SDL_NumJoysticks() &&
		SDL_JoystickGetDeviceInstanceID(device_index) == joy_active_instanceid)
	{
		if ((int)joy_device.value != device_index)
			Cvar_SetValueQuick(&joy_device, device_index);
		return true;
	}

	if (joy_active_controller)
		IN_CloseActiveController(device_index == -1);
	else
		IN_ResetJoystickState();

	if (device_index == -1)
		return true;

	if (device_index < 0 || device_index >= SDL_NumJoysticks())
		return false;

	if (!SDL_IsGameController(device_index))
	{
		const char *joyname = SDL_JoystickNameForIndex(device_index);
		Con_Warning("joystick missing controller mappings: %s\n",
			joyname != NULL ? joyname : "NULL");
		return false;
	}

	gamecontroller = SDL_GameControllerOpen(device_index);
	if (!gamecontroller)
	{
		Con_Warning("couldn't open gamepad device %d\n", device_index);
		return false;
	}

	joy_active_controller = gamecontroller;
	joy_active_instanceid = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamecontroller));
	joy_active_device = device_index;
	Cvar_SetValueQuick(&joy_device, device_index);
	IN_RefreshActiveControllerInfo();
	Con_Printf("Using gamepad: %s\n", joy_active_name);

#if SDL_VERSION_ATLEAST(2, 0, 14)
	if (gyro_present)
	{
#if SDL_VERSION_ATLEAST(2, 0, 16)
		Con_Printf("Gyro sensor enabled at %g Hz\n",
			SDL_GameControllerGetSensorDataRate(joy_active_controller, SDL_SENSOR_GYRO));
#else
		Con_Printf("Gyro sensor enabled.\n");
#endif
	}
	else
	{
		Con_Printf("Gyro sensor not found\n");
	}
#endif

	return true;
}

static void IN_SetupJoystick(void)
{
	int count = SDL_NumJoysticks();
	int device_index;
	int i;

	if (count < 0)
	{
		Con_Warning("couldn't enumerate joystick devices: %s\n", SDL_GetError());
		if (joy_active_controller && !SDL_GameControllerGetAttached(joy_active_controller))
			IN_UseController(-1);
		return;
	}

	device_index = CLAMP(-1, (int)joy_device.value, count - 1);

	if (device_index == -1)
	{
		IN_UseController(-1);
		return;
	}

	// Device indices include raw joysticks that do not have a controller
	// mapping. Prefer the configured index, then fall back to the first usable
	// game controller so an unmapped joystick cannot block plug-and-play.
	if (SDL_IsGameController(device_index) && IN_UseController(device_index))
		return;
	for (i = 0; i < count; i++)
	{
		if (i != device_index && SDL_IsGameController(i) && IN_UseController(i))
			return;
	}

	// Preserve the existing diagnostic when only an unmapped joystick exists.
	if (!SDL_IsGameController(device_index))
		IN_UseController(device_index);
}

static qboolean IN_RemapJoystick(void)
{
	int i, count, old_device;

	if (joy_active_instanceid == -1)
		return false;

	old_device = joy_active_device;

	for (i = 0, count = SDL_NumJoysticks(); i < count; i++)
	{
		if (SDL_JoystickGetDeviceInstanceID(i) == joy_active_instanceid)
		{
			joy_active_device = i;
			if ((int)joy_device.value == old_device)
				Cvar_SetValueQuick(&joy_device, i);
			return true;
		}
	}

	return false;
}

static void Joy_Device_f(cvar_t *cvar)
{
	if ((int)cvar->value != joy_active_device)
		IN_SetupJoystick();
}

static void Joy_Device_Completion_f(cvar_t *cvar, const char *partial)
{
	int i, count;

	(void)cvar;

	for (i = 0, count = SDL_NumJoysticks(); i < count; i++)
	{
		if (SDL_IsGameController(i))
			Con_AddToTabList(va("%d", i), partial, SDL_GameControllerNameForIndex(i), NULL);
	}
}

static void Joy_Flick_f(cvar_t *cvar)
{
	(void)cvar;
	IN_ResetFlickState();
}

void IN_GyroActionDown (void)
{
	gyro_button_pressed = true;
}

void IN_GyroActionUp (void)
{
	gyro_button_pressed = false;
}
#endif

/*
================
IN_Commands

Emit key events for game controller buttons, including emulated buttons for analog sticks/triggers
================
*/
void IN_Commands (void)
{
#if defined(USE_SDL2)
	joyaxisstate_t newaxisstate;
	joyaxisstate_t oldaxisstate;
	joyaxis_t old_move, new_move, raw, deadzone, eased;
	int i;
	const float stickthreshold = 0.9;
	const float triggerthreshold = joy_deadzone_trigger.value;
	
	if (!joy_enable.value)
	{
		IN_ResetJoystickState();
		return;
	}
	
	if (!joy_active_controller)
	{
		IN_ResetJoystickState();
		return;
	}

	// emit key events for controller buttons
	for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
	{
		qboolean newstate = SDL_GameControllerGetButton(joy_active_controller, (SDL_GameControllerButton)i);
		qboolean oldstate = joy_buttonstate.buttondown[i];
		int key;

		joy_buttonstate.buttondown[i] = newstate;
		if (newstate && !oldstate)
			joy_buttonkey[i] = IN_KeyForControllerButton((SDL_GameControllerButton)i);
		key = joy_buttonkey[i] ? joy_buttonkey[i] : IN_KeyForControllerButton((SDL_GameControllerButton)i);

		// weapon wheel: B cancels an open wheel without firing.  Only intercept the
		// down-edge while already open, so a B-bound +weaponwheel still works.
		if (i == SDL_CONTROLLER_BUTTON_B)
		{
			if (Wheel_IsOpen () && newstate && !oldstate)
			{
				Wheel_Cancel ();
				Wheel_BlockBButtonRelease ();
				continue;
			}
			if (Wheel_BlockBButton ())
			{
				if (!newstate)
					Wheel_ClearBBlock ();
				continue;
			}
		}

		// NOTE: This can cause a reentrant call of IN_Commands, via SCR_ModalMessage when confirming a new game.
		IN_JoyKeyEvent(oldstate, newstate, key, &joy_buttontimer[i]);
		if (oldstate && !newstate)
			joy_buttonkey[i] = 0;
	}

	// Button key events can re-enter IN_Commands.  Take the axis snapshot
	// after that loop so trigger/menu transitions already handled by a nested
	// pass are not synthesized again by this pass.
	oldaxisstate = joy_axisstate;
	
	for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
	{
		newaxisstate.axisvalue[i] = SDL_GameControllerGetAxis(joy_active_controller, (SDL_GameControllerAxis)i) / 32768.0f;
	}

	joy_axisstate = newaxisstate;
	IN_UpdateCSQCAxisEvents(&newaxisstate);
	
	// emit emulated arrow keys so the analog sticks can be used in the menu
	if (key_dest != key_game)
	{
		old_move = IN_GetMoveAxis(&oldaxisstate);
		new_move = IN_GetMoveAxis(&newaxisstate);
		IN_JoyKeyEvent(old_move.x < -stickthreshold, new_move.x < -stickthreshold, K_LEFTARROW, &joy_emulatedkeytimer[0]);
		IN_JoyKeyEvent(old_move.x >  stickthreshold, new_move.x >  stickthreshold, K_RIGHTARROW, &joy_emulatedkeytimer[1]);
		IN_JoyKeyEvent(old_move.y < -stickthreshold, new_move.y < -stickthreshold, K_UPARROW, &joy_emulatedkeytimer[2]);
		IN_JoyKeyEvent(old_move.y >  stickthreshold, new_move.y >  stickthreshold, K_DOWNARROW, &joy_emulatedkeytimer[3]);
	}
	else if (lastactivetype != KD_GAMEPAD)
	{
		if (IN_AxisMagnitude(IN_GetLookAxis(&newaxisstate)) > joy_deadzone_look.value ||
			IN_AxisMagnitude(IN_GetMoveAxis(&newaxisstate)) > joy_deadzone_move.value)
			lastactivetype = KD_GAMEPAD;
	}

	if (key_dest == key_console)
	{
		const float scrollthreshold = 0.1f;
		const float maxscrollspeed = 72.f;
		const float scrollinterval = 1.f / maxscrollspeed;
		static double timer = 0.0;
		float scale;

		raw = IN_GetLookAxis(&newaxisstate);
		deadzone = IN_ApplyDeadzone(raw, joy_deadzone_look.value, joy_outer_threshold_look.value);
		eased = IN_ApplyEasing(deadzone, joy_exponent.value);
		if (joy_invert.value)
			eased.y = -eased.y;

		scale = fabs(eased.y);
		if (scale > scrollthreshold)
		{
			scale = (scale - scrollthreshold) / (1.f - scrollthreshold);
			timer -= scale * host_frametime;
			if (timer < 0.0)
			{
				int ticks = (int)ceil(-timer / scrollinterval);
				timer += ticks * scrollinterval;
				Con_Scroll(eased.y < 0.0f ? ticks : -ticks);
			}
		}
		else
		{
			timer = 0.0;
		}
	}
	
	// Emit emulated keys for the analog triggers unless CSQC consumed the
	// corresponding axis event.  Release an already-held key if consumption
	// changes while the trigger is down.
	IN_JoyTriggerKeyEvent(joy_axis_consumed[SDL_CONTROLLER_AXIS_TRIGGERLEFT],
		oldaxisstate.axisvalue[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > triggerthreshold,
		newaxisstate.axisvalue[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > triggerthreshold,
		K_LTRIGGER, &joy_emulatedkeytimer[4]);
	IN_JoyTriggerKeyEvent(joy_axis_consumed[SDL_CONTROLLER_AXIS_TRIGGERRIGHT],
		oldaxisstate.axisvalue[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > triggerthreshold,
		newaxisstate.axisvalue[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > triggerthreshold,
		K_RTRIGGER, &joy_emulatedkeytimer[5]);

#if SDL_VERSION_ATLEAST(2, 0, 9)
	if ((joy_has_rumble || joy_has_trigger_rumble) && !IN_IsCalibratingGyro() &&
		Sys_DoubleTime() >= joy_rumble_test_end && IN_JoyActive())
	{
		float lofreq = GetClampedFraction(S_GetLoFreqLevel(), 0.067f, 0.45f);
		float hifreq = GetClampedFraction(S_GetHiFreqLevel(), 0.061f, 0.45f);

		hifreq *= hifreq;
		if (joy_has_rumble && joy_rumble.value > 0.f)
		{
			float strength = CLAMP(0.f, joy_rumble.value, 1.f) * 0xffff;
			SDL_GameControllerRumble(joy_active_controller, lofreq * strength, hifreq * strength, 100);
		}
#if SDL_VERSION_ATLEAST(2, 0, 18)
		if (joy_has_trigger_rumble && joy_rumble_triggers.value > 0.f)
		{
			float strength = CLAMP(0.f, joy_rumble_triggers.value, 1.f) * 0xffff;
			float level = q_max(lofreq, hifreq);
			SDL_GameControllerRumbleTriggers(joy_active_controller,
				level * strength, level * strength, 100);
		}
#endif
	}
#endif
#endif
}

/*
================
IN_FlickStickEasing
================
*/
static float IN_FlickStickEasing(float frac)
{
	frac = 1.f - frac;
	frac = 1.f - frac * frac;
	return frac;
}

/*
================
IN_JoyMove
================
*/
void IN_JoyMove (usercmd_t *cmd)
{
#if defined(USE_SDL2)
	float speed;
	const float csqcsens = cl.csqc_sensitivity;
	joyaxis_t moveRaw, moveDeadzone, moveEased;
	joyaxis_t lookRaw, lookDeadzone, lookEased;
	extern	cvar_t	sv_maxspeed;

	if (!joy_enable.value)
		return;
	
	if (!joy_active_controller)
		return;

	if (cl.paused || key_dest != key_game)
		return;

	moveRaw = IN_GetMoveAxis(&joy_axisstate);
	lookRaw = IN_GetLookAxis(&joy_axisstate);
	moveDeadzone = IN_ApplyDeadzone(moveRaw, joy_deadzone_move.value, joy_outer_threshold_move.value);
	lookDeadzone = IN_ApplyDeadzone(lookRaw, joy_deadzone_look.value, joy_outer_threshold_look.value);

	moveEased = IN_ApplyEasing(moveDeadzone, joy_exponent_move.value);
	lookEased = IN_ApplyEasing(lookDeadzone, joy_exponent.value);

	// Apply consumption after the radial deadzone/easing calculations.  Masking
	// the raw vector first would change its magnitude and unintentionally reduce
	// the response of the unconsumed sibling axis.  The raw look vector is also
	// masked for the weapon wheel and flick-stick paths below.
	if (joy_axis_consumed[joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX])
		moveEased.x = 0.0f;
	if (joy_axis_consumed[joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY])
		moveEased.y = 0.0f;
	if (joy_axis_consumed[joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_RIGHTX])
	{
		lookEased.x = 0.0f;
		lookRaw.x = 0.0f;
	}
	if (joy_axis_consumed[joy_swapmovelook.value ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_RIGHTY])
	{
		lookEased.y = 0.0f;
		lookRaw.y = 0.0f;
	}

	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0 || cl_forwardspeed.value >= sv_maxspeed.value))
		// running
		speed = sv_maxspeed.value;
	else if (cl_forwardspeed.value >= sv_maxspeed.value)
		// not running, with always run = vanilla
		speed = q_min(sv_maxspeed.value, cl_forwardspeed.value / cl_movespeedkey.value);
	else
		// not running, with always run = off or quakespasm
		speed = cl_forwardspeed.value;

	cmd->sidemove += speed * moveEased.x;
	cmd->forwardmove -= speed * moveEased.y;

	// weapon wheel: consume the look stick for sector selection but keep
	// left-stick movement working so the player can strafe while picking.
	if (Wheel_IsOpen ())
	{
		Wheel_UpdateSelection (lookRaw.x, -lookRaw.y);
		IN_ResetFlickState ();
		return;
	}

	if (joy_flick.value && gyro_present && gyro_enable.value)
	{
		float angle, scale, lerp_frac, delta;
		qboolean isactive, wasactive;

		angle = NormalizeAngle(RAD2DEG(atan2(lookRaw.y, lookRaw.x)) + 90.f);
		scale = IN_AxisMagnitude(lookRaw);

		isactive = scale > joy_flick_deadzone.value;
		wasactive = flick.prev_scale > joy_flick_deadzone.value;
			if (isactive != wasactive)
			{
				if (!wasactive)
				{
					flick.prev_lerp_frac = 0.f;
					flick.yaw = angle;
					flick.pitch = cl.viewangles[PITCH];
					flick.prev_angle = angle;
					flick.yaw_delta = 0.f;
				}
			}
		else if (isactive)
		{
			delta = AngleDifference(angle, flick.prev_angle);
			if (joy_flick_noise_thresh.value > 0.f)
			{
				float filter_scale = fabs(delta) / joy_flick_noise_thresh.value;

				if (filter_scale < 1.f)
				{
					filter_scale = LERP(0.05f, 1.f, filter_scale * filter_scale);
					delta *= filter_scale;
					angle = NormalizeAngle(flick.prev_angle + delta);
				}
			}
			flick.yaw_delta += delta;
		}

		if (joy_flick_adjust_speed.value > 0.f)
			delta = flick.yaw_delta * q_min(1.0, host_frametime * joy_flick_adjust_speed.value);
		else
			delta = flick.yaw_delta;
		if (fabs(delta) > 0.01f)
		{
			cl.viewangles[YAW] -= delta * csqcsens;
			flick.yaw_delta -= delta;
		}

		if (joy_flick_time.value > 0.f)
		{
			lerp_frac = flick.prev_lerp_frac + host_frametime / joy_flick_time.value;
			lerp_frac = CLAMP(0.f, lerp_frac, 1.f);
		}
		else
		{
			lerp_frac = 1.f;
		}

		delta = IN_FlickStickEasing(lerp_frac) - IN_FlickStickEasing(flick.prev_lerp_frac);
		cl.viewangles[YAW] -= flick.yaw * delta * csqcsens;
		cl.viewangles[PITCH] -= flick.pitch * delta * CLAMP(0.f, joy_flick_recenter.value, 1.f) * csqcsens;

		flick.prev_scale = scale;
		flick.prev_angle = angle;
		flick.prev_lerp_frac = lerp_frac;
	}
	else
	{
		IN_ResetFlickState();

		cl.viewangles[YAW] -= lookEased.x * joy_sensitivity_yaw.value * host_frametime * csqcsens;
		cl.viewangles[PITCH] += lookEased.y * joy_sensitivity_pitch.value * (joy_invert.value ? -1.0 : 1.0) * host_frametime * csqcsens;

		if (lookEased.x != 0 || lookEased.y != 0)
			V_StopPitchDrift();
	}

	if (cl.fullpitch == 0) // woods #pqfullpitch -- force client to adapt when not allowed
	{
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		/* johnfitz -- variable pitch clamping */
		if (cl.viewangles[PITCH] > cl_maxpitch.value)
			cl.viewangles[PITCH] = cl_maxpitch.value;
		if (cl.viewangles[PITCH] < cl_minpitch.value)
			cl.viewangles[PITCH] = cl_minpitch.value;
	}
#endif
}

void IN_GyroMove(usercmd_t *cmd)
{
#if defined(USE_SDL2)
	float scale, duration, lerp_frac;

	(void)cmd;

	if (!joy_enable.value)
		return;
	if (!gyro_enable.value)
		return;
	if (!IN_JoyActive())
		return;
	if (cl.paused || key_dest != key_game)
		return;
	if (Wheel_IsOpen ())
		return;
	scale = (180.f / M_PI) * host_frametime * IN_FovScale() * cl.csqc_sensitivity;
	switch ((int)gyro_mode.value)
	{
	case GYRO_BUTTON_DISABLES:
		if (gyro_button_pressed)
			return;
		break;
	case GYRO_BUTTON_ENABLES:
		if (!gyro_button_pressed)
			return;
		break;
	case GYRO_BUTTON_INVERTS_DIR:
		if (gyro_button_pressed)
			scale = -scale;
		break;
	default:
		break;
	}

	cl.viewangles[YAW] += scale * gyro_yaw * gyro_yawsensitivity.value;
	cl.viewangles[PITCH] -= scale * gyro_pitch * gyro_pitchsensitivity.value;

	// Default pitch drift constantly pulls toward idealpitch. With gyro active,
	// keep that from fighting the player's aim and re-apply the centering delta
	// additively when pitch drift was started this frame.
	V_StopPitchDrift();

	if (cl.lastcenterstart == cl.time)
	{
		gyro_center_frac = 0.f;
		gyro_center_amount = cl.statsf[STAT_IDEALPITCH] - cl.viewangles[PITCH];
	}

	if (gyro_center_amount != 0.f && v_centerspeed.value > 0.f)
	{
		duration = fabs(gyro_center_amount / v_centerspeed.value);
		lerp_frac = gyro_center_frac + host_frametime / duration;
		lerp_frac = CLAMP(0.f, lerp_frac, 1.f);
	}
	else
	{
		lerp_frac = 1.f;
	}
	scale = IN_RecenterEasing(lerp_frac) - IN_RecenterEasing(gyro_center_frac);
	gyro_center_frac = lerp_frac;
	cl.viewangles[PITCH] += gyro_center_amount * scale;

	if (cl.fullpitch == 0)
	{
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		if (cl.viewangles[PITCH] > cl_maxpitch.value)
			cl.viewangles[PITCH] = cl_maxpitch.value;
		if (cl.viewangles[PITCH] < cl_minpitch.value)
			cl.viewangles[PITCH] = cl_minpitch.value;
	}
#else
	(void)cmd;
#endif
}

void IN_MouseMove(usercmd_t *cmd)
{
	float	dmx, dmy, raw_dx, raw_dy;
	float		sens; // woods #zoom (ironwail)
	qboolean pong_active = Pong_Enabled() && !cls.demoplayback &&
		(cl.paused || cl.match_pause_time); // woods #pong

#ifdef __APPLE__
	// Add HID raw mouse movement if available
	if (hid_mouse_active) {
		int hid_dx, hid_dy;
		HID_MouseGetMovement(&hid_dx, &hid_dy);
		total_dx += hid_dx;
		total_dy += hid_dy;
	}
#endif

	if (cls.state != ca_connected || cls.signon != SIGNONS)
	{
		total_dx = 0;
		total_dy = 0;
		return;
	}

	sens = tan(DEG2RAD(r_refdef.basefov) * 0.5f) / tan(DEG2RAD(scr_fov.value) * 0.5f); // woods #zoom (ironwail)
	sens *= sensitivity.value; // woods #zoom (ironwail)

	raw_dx = (float)total_dx;
	raw_dy = (float)total_dy;
	dmx = raw_dx * sens; // woods #zoom (ironwail)
	dmy = raw_dy * sens; // woods #zoom (ironwail)

	total_dx = 0;
	total_dy = 0;

	if (pong_active) // woods #pong
	{
		int wx, wy;
		SDL_GetMouseState(&wx, &wy); // Need to get current absolute position
		Pong_MouseMove(wx, wy);
	}

	// do pause/pong check after resetting total_d* so mouse movements don't accumulate
	// Return if Pong is active OR the game is paused OR input isn't for the game
	// Also return if demo is playing or match is paused
	if (pong_active || cl.paused || key_dest != key_game || cls.demoplayback || cl.match_pause_time > 0) // woods #pong
		return;

	// weapon wheel eats mouse motion before it can rotate the view.
	if (Wheel_IsOpen ())
	{
		Wheel_UpdateMouse (raw_dx, raw_dy);
		return;
	}

	if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
		cl.accummoves[1] += m_side.value * dmx;
	else
		cl.viewangles[YAW] -= m_yaw.value * dmx * cl.csqc_sensitivity;

	if (in_mlook.state & 1)
	{
		if (dmx || dmy)
			V_StopPitchDrift ();
	}

	if ( (in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * dmy * cl.csqc_sensitivity;
		if (cl.fullpitch == 0) // woods #pqfullpitch -- force client to adapt when not allowed
		{
			if (cl.viewangles[PITCH] > 80)
				cl.viewangles[PITCH] = 80;
			if (cl.viewangles[PITCH] < -70)
				cl.viewangles[PITCH] = -70;
		}
		else
		{
			/* johnfitz -- variable pitch clamping */
			if (cl.viewangles[PITCH] > cl_maxpitch.value)
				cl.viewangles[PITCH] = cl_maxpitch.value;
			if (cl.viewangles[PITCH] < cl_minpitch.value)
				cl.viewangles[PITCH] = cl_minpitch.value;
		}
	}
	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			cl.accummoves[2] -= m_forward.value * dmy;
		else
			cl.accummoves[0] -= m_forward.value * dmy;
	}
}

void IN_Move(usercmd_t *cmd)
{
	IN_JoyMove(cmd);
	IN_GyroMove(cmd);
	IN_MouseMove(cmd);
}

void IN_ClearStates (void)
{
}

void IN_UpdateInputMode (void)
{
	qboolean want_textmode = Key_TextEntry();
	// Stop text input outside text entry so the console key cannot leave a
	// pending dead-key accent (e.g. ^ on German keyboards) while playing.
	if (textmode != want_textmode)
	{
		textmode = want_textmode;
#if !defined(USE_SDL2)
		SDL_EnableUNICODE(textmode);
		if (in_debugkeys.value)
			Con_Printf("SDL_EnableUNICODE %d time: %g\n", textmode, Sys_DoubleTime());
#else
		if (textmode)
		{
			SDL_StartTextInput();
			if (in_debugkeys.value)
				Con_Printf("SDL_StartTextInput time: %g\n", Sys_DoubleTime());
		}
		else
		{
			SDL_StopTextInput();
			if (in_debugkeys.value)
				Con_Printf("SDL_StopTextInput time: %g\n", Sys_DoubleTime());
		}
#endif
	}
}

#if !defined(USE_SDL2)
static inline int IN_SDL_KeysymToQuakeKey(SDLKey sym)
{
	if (sym > SDLK_SPACE && sym < SDLK_DELETE)
		return sym;

	switch (sym)
	{
	case SDLK_TAB: return K_TAB;
	case SDLK_RETURN: return K_ENTER;
	case SDLK_ESCAPE: return K_ESCAPE;
	case SDLK_SPACE: return K_SPACE;

	case SDLK_BACKSPACE: return K_BACKSPACE;
	case SDLK_CAPSLOCK: return K_CAPSLOCK; // woods #capslock
	case SDLK_PRINTSCREEN: return K_PRINTSCREEN; // woods #printscreen
	case SDLK_UP: return K_UPARROW;
	case SDLK_DOWN: return K_DOWNARROW;
	case SDLK_LEFT: return K_LEFTARROW;
	case SDLK_RIGHT: return K_RIGHTARROW;

	case SDLK_LALT: return K_ALT;
	case SDLK_RALT: return K_ALT;
	case SDLK_LCTRL: return K_CTRL;
	case SDLK_RCTRL: return K_CTRL;
	case SDLK_LSHIFT: return K_SHIFT;
	case SDLK_RSHIFT: return K_SHIFT;

	case SDLK_F1: return K_F1;
	case SDLK_F2: return K_F2;
	case SDLK_F3: return K_F3;
	case SDLK_F4: return K_F4;
	case SDLK_F5: return K_F5;
	case SDLK_F6: return K_F6;
	case SDLK_F7: return K_F7;
	case SDLK_F8: return K_F8;
	case SDLK_F9: return K_F9;
	case SDLK_F10: return K_F10;
	case SDLK_F11: return K_F11;
	case SDLK_F12: return K_F12;
	case SDLK_INSERT: return K_INS;
	case SDLK_DELETE: return K_DEL;
	case SDLK_PAGEDOWN: return K_PGDN;
	case SDLK_PAGEUP: return K_PGUP;
	case SDLK_HOME: return K_HOME;
	case SDLK_END: return K_END;

	case SDLK_NUMLOCK: return K_KP_NUMLOCK;
	case SDLK_KP_DIVIDE: return K_KP_SLASH;
	case SDLK_KP_MULTIPLY: return K_KP_STAR;
	case SDLK_KP_MINUS:return K_KP_MINUS;
	case SDLK_KP7: return K_KP_HOME;
	case SDLK_KP8: return K_KP_UPARROW;
	case SDLK_KP9: return K_KP_PGUP;
	case SDLK_KP_PLUS: return K_KP_PLUS;
	case SDLK_KP4: return K_KP_LEFTARROW;
	case SDLK_KP5: return K_KP_5;
	case SDLK_KP6: return K_KP_RIGHTARROW;
	case SDLK_KP1: return K_KP_END;
	case SDLK_KP2: return K_KP_DOWNARROW;
	case SDLK_KP3: return K_KP_PGDN;
	case SDLK_KP_ENTER: return K_KP_ENTER;
	case SDLK_KP0: return K_KP_INS;
	case SDLK_KP_PERIOD: return K_KP_DEL;

	case SDLK_LMETA: return K_COMMAND;
	case SDLK_RMETA: return K_COMMAND;

	case SDLK_BREAK: return K_PAUSE;
	case SDLK_PAUSE: return K_PAUSE;

	case SDLK_WORLD_18: return '~'; // the alternate tilde key

	default: return 0;
	}
}
#endif

#if defined(USE_SDL2)
static inline int IN_SDL2_ScancodeToQuakeKey(SDL_Scancode scancode)
{
	switch (scancode)
	{
	case SDL_SCANCODE_TAB: return K_TAB;
	case SDL_SCANCODE_RETURN: return K_ENTER;
	case SDL_SCANCODE_RETURN2: return K_ENTER;
	case SDL_SCANCODE_ESCAPE: return K_ESCAPE;
	case SDL_SCANCODE_SPACE: return K_SPACE;

	case SDL_SCANCODE_A: return 'a';
	case SDL_SCANCODE_B: return 'b';
	case SDL_SCANCODE_C: return 'c';
	case SDL_SCANCODE_D: return 'd';
	case SDL_SCANCODE_E: return 'e';
	case SDL_SCANCODE_F: return 'f';
	case SDL_SCANCODE_G: return 'g';
	case SDL_SCANCODE_H: return 'h';
	case SDL_SCANCODE_I: return 'i';
	case SDL_SCANCODE_J: return 'j';
	case SDL_SCANCODE_K: return 'k';
	case SDL_SCANCODE_L: return 'l';
	case SDL_SCANCODE_M: return 'm';
	case SDL_SCANCODE_N: return 'n';
	case SDL_SCANCODE_O: return 'o';
	case SDL_SCANCODE_P: return 'p';
	case SDL_SCANCODE_Q: return 'q';
	case SDL_SCANCODE_R: return 'r';
	case SDL_SCANCODE_S: return 's';
	case SDL_SCANCODE_T: return 't';
	case SDL_SCANCODE_U: return 'u';
	case SDL_SCANCODE_V: return 'v';
	case SDL_SCANCODE_W: return 'w';
	case SDL_SCANCODE_X: return 'x';
	case SDL_SCANCODE_Y: return 'y';
	case SDL_SCANCODE_Z: return 'z';

	case SDL_SCANCODE_1: return '1';
	case SDL_SCANCODE_2: return '2';
	case SDL_SCANCODE_3: return '3';
	case SDL_SCANCODE_4: return '4';
	case SDL_SCANCODE_5: return '5';
	case SDL_SCANCODE_6: return '6';
	case SDL_SCANCODE_7: return '7';
	case SDL_SCANCODE_8: return '8';
	case SDL_SCANCODE_9: return '9';
	case SDL_SCANCODE_0: return '0';

	case SDL_SCANCODE_MINUS: return '-';
	case SDL_SCANCODE_EQUALS: return '=';
	case SDL_SCANCODE_LEFTBRACKET: return '[';
	case SDL_SCANCODE_RIGHTBRACKET: return ']';
	case SDL_SCANCODE_BACKSLASH: return '\\';
	case SDL_SCANCODE_NONUSHASH: return '#';
	case SDL_SCANCODE_SEMICOLON: return ';';
	case SDL_SCANCODE_APOSTROPHE: return '\'';
	case SDL_SCANCODE_GRAVE: return '`';
	case SDL_SCANCODE_COMMA: return ',';
	case SDL_SCANCODE_PERIOD: return '.';
	case SDL_SCANCODE_SLASH: return '/';
	case SDL_SCANCODE_NONUSBACKSLASH: return '\\';

	case SDL_SCANCODE_BACKSPACE: return K_BACKSPACE;
	case SDL_SCANCODE_CAPSLOCK: return K_CAPSLOCK; // woods #capslock
	case SDL_SCANCODE_PRINTSCREEN: return K_PRINTSCREEN; // woods #printscreen
	case SDL_SCANCODE_UP: return K_UPARROW;
	case SDL_SCANCODE_DOWN: return K_DOWNARROW;
	case SDL_SCANCODE_LEFT: return K_LEFTARROW;
	case SDL_SCANCODE_RIGHT: return K_RIGHTARROW;

	case SDL_SCANCODE_LALT: return K_ALT;
	case SDL_SCANCODE_RALT: return K_ALT;
	case SDL_SCANCODE_LCTRL: return K_CTRL;
	case SDL_SCANCODE_RCTRL: return K_CTRL;
	case SDL_SCANCODE_LSHIFT: return K_SHIFT;
	case SDL_SCANCODE_RSHIFT: return K_SHIFT;

	case SDL_SCANCODE_F1: return K_F1;
	case SDL_SCANCODE_F2: return K_F2;
	case SDL_SCANCODE_F3: return K_F3;
	case SDL_SCANCODE_F4: return K_F4;
	case SDL_SCANCODE_F5: return K_F5;
	case SDL_SCANCODE_F6: return K_F6;
	case SDL_SCANCODE_F7: return K_F7;
	case SDL_SCANCODE_F8: return K_F8;
	case SDL_SCANCODE_F9: return K_F9;
	case SDL_SCANCODE_F10: return K_F10;
	case SDL_SCANCODE_F11: return K_F11;
	case SDL_SCANCODE_F12: return K_F12;
	case SDL_SCANCODE_INSERT: return K_INS;
	case SDL_SCANCODE_DELETE: return K_DEL;
	case SDL_SCANCODE_PAGEDOWN: return K_PGDN;
	case SDL_SCANCODE_PAGEUP: return K_PGUP;
	case SDL_SCANCODE_HOME: return K_HOME;
	case SDL_SCANCODE_END: return K_END;

	case SDL_SCANCODE_NUMLOCKCLEAR: return K_KP_NUMLOCK;
	case SDL_SCANCODE_KP_DIVIDE: return K_KP_SLASH;
	case SDL_SCANCODE_KP_MULTIPLY: return K_KP_STAR;
	case SDL_SCANCODE_KP_MINUS: return K_KP_MINUS;
	case SDL_SCANCODE_KP_7: return K_KP_HOME;
	case SDL_SCANCODE_KP_8: return K_KP_UPARROW;
	case SDL_SCANCODE_KP_9: return K_KP_PGUP;
	case SDL_SCANCODE_KP_PLUS: return K_KP_PLUS;
	case SDL_SCANCODE_KP_4: return K_KP_LEFTARROW;
	case SDL_SCANCODE_KP_5: return K_KP_5;
	case SDL_SCANCODE_KP_6: return K_KP_RIGHTARROW;
	case SDL_SCANCODE_KP_1: return K_KP_END;
	case SDL_SCANCODE_KP_2: return K_KP_DOWNARROW;
	case SDL_SCANCODE_KP_3: return K_KP_PGDN;
	case SDL_SCANCODE_KP_ENTER: return K_KP_ENTER;
	case SDL_SCANCODE_KP_0: return K_KP_INS;
	case SDL_SCANCODE_KP_PERIOD: return K_KP_DEL;

	case SDL_SCANCODE_LGUI: return K_COMMAND;
	case SDL_SCANCODE_RGUI: return K_COMMAND;

	case SDL_SCANCODE_PAUSE: return K_PAUSE;

	default: return 0;
	}
}
#endif

#if defined(USE_SDL2)
static void IN_DebugTextEvent(SDL_Event *event)
{
	Con_Printf ("SDL_TEXTINPUT '%s' time: %g\n", event->text.text, Sys_DoubleTime());
}
#endif

static void IN_DebugKeyEvent(SDL_Event *event)
{
	const char *eventtype = (event->key.state == SDL_PRESSED) ? "SDL_KEYDOWN" : "SDL_KEYUP";
#if defined(USE_SDL2)
	Con_Printf ("%s scancode: '%s' keycode: '%s' time: %g\n",
		eventtype,
		SDL_GetScancodeName(event->key.keysym.scancode),
		SDL_GetKeyName(event->key.keysym.sym),
		Sys_DoubleTime());
#else
	Con_Printf ("%s sym: '%s' unicode: %04x time: %g\n",
		eventtype,
		SDL_GetKeyName(event->key.keysym.sym),
		(int)event->key.keysym.unicode,
		Sys_DoubleTime());
#endif
}

// woods #eyemouse

#define LONG_PRESS_TIME 500 // 500ms = 0.5 seconds
#define COOL_DOWN_TIME 300 // 300ms = 0.3 seconds
static qboolean is_long_pressing = false;
static Uint32 press_start_time = 0;
static qboolean long_press_triggered = false; // Add this to prevent multiple triggers

static void IN_HandleObserverMouseEvents (SDL_Event* event) // woods #eyemouse
{
	if (event->button.button == 1)  // Left click
	{
		if (event->button.state == SDL_PRESSED)
		{
			Uint32 current_time = SDL_GetTicks();

			// Handle observer frags click
			IN_ObsFragsClick(event->button.x, event->button.y);

			// Update the cursor idle time on mouse click
			obs_cursor_last_move = current_time;
			if (obs_cursor_hidden) {
				SDL_ShowCursor(SDL_ENABLE);
				obs_cursor_hidden = false;
				IN_UpdateGrabs(); // Refresh grabs to ensure cursor is visible
			}

			press_start_time = current_time;
			is_long_pressing = true;
			long_press_triggered = false;
		}
		else if (event->button.state == SDL_RELEASED)
		{
			if (is_long_pressing && long_press_triggered)
			{
				Cbuf_AddText("-showscores\n");
			}
			is_long_pressing = false;
			long_press_triggered = false;
		}
	}
	else if (event->button.button == 3)  // Right click
	{
		static Uint32 last_flyme_time = 0;
		Uint32 current_time = SDL_GetTicks();

		if (event->button.state == SDL_PRESSED)
		{
			// Update the cursor idle time on mouse click
			obs_cursor_last_move = current_time;
			if (obs_cursor_hidden) {
				SDL_ShowCursor(SDL_ENABLE);
				obs_cursor_hidden = false;
				IN_UpdateGrabs(); // Refresh grabs to ensure cursor is visible
			}

			// Add cooldown to prevent accidental double-clicks (300ms)
			if (current_time - last_flyme_time > COOL_DOWN_TIME)
			{
				// Execute flyme command on right-click
				if (Cmd_AliasExists("flyme"))
					Cbuf_AddText("flyme\n");
				else
					Cbuf_AddText("impulse 142\n");
				Cbuf_AddText("wait;wait;setinfo observing off\n");

				last_flyme_time = current_time;
			}
		}
	}
}

#if defined(USE_SDL2)
static void IN_NormalizeDroppedPath(char *path)
{
	char	*c;

	if (!path)
		return;

	for (c = path; *c; ++c)
	{
		if (*c == '\\')
			*c = '/';
	}
}

static qboolean IN_BuildRelativeDroppedPath(const char *absolute_path, char *relative_path, size_t relative_path_size, qboolean *out_is_in_gamedir)
{
	searchpath_t	*search;
	char	normalized_absolute[MAX_OSPATH];
	const char	*absolute_for_compare;
	char	normalized_gamedir[MAX_OSPATH];
	const char	*gamedir_for_compare;
#ifdef _WIN32
	char	compare_absolute[MAX_OSPATH];
	char	compare_gamedir[MAX_OSPATH];
#endif

	if (!absolute_path || !*absolute_path || !relative_path || relative_path_size == 0)
		return false;

	relative_path[0] = '\0';

	if (out_is_in_gamedir)
		*out_is_in_gamedir = false;

	q_strlcpy(normalized_absolute, absolute_path, sizeof(normalized_absolute));
	IN_NormalizeDroppedPath(normalized_absolute);
#ifdef _WIN32
	q_strlcpy(compare_absolute, normalized_absolute, sizeof(compare_absolute));
	q_strlwr(compare_absolute);
	absolute_for_compare = compare_absolute;
#else
	absolute_for_compare = normalized_absolute;
#endif

	q_strlcpy(normalized_gamedir, com_gamedir, sizeof(normalized_gamedir));
	IN_NormalizeDroppedPath(normalized_gamedir);
#ifdef _WIN32
	q_strlcpy(compare_gamedir, normalized_gamedir, sizeof(compare_gamedir));
	q_strlwr(compare_gamedir);
	gamedir_for_compare = compare_gamedir;
#else
	gamedir_for_compare = normalized_gamedir;
#endif

	for (search = com_searchpaths; search; search = search->next)
	{
		char	normalized_search[MAX_OSPATH];
		const char	*search_for_compare;
		size_t	prefix_len;
		char	next_char;
#ifdef _WIN32
		char	compare_search[MAX_OSPATH];
#endif

		if (search->pack)
			continue;

		if (!search->filename[0])
			continue;

		q_strlcpy(normalized_search, search->filename, sizeof(normalized_search));
		IN_NormalizeDroppedPath(normalized_search);
#ifdef _WIN32
		q_strlcpy(compare_search, normalized_search, sizeof(compare_search));
		q_strlwr(compare_search);
		search_for_compare = compare_search;
#else
		search_for_compare = normalized_search;
#endif
		prefix_len = strlen(search_for_compare);
		if (prefix_len == 0)
			continue;
		if (strncmp(absolute_for_compare, search_for_compare, prefix_len) != 0)
			continue;

		next_char = absolute_for_compare[prefix_len];
		if (next_char != '\0' && next_char != '/')
			continue;

		if (out_is_in_gamedir)
		{
#ifdef _WIN32
			if (!q_strcasecmp(search_for_compare, gamedir_for_compare))
#else
			if (!strcmp(search_for_compare, gamedir_for_compare))
#endif
			{
				*out_is_in_gamedir = true;
			}
		}

		const char *rest = normalized_absolute + prefix_len;
		if (*rest == '/')
			rest++;
		if (*rest == '\0')
			continue;

		q_strlcpy(relative_path, rest, relative_path_size);
		return true;
	}

	return false;
}

static qboolean IN_CopyExternalFile(const char *source, const char *destination)
{
	FILE	*src;
	FILE	*dst;
	qboolean success = true;
	char	 buffer[4096];
	size_t	count;
	char	tmp_path[MAX_OSPATH];
	int	result;

	result = q_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", destination);
	if (result < 0 || (size_t)result >= sizeof(tmp_path))
	{
		Con_Printf("Path too long for file temp copy.\n");
		return false;
	}

	src = fopen(source, "rb");
	if (!src)
	{
		Con_Printf("Failed to open file \"%s\": %s\n", source, strerror(errno));
		return false;
	}

	dst = fopen(tmp_path, "wb");
	if (!dst)
	{
		Con_Printf("Failed to create \"%s\": %s\n", tmp_path, strerror(errno));
		fclose(src);
		return false;
	}

	while ((count = fread(buffer, 1, sizeof(buffer), src)) > 0)
	{
		if (fwrite(buffer, 1, count, dst) != count)
		{
			Con_Printf("Failed to write file to \"%s\": %s\n", tmp_path, strerror(errno));
			success = false;
			break;
		}
	}

	if (success && ferror(src))
	{
		Con_Printf("Failed to read file \"%s\": %s\n", source, strerror(errno));
		success = false;
	}

	fclose(src);

	if (fclose(dst) != 0)
	{
		Con_Printf("Failed to close \"%s\": %s\n", tmp_path, strerror(errno));
		success = false;
	}

	if (!success)
	{
		remove(tmp_path);
		return false;
	}

#ifdef _WIN32
	if (!MoveFileExA(tmp_path, destination, MOVEFILE_REPLACE_EXISTING))
	{
		Con_Printf("Failed to move temporary file into place: %s\n", destination);
		remove(tmp_path);
		return false;
	}
#else
	if (rename(tmp_path, destination) != 0)
	{
		Con_Printf("Failed to move \"%s\" to \"%s\": %s\n", tmp_path, destination, strerror(errno));
		remove(tmp_path);
		return false;
	}
#endif

	return true;
}

static qboolean IN_IsSafeQuotedCommandArg(const char *text)
{
	const char *c;

	if (!text || !*text)
		return false;
	for (c = text; *c; ++c)
	{
		if (*c == '\"' || *c == ';' || *c == '\r' || *c == '\n')
			return false;
	}
	return true;
}

static qboolean IN_IsActiveMatchParticipant(void)
{
	return cls.state == ca_connected && !cls.demoplayback &&
		cl.matchinp == 1 && cl.notobserver == 1 && cl.teamcolor[0] != '\0';
}

static void IN_PlayCopySound(void)
{
	S_NotificationSound_Copy();
}

static qboolean IN_InstallExternalFile(const char *source_path, const char *source_desc, qboolean suppress_demo_autoplay)
{
	char	normalized_path[MAX_OSPATH];
	char	relative_path[MAX_OSPATH];
	char	dest_path[MAX_OSPATH];
	char	command_path[MAX_OSPATH];
	const char	*extension;
	const char	*subdir;
	const char	*dest_ext;
	qboolean	is_demo;
	qboolean	is_map;
	qboolean	is_loc;
	qboolean	is_lit;
	qboolean	has_relative;
	qboolean	should_copy;
	qboolean	relative_in_gamedir = false;

	if (!source_path || !*source_path)
		return false;

	if (!source_desc || !*source_desc)
		source_desc = "file";

	if (q_strlcpy(normalized_path, source_path, sizeof(normalized_path)) >= sizeof(normalized_path))
	{
		Con_Printf("Path too long for %s.\n", source_desc);
		return false;
	}
	IN_NormalizeDroppedPath(normalized_path);

	extension = COM_FileGetExtension(normalized_path);
	is_demo = !q_strcasecmp(extension, "dem");
	is_map = !q_strcasecmp(extension, "bsp");
	is_loc = !q_strcasecmp(extension, "loc");
	is_lit = !q_strcasecmp(extension, "lit");

	if (!is_demo && !is_map && !is_loc && !is_lit)
	{
		Con_Printf("Unsupported %s: %s\n", source_desc, COM_SkipPath(normalized_path));
		return false;
	}

	if (is_demo)
	{
		subdir = "demos";
		dest_ext = ".dem";
	}
	else if (is_loc)
	{
		subdir = "locs";
		dest_ext = ".loc";
	}
	else if (is_map)
	{
		subdir = "maps";
		dest_ext = ".bsp";
	}
	else
	{
		subdir = "maps";
		dest_ext = ".lit";
	}

	relative_path[0] = '\0';
	has_relative = IN_BuildRelativeDroppedPath(normalized_path, relative_path, sizeof(relative_path), &relative_in_gamedir);
	should_copy = !has_relative || !relative_in_gamedir;

	if (has_relative && relative_in_gamedir)
	{
		if (is_demo)
		{
			if (strchr(relative_path, '/'))
			{
#ifdef _WIN32
				if (q_strncasecmp(relative_path, "demos/", 6) != 0)
#else
				if (strncmp(relative_path, "demos/", 6) != 0)
#endif
					should_copy = true;
				else
					should_copy = false;
			}
			else
				should_copy = false;
		}
		else if (is_loc)
		{
#ifdef _WIN32
			if (q_strncasecmp(relative_path, "locs/", 5) != 0)
#else
			if (strncmp(relative_path, "locs/", 5) != 0)
#endif
				should_copy = true;
			else
				should_copy = false;
		}
		else /* .bsp and .lit both go in maps/ */
		{
#ifdef _WIN32
			if (q_strncasecmp(relative_path, "maps/", 5) != 0)
#else
			if (strncmp(relative_path, "maps/", 5) != 0)
#endif
				should_copy = true;
			else
				should_copy = false;
		}
	}

	if (should_copy)
	{
		char	dest_filename[MAX_OSPATH];
		qboolean same_path;
		int	result;

		COM_StripExtension(COM_SkipPath(normalized_path), dest_filename, sizeof(dest_filename));
		if ((size_t)q_strlcat(dest_filename, dest_ext, sizeof(dest_filename)) >= sizeof(dest_filename))
		{
			Con_Printf("Filename too long for %s.\n", source_desc);
			return false;
		}

		result = q_snprintf(relative_path, sizeof(relative_path), "%s/%s", subdir, dest_filename);
		if (result < 0 || (size_t)result >= sizeof(relative_path))
		{
			Con_Printf("Path too long for %s.\n", source_desc);
			return false;
		}

		result = q_snprintf(dest_path, sizeof(dest_path), "%s/%s", com_gamedir, relative_path);
		if (result < 0 || (size_t)result >= sizeof(dest_path))
		{
			Con_Printf("Path too long for %s.\n", source_desc);
			return false;
		}

		IN_NormalizeDroppedPath(dest_path);
#ifdef _WIN32
		same_path = (q_strcasecmp(normalized_path, dest_path) == 0);
#else
		same_path = (strcmp(normalized_path, dest_path) == 0);
#endif
		if (!same_path)
		{
			char	path_copy[MAX_OSPATH];
			qboolean dest_exists;

			dest_exists = (Sys_FileType(dest_path) & FS_ENT_FILE) != 0;
			if (dest_exists)
			{
				Con_SafePrintf("Using existing file at ");
				Con_LinkPrintf(dest_path, "%s/%s", COM_SkipPath(com_gamedir), relative_path);
				Con_SafePrintf("\n");
			}
			else
			{
				q_strlcpy(path_copy, dest_path, sizeof(path_copy));
				COM_CreatePath(path_copy);

				if (!IN_CopyExternalFile(normalized_path, dest_path))
					return false;

				Con_Printf("Copied %s to ", source_desc);
				Con_LinkPrintf(dest_path, "%s/%s", COM_SkipPath(com_gamedir), relative_path);
				Con_Printf("\n");
			}

			/* Also copy .lit file if installing a .bsp */
			if (is_map)
			{
				char	lit_source[MAX_OSPATH];
				char	lit_dest[MAX_OSPATH];

				COM_StripExtension(normalized_path, lit_source, sizeof(lit_source));
				if ((size_t)q_strlcat(lit_source, ".lit", sizeof(lit_source)) < sizeof(lit_source))
				{
					if (Sys_FileType(lit_source) & FS_ENT_FILE)
					{
						COM_StripExtension(dest_path, lit_dest, sizeof(lit_dest));
						if ((size_t)q_strlcat(lit_dest, ".lit", sizeof(lit_dest)) < sizeof(lit_dest))
						{
							if (Sys_FileType(lit_dest) & FS_ENT_FILE)
							{
								Con_SafePrintf("Using existing .lit file at ");
								Con_LinkPrintf(lit_dest, "%s/maps/%s", COM_SkipPath(com_gamedir), COM_SkipPath(lit_dest));
								Con_SafePrintf("\n");
							}
							else if (IN_CopyExternalFile(lit_source, lit_dest))
							{
								Con_SafePrintf("Copied .lit file to ");
								Con_LinkPrintf(lit_dest, "%s/maps/%s", COM_SkipPath(com_gamedir), COM_SkipPath(lit_dest));
								Con_SafePrintf("\n");
							}
						}
					}
				}
			}
		}
	}

	if (is_demo || is_map)
	{
		COM_StripExtension(relative_path, command_path, sizeof(command_path));
		if (is_demo)
		{
			if (suppress_demo_autoplay)
				return true;

#ifdef _WIN32
			if (!q_strncasecmp(command_path, "demos/", 6))
#else
			if (!strncmp(command_path, "demos/", 6))
#endif
				memmove(command_path, command_path + 6, strlen(command_path + 6) + 1);

			if (!command_path[0])
				q_strlcpy(command_path, COM_SkipPath(relative_path), sizeof(command_path));

			if (!IN_IsSafeQuotedCommandArg(command_path))
			{
				Con_Printf("Installed %s, not auto-running because the filename contains command characters.\n", source_desc);
				return true;
			}
			Cbuf_AddText(va("stopdemo; playdemo \"%s\"\n", command_path));
		}
		else
		{
#ifdef _WIN32
			if (!q_strncasecmp(command_path, "maps/", 5))
#else
			if (!strncmp(command_path, "maps/", 5))
#endif
				memmove(command_path, command_path + 5, strlen(command_path + 5) + 1);

			if (!command_path[0])
				q_strlcpy(command_path, COM_SkipPath(relative_path), sizeof(command_path));

			if (!IN_IsSafeQuotedCommandArg(command_path))
			{
				Con_Printf("Installed %s, not auto-running because the filename contains command characters.\n", source_desc);
				return true;
			}
			Cbuf_AddText(va("map \"%s\"\n", command_path));
		}
	}

	return true;
}

typedef struct
{
	char	path[MAX_OSPATH];
	char	basename[MAX_OSPATH];
	char	relative_path[MAX_OSPATH];
	char	dest_path[MAX_OSPATH];
	size_t	light_lump_len;
	qboolean	valid;
	qboolean	installed;
	qboolean	using_existing;
	qboolean	existing_matches_source;
} in_map_bsp_t;

typedef struct
{
	char	path[MAX_OSPATH];
	char	basename[MAX_OSPATH];
} in_map_sidecar_t;

static qboolean IN_GetExternalFileSize(FILE *f, size_t *filesize, char *reason, size_t reason_size)
{
	long end;

	if (fseek(f, 0, SEEK_END) != 0)
	{
		q_snprintf(reason, reason_size, "could not seek file: %s", strerror(errno));
		return false;
	}
	end = ftell(f);
	if (end < 0)
	{
		q_snprintf(reason, reason_size, "could not read file size: %s", strerror(errno));
		return false;
	}
	if (fseek(f, 0, SEEK_SET) != 0)
	{
		q_snprintf(reason, reason_size, "could not rewind file: %s", strerror(errno));
		return false;
	}

	*filesize = (size_t)end;
	return true;
}

static qboolean IN_ExternalFilesMatch(const char *path1, const char *path2, qboolean *match)
{
	FILE	*f1 = NULL;
	FILE	*f2 = NULL;
	byte	buffer1[4096];
	byte	buffer2[4096];
	size_t	size1;
	size_t	size2;
	size_t	remaining;
	qboolean ok = false;
	char	reason[128];

	if (match)
		*match = false;
	if (!path1 || !path2 || !match)
		return false;

	f1 = fopen(path1, "rb");
	if (!f1)
		goto done;
	f2 = fopen(path2, "rb");
	if (!f2)
		goto done;

	if (!IN_GetExternalFileSize(f1, &size1, reason, sizeof(reason)) ||
		!IN_GetExternalFileSize(f2, &size2, reason, sizeof(reason)))
		goto done;
	if (size1 != size2)
	{
		ok = true;
		goto done;
	}

	remaining = size1;
	while (remaining > 0)
	{
		size_t chunk = q_min(remaining, sizeof(buffer1));

		if (fread(buffer1, 1, chunk, f1) != chunk ||
			fread(buffer2, 1, chunk, f2) != chunk)
			goto done;
		if (memcmp(buffer1, buffer2, chunk) != 0)
		{
			ok = true;
			goto done;
		}
		remaining -= chunk;
	}

	*match = true;
	ok = true;

done:
	if (f1)
		fclose(f1);
	if (f2)
		fclose(f2);
	return ok;
}

static qboolean IN_ModelLumpLooksHexen2(FILE *f, const lump_t *model_lump)
{
	dmodelq1_t	models[2];
	long	saved_offset;
	qboolean	result = false;

	if (model_lump->filelen < sizeof(dmodelh2_t) ||
		model_lump->filelen % sizeof(dmodelh2_t) != 0 ||
		model_lump->filelen < 2 * sizeof(dmodelq1_t))
		return false;

	saved_offset = ftell(f);
	if (saved_offset < 0)
		return false;
	if (fseek(f, (long)model_lump->fileofs, SEEK_SET) != 0)
		goto done;
	if (fread(models, sizeof(models), 1, f) != 1)
		goto done;

	result = (LittleLong(models[0].numfaces) == 0 && LittleLong(models[1].firstface) != 0);

done:
	fseek(f, saved_offset, SEEK_SET);
	clearerr(f);
	return result;
}

static qboolean IN_ValidateBSPFile(const char *path, in_map_bsp_t *info, char *reason, size_t reason_size)
{
	FILE	*f;
	dheader_t	header;
	lump_t	*model_lump;
	size_t	filesize;
	int	version;
	int	i;
	qboolean	ok = false;

	f = fopen(path, "rb");
	if (!f)
	{
		q_snprintf(reason, reason_size, "could not open file: %s", strerror(errno));
		return false;
	}

	if (!IN_GetExternalFileSize(f, &filesize, reason, reason_size))
		goto done;
	if (filesize < sizeof(header))
	{
		q_snprintf(reason, reason_size, "file is too small for a BSP header");
		goto done;
	}
	if (fread(&header, sizeof(header), 1, f) != 1)
	{
		q_snprintf(reason, reason_size, "could not read BSP header: %s", strerror(errno));
		goto done;
	}

	version = LittleLong(header.version);
	switch (version)
	{
	case BSPVERSION:
	case BSP2VERSION_2PSB:
	case BSP2VERSION_BSP2:
	case BSPVERSION_QUAKE64:
		break;
	default:
		q_snprintf(reason, reason_size, "unsupported BSP version %d", version);
		goto done;
	}

	for (i = 0; i < HEADER_LUMPS; ++i)
	{
		size_t fileofs, filelen;

		header.lumps[i].fileofs = (unsigned int)LittleLong((int)header.lumps[i].fileofs);
		header.lumps[i].filelen = (unsigned int)LittleLong((int)header.lumps[i].filelen);
		fileofs = (size_t)header.lumps[i].fileofs;
		filelen = (size_t)header.lumps[i].filelen;
		if (fileofs > filesize || filelen > filesize - fileofs)
		{
			q_snprintf(reason, reason_size, "BSP lump %d is outside the file", i);
			goto done;
		}
	}

	if (header.lumps[LUMP_ENTITIES].filelen == 0)
	{
		q_snprintf(reason, reason_size, "missing entity lump");
		goto done;
	}

	model_lump = &header.lumps[LUMP_MODELS];
	if (model_lump->filelen < sizeof(dmodelq1_t) ||
		(model_lump->filelen % sizeof(dmodelq1_t) && !IN_ModelLumpLooksHexen2(f, model_lump)))
	{
		q_snprintf(reason, reason_size, "invalid model lump");
		goto done;
	}

	if (info)
		info->light_lump_len = (size_t)header.lumps[LUMP_LIGHTING].filelen;
	ok = true;

done:
	fclose(f);
	return ok;
}

static qboolean IN_ValidateLITFile(const char *path, const in_map_bsp_t *bsp, char *reason, size_t reason_size)
{
	FILE	*f;
	byte	header[8];
	size_t	filesize;
	size_t	expected_size;
	size_t	sample_size;
	int	version;
	qboolean	ok = false;

	if (!bsp)
	{
		q_snprintf(reason, reason_size, "matching BSP is missing");
		return false;
	}

	f = fopen(path, "rb");
	if (!f)
	{
		q_snprintf(reason, reason_size, "could not open file: %s", strerror(errno));
		return false;
	}

	if (!IN_GetExternalFileSize(f, &filesize, reason, reason_size))
		goto done;
	if (filesize < sizeof(header))
	{
		q_snprintf(reason, reason_size, "file is too small for a LIT header");
		goto done;
	}
	if (fread(header, sizeof(header), 1, f) != 1)
	{
		q_snprintf(reason, reason_size, "could not read LIT header: %s", strerror(errno));
		goto done;
	}
	if (memcmp(header, "QLIT", 4) != 0)
	{
		q_snprintf(reason, reason_size, "missing QLIT header");
		goto done;
	}

	memcpy(&version, header + 4, sizeof(version));
	version = LittleLong(version);
	if (version == 1)
		sample_size = 3;
	else if (version == 0x10001)
		sample_size = 4;
	else
	{
		q_snprintf(reason, reason_size, "unsupported LIT version %d", version);
		goto done;
	}

	if (bsp->light_lump_len > (((size_t)-1) - 8) / sample_size)
	{
		q_snprintf(reason, reason_size, "matching BSP light lump is too large");
		goto done;
	}
	expected_size = 8 + bsp->light_lump_len * sample_size;
	if (filesize != expected_size)
	{
		q_snprintf(reason, reason_size, "size mismatch, expected %llu bytes but got %llu",
			(unsigned long long)expected_size, (unsigned long long)filesize);
		goto done;
	}

	ok = true;

done:
	fclose(f);
	return ok;
}

static qboolean IN_LoadExternalTextFile(const char *path, char **data, size_t *filesize, char *reason, size_t reason_size)
{
	FILE	*f;
	char	*buffer = NULL;
	size_t	size;
	qboolean ok = false;

	*data = NULL;
	if (filesize)
		*filesize = 0;

	f = fopen(path, "rb");
	if (!f)
	{
		q_snprintf(reason, reason_size, "could not open file: %s", strerror(errno));
		return false;
	}

	if (!IN_GetExternalFileSize(f, &size, reason, reason_size))
		goto done;
	if (size == 0)
	{
		q_snprintf(reason, reason_size, "file is empty");
		goto done;
	}
	if (size >= (size_t)Q_MAXINT)
	{
		q_snprintf(reason, reason_size, "file is too large");
		goto done;
	}

	buffer = (char *) Z_Malloc((int)size + 1);
	if (fread(buffer, 1, size, f) != size)
	{
		q_snprintf(reason, reason_size, "could not read file: %s", strerror(errno));
		goto done;
	}
	buffer[size] = '\0';

	*data = buffer;
	if (filesize)
		*filesize = size;
	buffer = NULL;
	ok = true;

done:
	if (buffer)
		Z_Free(buffer);
	fclose(f);
	return ok;
}

static qboolean IN_ValidateENTFile(const char *path, char *reason, size_t reason_size)
{
	char	*data = NULL;
	const char *p;
	int	depth = 0;
	qboolean saw_entity = false;
	qboolean ok = false;

	if (!IN_LoadExternalTextFile(path, &data, NULL, reason, reason_size))
		return false;

	p = data;
	while ((p = COM_Parse(p)) != NULL)
	{
		if (!strcmp(com_token, "{"))
		{
			if (depth != 0)
			{
				q_snprintf(reason, reason_size, "nested entity block");
				goto done;
			}
			++depth;
		}
		else if (!strcmp(com_token, "}"))
		{
			if (depth != 1)
			{
				q_snprintf(reason, reason_size, "unmatched entity close brace");
				goto done;
			}
			--depth;
			saw_entity = true;
		}
	}

	if (depth != 0)
	{
		q_snprintf(reason, reason_size, "unterminated entity block");
		goto done;
	}
	if (!saw_entity)
	{
		q_snprintf(reason, reason_size, "no entity blocks found");
		goto done;
	}

	ok = true;

done:
	Z_Free(data);
	return ok;
}

static qboolean IN_VISMapNameMatches(const char *mapname, const char *basename)
{
	char	expected[32];
	int	result;

	result = q_snprintf(expected, sizeof(expected), "%s.bsp", basename);
	if (result < 0 || (size_t)result >= sizeof(expected))
		return false;
	return !q_strcasecmp(mapname, expected);
}

static qboolean IN_ValidateVISFile(const char *path, const char *basename, char *reason, size_t reason_size)
{
	typedef struct
	{
		char	mapname[32];
		int	filelen;
	} in_vispatch_t;

	FILE	*f;
	in_vispatch_t header;
	size_t	filesize;
	size_t	offset = 0;
	qboolean ok = false;

	if (!basename || !*basename)
	{
		q_snprintf(reason, reason_size, "matching BSP is missing");
		return false;
	}

	f = fopen(path, "rb");
	if (!f)
	{
		q_snprintf(reason, reason_size, "could not open file: %s", strerror(errno));
		return false;
	}

	if (!IN_GetExternalFileSize(f, &filesize, reason, reason_size))
		goto done;
	if (filesize < sizeof(header))
	{
		q_snprintf(reason, reason_size, "file is too small for a VIS header");
		goto done;
	}

	while (offset + sizeof(header) <= filesize)
	{
		int	filelen;
		size_t	entry_size;

		if (fseek(f, (long)offset, SEEK_SET) != 0)
		{
			q_snprintf(reason, reason_size, "could not seek VIS entry: %s", strerror(errno));
			goto done;
		}
		if (fread(&header, 1, sizeof(header), f) != sizeof(header))
		{
			q_snprintf(reason, reason_size, "could not read VIS header: %s", strerror(errno));
			goto done;
		}
		if (!memchr(header.mapname, '\0', sizeof(header.mapname)))
		{
			q_snprintf(reason, reason_size, "VIS map name is not terminated");
			goto done;
		}

		filelen = LittleLong(header.filelen);
		if (filelen <= 0)
		{
			q_snprintf(reason, reason_size, "VIS entry has invalid length");
			goto done;
		}
		entry_size = (size_t)filelen;
		if (entry_size > filesize - offset - sizeof(header))
		{
			q_snprintf(reason, reason_size, "VIS entry is outside the file");
			goto done;
		}

		if (IN_VISMapNameMatches(header.mapname, basename))
		{
			int vis_len;

			if (entry_size < sizeof(vis_len))
			{
				q_snprintf(reason, reason_size, "matching VIS entry is too small");
				goto done;
			}
			if (fread(&vis_len, 1, sizeof(vis_len), f) != sizeof(vis_len))
			{
				q_snprintf(reason, reason_size, "could not read VIS data length: %s", strerror(errno));
				goto done;
			}
			vis_len = LittleLong(vis_len);
			if (vis_len <= 0 || (size_t)vis_len > entry_size - sizeof(vis_len))
			{
				q_snprintf(reason, reason_size, "matching VIS entry has invalid data length");
				goto done;
			}
			ok = true;
			goto done;
		}

		offset += sizeof(header) + entry_size;
	}

	q_snprintf(reason, reason_size, "no VIS entry for %s.bsp", basename);

done:
	fclose(f);
	return ok;
}

static qboolean IN_BuildMapInstallPaths(const char *basename, const char *extension, char *relative_path, size_t relative_path_size, char *dest_path, size_t dest_path_size)
{
	int result;

	if (!basename || !*basename)
		return false;

	result = q_snprintf(relative_path, relative_path_size, "maps/%s%s", basename, extension);
	if (result < 0 || (size_t)result >= relative_path_size)
	{
		Con_Printf("Path too long for map file.\n");
		return false;
	}

	result = q_snprintf(dest_path, dest_path_size, "%s/%s", com_gamedir, relative_path);
	if (result < 0 || (size_t)result >= dest_path_size)
	{
		Con_Printf("Path too long for map destination.\n");
		return false;
	}
	IN_NormalizeDroppedPath(dest_path);
	return true;
}

static qboolean IN_CopyExternalFileToMapPath(const char *source_path, const char *source_desc, const char *relative_path, const char *dest_path, qboolean *using_existing)
{
	char	normalized_path[MAX_OSPATH];
	char	path_copy[MAX_OSPATH];
	qboolean same_path;

	if (using_existing)
		*using_existing = false;

	if (q_strlcpy(normalized_path, source_path, sizeof(normalized_path)) >= sizeof(normalized_path))
	{
		Con_Printf("Path too long for %s.\n", source_desc);
		return false;
	}
	IN_NormalizeDroppedPath(normalized_path);

#ifdef _WIN32
	same_path = (q_strcasecmp(normalized_path, dest_path) == 0);
#else
	same_path = (strcmp(normalized_path, dest_path) == 0);
#endif
	if (same_path)
		return true;

	if (Sys_FileType(dest_path) & FS_ENT_FILE)
	{
		if (using_existing)
			*using_existing = true;
		Con_SafePrintf("Using existing file at ");
		Con_LinkPrintf(dest_path, "%s/%s", COM_SkipPath(com_gamedir), relative_path);
		Con_SafePrintf("\n");
		return true;
	}

	q_strlcpy(path_copy, dest_path, sizeof(path_copy));
	COM_CreatePath(path_copy);

	if (!IN_CopyExternalFile(normalized_path, dest_path))
		return false;

	Con_Printf("Copied %s to ", source_desc);
	Con_LinkPrintf(dest_path, "%s/%s", COM_SkipPath(com_gamedir), relative_path);
	Con_Printf("\n");
	return true;
}

static int IN_FindBatchBSPForBasename(const char *basename, const in_map_bsp_t *bsps, int bsp_count, qboolean *ambiguous)
{
	int i;
	int match = -1;

	*ambiguous = false;
	for (i = 0; i < bsp_count; ++i)
	{
		if (!bsps[i].valid)
			continue;
		if (q_strcasecmp(basename, bsps[i].basename))
			continue;
		if (match >= 0)
		{
			*ambiguous = true;
			return -1;
		}
		match = i;
	}

	return match;
}

static qboolean IN_LoadBSPInfoFromPath(const char *path, in_map_bsp_t *bsp, char *reason, size_t reason_size)
{
	if (!(Sys_FileType(path) & FS_ENT_FILE))
	{
		q_snprintf(reason, reason_size, "matching .bsp was not found");
		return false;
	}
	if (q_strlcpy(bsp->path, path, sizeof(bsp->path)) >= sizeof(bsp->path))
	{
		q_snprintf(reason, reason_size, "matching .bsp path is too long");
		return false;
	}
	IN_NormalizeDroppedPath(bsp->path);
	COM_StripExtension(COM_SkipPath(bsp->path), bsp->basename, sizeof(bsp->basename));
	bsp->valid = IN_ValidateBSPFile(bsp->path, bsp, reason, reason_size);
	return bsp->valid;
}

static qboolean IN_FindExternalBSPForSidecar(const in_map_sidecar_t *sidecar, in_map_bsp_t *bsp, char *reason, size_t reason_size)
{
	char	root[MAX_OSPATH];
	char	candidate[MAX_OSPATH];
	char	invalid_reason[128];
	qboolean saw_invalid = false;
	int	result;

	COM_StripExtension(sidecar->path, root, sizeof(root));

	result = q_snprintf(candidate, sizeof(candidate), "%s/maps/%s.bsp", com_gamedir, sidecar->basename);
	if (result >= 0 && (size_t)result < sizeof(candidate))
	{
		IN_NormalizeDroppedPath(candidate);
		if (IN_LoadBSPInfoFromPath(candidate, bsp, invalid_reason, sizeof(invalid_reason)))
			return true;
		if (Sys_FileType(candidate) & FS_ENT_FILE)
			saw_invalid = true;
	}

	result = q_snprintf(candidate, sizeof(candidate), "%s.bsp", root);
	if (result >= 0 && (size_t)result < sizeof(candidate))
	{
		if (IN_LoadBSPInfoFromPath(candidate, bsp, invalid_reason, sizeof(invalid_reason)))
			return true;
		if (Sys_FileType(candidate) & FS_ENT_FILE)
			saw_invalid = true;
	}

	result = q_snprintf(candidate, sizeof(candidate), "%s.BSP", root);
	if (result >= 0 && (size_t)result < sizeof(candidate))
	{
		if (IN_LoadBSPInfoFromPath(candidate, bsp, invalid_reason, sizeof(invalid_reason)))
			return true;
		if (Sys_FileType(candidate) & FS_ENT_FILE)
			saw_invalid = true;
	}

	if (saw_invalid)
		q_snprintf(reason, reason_size, "matching .bsp is invalid: %s", invalid_reason);
	else
		q_snprintf(reason, reason_size, "matching .bsp was not found");
	return false;
}

static qboolean IN_TryInstallAdjacentLITForBSP(const in_map_bsp_t *bsp, const char *lit_desc)
{
	in_map_sidecar_t lit;
	char	reason[128];
	char	relative_path[MAX_OSPATH];
	char	dest_path[MAX_OSPATH];
	qboolean result;

	if (!bsp->installed)
		return false;
	if (bsp->using_existing && !bsp->existing_matches_source)
	{
		Con_Printf("Skipping adjacent .lit: existing .bsp differs from pasted .bsp.\n");
		return false;
	}

	COM_StripExtension(bsp->path, lit.path, sizeof(lit.path));
	if ((size_t)q_strlcat(lit.path, ".lit", sizeof(lit.path)) >= sizeof(lit.path))
		return false;
	if (!(Sys_FileType(lit.path) & FS_ENT_FILE))
	{
		COM_StripExtension(bsp->path, lit.path, sizeof(lit.path));
		if ((size_t)q_strlcat(lit.path, ".LIT", sizeof(lit.path)) >= sizeof(lit.path))
			return false;
		if (!(Sys_FileType(lit.path) & FS_ENT_FILE))
			return false;
	}
	q_strlcpy(lit.basename, bsp->basename, sizeof(lit.basename));

	if (!IN_ValidateLITFile(lit.path, bsp, reason, sizeof(reason)))
	{
		Con_Printf("Skipping invalid adjacent .lit %s: %s\n", COM_SkipPath(lit.path), reason);
		return false;
	}
	if (!IN_BuildMapInstallPaths(bsp->basename, ".lit", relative_path, sizeof(relative_path), dest_path, sizeof(dest_path)))
		return false;

	result = IN_CopyExternalFileToMapPath(lit.path, lit_desc, relative_path, dest_path, NULL);
	return result;
}

static qboolean IN_InstallMapLIT(const in_map_sidecar_t *lit, const in_map_bsp_t *bsp, const char *dest_basename, const char *lit_desc)
{
	char	reason[128];
	char	relative_path[MAX_OSPATH];
	char	dest_path[MAX_OSPATH];

	if (!IN_ValidateLITFile(lit->path, bsp, reason, sizeof(reason)))
	{
		Con_Printf("Skipping invalid .lit %s: %s\n", COM_SkipPath(lit->path), reason);
		return false;
	}
	if (!IN_BuildMapInstallPaths(dest_basename, ".lit", relative_path, sizeof(relative_path), dest_path, sizeof(dest_path)))
		return false;
	return IN_CopyExternalFileToMapPath(lit->path, lit_desc, relative_path, dest_path, NULL);
}

static qboolean IN_InstallMapENT(const in_map_sidecar_t *ent, const in_map_bsp_t *bsp, const char *dest_basename, const char *ent_desc)
{
	char	reason[128];
	char	relative_path[MAX_OSPATH];
	char	dest_path[MAX_OSPATH];

	if (!bsp)
	{
		Con_Printf("Skipping .ent %s: matching .bsp is missing.\n", COM_SkipPath(ent->path));
		return false;
	}
	if (!IN_ValidateENTFile(ent->path, reason, sizeof(reason)))
	{
		Con_Printf("Skipping invalid .ent %s: %s\n", COM_SkipPath(ent->path), reason);
		return false;
	}
	if (!IN_BuildMapInstallPaths(dest_basename, ".ent", relative_path, sizeof(relative_path), dest_path, sizeof(dest_path)))
		return false;
	return IN_CopyExternalFileToMapPath(ent->path, ent_desc, relative_path, dest_path, NULL);
}

static qboolean IN_InstallMapVIS(const in_map_sidecar_t *vis, const in_map_bsp_t *bsp, const char *dest_basename, const char *vis_desc)
{
	char	reason[128];
	char	relative_path[MAX_OSPATH];
	char	dest_path[MAX_OSPATH];

	if (!bsp)
	{
		Con_Printf("Skipping .vis %s: matching .bsp is missing.\n", COM_SkipPath(vis->path));
		return false;
	}
	if (!IN_ValidateVISFile(vis->path, dest_basename, reason, sizeof(reason)))
	{
		Con_Printf("Skipping invalid .vis %s: %s\n", COM_SkipPath(vis->path), reason);
		return false;
	}
	if (!IN_BuildMapInstallPaths(dest_basename, ".vis", relative_path, sizeof(relative_path), dest_path, sizeof(dest_path)))
		return false;
	return IN_CopyExternalFileToMapPath(vis->path, vis_desc, relative_path, dest_path, NULL);
}

static qboolean IN_TryInstallAdjacentENTForBSP(const in_map_bsp_t *bsp, const char *ent_desc)
{
	in_map_sidecar_t ent;

	if (!bsp->installed)
		return false;
	if (bsp->using_existing && !bsp->existing_matches_source)
	{
		Con_Printf("Skipping adjacent .ent: existing .bsp differs from pasted .bsp.\n");
		return false;
	}

	COM_StripExtension(bsp->path, ent.path, sizeof(ent.path));
	if ((size_t)q_strlcat(ent.path, ".ent", sizeof(ent.path)) >= sizeof(ent.path))
		return false;
	if (!(Sys_FileType(ent.path) & FS_ENT_FILE))
	{
		COM_StripExtension(bsp->path, ent.path, sizeof(ent.path));
		if ((size_t)q_strlcat(ent.path, ".ENT", sizeof(ent.path)) >= sizeof(ent.path))
			return false;
		if (!(Sys_FileType(ent.path) & FS_ENT_FILE))
			return false;
	}
	q_strlcpy(ent.basename, bsp->basename, sizeof(ent.basename));
	return IN_InstallMapENT(&ent, bsp, bsp->basename, ent_desc);
}

static qboolean IN_TryInstallAdjacentVISForBSP(const in_map_bsp_t *bsp, const char *vis_desc)
{
	in_map_sidecar_t vis;

	if (!bsp->installed)
		return false;
	if (bsp->using_existing && !bsp->existing_matches_source)
	{
		Con_Printf("Skipping adjacent .vis: existing .bsp differs from pasted .bsp.\n");
		return false;
	}

	COM_StripExtension(bsp->path, vis.path, sizeof(vis.path));
	if ((size_t)q_strlcat(vis.path, ".vis", sizeof(vis.path)) >= sizeof(vis.path))
		return false;
	if (!(Sys_FileType(vis.path) & FS_ENT_FILE))
	{
		COM_StripExtension(bsp->path, vis.path, sizeof(vis.path));
		if ((size_t)q_strlcat(vis.path, ".VIS", sizeof(vis.path)) >= sizeof(vis.path))
			return false;
		if (!(Sys_FileType(vis.path) & FS_ENT_FILE))
			return false;
	}
	q_strlcpy(vis.basename, bsp->basename, sizeof(vis.basename));
	return IN_InstallMapVIS(&vis, bsp, bsp->basename, vis_desc);
}

static qboolean IN_IsMapAssetExtension(const char *extension)
{
	return !q_strcasecmp(extension, "bsp") || !q_strcasecmp(extension, "lit") ||
		!q_strcasecmp(extension, "ent") || !q_strcasecmp(extension, "vis");
}

static qboolean IN_IsMapAssetFile(const char *path)
{
	char	normalized_path[MAX_OSPATH];

	if (!path || !*path)
		return false;
	if (q_strlcpy(normalized_path, path, sizeof(normalized_path)) >= sizeof(normalized_path))
		return false;
	IN_NormalizeDroppedPath(normalized_path);
	return IN_IsMapAssetExtension(COM_FileGetExtension(normalized_path));
}

static qboolean IN_IsSkyboxImageFile(const char *path);

static qboolean IN_HasMapAssetFiles(char **paths, int count)
{
	int i;

	for (i = 0; i < count; ++i)
	{
		if (IN_IsMapAssetFile(paths[i]))
			return true;
	}
	return false;
}

static qboolean IN_IsLegacyExternalExtension(const char *extension)
{
	return !q_strcasecmp(extension, "dem") || !q_strcasecmp(extension, "loc");
}

static qboolean IN_IsLegacyExternalFile(const char *path)
{
	char	normalized_path[MAX_OSPATH];

	if (!path || !*path)
		return false;
	if (q_strlcpy(normalized_path, path, sizeof(normalized_path)) >= sizeof(normalized_path))
		return false;
	IN_NormalizeDroppedPath(normalized_path);
	return IN_IsLegacyExternalExtension(COM_FileGetExtension(normalized_path));
}

static qboolean IN_IsDemoExternalFile(const char *path)
{
	char	normalized_path[MAX_OSPATH];

	if (!path || !*path)
		return false;
	if (q_strlcpy(normalized_path, path, sizeof(normalized_path)) >= sizeof(normalized_path))
		return false;
	IN_NormalizeDroppedPath(normalized_path);
	return !q_strcasecmp(COM_FileGetExtension(normalized_path), "dem");
}

static int IN_CountDemoExternalFiles(char **paths, int count)
{
	int i;
	int demo_count = 0;

	for (i = 0; i < count; ++i)
	{
		if (IN_IsDemoExternalFile(paths[i]))
			++demo_count;
	}
	return demo_count;
}

static qboolean IN_ResolveMapSidecarBSP(const in_map_sidecar_t *sidecar, const char *extension, const in_map_bsp_t *bsps, int bsp_count, in_map_bsp_t *external_bsp, const in_map_bsp_t **paired_bsp, const char **dest_basename)
{
	char reason[128];
	qboolean ambiguous;
	int bsp_index;

	*paired_bsp = NULL;
	*dest_basename = NULL;

	bsp_index = IN_FindBatchBSPForBasename(sidecar->basename, bsps, bsp_count, &ambiguous);
	if (ambiguous)
	{
		Con_Printf("Skipping %s %s: ambiguous matching .bsp in batch.\n", extension, COM_SkipPath(sidecar->path));
		return false;
	}
	if (bsp_index >= 0)
	{
		if (!bsps[bsp_index].installed)
		{
			Con_Printf("Skipping %s %s: matching .bsp was not installed.\n", extension, COM_SkipPath(sidecar->path));
			return false;
		}
		if (bsps[bsp_index].using_existing && !bsps[bsp_index].existing_matches_source)
		{
			Con_Printf("Skipping %s %s: existing .bsp differs from pasted .bsp.\n", extension, COM_SkipPath(sidecar->path));
			return false;
		}
		*paired_bsp = &bsps[bsp_index];
		*dest_basename = bsps[bsp_index].basename;
		return true;
	}

	memset(external_bsp, 0, sizeof(*external_bsp));
	if (!IN_FindExternalBSPForSidecar(sidecar, external_bsp, reason, sizeof(reason)))
	{
		Con_Printf("Skipping orphan %s %s: %s\n", extension, COM_SkipPath(sidecar->path), reason);
		return false;
	}

	*paired_bsp = external_bsp;
	*dest_basename = external_bsp->basename;
	return true;
}

static qboolean IN_AddMapSidecar(in_map_sidecar_t *sidecars, int *sidecar_count, const char *normalized_path, const char *extension, const char *file_desc, int *skipped_count)
{
	in_map_sidecar_t *sidecar = &sidecars[(*sidecar_count)++];

	q_strlcpy(sidecar->path, normalized_path, sizeof(sidecar->path));
	COM_StripExtension(COM_SkipPath(normalized_path), sidecar->basename, sizeof(sidecar->basename));
	if (!sidecar->basename[0] || sidecar->basename[0] == '.')
	{
		Con_Printf("Skipping %s with invalid filename: %s\n", extension, COM_SkipPath(normalized_path));
		--(*sidecar_count);
		++(*skipped_count);
		return false;
	}
	return true;
}

static qboolean IN_InstallMapFiles(char **paths, int path_count, const char *batch_desc, const char *file_desc, const char *bsp_desc, const char *lit_desc, const char *ent_desc, const char *vis_desc, qboolean *installed_any)
{
	in_map_bsp_t	*bsps = NULL;
	in_map_sidecar_t	*lits = NULL;
	in_map_sidecar_t	*ents = NULL;
	in_map_sidecar_t	*vises = NULL;
	int	i;
	int	bsp_count = 0;
	int	lit_count = 0;
	int	ent_count = 0;
	int	vis_count = 0;
	int	valid_bsp_count = 0;
	int	ready_bsp_count = 0;
	int	ready_lit_count = 0;
	int	ready_ent_count = 0;
	int	ready_vis_count = 0;
	int	skipped_count = 0;
	int	auto_run_bsp = -1;
	qboolean	found_map_asset = false;

	if (installed_any)
		*installed_any = false;

	if (!batch_desc || !*batch_desc)
		batch_desc = "Map";
	if (!file_desc || !*file_desc)
		file_desc = "map file";
	if (!bsp_desc || !*bsp_desc)
		bsp_desc = "map .bsp";
	if (!lit_desc || !*lit_desc)
		lit_desc = "map .lit";
	if (!ent_desc || !*ent_desc)
		ent_desc = "map .ent";
	if (!vis_desc || !*vis_desc)
		vis_desc = "map .vis";

	if (!paths || path_count <= 0)
		return false;
	if ((size_t)path_count > (size_t)Q_MAXINT / sizeof(*bsps) ||
		(size_t)path_count > (size_t)Q_MAXINT / sizeof(*lits) ||
		(size_t)path_count > (size_t)Q_MAXINT / sizeof(*ents) ||
		(size_t)path_count > (size_t)Q_MAXINT / sizeof(*vises))
	{
		Con_Printf("Too many map files.\n");
		return true;
	}

	bsps = (in_map_bsp_t *) Z_Malloc(path_count * (int)sizeof(*bsps));
	lits = (in_map_sidecar_t *) Z_Malloc(path_count * (int)sizeof(*lits));
	ents = (in_map_sidecar_t *) Z_Malloc(path_count * (int)sizeof(*ents));
	vises = (in_map_sidecar_t *) Z_Malloc(path_count * (int)sizeof(*vises));

	for (i = 0; i < path_count; ++i)
	{
		char	normalized_path[MAX_OSPATH];
		const char *extension;

		if (!paths[i] || !*paths[i])
			continue;
		if (q_strlcpy(normalized_path, paths[i], sizeof(normalized_path)) >= sizeof(normalized_path))
		{
			Con_Printf("Skipping %s with too-long path.\n", file_desc);
			++skipped_count;
			continue;
		}
		IN_NormalizeDroppedPath(normalized_path);
		extension = COM_FileGetExtension(normalized_path);

		if (!q_strcasecmp(extension, "bsp"))
		{
			in_map_bsp_t *bsp = &bsps[bsp_count++];
			char reason[128];

			found_map_asset = true;
			q_strlcpy(bsp->path, normalized_path, sizeof(bsp->path));
			COM_StripExtension(COM_SkipPath(normalized_path), bsp->basename, sizeof(bsp->basename));
			if (!bsp->basename[0] || bsp->basename[0] == '.')
			{
				Con_Printf("Skipping .bsp with invalid filename: %s\n", COM_SkipPath(normalized_path));
				--bsp_count;
				++skipped_count;
				continue;
			}
			bsp->valid = IN_ValidateBSPFile(bsp->path, bsp, reason, sizeof(reason));
			if (bsp->valid)
			{
				++valid_bsp_count;
				auto_run_bsp = bsp_count - 1;
			}
			else
			{
				Con_Printf("Skipping invalid .bsp %s: %s\n", COM_SkipPath(normalized_path), reason);
				++skipped_count;
			}
		}
		else if (!q_strcasecmp(extension, "lit"))
		{
			found_map_asset = true;
			IN_AddMapSidecar(lits, &lit_count, normalized_path, ".lit", file_desc, &skipped_count);
		}
		else if (!q_strcasecmp(extension, "ent"))
		{
			found_map_asset = true;
			IN_AddMapSidecar(ents, &ent_count, normalized_path, ".ent", file_desc, &skipped_count);
		}
		else if (!q_strcasecmp(extension, "vis"))
		{
			found_map_asset = true;
			IN_AddMapSidecar(vises, &vis_count, normalized_path, ".vis", file_desc, &skipped_count);
		}
		else if (IN_IsSkyboxImageFile(normalized_path))
		{
			/* Skybox faces are installed by IN_ProcessSkyboxFiles below. */
		}
		else if (path_count > 1 && !IN_IsLegacyExternalExtension(extension))
		{
			Con_Printf("Unsupported %s: %s\n", file_desc, COM_SkipPath(normalized_path));
			++skipped_count;
		}
	}

	for (i = 0; i < bsp_count; ++i)
	{
		if (!bsps[i].valid)
			continue;
		if (!IN_BuildMapInstallPaths(bsps[i].basename, ".bsp", bsps[i].relative_path, sizeof(bsps[i].relative_path), bsps[i].dest_path, sizeof(bsps[i].dest_path)))
		{
			++skipped_count;
			continue;
		}
		bsps[i].installed = IN_CopyExternalFileToMapPath(bsps[i].path, bsp_desc, bsps[i].relative_path, bsps[i].dest_path, &bsps[i].using_existing);
		if (bsps[i].installed && bsps[i].using_existing)
		{
			in_map_bsp_t dest_bsp;
			char reason[128];

			memset(&dest_bsp, 0, sizeof(dest_bsp));
			if (!IN_ExternalFilesMatch(bsps[i].path, bsps[i].dest_path, &bsps[i].existing_matches_source))
				bsps[i].existing_matches_source = false;
			if (!IN_LoadBSPInfoFromPath(bsps[i].dest_path, &dest_bsp, reason, sizeof(reason)))
			{
				Con_Printf("Skipping .bsp %s: existing destination is invalid: %s\n", COM_SkipPath(bsps[i].path), reason);
				bsps[i].installed = false;
			}
			else
			{
				bsps[i].light_lump_len = dest_bsp.light_lump_len;
			}
		}
		if (bsps[i].installed)
			++ready_bsp_count;
		else
			++skipped_count;
	}

	for (i = 0; i < lit_count; ++i)
	{
		in_map_bsp_t external_bsp;
		const in_map_bsp_t *paired_bsp = NULL;
		const char *dest_basename = NULL;

		if (!IN_ResolveMapSidecarBSP(&lits[i], ".lit", bsps, bsp_count, &external_bsp, &paired_bsp, &dest_basename))
		{
			++skipped_count;
			continue;
		}

		if (IN_InstallMapLIT(&lits[i], paired_bsp, dest_basename, lit_desc))
			++ready_lit_count;
		else
			++skipped_count;
	}

	for (i = 0; i < ent_count; ++i)
	{
		in_map_bsp_t external_bsp;
		const in_map_bsp_t *paired_bsp = NULL;
		const char *dest_basename = NULL;

		if (!IN_ResolveMapSidecarBSP(&ents[i], ".ent", bsps, bsp_count, &external_bsp, &paired_bsp, &dest_basename))
		{
			++skipped_count;
			continue;
		}

		if (IN_InstallMapENT(&ents[i], paired_bsp, dest_basename, ent_desc))
			++ready_ent_count;
		else
			++skipped_count;
	}

	for (i = 0; i < vis_count; ++i)
	{
		in_map_bsp_t external_bsp;
		const in_map_bsp_t *paired_bsp = NULL;
		const char *dest_basename = NULL;

		if (!IN_ResolveMapSidecarBSP(&vises[i], ".vis", bsps, bsp_count, &external_bsp, &paired_bsp, &dest_basename))
		{
			++skipped_count;
			continue;
		}

		if (IN_InstallMapVIS(&vises[i], paired_bsp, dest_basename, vis_desc))
			++ready_vis_count;
		else
			++skipped_count;
	}

	if (path_count == 1 && valid_bsp_count == 1 && lit_count == 0 && ent_count == 0 && vis_count == 0 && auto_run_bsp >= 0)
	{
		if (IN_TryInstallAdjacentLITForBSP(&bsps[auto_run_bsp], lit_desc))
			++ready_lit_count;
		if (IN_TryInstallAdjacentENTForBSP(&bsps[auto_run_bsp], ent_desc))
			++ready_ent_count;
		if (IN_TryInstallAdjacentVISForBSP(&bsps[auto_run_bsp], vis_desc))
			++ready_vis_count;
	}

	if (ready_bsp_count > 0)
		ExtraMaps_NewGame();
	if (valid_bsp_count == 1 && auto_run_bsp >= 0 && bsps[auto_run_bsp].installed)
	{
		if (IN_IsSafeQuotedCommandArg(bsps[auto_run_bsp].basename))
			Cbuf_AddText(va("map \"%s\"\n", bsps[auto_run_bsp].basename));
		else
			Con_Printf("Installed map, not auto-running because the filename contains command characters.\n");
	}

	if (found_map_asset)
		Con_Printf("%s maps: %d .bsp ready, %d .lit ready, %d .ent ready, %d .vis ready, %d skipped.\n",
			batch_desc, ready_bsp_count, ready_lit_count, ready_ent_count, ready_vis_count, skipped_count);
	if (installed_any)
		*installed_any = (ready_bsp_count > 0 || ready_lit_count > 0 || ready_ent_count > 0 || ready_vis_count > 0);

	Z_Free(bsps);
	Z_Free(lits);
	Z_Free(ents);
	Z_Free(vises);
	return found_map_asset;
}

#define IN_SKYBOX_FACE_COUNT 6

static const char *in_skybox_suffixes[IN_SKYBOX_FACE_COUNT] =
{
	"rt", "bk", "lf", "ft", "up", "dn"
};

static const char *IN_SkyboxImageExtension(const char *extension)
{
	static const char *extensions[] = { "dds", "tga", "png", "jpeg", "jpg", "pcx" };
	int i;

	if (!extension || !*extension)
		return NULL;
	for (i = 0; i < (int)Q_COUNTOF(extensions); ++i)
	{
		if (!q_strcasecmp(extension, extensions[i]))
			return extensions[i];
	}
	return NULL;
}

static qboolean IN_ParseSkyboxImagePath(const char *path, char *skyname, size_t skyname_size, int *face_index, qboolean *has_underscore)
{
	char normalized_path[MAX_OSPATH];
	char stem[MAX_OSPATH];
	const char *extension;
	const char *basename;
	size_t len;
	int i;

	if (!path || !*path || !skyname || skyname_size == 0)
		return false;
	if (q_strlcpy(normalized_path, path, sizeof(normalized_path)) >= sizeof(normalized_path))
		return false;
	IN_NormalizeDroppedPath(normalized_path);

	extension = IN_SkyboxImageExtension(COM_FileGetExtension(normalized_path));
	if (!extension)
		return false;

	basename = COM_SkipPath(normalized_path);
	COM_StripExtension(basename, stem, sizeof(stem));
	len = strlen(stem);
	if (len <= 2)
		return false;

	for (i = 0; i < IN_SKYBOX_FACE_COUNT; ++i)
	{
		qboolean underscore = false;
		size_t prefix_len;

		if (q_strcasecmp(stem + len - 2, in_skybox_suffixes[i]) != 0)
			continue;

		prefix_len = len - 2;
		if (prefix_len > 0 && stem[prefix_len - 1] == '_')
		{
			underscore = true;
			--prefix_len;
		}
		if (prefix_len == 0 || prefix_len >= skyname_size)
			return false;

		memcpy(skyname, stem, prefix_len);
		skyname[prefix_len] = '\0';
		if (!q_strcasecmp(skyname, ".") || !q_strcasecmp(skyname, ".."))
			return false;

		if (face_index)
			*face_index = i;
		if (has_underscore)
			*has_underscore = underscore;
		return true;
	}

	return false;
}

static void IN_GetSkyboxImageDirectory(const char *path, char *directory, size_t directory_size)
{
	const char *basename;
	size_t directory_len;

	if (!directory || directory_size == 0)
		return;
	directory[0] = '\0';
	if (!path || !*path)
		return;

	basename = COM_SkipPath(path);
	directory_len = (size_t)(basename - path);
	if (directory_len > 0 && path[directory_len - 1] == '/')
		--directory_len;
	if (directory_len >= directory_size)
		return;
	memcpy(directory, path, directory_len);
	directory[directory_len] = '\0';
}

static qboolean IN_SameSkyboxGroup(const char *path, const char *directory, const char *skyname)
{
	char candidate_directory[MAX_OSPATH];
	char candidate_skyname[MAX_OSPATH];

	if (!IN_ParseSkyboxImagePath(path, candidate_skyname, sizeof(candidate_skyname), NULL, NULL))
		return false;
	IN_GetSkyboxImageDirectory(path, candidate_directory, sizeof(candidate_directory));
#ifdef _WIN32
	return !q_strcasecmp(candidate_directory, directory) && !q_strcasecmp(candidate_skyname, skyname);
#else
	return !strcmp(candidate_directory, directory) && !q_strcasecmp(candidate_skyname, skyname);
#endif
}

static const char *IN_FindSkyboxFaceInBatch(char **paths, int path_count, const char *directory, const char *skyname, int face_index)
{
	int i;

	for (i = 0; i < path_count; ++i)
	{
		char candidate_skyname[MAX_OSPATH];
		int candidate_face;

		if (!IN_ParseSkyboxImagePath(paths[i], candidate_skyname, sizeof(candidate_skyname), &candidate_face, NULL))
			continue;
		if (candidate_face != face_index || !IN_SameSkyboxGroup(paths[i], directory, skyname))
			continue;
		return paths[i];
	}
	return NULL;
}

static qboolean IN_BuildSkyboxFaceCandidate(const char *directory, const char *skyname, int face_index, qboolean underscore, const char *extension, char *path, size_t path_size)
{
	int result;

	if (directory && directory[0])
		result = q_snprintf(path, path_size, "%s/%s%s%s.%s", directory, skyname,
			underscore ? "_" : "", in_skybox_suffixes[face_index], extension);
	else
		result = q_snprintf(path, path_size, "%s%s%s.%s", skyname,
			underscore ? "_" : "", in_skybox_suffixes[face_index], extension);
	return result >= 0 && (size_t)result < path_size;
}

static qboolean IN_FindAdjacentSkyboxFace(const char *directory, const char *skyname, int face_index, qboolean preferred_underscore, char *path, size_t path_size)
{
	static const char *extensions[] = { "dds", "tga", "png", "jpeg", "jpg", "pcx" };
	int style;
	int extension_index;

	for (style = 0; style < 2; ++style)
	{
		qboolean underscore = style == 0 ? preferred_underscore : !preferred_underscore;

		for (extension_index = 0; extension_index < (int)Q_COUNTOF(extensions); ++extension_index)
		{
			if (!IN_BuildSkyboxFaceCandidate(directory, skyname, face_index, underscore,
				extensions[extension_index], path, path_size))
				continue;
			if (Sys_FileType(path) & FS_ENT_FILE)
				return true;
		}
	}
	return false;
}

static qboolean IN_IsSkyboxImageFile(const char *path)
{
	char skyname[MAX_OSPATH];

	return IN_ParseSkyboxImagePath(path, skyname, sizeof(skyname), NULL, NULL);
}

static qboolean IN_FindExistingSkyboxFace(const char *relative_path, const char *extension,
	char *existing_relative_path, size_t existing_relative_size,
	char *existing_dest_path, size_t existing_dest_size)
{
	static const char *extensions[] = { "dds", "tga", "png", "jpeg", "jpg", "pcx" };
	char relative_base[MAX_OSPATH];
	int i;

	if (!relative_path || !extension || !existing_relative_path || existing_relative_size == 0 ||
		!existing_dest_path || existing_dest_size == 0)
		return false;
	COM_StripExtension(relative_path, relative_base, sizeof(relative_base));

	for (i = 0; i < (int)Q_COUNTOF(extensions); ++i)
	{
		int result;

		if (!q_strcasecmp(extension, extensions[i]))
			continue;
		result = q_snprintf(existing_relative_path, existing_relative_size, "%s.%s",
			relative_base, extensions[i]);
		if (result < 0 || (size_t)result >= existing_relative_size)
			continue;
		result = q_snprintf(existing_dest_path, existing_dest_size, "%s/%s",
			com_gamedir, existing_relative_path);
		if (result < 0 || (size_t)result >= existing_dest_size)
			continue;
		IN_NormalizeDroppedPath(existing_dest_path);
		if (Sys_FileType(existing_dest_path) & FS_ENT_FILE)
			return true;
	}

	return false;
}

static qboolean IN_ProcessSkyboxFiles(char **paths, int path_count, const char *batch_desc, const char *file_desc, qboolean *installed_any)
{
	int group_index;
	int face_index;
	int groups = 0;
	int ready_faces = 0;
	qboolean found_skybox = false;

	if (!paths || path_count <= 0)
		return false;
	if (installed_any)
		*installed_any = false;
	if (!batch_desc || !*batch_desc)
		batch_desc = "Skybox";
	if (!file_desc || !*file_desc)
		file_desc = "skybox face";

	for (group_index = 0; group_index < path_count; ++group_index)
	{
		char normalized_path[MAX_OSPATH];
		char directory[MAX_OSPATH];
		char skyname[MAX_OSPATH];
		char source_path[MAX_OSPATH];
		char source_paths[IN_SKYBOX_FACE_COUNT][MAX_OSPATH];
		char relative_path[MAX_OSPATH];
		char dest_path[MAX_OSPATH];
		qboolean preferred_underscore;
		int source_count = 0;
		int face_count = 0;

		if (!paths[group_index] || !*paths[group_index])
			continue;
		if (!IN_ParseSkyboxImagePath(paths[group_index], skyname, sizeof(skyname),
			NULL, &preferred_underscore))
			continue;
		if (q_strlcpy(normalized_path, paths[group_index], sizeof(normalized_path)) >= sizeof(normalized_path))
			continue;
		IN_NormalizeDroppedPath(normalized_path);
		IN_GetSkyboxImageDirectory(normalized_path, directory, sizeof(directory));

		/* Process each pasted/source directory and skybox name only once. */
		{
			int previous;
			qboolean already_processed = false;
			for (previous = 0; previous < group_index; ++previous)
			{
				if (IN_SameSkyboxGroup(paths[previous], directory, skyname))
				{
					already_processed = true;
					break;
				}
			}
			if (already_processed)
				continue;
		}

		memset(source_paths, 0, sizeof(source_paths));
		for (face_index = 0; face_index < IN_SKYBOX_FACE_COUNT; ++face_index)
		{
			const char *batch_face;

			batch_face = IN_FindSkyboxFaceInBatch(paths, path_count, directory, skyname, face_index);
			if (batch_face)
			{
				if (q_strlcpy(source_path, batch_face, sizeof(source_path)) >= sizeof(source_path))
					continue;
				IN_NormalizeDroppedPath(source_path);
			}
			else if (!IN_FindAdjacentSkyboxFace(directory, skyname, face_index,
				preferred_underscore, source_path, sizeof(source_path)))
			{
				continue;
			}
			q_strlcpy(source_paths[face_index], source_path, sizeof(source_paths[face_index]));
			++source_count;
		}

		/* A lone image with a coincidental face suffix is not enough to
		 * identify a skybox.  A complete set is required before installing. */
		if (source_count < 2)
			continue;

		found_skybox = true;
		++groups;
		if (source_count != IN_SKYBOX_FACE_COUNT)
		{
			Con_Printf("%s skybox %s incomplete: %d/%d faces found; not installed.\n",
				batch_desc, skyname, source_count, IN_SKYBOX_FACE_COUNT);
			continue;
		}

		for (face_index = 0; face_index < IN_SKYBOX_FACE_COUNT; ++face_index)
		{
			const char *extension;
			char existing_relative_path[MAX_OSPATH];
			char existing_dest_path[MAX_OSPATH];
			qboolean copied;

			extension = IN_SkyboxImageExtension(COM_FileGetExtension(source_paths[face_index]));
			if (!extension)
				continue;
			if (q_snprintf(relative_path, sizeof(relative_path), "gfx/env/%s%s.%s",
				skyname, in_skybox_suffixes[face_index], extension) < 0 ||
				strlen(relative_path) >= sizeof(relative_path))
			{
				Con_Printf("Path too long for %s.\n", file_desc);
				continue;
			}
			if (IN_FindExistingSkyboxFace(relative_path, extension,
				existing_relative_path, sizeof(existing_relative_path),
				existing_dest_path, sizeof(existing_dest_path)))
			{
				Con_SafePrintf("Using existing skybox face at ");
				Con_LinkPrintf(existing_dest_path, "%s/%s", COM_SkipPath(com_gamedir), existing_relative_path);
				Con_SafePrintf("\n");
				++face_count;
				continue;
			}
			if (q_snprintf(dest_path, sizeof(dest_path), "%s/%s", com_gamedir, relative_path) < 0 ||
				strlen(dest_path) >= sizeof(dest_path))
			{
				Con_Printf("Path too long for %s.\n", file_desc);
				continue;
			}
			IN_NormalizeDroppedPath(dest_path);
			copied = IN_CopyExternalFileToMapPath(source_paths[face_index], file_desc, relative_path, dest_path, NULL);
			if (copied)
				++face_count;
		}

		ready_faces += face_count;
		if (face_count > 0 && installed_any)
			*installed_any = true;
		Con_Printf("%s skybox %s: %d/%d faces installed%s.\n", batch_desc, skyname,
			face_count, IN_SKYBOX_FACE_COUNT,
			face_count == IN_SKYBOX_FACE_COUNT ? "" : "; one or more faces could not be copied");
	}

	if (found_skybox && groups > 1)
		Con_Printf("%s skyboxes: %d groups, %d faces ready.\n", batch_desc, groups, ready_faces);
	return found_skybox;
}

static qboolean IN_ProcessExternalFiles(char **paths, int count, const char *batch_desc, const char *file_desc, const char *bsp_desc, const char *lit_desc, const char *ent_desc, const char *vis_desc)
{
	qboolean handled = false;
	qboolean handled_maps;
	qboolean handled_skyboxes;
	qboolean skybox_installed = false;
	qboolean installed_any = false;
	qboolean map_installed = false;
	qboolean suppress_demo_autoplay_for_match;
	qboolean suppress_demo_autoplay;
	qboolean suppressed_demo_autoplay = false;
	int demo_count;
	int i;

	if (!paths || count <= 0)
		return false;

	demo_count = IN_CountDemoExternalFiles(paths, count);
	suppress_demo_autoplay_for_match = (demo_count > 0 && IN_IsActiveMatchParticipant());
	suppress_demo_autoplay = (demo_count > 1 || suppress_demo_autoplay_for_match);

	handled_maps = IN_HasMapAssetFiles(paths, count) &&
		IN_InstallMapFiles(paths, count, batch_desc, file_desc, bsp_desc, lit_desc, ent_desc, vis_desc, &map_installed);
	if (handled_maps)
		handled = true;
	handled_skyboxes = IN_ProcessSkyboxFiles(paths, count, batch_desc, file_desc, &skybox_installed);
	if (handled_skyboxes)
		handled = true;
	if (skybox_installed)
		installed_any = true;

	for (i = 0; i < count; ++i)
	{
		if (!paths[i])
			continue;
		if ((handled_maps && !IN_IsLegacyExternalFile(paths[i])) ||
			(handled_skyboxes && IN_IsSkyboxImageFile(paths[i])))
			continue;
		if (IN_InstallExternalFile(paths[i], file_desc, suppress_demo_autoplay))
		{
			handled = true;
			installed_any = true;
			if (suppress_demo_autoplay && IN_IsDemoExternalFile(paths[i]))
				suppressed_demo_autoplay = true;
		}
	}

	if (suppressed_demo_autoplay)
	{
		if (suppress_demo_autoplay_for_match)
			Con_Printf("Demo imported; not auto-playing while you are an active match participant.\n");
		else
			Con_Printf("Multiple demos imported; not auto-playing.\n");
	}

	if (map_installed || installed_any)
		IN_PlayCopySound();

	return handled;
}

qboolean IN_PasteClipboardFile (void)
{
	char	**clipboard_files;
	int	count = 0;
	qboolean handled = false;

	clipboard_files = PL_GetClipboardFilePaths(&count);
	if (!clipboard_files || count <= 0)
	{
		PL_FreeClipboardFilePaths(clipboard_files, count);
		return false;
	}

	handled = IN_ProcessExternalFiles(clipboard_files, count, "Clipboard", "clipboard file",
		"clipboard .bsp", "clipboard .lit", "clipboard .ent", "clipboard .vis");

	PL_FreeClipboardFilePaths(clipboard_files, count);
	return handled;
}

typedef struct
{
	char	**paths;
	int	count;
	int	capacity;
	qboolean	active;
} in_drop_batch_t;

static in_drop_batch_t in_drop_batch;

static void IN_ClearDropBatch(void)
{
	int i;

	if (in_drop_batch.paths)
	{
		for (i = 0; i < in_drop_batch.count; ++i)
		{
			if (in_drop_batch.paths[i])
				Z_Free(in_drop_batch.paths[i]);
		}
		Z_Free(in_drop_batch.paths);
	}

	in_drop_batch.paths = NULL;
	in_drop_batch.count = 0;
	in_drop_batch.capacity = 0;
	in_drop_batch.active = false;
}

static qboolean IN_AddDropBatchFile(const char *path)
{
	char	**new_paths;
	char	*copy;
	size_t	len;
	size_t	needed;
	size_t	max_paths;
	int	new_capacity;

	if (!path || !*path)
		return false;

	needed = (size_t)in_drop_batch.count + 1;
	max_paths = (size_t)Q_MAXINT / sizeof(*new_paths);
	if (needed > max_paths)
	{
		Con_Printf("Too many dropped files.\n");
		return false;
	}

	if (in_drop_batch.count >= in_drop_batch.capacity)
	{
		new_capacity = in_drop_batch.capacity > 0 ? in_drop_batch.capacity : 4;
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
		if (in_drop_batch.paths)
		{
			memcpy(new_paths, in_drop_batch.paths, in_drop_batch.count * sizeof(*new_paths));
			Z_Free(in_drop_batch.paths);
		}
		in_drop_batch.paths = new_paths;
		in_drop_batch.capacity = new_capacity;
	}

	len = strlen(path) + 1;
	if (len > (size_t)Q_MAXINT)
	{
		Con_Printf("Skipping dropped file with too-long path.\n");
		return false;
	}

	copy = (char *) Z_Malloc((int)len);
	q_strlcpy(copy, path, len);

	in_drop_batch.paths[in_drop_batch.count] = copy;
	++in_drop_batch.count;
	return true;
}

static qboolean IN_ProcessDroppedFiles(char **paths, int count)
{
	return IN_ProcessExternalFiles(paths, count, "Dropped", "dropped file",
		"dropped .bsp", "dropped .lit", "dropped .ent", "dropped .vis");
}

static qboolean IN_FinishDropBatch(void)
{
	qboolean handled;

	handled = IN_ProcessDroppedFiles(in_drop_batch.paths, in_drop_batch.count);
	IN_ClearDropBatch();
	return handled;
}
#else
qboolean IN_PasteClipboardFile (void)
{
	return false;
}
#endif

void IN_SendKeyEvents (void)
{
	SDL_Event event;
	int key;
	qboolean down;

	char afktype[4];
	sprintf(afktype, "%s", "AFK");

	if (is_long_pressing && !long_press_triggered && cl.modtype == 1 && cl.eyecam) // woods #eyemouse
	{
		Uint32 current_time = SDL_GetTicks();
		if (current_time - press_start_time >= LONG_PRESS_TIME)
		{
			long_press_triggered = true;  // Prevent multiple triggers
			Cbuf_AddText("+showscores\n");
		}
	}

	if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
	{
		if (cl.modtype == 1)
		{
			char qfAFK[4] = { 193, 198, 203, '\0' }; // woods -- quake font red 'AFK'
			sprintf(afktype, "%s", qfAFK);
		}
	}

	IN_UpdateGrabs();

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
#if defined(USE_SDL2)
		case SDL_WINDOWEVENT:

			if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
			{
				Sys_ClearDockNotificationBadge();
#if defined(_WIN32) // woods #disablecaps via ironwail
				Sys_ActivateKeyFilter(true);
#endif
				//S_UnblockSound();
				windowhasfocus=true;
				VID_Gamma_Reapply();
				BGM_Resume(); // woods #usermute  - music
				if (!strcmp(mute, "y")) // woods #usermute
					Sound_Toggle_Mute_On_f(); // woods #mute -- adapted from Fitzquake Mark V
				else
					Sound_Toggle_Mute_Off_f();

#ifdef MACOS_X_ACCELERATION_HACK
				/* Re-disable acceleration when returning to game */
				if (in_disablemacosxmouseaccel.value == 1)
				{
					Con_DPrintf("Focus gained: re-disabling mouse acceleration (originalMouseSpeed=%g)\n", originalMouseSpeed);
					IN_RefreshOriginalAccel();
					if (originalMouseSpeed != -1)
					{
						IN_DisableOSXMouseAccelOnly();
					}
					else
					{
						Con_DPrintf("Focus gained: Cannot disable - originalMouseSpeed is -1\n");
					}
				}
				else
				{
					Con_DPrintf("Focus gained: in_disablemacosxmouseaccel is disabled\n");
				}
#endif

				if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
				{
					if (cl.modtype == 1 || cl.modtype == 4)
						Cmd_ExecuteString("cmd afkoff", src_command); // afk

					SetChatInfo (0); // woods #chatinfo
				}

				if (cl_afk.value) // woods #smartafk
				{
					if (strlen(afk_name) > 1) // intiate only if a AFK event has occured
						Cvar_Set("name", afk_name);
				}

				// be polite during matches (only) and let teammates know you have alt-tabbed
				if (cl_bottomcolor.value != 0 && cl.notobserver && cl.matchinp && cl.teamcolor[0] && !IsOneVsOneMatch())
					Cmd_ExecuteString("say_team \"back from alt-tab\"", src_command);
			}

			else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			{
#if defined(_WIN32) // woods #disablecaps via ironwail
				Sys_ActivateKeyFilter(false);
#endif
				//S_BlockSound();
				windowhasfocus=false;
				if (CL_DemoScrubActive())
					CL_DemoScrub_Cancel();
				IN_DemoScrubSetHover(false);
				IN_UpdateGrabs();
				BGM_Pause(); // woods #usermute - music
				Sound_Toggle_Mute_On_f(); // woods #mute -- adapted from Fitzquake Mark V

#ifdef MACOS_X_ACCELERATION_HACK
				/* NEW: Force restore on focus lost to avoid timing issues */
				if (originalMouseSpeed != -1)
				{
					Con_DPrintf("Focus lost: calling IN_ReenableOSXMouseAccel (originalMouseSpeed=%g)\n", originalMouseSpeed);
					IN_ReenableOSXMouseAccelForFocus();
				}
				else
				{
					Con_DPrintf("Focus lost: acceleration was not disabled (originalMouseSpeed=-1)\n");
				}
#endif

				if ((cl.gametype == GAME_DEATHMATCH) && (cls.state == ca_connected))
				{
					if (cl.modtype == 1 || cl.modtype == 4) // woods if afk is NO
						Cmd_ExecuteString("cmd afkon", src_command); // afk

					SetChatInfo (CIF_AFK); // woods #chatinfo
				}
				if (cl_afk.value) // woods #smartafk
				{
					const char *afk_marker = strstr(cl_name.string, afktype);

					if (!afk_marker) // initiate AFK-in-name if AFK not already in the name
					{
						q_strlcpy(afk_name, cl_name.string, sizeof(afk_name)); // store name to memory
						sprintf(normalname, "%.11s", cl_name.string); // cut name
						sprintf(normalname2, "%s %s", normalname, afktype); // add AFK to name
						Cvar_Set("name", normalname2); // set name with AFK
						Host_Name_Backup_f(); // back up the full name incase of crash
					}
					else if (!afk_name[0]) // recover the base name from an existing/crash-restored AFK name
					{
						size_t base_length = afk_marker - cl_name.string;

						while (base_length && cl_name.string[base_length - 1] == ' ')
							base_length--;
						base_length = q_min(base_length, sizeof(afk_name) - 1);
						memcpy(afk_name, cl_name.string, base_length);
						afk_name[base_length] = '\0';
					}
				}

				// be polite during matches (only) and let teammates know you have alt-tabbed
				if (cl_bottomcolor.value != 0 && cl.notobserver && cl.matchinp && cl.teamcolor[0] && !IsOneVsOneMatch())
					Cmd_ExecuteString("say_team alt-tabbed", src_command);
			}

			else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				VID_OnResize (event.window.data1, event.window.data2); // github.com/andrei-drexler/ironwail (Enable resizing)
			}
			else if (event.window.event == SDL_WINDOWEVENT_RESTORED ||
				event.window.event == SDL_WINDOWEVENT_SHOWN ||
#if SDL_VERSION_ATLEAST(2, 0, 18)
				event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED || // ramp was applied to the old display
#endif
				event.window.event == SDL_WINDOWEVENT_EXPOSED)
			{
				VID_Gamma_Reapply();
			}
			else if (event.window.event == SDL_WINDOWEVENT_ENTER)
			{
				IN_DemoScrubSeedHover();
				IN_UpdateGrabs();
			}
			break;
#else
		case SDL_ACTIVEEVENT:
			if (event.active.state & (SDL_APPINPUTFOCUS|SDL_APPACTIVE))
			{
				if (event.active.gain)
				{
					windowhasfocus = true;
					S_UnblockSound();
				}
				else
				{
					windowhasfocus = false;
					S_BlockSound();
				}
			}
			break;
#endif
#if defined(USE_SDL2)
		case SDL_TEXTINPUT:
			lastactivetype = KD_KEYBOARD;
			if (in_debugkeys.value)
				IN_DebugTextEvent(&event);

		// SDL2: We use SDL_TEXTINPUT for typing in the console / chat.
		// SDL2 uses the local keyboard layout and handles modifiers
		// (shift for uppercase, etc.) for us.
			{
				unsigned char *ch;
				for (ch = (unsigned char *)event.text.text; *ch; ch++)
					if ((*ch & ~0x7F) == 0)
						Char_Event (*ch);
			}
			break;
#endif
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			down = (event.key.state == SDL_PRESSED);
			lastactivetype = KD_KEYBOARD;

			if (in_debugkeys.value)
				IN_DebugKeyEvent(&event);

#if defined(USE_SDL2)
		// SDL2: we interpret the keyboard as the US layout, so keybindings
		// are based on key position, not the label on the key cap.
			key = IN_SDL2_ScancodeToQuakeKey(event.key.keysym.scancode);
#else
			key = IN_SDL_KeysymToQuakeKey(event.key.keysym.sym);
#endif

			if (Wheel_IsOpen () && down && key == K_ESCAPE && key_dest == key_game)
			{
				Wheel_Cancel ();
				break;
			}

		// also pass along the underlying keycode using the proper current layout for Y/N prompts.
			Key_EventWithKeycode (key, down, event.key.keysym.sym);

#if !defined(USE_SDL2)
			if (down && (event.key.keysym.unicode & ~0x7F) == 0)
				Char_Event (event.key.keysym.unicode);
#endif
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			if (event.button.button < 1 ||
				event.button.button > sizeof(buttonremap) / sizeof(buttonremap[0]))
			{
				Con_Printf ("Ignored event for mouse button %d\n",
					event.button.button);
				break;
			}

			if (IN_DemoScrubHandleButton(&event))
			{
				lastactivetype = KD_MOUSE;
				break;
			}

			if (event.button.button == SDL_BUTTON_RIGHT && wheel_block_mouse2)
			{
				lastactivetype = KD_MOUSE;
				if (event.button.state == SDL_RELEASED)
					wheel_block_mouse2 = false;
				break;
			}

			if (Wheel_IsOpen () &&
				event.button.state == SDL_PRESSED &&
				event.button.button == SDL_BUTTON_RIGHT &&
				key_dest == key_game)
			{
				lastactivetype = KD_MOUSE;
				Wheel_Cancel ();
				wheel_block_mouse2 = true;
				break;
			}

			// Handle eyecam observer mode #eyemouse
			if (key_dest == key_game && cl.modtype == 1 && cl.eyecam)
			{
				IN_HandleObserverMouseEvents(&event);

				// Always send button release events
				// even in eyecam mode to ensure buttons don't get stuck
				if (event.button.state == SDL_RELEASED)
				{
					Key_Event(buttonremap[event.button.button - 1], false);
				}
				break;
			}

				if (event.button.state == SDL_PRESSED && // woods #pong
					event.button.button == SDL_BUTTON_LEFT &&
					Pong_Enabled() && !cls.demoplayback &&
					(cl.paused || cl.match_pause_time) &&
					key_dest == key_game)
				 {
				Pong_ToggleFreeze();
				break;  /* consume the click */
				}

			if (key_dest == key_menu) // woods #mousemenu
				M_Mousemove(event.button.x, event.button.y);
			lastactivetype = KD_MOUSE;
			Key_Event(buttonremap[event.button.button - 1], event.button.state == SDL_PRESSED);
			break;

#if defined(USE_SDL2)
		case SDL_MOUSEWHEEL:
			lastactivetype = KD_MOUSE;
			if (IN_DemoScrubHandleWheel(&event))
				break;
			if (Wheel_IsOpen ())
			{
				int steps = event.wheel.y;

				while (steps > 0)
				{
					Wheel_ScrollSelection (-1);
					steps--;
				}
				while (steps < 0)
				{
					Wheel_ScrollSelection (1);
					steps++;
				}
				break;
			}
			IN_EmitWheelKeySteps(event.wheel.y);
			break;
#endif

		case SDL_MOUSEMOTION:
			lastactivetype = KD_MOUSE;
			if (IN_DemoScrubHandleMotion(&event))
				break;
			if (key_dest == key_menu) // woods #mousemenu
			{
				M_Mousemove(event.button.x, event.button.y);
			}
			if (!(key_dest == key_game && cl.modtype == 1 && cl.eyecam)) // woods #eyemouse
			{
				IN_MouseMotion(event.motion.xrel, event.motion.yrel, event.motion.x, event.motion.y);
			}
			else if (key_dest == key_game && CL_IsActiveObserver()) // woods #eyemouse
			{
				// Update the cursor idle time on mouse movement in observer mode
				obs_cursor_last_move = SDL_GetTicks();
				if (obs_cursor_hidden) {
					SDL_ShowCursor(SDL_ENABLE);
					obs_cursor_hidden = false;
					IN_UpdateGrabs(); // Refresh grabs to ensure cursor is visible
				}
			}
			break;

#if defined(USE_SDL2)
		case SDL_JOYDEVICEADDED:
			// Raw joysticks share SDL's device-index namespace with gamepads.
			// Keep the selected gamepad index synchronized even when the changed
			// device itself has no controller mapping.
			if (joy_active_controller)
				IN_RemapJoystick();
			break;
		case SDL_JOYDEVICEREMOVED:
			if (joy_active_controller && !IN_RemapJoystick())
				IN_SetupJoystick();
			break;
#if SDL_VERSION_ATLEAST(2, 0, 14)
		case SDL_CONTROLLERTOUCHPADDOWN:
		case SDL_CONTROLLERTOUCHPADMOTION:
			if (event.ctouchpad.which == joy_active_instanceid &&
				event.ctouchpad.touchpad == 0 && event.ctouchpad.finger == 0 &&
				joy_enable.value && joy_touchpad.value && key_dest == key_menu)
			{
				SDL_Window *window = (SDL_Window *)VID_GetWindow();
				int width = 0, height = 0;

				if (window)
					SDL_GetWindowSize(window, &width, &height);
				if (width > 0 && height > 0)
				{
					M_Mousemove((int)(CLAMP(0.f, event.ctouchpad.x, 1.f) * (width - 1)),
						(int)(CLAMP(0.f, event.ctouchpad.y, 1.f) * (height - 1)));
					lastactivetype = KD_GAMEPAD;
				}
			}
			break;
		case SDL_CONTROLLERTOUCHPADUP:
			break;
		case SDL_CONTROLLERSENSORUPDATE:
			if (event.csensor.sensor == SDL_SENSOR_GYRO && event.csensor.which == joy_active_instanceid)
			{
				float prev_yaw = gyro_yaw;
				float prev_pitch = gyro_pitch;

				if (IN_UpdateGyroCalibration(event.csensor.data))
					break;

				if (!gyro_turning_axis.value)
					gyro_yaw = event.csensor.data[1] - gyro_calibration_y.value;
				else
					gyro_yaw = -(event.csensor.data[2] - gyro_calibration_z.value);
				gyro_pitch = event.csensor.data[0] - gyro_calibration_x.value;
				gyro_raw_mag = RAD2DEG(sqrt(gyro_yaw * gyro_yaw + gyro_pitch * gyro_pitch));
				gyro_yaw = IN_FilterGyroSample(prev_yaw, gyro_yaw);
				gyro_pitch = IN_FilterGyroSample(prev_pitch, gyro_pitch);
			}
			break;
#endif
#if SDL_VERSION_ATLEAST(2, 24, 0)
		case SDL_JOYBATTERYUPDATED:
			if (event.jbattery.which == joy_active_instanceid)
				IN_UpdateGamepadPower(event.jbattery.level, joy_enable.value != 0.f);
			break;
#endif
		case SDL_CONTROLLERDEVICEADDED:
			if (!IN_RemapJoystick() && (int)joy_device.value >= 0)
				IN_SetupJoystick();
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			if (!IN_RemapJoystick())
				IN_SetupJoystick();
			break;
		case SDL_CONTROLLERDEVICEREMAPPED:
			if (!IN_RemapJoystick())
				IN_SetupJoystick();
			else if (event.cdevice.which == joy_active_instanceid)
				IN_RefreshActiveControllerInfo();
			break;
#if SDL_VERSION_ATLEAST(2, 30, 0)
		case SDL_CONTROLLERSTEAMHANDLEUPDATED:
			if (event.cdevice.which == joy_active_instanceid)
				IN_RefreshActiveControllerInfo();
			break;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 5)
		case SDL_DROPBEGIN:
			IN_ClearDropBatch();
			in_drop_batch.active = true;
			break;
		case SDL_DROPCOMPLETE:
			if (in_drop_batch.active)
				IN_FinishDropBatch();
			else
				IN_ClearDropBatch();
			break;
#endif
		case SDL_DROPFILE:
		{
			char	*dropped_file;

			dropped_file = event.drop.file;
			if (dropped_file)
			{
#if SDL_VERSION_ATLEAST(2, 0, 5)
				if (in_drop_batch.active)
				{
					IN_AddDropBatchFile(dropped_file);
				}
				else
#endif
				{
					char *single_drop[1];

					single_drop[0] = dropped_file;
					IN_ProcessDroppedFiles(single_drop, 1);
				}
				SDL_free(dropped_file);
				event.drop.file = NULL;
			}
		}
		break;
#endif

		case SDL_QUIT:
			Con_DPrintf("SDL_QUIT event received\n");
			Host_Quit_f ();
			break;

		default:
			break;
		}
	}

	IN_DemoScrubRefreshCursor();

	if (key_dest == key_game && CL_IsActiveObserver()) // woods -- observer cursor auto-hide #eyemouse
	{
		Uint32 now = SDL_GetTicks();
		if (!obs_cursor_hidden && now - obs_cursor_last_move >= OBS_CURSOR_IDLE_MS)
		{
			IN_UpdateGrabs(); // Refresh grabs to ensure the mouse stays in free-mode
			SDL_ShowCursor(SDL_DISABLE);
			obs_cursor_hidden = true;
			
			
		}
	}
	else if (obs_cursor_hidden && (!CL_IsActiveObserver() || key_dest != key_game))
	{
		// If we leave observer mode while cursor is hidden, reset state
		IN_UpdateGrabs();
		SDL_ShowCursor(SDL_ENABLE);
		obs_cursor_hidden = false;
		
	}

	static qboolean was_in_eyecam = false; // woods #eyemouse
	if (was_in_eyecam && !(cl.modtype == 1 && cl.eyecam))
	{
		// We just exited eyecam mode, make sure mouse buttons are released
		if (is_long_pressing)
		{
			if (long_press_triggered)
				Cbuf_AddText("-showscores\n");

			is_long_pressing = false;
			long_press_triggered = false;
		}
	}
	was_in_eyecam = (cl.modtype == 1 && cl.eyecam);
}
