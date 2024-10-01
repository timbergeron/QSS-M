/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2019 QuakeSpasm developers

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

#ifndef QUAKEDEFS_H
#define QUAKEDEFS_H

// quakedef.h -- primary header for client

#define	QUAKE_GAME		// as opposed to utilities

#define	VERSION			1.09
#define	GLQUAKE_VERSION		1.00
#define	D3DQUAKE_VERSION	0.01
#define	WINQUAKE_VERSION	0.996
#define	LINUX_VERSION		1.30
#define	X11_VERSION		1.10

#define	FITZQUAKE_VERSION	0.85	//johnfitz
#define	QUAKESPASM_VERSION	0.96
#define	QUAKESPASM_VER_PATCH	0	// helper to print a string like 0.94.7
#ifndef	QUAKESPASM_VER_SUFFIX
#define	QUAKESPASM_VER_SUFFIX		// optional version suffix string literal like "-beta1"
#endif

#define QSS_VER	"3-01-24" // woods

// woods add qss-m versions info

#define QSSM_VER_MAJOR		1
#define QSSM_VER_MINOR		6
#define QSSM_VER_PATCH		2
#ifndef QSSM_VER_SUFFIX
#define QSSM_VER_SUFFIX			// optional version suffix string literal like "-beta1"
#endif

#define	QS_STRINGIFY_(x)	#x
#define	QS_STRINGIFY(x)	QS_STRINGIFY_(x)

// combined version string like "0.92.1-beta1"
#define	QUAKESPASM_VER_STRING	QS_STRINGIFY(QUAKESPASM_VERSION) "." QS_STRINGIFY(QUAKESPASM_VER_PATCH)
#define	QSSM_VER_STRING		QS_STRINGIFY(QSSM_VER_MAJOR) "." QS_STRINGIFY(QSSM_VER_MINOR) "." QS_STRINGIFY(QSSM_VER_PATCH) QSSM_VER_SUFFIX

#ifdef QSS_DATE
	// combined version string like "2020-10-20-beta1"
	#define	ENGINE_NAME_AND_VER	"QSS " QS_STRINGIFY(QSS_DATE) QUAKESPASM_VER_SUFFIX
#else
	#define ENGINE_NAME_AND_VER "QSS-M " QSSM_VER_STRING
#endif

// SDL version the code was compiled with -- woods (iw)
#define Q_SDL_COMPILED_VERSION_STRING	QS_STRINGIFY(SDL_MAJOR_VERSION) "." QS_STRINGIFY(SDL_MINOR_VERSION) "." QS_STRINGIFY(SDL_PATCHLEVEL)

//define	PARANOID			// speed sapping error checking

#define	GAMENAME	"id1"		// directory to look in by default

#define PSET_SCRIPT		//enable the scriptable particle system (poorly ported from FTE)
#define PSET_SCRIPT_EFFECTINFO	//scripted particle system can load dp's effects


#include "q_stdinc.h"

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CACHE_SIZE	32	// used to align key data structures

#define Q_UNUSED(x)	(x = x)	// for pesky compiler / lint warnings

#define	MINIMUM_MEMORY	0x550000
#define	MINIMUM_MEMORY_LEVELPAK	(MINIMUM_MEMORY + 0x100000)

#define MAX_NUM_ARGVS	50

// up / down
#define	PITCH		0

// left / right
#define	YAW		1

// fall over
#define	ROLL		2


#define	MAX_QPATH	64		// max length of a quake game pathname

#define	ON_EPSILON	0.1		// point on plane side epsilon

#define	DIST_EPSILON	(0.03125)	// 1/32 epsilon to keep floating point happy (moved from world.c)

#define	MAX_MSGLEN	64000		// max length of a reliable message //ericw -- was 32000
#define	MAX_DATAGRAM	64000		// max length of unreliable message //johnfitz -- was 1024 -- woods iw

#define	DATAGRAM_MTU	1400		// johnfitz -- actual limit for unreliable messages to nonlocal clients

//
// per-level limits
//
#define	MIN_EDICTS			256		// johnfitz -- lowest allowed value for max_edicts cvar
#define	MAX_EDICTS			32000	// johnfitz -- highest allowed value for max_edicts cvar
									// ents past 8192 can't play sounds in the standard protocol
#define	MAX_LIGHTSTYLES		1024	//spike -- file format max of 255, increasing will break saved games.
#define	MAX_MODELS			4096	// johnfitz -- was 256
#define	MAX_SOUNDS			2048	// johnfitz -- was 256
#define	MAX_PARTICLETYPES	2048

#define	SAVEGAME_COMMENT_LENGTH	39

#define	MAX_LIGHTSTYLES_VANILLA	64
#define	MAX_STYLESTRING		64

