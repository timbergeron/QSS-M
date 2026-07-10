/*
Copyright (C) 1996-1997 Id Software, Inc.

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

#define BUTTON_ATTACK 1
#define BUTTON_JUMP 2

typedef enum {
	PM_NORMAL,			// normal ground movement
	PM_OLD_SPECTATOR,	// fly, no clip to world (QW bug)
	PM_SPECTATOR,		// fly, no clip to world
	PM_DEAD,			// no acceleration
	PM_FLY,				// fly, bump into walls
	PM_NONE,			// can't move
	PM_FREEZE,			// can't move or look around (TODO)
	PM_WALLWALK,		// sticks to walls. on ground while near one
	PM_6DOF				// spaceship mode
} pmtype_t;

#define PMF_JUMP_HELD			1
#define PMF_LADDER				2	//pmove flags. seperate from flags

#define	MAX_PHYSENTS	64//2048
typedef struct
{
	vec3_t	origin;
	vec3_t	angles;
	qmodel_t	*model;		// only for bsp models
	vec3_t	mins, maxs;	// only for non-bsp models
	int	info;		// for client or server to identify
	unsigned int forcecontentsmask;	//set from .skin
} physent_t;

typedef struct
{
	// player state
	vec3_t		origin;
	vec3_t		safeorigin;	//valid when safeorigin_known. needed for extrasr4's ladders otherwise they bug out.
	vec3_t		angles;
	vec3_t		velocity;
	vec3_t		gravitydir;
	qboolean		jump_held;
	float			jump_secs;	// msec since last jump
	float		waterjumptime;
	int			pm_type;
	vec3_t		player_mins;
	vec3_t		player_maxs;

	// world state
	int			numphysent;
	physent_t	physents[MAX_PHYSENTS];	// 0 should be the world

	// input
	usercmd_t	cmd;

	qboolean onladder;
	qboolean safeorigin_known;

	// results
	int			skipent;
	int			numtouch;
	int			touchindex[MAX_PHYSENTS];
	vec3_t		touchvel[MAX_PHYSENTS];
	qboolean		onground;
	int			groundent;		// index in physents array, only valid
								// when onground is true
	int			waterlevel;
	int			watertype;

	struct world_s		*world;
} playermove_t;

typedef struct {
	//standard quakeworld
	float gravity;
	float stopspeed;
	float maxspeed;
	float spectatormaxspeed;
	float maxairspeed;
	float accelerate;
	float airaccelerate;
	float wateraccelerate;
	float friction;
	float waterfriction;
	float flyfriction;
	float entgravity;

	//extended stuff, sent via serverinfo
	float bunnyspeedcap;
	float watersinkspeed;
	float ktjump;
	float edgefriction; //default 2
	float jumpspeed;
	int	walljump;
	qboolean slidefix;
	qboolean airstep;
	qboolean pground;
	qboolean stepdown;
	qboolean slidyslopes;
	qboolean autobunny;
	qboolean bunnyfriction;	//force at least one frame of friction when bunnying.
	int stepheight;

	unsigned protocolflags;

	unsigned int	flags;
} movevars_t;

#define MOVEFLAG_VALID							0x80000000	//to signal that these are actually known. otherwise reserved.
//#define MOVEFLAG_Q2AIRACCELERATE				0x00000001
#define MOVEFLAG_NOGRAVITYONGROUND				0x00000002	//no slope sliding
//#define MOVEFLAG_GRAVITYUNAFFECTEDBYTICRATE	0x00000004	//apply half-gravity both before AND after the move, which better matches the curve
#define MOVEFLAG_QWEDGEBOX						0x00010000	//calculate edgefriction using tracebox and a buggy start pos
#define MOVEFLAG_USEAIRACCEL					0x00020000	//the vanilla qw pmove code used movevars.accelerate and NEVER movevars.airaccelerate, but different defaults make fixing this awkward.
#define MOVEFLAG_QWCOMPAT						(MOVEFLAG_NOGRAVITYONGROUND|MOVEFLAG_QWEDGEBOX)

#define MASK_PLAYERSOLID	CONTENTMASK_ANYSOLID
#define CONTENTBIT_EMPTY	CONTENTMASK_FROMQ1(CONTENTS_EMPTY)
#define CONTENTBIT_SOLID	CONTENTMASK_FROMQ1(CONTENTS_SOLID)
#define CONTENTBIT_WATER	CONTENTMASK_FROMQ1(CONTENTS_WATER)
#define CONTENTBIT_SLIME	CONTENTMASK_FROMQ1(CONTENTS_SLIME)
#define CONTENTBIT_LAVA		CONTENTMASK_FROMQ1(CONTENTS_LAVA)
#define CONTENTBIT_SKY		CONTENTMASK_FROMQ1(CONTENTS_SKY)
#define CONTENTBIT_CLIP		CONTENTMASK_FROMQ1(CONTENTS_CLIP)
#define CONTENTBIT_LADDER	CONTENTMASK_FROMQ1(CONTENTS_LADDER)
#define CONTENTBITS_FLUID	(CONTENTBIT_WATER|CONTENTBIT_SLIME|CONTENTBIT_LAVA)

extern	movevars_t		movevars;
extern	playermove_t	pmove;

void PM_PlayerMove (float gamespeed);
void PM_Init (void);
void PM_InitBoxHull (void);

void PM_CategorizePosition (void);
int PM_HullPointContents (hull_t *hull, int num, vec3_t p);

int PM_ExtraBoxContents (vec3_t p);	//Peeks for HL-style water.
int PM_PointContents (vec3_t point);
qboolean PM_TestPlayerPosition (vec3_t point);
#ifndef __cplusplus
struct trace_s PM_PlayerTrace (vec3_t start, vec3_t stop, unsigned int solidmask);
#endif

//stuff that should be elsewhere...
int SV_HullPointContents (hull_t *hull, int num, vec3_t p);
void SV_Impact (edict_t *e1, edict_t *e2);
void World_AddEntsToPmove(edict_t *ignore, vec3_t boxminmax[2]);
void PMCL_ServerinfoUpdated(void);
void PMCL_SetMoveVars(void);
void PMSV_SetMoveStats(edict_t *plent, float *fstat, int *istat);	//so client has the right movevars as stats.
void PMSV_UpdateMovevars(void);
void PF_sv_pmove(void);
void PMSV_UpdateMovevars(void);
qboolean PM_BunnyFrictionEnabled(void);
void PM_Register(void);
#define VectorClear(v) ((v)[0] = (v)[1] = (v)[2] = 0)
#define VectorSet(r,x,y,z) do{(r)[0] = x; (r)[1] = y;(r)[2] = z;}while(0)
#define Length VectorLength
#define VectorNegate(a,b)		((b)[0]=-(a)[0],(b)[1]=-(a)[1],(b)[2]=-(a)[2])
