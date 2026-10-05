"""Exercise deferred DirectInput discovery and selection using production functions.

SDL inventories are injected at the device boundary; no physical controller is
required. The watcher checks use fake Win32 message/timer boundaries as well.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
input_source = (ROOT / 'Quake/in_sdl.c').read_text()
system_source = (ROOT / 'Quake/sys_sdl_win.c').read_text()

def function(text, declaration):
    start = text.index(declaration + '\n{')
    return text[start:text.index('\n}', start) + 2] + '\n'

source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _WIN32 1
#define true 1
#define false 0
#define countof(a) (sizeof(a)/sizeof((a)[0]))
#define CLAMP(a,b,c) ((b)<(a)?(a):((b)>(c)?(c):(b)))
#define MAX_OSPATH 256
#define SDL_INIT_GAMEPAD 1
#define SDL_HINT_JOYSTICK_DIRECTINPUT "directinput"
#define SDL_HINT_DEFAULT 0
#define SDL_SENSOR_GYRO 0
typedef int qboolean;
typedef uint16_t Uint16;
typedef uint64_t Uint64;
typedef int SDL_JoystickID;
typedef int SDL_Gamepad;
typedef int SDL_Joystick;
typedef struct { unsigned char data[16]; } SDL_GUID;
typedef struct { float value; } cvar_t;
typedef struct { int id, vendor, product, mapped; const char *path; SDL_GUID guid; } Pad;
static Pad before[20], after[20], *pads;
static int n_before, n_after, n_pads, restarts, scan_count, hint_override, env_disabled;
static int start_failures, initialized, watch_stops;
static unsigned int scan_ids[16], scan_generation;
static qboolean joy_directinput, joy_directinput_pending;
static unsigned int joy_directinput_scan;
static Uint64 joy_directinput_since;
static SDL_Gamepad *joy_active_controller;
static SDL_JoystickID joy_active_instanceid;
static int joy_active_device, gyro_present;
static const char *joy_active_name = "test";
static cvar_t joy_device;
static Pad *pad(int id) { for(int i=0;i<n_pads;i++) if(pads[i].id==id) return &pads[i]; return NULL; }
static const char *SDL_getenv(const char *name) { (void)name; return env_disabled ? "0" : NULL; }
static int SDL_WasInit(int flags) { return initialized ? flags : 0; }
static void SDL_QuitSubSystem(int flags) { (void)flags; initialized=0; }
static void Sys_StopDirectInputWatch(void) { watch_stops++; }
static Uint64 SDL_GetTicks(void) { return 1000; }
static int Sys_DirectInputControllers(unsigned int *ids, int max, unsigned int *gen) {
    *gen = scan_generation; if(scan_count>0) memcpy(ids,scan_ids,(scan_count<max?scan_count:max)*sizeof(*ids)); return scan_count;
}
static SDL_JoystickID *SDL_GetJoysticks(int *count) {
    SDL_JoystickID *ids=malloc((n_pads+1)*sizeof(*ids)); *count=n_pads;
    for(int i=0;i<n_pads;i++) ids[i]=pads[i].id; ids[n_pads]=0; return ids;
}
static Uint16 SDL_GetJoystickVendorForID(int id) { return (Uint16)pad(id)->vendor; }
static Uint16 SDL_GetJoystickProductForID(int id) { return (Uint16)pad(id)->product; }
static SDL_GUID SDL_GetJoystickGUIDForID(int id) { return pad(id)->guid; }
static const char *SDL_GetJoystickPathForID(int id) { return pad(id)->path; }
static int SDL_IsGamepad(int id) { return pad(id)->mapped; }
static void SDL_free(void *p) { free(p); }
static int SDL_SetHintWithPriority(const char *name, const char *value, int priority) {
    (void)name; (void)value; (void)priority; return !hint_override;
}
static int SDL_GetHintBoolean(const char *name, int fallback) { (void)name; (void)fallback; return !hint_override; }
static void Cvar_SetValueQuick(cvar_t *v, int i) { v->value=(float)i; }
static void q_strlcpy(char *dst, const char *src, size_t n) { snprintf(dst,n,"%s",src); }
static int q_strcasecmp(const char *a, const char *b) { return strcmp(a,b); }
#define Con_DPrintf(...) ((void)0)
#define Con_Printf(...) ((void)0)
#define Con_Warning(...) ((void)0)
static int IN_DirectInputIgnored(unsigned int id) { return id == 0x99998888; }
static void IN_CloseActiveController(int announce) { (void)announce; joy_active_controller=NULL; joy_active_instanceid=0; joy_active_device=-1; }
static void IN_ShutdownJoystick(void) { IN_CloseActiveController(0); }
static int IN_StartJoystick(int select) {
    (void)select; restarts++; if(start_failures) { start_failures--; return 0; }
    initialized=1; pads=joy_directinput ? after : before; n_pads=joy_directinput ? n_after : n_before;
    joy_directinput_pending=!joy_directinput; joy_directinput_scan=0; return 1;
}
static int IN_GetJoystickCount(void) { return n_pads; }
static int IN_JoystickIDAt(int index) { return index>=0 && index<n_pads ? pads[index].id : 0; }
static int IN_IsGamepadAt(int index) { int id=IN_JoystickIDAt(index); return id && SDL_IsGamepad(id); }
static const char *SDL_GetError(void) { return "test"; }
static int SDL_GamepadConnected(SDL_Gamepad *gamepad) { return pad(*gamepad)!=NULL; }
static void IN_ResetJoystickState(void) {}
static const char *SDL_GetJoystickNameForID(int id) { (void)id; return "test"; }
static SDL_Gamepad *SDL_OpenGamepad(int id) { return &pad(id)->id; }
static SDL_Joystick *SDL_GetGamepadJoystick(SDL_Gamepad *p) { return p; }
static int SDL_GetJoystickID(SDL_Joystick *p) { return *p; }
static void IN_RefreshActiveControllerInfo(void) {}
static float SDL_GetGamepadSensorDataRate(SDL_Gamepad *p, int sensor) { (void)p; (void)sensor; return 0; }
static void IN_SetupJoystick(void);
'''
if 'static SDL_GUID IN_DirectInputIdentity' in input_source:
    source += function(input_source, 'static SDL_GUID IN_DirectInputIdentity (SDL_GUID guid)')
source += function(input_source, 'static qboolean IN_UseController(int device_index)')
source += function(input_source, 'static void IN_SetupJoystick(void)')
source += function(input_source, 'static void IN_CheckDirectInputControllers(void)')
source += r'''
static Pad device(int id, int mapped, int signature, const char *path) {
    Pad p={0}; p.id=id; p.vendor=0x1234; p.product=0x5678; p.mapped=mapped; p.path=path;
    p.guid.data[0]=3; p.guid.data[4]=0x34; p.guid.data[5]=0x12;
    p.guid.data[8]=0x78; p.guid.data[9]=0x56; p.guid.data[14]=(unsigned char)signature;
    if(signature=='w') p.guid.data[15]=7; return p;
}
static void reset(void) {
    memset(before,0,sizeof(before)); memset(after,0,sizeof(after));
    n_before=n_after=n_pads=restarts=hint_override=env_disabled=0;
    start_failures=watch_stops=0; initialized=1;
    pads=before; scan_count=1; scan_ids[0]=0x56781234; scan_generation=1;
    joy_directinput_scan=0; joy_directinput_since=0; joy_directinput=joy_directinput_pending=0;
    IN_CloseActiveController(0); joy_device.value=0;
}
int main(void) {
    /* An unmapped WGI joystick must not hide a mapped DirectInput controller. */
    reset(); before[0]=device(1,0,'w',NULL); after[0]=device(2,1,0,"di"); n_pads=n_before=n_after=1;
    IN_CheckDirectInputControllers(); assert(restarts==1 && joy_active_instanceid==2);
    /* Even a mapped WGI view can lack the user's DirectInput-specific mapping. */
    reset(); before[0]=device(1,1,'w',NULL); after[0]=device(2,1,0,"di"); n_pads=n_before=n_after=1;
    IN_CheckDirectInputControllers(); assert(restarts==1);
    /* HIDAPI already handles this device; leave SDL running. */
    reset(); before[0]=device(1,1,'h',"hid"); n_pads=1;
    IN_CheckDirectInputControllers(); assert(restarts==0);
    /* Two physical pads require two equivalent usable SDL devices. */
    reset(); before[0]=device(1,1,'h',"hid"); after[0]=before[0]; after[1]=device(2,1,0,"di");
    n_pads=1; n_after=scan_count=2; scan_ids[1]=scan_ids[0];
    IN_CheckDirectInputControllers(); assert(restarts==1);
    /* A pending saved index must survive temporary startup inventory. */
    reset(); before[0]=device(1,1,'h',"hid"); after[0]=before[0]; after[1]=device(2,1,0,"di");
    n_pads=1; n_after=scan_count=2; scan_ids[1]=scan_ids[0]; joy_device.value=1; joy_directinput_pending=1;
    IN_SetupJoystick(); assert(joy_active_instanceid==1 && joy_device.value==1);
    IN_CheckDirectInputControllers(); assert(joy_active_instanceid==2 && joy_device.value==1);
    /* Complete discovery with no legacy device and settle normal selection. */
    reset(); before[0]=device(1,1,'h',"hid"); n_pads=1; scan_count=0;
    joy_device.value=5; joy_directinput_pending=1; IN_SetupJoystick();
    IN_CheckDirectInputControllers(); assert(!joy_directinput_pending && joy_device.value==0);
    /* Hotplug restart preserves a selected WGI pad across its DI GUID change. */
    reset(); before[0]=device(1,1,'w',NULL); before[1]=device(3,1,'h',"other"); n_pads=2;
    after[0]=before[1]; after[1]=device(2,1,0,"di"); n_after=scan_count=2; scan_ids[1]=scan_ids[0];
    IN_SetupJoystick(); IN_CheckDirectInputControllers(); assert(joy_active_instanceid==2 && joy_device.value==1);
    /* Real WGI wireless GUIDs also differ in bus and display-name CRC. */
    reset(); before[0]=device(1,1,'w',NULL); before[0].guid.data[0]=5; before[0].guid.data[2]=0xAB;
    before[1]=device(3,1,'h',"other"); before[1].vendor=0x4321; n_pads=2;
    after[0]=before[1]; after[1]=device(2,1,0,"di"); after[1].guid.data[2]=0xCD; n_after=scan_count=2;
    scan_ids[1]=0x56784321;
    IN_SetupJoystick(); IN_CheckDirectInputControllers(); assert(joy_active_instanceid==2 && joy_device.value==1);
    /* Exact paths win when identical pads reorder. */
    reset(); before[0]=device(1,1,'h',"chosen"); n_pads=1;
    after[0]=device(2,1,'h',"other"); after[1]=device(3,1,'h',"chosen"); n_after=scan_count=2; scan_ids[1]=scan_ids[0];
    IN_SetupJoystick(); IN_CheckDirectInputControllers(); assert(joy_active_instanceid==3);
    /* Failed watcher requests ordinary SDL enumeration; explicit opt-outs win. */
    reset(); scan_count=-2; IN_CheckDirectInputControllers(); assert(restarts==1);
    reset(); hint_override=1; IN_CheckDirectInputControllers(); assert(restarts==0);
    scan_generation++; IN_CheckDirectInputControllers(); assert(restarts==0);
    reset(); env_disabled=1; IN_CheckDirectInputControllers(); assert(restarts==0);
    reset(); scan_ids[0]=0x99998888; IN_CheckDirectInputControllers(); assert(restarts==0);
    /* A transient failed DI initialization restores the previously working pad. */
    reset(); before[0]=device(1,1,'h',"chosen"); before[1]=device(2,0,'w',NULL);
    after[0]=device(3,1,0,"di"); after[1]=before[0]; n_pads=n_before=n_after=scan_count=2; scan_ids[1]=scan_ids[0];
    start_failures=1; IN_SetupJoystick(); IN_CheckDirectInputControllers();
    assert(restarts==2 && !joy_directinput && initialized && joy_active_instanceid==1 && !watch_stops);
    IN_CheckDirectInputControllers(); assert(restarts==2); /* no repeated restart for the failed scan */
    scan_generation++; IN_CheckDirectInputControllers();
    assert(restarts==3 && joy_directinput && joy_active_instanceid==1 && watch_stops==1);
    puts("startup controllers: PASS (backend coverage, duplicates, saved selection, identity, opt-outs, watcher failure, restart rollback)");
}
'''

watch = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define CALLBACK
#define TRUE 1
#define DIENUM_CONTINUE 1
#define DIENUM_STOP 0
#define DIDEVTYPE_HID 0x10000
#define DIWATCH_MAX_IDS 16
#define DIWATCH_RESCAN_TIMER 1
#define DIWATCH_RESCAN_DELAY 250
#define WM_DEVICECHANGE 1
#define DBT_DEVICEARRIVAL 2
#define WM_TIMER 3
typedef int qboolean;
typedef int BOOL;
typedef void *LPVOID;
typedef void *HWND;
typedef unsigned int UINT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef struct { unsigned int dwDevType; struct { unsigned int Data1; } guidProduct; wchar_t tszProductName[64]; } DIDEVICEINSTANCEW;
typedef const DIDEVICEINSTANCEW *LPCDIDEVICEINSTANCEW;
typedef struct { int count; unsigned int ids[16]; } diwatch_scan_t;
static struct { int lock, count; unsigned int generation; } diwatch;
static int timer_ok=1, scans;
static void EnterCriticalSection(int *p) { (void)p; }
static void LeaveCriticalSection(int *p) { (void)p; }
static int Sys_DIWatchIgnoredName(const wchar_t *name) { return !wcscmp(name,L"Mouse test"); }
static void Sys_DIWatchScan(void) { scans++; }
static int SetTimer(HWND h, int id, int delay, void *cb) { (void)h; (void)id; (void)delay; (void)cb; return timer_ok; }
static int KillTimer(HWND h, int id) { (void)h; (void)id; return 1; }
static LRESULT DefWindowProcW(HWND h, UINT m, WPARAM w, LPARAM l) { (void)h; (void)m; (void)w; (void)l; return 0; }
'''
if 'static void Sys_DIWatchFailed (void)' in system_source:
    watch += function(system_source, 'static void Sys_DIWatchFailed (void)')
watch += function(system_source, 'static BOOL CALLBACK Sys_DIWatchEnum (LPCDIDEVICEINSTANCEW instance, LPVOID context)')
watch += function(system_source, 'static LRESULT CALLBACK Sys_DIWatchWndProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)')
watch += r'''
int main(void) {
    diwatch_scan_t scan={0}; DIDEVICEINSTANCEW d={0}; d.dwDevType=DIDEVTYPE_HID;
    for(int i=0;i<16;i++) { d.guidProduct.Data1=(unsigned int)i; assert(Sys_DIWatchEnum(&d,&scan)==DIENUM_CONTINUE); }
    assert(scan.count==16);
    assert(Sys_DIWatchEnum(&d,&scan)==DIENUM_STOP && scan.count==-2);
    memset(&scan,0,sizeof(scan)); memcpy(d.tszProductName,L"Mouse test",sizeof(L"Mouse test"));
    assert(Sys_DIWatchEnum(&d,&scan)==DIENUM_CONTINUE && scan.count==0);
    Sys_DIWatchWndProc(NULL,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,0); assert(diwatch.generation==0 && scans==0);
    Sys_DIWatchWndProc(NULL,WM_TIMER,DIWATCH_RESCAN_TIMER,0); assert(scans==1);
    timer_ok=0; Sys_DIWatchWndProc(NULL,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,0);
    assert(diwatch.count==-2 && diwatch.generation==1);
    diwatch.generation=~0u; Sys_DIWatchWndProc(NULL,WM_DEVICECHANGE,DBT_DEVICEARRIVAL,0);
    assert(diwatch.generation==1);
    puts("DirectInput watcher: PASS (overflow, filters, deferred rescan, failed timer, generation rollover)");
}
'''

with tempfile.TemporaryDirectory(prefix='qssm-startup-controllers-') as tmp:
    for name, code in [('controllers', source), ('watcher', watch)]:
        path = Path(tmp)
        (path / (name + '.c')).write_text(code)
        binary = path / name
        compiler = os.environ.get('CC', 'cc')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            binary = binary.with_suffix('.exe')
            command = [compiler, '/nologo', '/std:c11', '/W3', '/fsanitize=address', '/Zi',
                '/Fe:' + str(binary), '/Fo:' + str(path / (name + '.obj')),
                '/Fd:' + str(path / (name + '.pdb')), str(path / (name + '.c'))]
        else:
            command = [compiler, '-std=c99', '-Wall', '-Wextra', '-Wno-unused-function',
                '-Wno-unused-variable', '-fsanitize=address,undefined', '-g',
                str(path / (name + '.c')), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