//
// stats are integers communicated to the client by the server
//
#define	MAX_CL_STATS		256
#define	STAT_HEALTH			0
//#define	STAT_FRAGS		1
#define	STAT_WEAPON			2
#define	STAT_AMMO			3
#define	STAT_ARMOR			4
#define	STAT_WEAPONFRAME	5
#define	STAT_SHELLS			6
#define	STAT_NAILS			7
#define	STAT_ROCKETS		8
#define	STAT_CELLS			9
#define	STAT_ACTIVEWEAPON	10
#define	STAT_TOTALSECRETS	11
#define	STAT_TOTALMONSTERS	12
#define	STAT_SECRETS		13	// bumped on client side by svc_foundsecret
#define	STAT_MONSTERS		14	// bumped by svc_killedmonster
#define STAT_ITEMS			15	//replaces clc_clientdata info
#define STAT_VIEWHEIGHT		16	//replaces clc_clientdata info
//#define STAT_TIME			17	//zquake, redundant for nq.
//#define STAT_MATCHSTARTTIME 18
//#define STAT_VIEW2		20
#define STAT_VIEWZOOM		21 // DP
//#define STAT_UNUSED3		22 
//#define STAT_UNUSED2		23 
//#define STAT_UNUSED1		24
#define STAT_IDEALPITCH		25	//nq-emu
#define STAT_PUNCHANGLE_X	26	//nq-emu
#define STAT_PUNCHANGLE_Y	27	//nq-emu
#define STAT_PUNCHANGLE_Z	28	//nq-emu
#define STAT_PUNCHVECTOR_X	29
#define STAT_PUNCHVECTOR_Y	30
#define STAT_PUNCHVECTOR_Z	31

//dp defines these. most are useless but we fill them in for consistency.
//#define STAT_MOVEVARS_AIRACCEL_QW_STRETCHFACTOR		220
//#define STAT_MOVEVARS_AIRCONTROL_PENALTY			221
//#define STAT_MOVEVARS_AIRSPEEDLIMIT_NONQW 			222
//#define STAT_MOVEVARS_AIRSTRAFEACCEL_QW 			223
//#define STAT_MOVEVARS_AIRCONTROL_POWER				224
#define STAT_MOVEFLAGS								225
//#define STAT_MOVEVARS_WARSOWBUNNY_AIRFORWARDACCEL	226
//#define STAT_MOVEVARS_WARSOWBUNNY_ACCEL				227
//#define STAT_MOVEVARS_WARSOWBUNNY_TOPSPEED			228
//#define STAT_MOVEVARS_WARSOWBUNNY_TURNACCEL			229
//#define STAT_MOVEVARS_WARSOWBUNNY_BACKTOSIDERATIO	230
//#define STAT_MOVEVARS_AIRSTOPACCELERATE				231
//#define STAT_MOVEVARS_AIRSTRAFEACCELERATE			232
//#define STAT_MOVEVARS_MAXAIRSTRAFESPEED				233
//#define STAT_MOVEVARS_AIRCONTROL					234
//#define STAT_FRAGLIMIT								235
//#define STAT_TIMELIMIT								236
//#define STAT_MOVEVARS_WALLFRICTION					237
#define STAT_MOVEVARS_FRICTION						238
#define STAT_MOVEVARS_WATERFRICTION					239
//#define STAT_MOVEVARS_TICRATE						240
#define STAT_MOVEVARS_TIMESCALE						241
#define STAT_MOVEVARS_GRAVITY						242
#define STAT_MOVEVARS_STOPSPEED						243
#define STAT_MOVEVARS_MAXSPEED						244
#define STAT_MOVEVARS_SPECTATORMAXSPEED				245
#define STAT_MOVEVARS_ACCELERATE					246
#define STAT_MOVEVARS_AIRACCELERATE					247
#define STAT_MOVEVARS_WATERACCELERATE				248
#define STAT_MOVEVARS_ENTGRAVITY					249
#define STAT_MOVEVARS_JUMPVELOCITY					250
#define STAT_MOVEVARS_EDGEFRICTION					251
#define STAT_MOVEVARS_MAXAIRSPEED					252
#define STAT_MOVEVARS_STEPHEIGHT					253
//#define STAT_MOVEVARS_AIRACCEL_QW					254
//#define STAT_MOVEVARS_AIRACCEL_SIDEWAYS_FRICTION	255

// stock defines
//
#define	IT_SHOTGUN		1
#define	IT_SUPER_SHOTGUN	2
#define	IT_NAILGUN		4
#define	IT_SUPER_NAILGUN	8
#define	IT_GRENADE_LAUNCHER	16
#define	IT_ROCKET_LAUNCHER	32
#define	IT_LIGHTNING		64
#define	IT_SUPER_LIGHTNING	128
#define	IT_SHELLS		256
#define	IT_NAILS		512
#define	IT_ROCKETS		1024
#define	IT_CELLS		2048
#define	IT_AXE			4096
#define	IT_ARMOR1		8192
#define	IT_ARMOR2		16384
#define	IT_ARMOR3		32768
#define	IT_SUPERHEALTH		65536
#define	IT_KEY1			131072
#define	IT_KEY2			262144
#define	IT_INVISIBILITY		524288
#define	IT_INVULNERABILITY	1048576
#define	IT_SUIT			2097152
#define	IT_QUAD			4194304
#define	IT_SIGIL1		(1<<28)
#define	IT_SIGIL2		(1<<29)
#define	IT_SIGIL3		(1<<30)
#define	IT_SIGIL4		(1<<31)

//===========================================
//rogue changed and added defines

#define	RIT_SHELLS		128
#define	RIT_NAILS		256
#define	RIT_ROCKETS		512
#define	RIT_CELLS		1024
#define	RIT_AXE			2048
#define	RIT_LAVA_NAILGUN	4096
#define	RIT_LAVA_SUPER_NAILGUN	8192
#define	RIT_MULTI_GRENADE	16384
#define	RIT_MULTI_ROCKET	32768
#define	RIT_PLASMA_GUN		65536
#define	RIT_ARMOR1		8388608
#define	RIT_ARMOR2		16777216
#define	RIT_ARMOR3		33554432
#define	RIT_LAVA_NAILS		67108864
#define	RIT_PLASMA_AMMO		134217728
#define	RIT_MULTI_ROCKETS	268435456
#define	RIT_SHIELD		536870912
#define	RIT_ANTIGRAV		1073741824
#define	RIT_SUPERHEALTH		2147483648

//MED 01/04/97 added hipnotic defines
//===========================================
//hipnotic added defines
#define	HIT_PROXIMITY_GUN_BIT	16
#define	HIT_MJOLNIR_BIT		7
#define	HIT_LASER_CANNON_BIT	23
#define	HIT_PROXIMITY_GUN	(1<<HIT_PROXIMITY_GUN_BIT)
#define	HIT_MJOLNIR		(1<<HIT_MJOLNIR_BIT)
#define	HIT_LASER_CANNON	(1<<HIT_LASER_CANNON_BIT)
#define	HIT_WETSUIT		(1<<(23+2))
#define	HIT_EMPATHY_SHIELDS	(1<<(23+3))

//===========================================

#define	MAX_SCOREBOARD		255
#define	MAX_SCOREBOARDNAME	32

#define	SOUND_CHANNELS		8

#define	SERVERLIST	"servers.txt"	// woods for server history + tab complete #serverlist
#define	BOOKMARKSLIST	"bookmarks.txt"	// woods #bookmarksmenu

typedef struct
{
	const char *basedir;
	const char *userdir;	// user's directory on UNIX platforms.
				// if user directories are enabled, basedir
				// and userdir will point to different
				// memory locations, otherwise to the same.
	int	argc;
	char	**argv;
	void	*membase;
	int	memsize;
	int	numcpus;
	int	errstate;
} quakeparms_t;

#include "common.h"
#include "bspfile.h"
#include "sys.h"
#include "zone.h"
#include "mathlib.h"
#include "cvar.h"

#include "protocol.h"
#include "net.h"

#include "cmd.h"
#include "crc.h"

#include "snd_voip.h"
#include "progs.h"
#include "server.h"

#include "platform.h"
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#else
#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include <SDL/SDL_opengl_glext.h>
#endif
#else
#include "SDL.h"
#include "SDL_opengl.h"
#include "SDL_opengl_glext.h"
#endif
#ifndef APIENTRY
#define	APIENTRY
#endif

#include "console.h"
#include "wad.h"
#include "vid.h"
#include "screen.h"
#include "draw.h"
#include "render.h"
#include "view.h"
#include "sbar.h"
#include "q_sound.h"
#include "client.h"

#include "gl_model.h"
#include "world.h"

#include "image.h"	//johnfitz
#include "gl_texmgr.h"	//johnfitz
#include "input.h"
#include "keys.h"
#include "menu.h"
#include "cdaudio.h"
#include "glquake.h"
#include "location.h"      // rook / woods #pqteam
#include "iplog.h"		// JPG 1.05 - ip address logging // woods #iplog

//=============================================================================

// the host system specifies the base of the directory tree, the
// command line parms passed to the program, and the amount of memory
// available for the program to use

extern qboolean noclip_anglehack;

//
// host
//
extern	quakeparms_t *host_parms;

extern	cvar_t		sys_ticrate;
extern	cvar_t		sys_throttle;
extern	cvar_t		sys_nostdout;
extern	cvar_t		developer;
extern	cvar_t		max_edicts; //johnfitz

extern	qboolean	host_initialized;	// true if into command execution
extern	double		host_frametime;
extern	byte		*host_colormap;
extern	int		host_framecount;	// incremented every frame, never reset
extern	double		realtime;		// not bounded in any way, changed at
							// start of every frame, never reset

extern	double		last_angle_time;	// JPG - need this for smooth chasecam (from Proquake)   // woods #smoothcam

typedef struct filelist_item_s
{
	char			name[MAX_QPATH];
	char			data[50]; // woods #demolistsort #mapdescriptions
	struct filelist_item_s	*next;
} filelist_item_t;

extern filelist_item_t	*modlist;
extern filelist_item_t	*extralevels;
extern filelist_item_t	*demolist;
extern filelist_item_t  *skylist; // woods #skylist
extern filelist_item_t	*execlist; // woods #execlist
extern filelist_item_t  *particlelist; // woods #particlelist
extern filelist_item_t  *serverlist; // woods #serverlist
extern filelist_item_t  *bookmarkslist; // woods #bookmarksmenu
extern filelist_item_t*	 folderlist; // woods #folderlist
extern filelist_item_t  *musiclist; // woods #musiclist
extern filelist_item_t  *textlist; // woods #textlist

void Write_List(filelist_item_t* list, const char* list_name); // woods #bookmarksmenu #serverlist

extern qboolean descriptionsParsed; // woods #mapdescriptions
void ExtraMaps_ParseDescriptions (void); // woods #mapdescriptions
extern int max_word_length; // woods #mapdescriptions

void Host_ClearMemory (void);
void Host_ServerFrame (void);
void Host_InitCommands (void);
void Host_Init (void);
void Host_Shutdown(void);
void Host_Callback_Notify (cvar_t *var);	/* callback function for CVAR_NOTIFY */
FUNC_NORETURN void Host_Error (const char *error, ...) FUNC_PRINTF(1,2);
FUNC_NORETURN void Host_EndGame (const char *message, ...) FUNC_PRINTF(1,2);
#ifdef __WATCOMC__
#pragma aux Host_Error aborts;
#pragma aux Host_EndGame aborts;
#endif
void Host_Frame (double time);
void Host_Quit_f (void);
void Host_ClientCommands (const char *fmt, ...) FUNC_PRINTF(1,2);
void Host_ShutdownServer (qboolean crash);
void Host_WriteConfiguration (void);
void Host_Resetdemos (void);

void Host_SaveConfiguration (void); // woods #cfgsave
void Host_BackupConfiguration (void); // woods #cfgbackup

void Host_AppendDownloadData(client_t *client, sizebuf_t *buf);
void Host_DownloadAck(client_t *client);

void ExtraMaps_Init (void);
void Modlist_Init (void);
void DemoList_Init (void);
void ExecList_Init(void);
void ParticleList_Init (void); // woods #particlelist
void ServerList_Init(void); // woods #serverlist
void BookmarksList_Init (void); // woods #bookmarksmenu
void FolderList_Init (void); // woods #folderlist
void SkyList_Init (void); // woods #folderlist
void MusicList_Init (void); // woods #musiclist
void TextList_Init (void); // woods #textlist


void ExtraMaps_NewGame (void);
void DemoList_Rebuild (void);
void ParticleList_Rebuild(void);
void SkyList_Rebuild (void);
void ServerList_Rebuild (void); // woods #serverlist
void FolderList_Rebuild (void); // woods #folderlist
void ExecList_Rebuild (void); // woods #execlist
void MusicList_Rebuild (void); // woods #musiclist
void TextList_Rebuild (void); // woods #textlist
void FileList_Add_MapDesc (const char* levelName); // woods #mapdescriptions

void M_CheckMods (void); // woods #modsmenu (iw)

extern cvar_t	gl_lightning_alpha; // woods #lightalpha
extern cvar_t	cl_damagehue;  // woods #damage
extern cvar_t	cl_damagehuecolor;  // woods #damage
extern	vec3_t	NULLVEC; // woods truelighting #truelight
extern char dequake[256];	// JPG 1.05 - dedicated console translation // woods for #iplog
extern	cvar_t	cl_autodemo; //r00k  / woods #autodemo

extern int		current_skill;	// skill level for currently loaded level (in case
					//  the user changes the cvar while the level is
					//  running, this reflects the level actually in use)

extern qboolean		isDedicated;

extern int		minimum_memory;

typedef struct // woods - #speedometer
{
	float speed;
	float jump_smove, jump_fmove;	// inputs right before last jump.

} speed_info_t;

extern speed_info_t speed_info; // woods - #speedometer

#define bound(a,b,c) ((a) >= (c) ? (a) : \
					(b) < (a) ? (a) : (b) > (c) ? (c) : (b)) // woods #configprint

#endif	/* QUAKEDEFS_H */

