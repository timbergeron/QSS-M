/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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
// cl.input.c  -- builds an intended movement command to send to the server

// Quake is a trademark of Id Software, Inc., (c) 1996 Id Software, Inc. All
// rights reserved.

#include "quakedef.h"

extern cvar_t cl_maxpitch; //johnfitz -- variable pitch clamping
extern cvar_t cl_minpitch; //johnfitz -- variable pitch clamping
extern cvar_t gl_load24bit; // woods #weaponwheel

cvar_t	cl_iDrive = {"cl_iDrive", "1", CVAR_ARCHIVE}; // woods #idrive

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as a parameter to the command so it can be matched up with
the release.

state bit 0 is the current state of the key
state bit 1 is edge triggered on the up to down transition
state bit 2 is edge triggered on the down to up transition

===============================================================================
*/


kbutton_t	in_mlook, in_klook;
kbutton_t	in_left, in_right, in_forward, in_back;
kbutton_t	in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t	in_strafe, in_speed, in_jump, in_attack, in_button3, in_button4, in_button5, in_button6, in_button7, in_button8;
kbutton_t	in_up, in_down;

int			in_impulse;

qboolean speed_boost_active = false; // woods #fastnoclip

extern cvar_t cl_forwardspeed; // woods #fastnoclip
extern cvar_t cl_backspeed; // woods #fastnoclip
extern cvar_t cl_sidespeed; // woods #fastnoclip
extern cvar_t sv_maxspeed; // woods #fastnoclip

extern edict_t* sv_player; // woods #fastnoclip

static int original_sv_maxspeed = 0; // woods #fastnoclip
static int original_cl_forwardspeed = 0; // woods #fastnoclip 
static int original_cl_backspeed = 0; // woods #fastnoclip
static int original_cl_sidespeed = 0; // woods #fastnoclip

// JPG 1.05 - translate +jump to +moveup under water
//extern cvar_t	pq_moveup;

static void In_Water(int contents) // woods #detectwater
{
	switch (contents)
	{
	case CONTENTS_WATER:
	case CONTENTS_SLIME:
	case CONTENTS_LAVA:
		cl.inwater = true;
		return;
	default:
		cl.inwater = false;
		return;
	}
}

void KeyDown (kbutton_t *b)
{
	int		k;
	const char	*c;

	mleaf_t* l; // woods #detectwater

	if (cl.worldmodel) // woods #detectwater
	{
		l = Mod_PointInLeaf(listener_origin, cl.worldmodel);
		In_Water(l->contents);
	}

	c = Cmd_Argv(1);
	if (c[0])
		k = atoi(c);
	else
		k = -1;		// typed manually at the console for continuous down

	// JPG 1.05 - if jump is pressed underwater, translate it to a moveup
	if (b == &in_jump /*&& pq_moveup.value*/ && cl.stats[STAT_HEALTH] > 0 && cl.inwater)
		b = &in_up;

	// woods #fastnoclip - handle speed boost when jump pressed in noclip
	if (b == &in_jump && svs.clients->spawned && sv_player && !sv_player->free &&
		sv_player->v.movetype == MOVETYPE_NOCLIP && sv_maxspeed.value < 720)
	{
		// Store original values before boosting
		original_sv_maxspeed = sv_maxspeed.value;
		original_cl_forwardspeed = cl_forwardspeed.value;
		original_cl_backspeed = cl_backspeed.value;
		original_cl_sidespeed = cl_sidespeed.value;

		speed_boost_active = true;

		float new_max = sv_maxspeed.value + 400;
		if (new_max > 720) new_max = 720;
		Cvar_SetQuick(&sv_maxspeed, va("%f", new_max));

		float new_forward = cl_forwardspeed.value + 400;
		if (new_forward > 720) new_forward = 720;
		Cvar_SetQuick(&cl_forwardspeed, va("%f", new_forward));

		float new_back = cl_backspeed.value + 400;
		if (new_back > 720) new_back = 720;
		Cvar_SetQuick(&cl_backspeed, va("%f", new_back));

		float new_side = cl_sidespeed.value + 400;
		if (new_side > 720) new_side = 720;
		Cvar_SetQuick(&cl_sidespeed, va("%f", new_side));
	}

	if (k == b->down[0] || k == b->down[1])
		return;		// repeating key

	if (!b->down[0])
		b->down[0] = k;
	else if (!b->down[1])
		b->down[1] = k;
	else
	{
		Con_Printf ("Three keys down for a button!\n");
		return;
	}

	if (b->state & 1)
		return;		// still down
	b->state |= 1 + 2;	// down + impulse down
	b->downtime = realtime; // woods #idrive
}

void KeyUp (kbutton_t *b)
{
	int		k;
	const char	*c;

	c = Cmd_Argv(1);
	if (c[0])
		k = atoi(c);
	else
	{ // typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->state = 4;	// impulse up
		b->uptime = realtime;
		return;
	}

	// JPG 1.05 - check to see if we need to translate -jump to -moveup
	if (b == &in_jump/* && pq_moveup.value*/)
	{
		if (k == in_up.down[0] || k == in_up.down[1])
			b = &in_up;
		else
		{
			// in case a -moveup got lost somewhere
			in_up.down[0] = in_up.down[1] = 0;
			in_up.state = 4;
		}

		if (speed_boost_active) // woods #fastnoclip - reset speeds when jump released
		{
			Cvar_SetQuick(&sv_maxspeed, va("%i", original_sv_maxspeed));
			Cvar_SetQuick(&cl_forwardspeed, va("%i", original_cl_forwardspeed));
			Cvar_SetQuick(&cl_backspeed, va("%i", original_cl_backspeed));
			Cvar_SetQuick(&cl_sidespeed, va("%i", original_cl_sidespeed));
			speed_boost_active = false;
		}
	}

	if (b->down[0] == k)
		b->down[0] = 0;
	else if (b->down[1] == k)
		b->down[1] = 0;
	else
		return;		// key up without coresponding down (menu pass through)
	if (b->down[0] || b->down[1])
		return;		// some other key is still holding it down

	if (!(b->state & 1))
		return;		// still up (this should not happen)
	b->state &= ~1;		// now up
	b->state |= 4; 		// impulse up
	b->uptime = realtime; // woods #idrive
}

void IN_KLookDown (void) {KeyDown(&in_klook);}
void IN_KLookUp (void) {KeyUp(&in_klook);}
void IN_MLookDown (void) {KeyDown(&in_mlook);}
void IN_MLookUp (void) {
	KeyUp(&in_mlook);
	if ( !(in_mlook.state&1) &&  lookspring.value)
		V_StartPitchDrift();
}
void IN_UpDown(void) {KeyDown(&in_up);}
void IN_UpUp(void) {KeyUp(&in_up);}
void IN_DownDown(void) {KeyDown(&in_down);}
void IN_DownUp(void) {KeyUp(&in_down);}
void IN_LeftDown(void) {KeyDown(&in_left);}
void IN_LeftUp(void) {KeyUp(&in_left);}
void IN_RightDown(void) {KeyDown(&in_right);}
void IN_RightUp(void) {KeyUp(&in_right);}
void IN_ForwardDown(void) {KeyDown(&in_forward);}
void IN_ForwardUp(void) {KeyUp(&in_forward);}
void IN_BackDown(void) {KeyDown(&in_back);}
void IN_BackUp(void) {KeyUp(&in_back);}
void IN_LookupDown(void) {KeyDown(&in_lookup);}
void IN_LookupUp(void) {KeyUp(&in_lookup);}
void IN_LookdownDown(void) {KeyDown(&in_lookdown);}
void IN_LookdownUp(void) {KeyUp(&in_lookdown);}
void IN_MoveleftDown(void) {KeyDown(&in_moveleft);}
void IN_MoveleftUp(void) {KeyUp(&in_moveleft);}
void IN_MoverightDown(void) {KeyDown(&in_moveright);}
void IN_MoverightUp(void) {KeyUp(&in_moveright);}

void IN_SpeedDown(void) {KeyDown(&in_speed);}
void IN_SpeedUp(void) {KeyUp(&in_speed);}
void IN_StrafeDown(void) {KeyDown(&in_strafe);}
void IN_StrafeUp(void) {KeyUp(&in_strafe);}

void IN_AttackDown(void) {KeyDown(&in_attack);}
void IN_AttackUp(void) {KeyUp(&in_attack);}

void IN_UseDown (void) {KeyDown(&in_button3);}
void IN_UseUp (void) {KeyUp(&in_button3);}
void IN_JumpDown (void) {KeyDown(&in_jump);}
void IN_JumpUp (void) {KeyUp(&in_jump);}

void IN_Button3Down(void) {KeyDown(&in_button3);}
void IN_Button3Up(void) {KeyUp(&in_button3);}
void IN_Button4Down(void) {KeyDown(&in_button4);}
void IN_Button4Up(void) {KeyUp(&in_button4);}
void IN_Button5Down(void) {KeyDown(&in_button5);}
void IN_Button5Up(void) {KeyUp(&in_button5);}
void IN_Button6Down(void) {KeyDown(&in_button6);}
void IN_Button6Up(void) {KeyUp(&in_button6);}
void IN_Button7Down(void) {KeyDown(&in_button7);}
void IN_Button7Up(void) {KeyUp(&in_button7);}
void IN_Button8Down(void) {KeyDown(&in_button8);}
void IN_Button8Up(void) {KeyUp(&in_button8);}

// Tonik ...
void IN_Impulse(void)
{
	int best, i, imp;

	in_impulse = Q_atoi(Cmd_Argv(1));

	if (Cmd_Argc() <= 2)
	{
		return;
	}

	best = 0;

	for (i = Cmd_Argc() - 1; i > 0; i--)
	{
		imp = Q_atoi(Cmd_Argv(i));
		if (imp < 1 || imp > 8)
			continue;
		switch (imp)
		{
		case 1:
			if (cl.items & IT_AXE)
				best = 1;
			break;
		case 2:
			if (cl.items & IT_SHOTGUN && cl.stats[STAT_SHELLS] >= 1)
				best = 2;
			break;
		case 3:
			if (cl.items & IT_SUPER_SHOTGUN && cl.stats[STAT_SHELLS] >= 2)
				best = 3;
			break;
		case 4:
			if (cl.items & IT_NAILGUN && cl.stats[STAT_NAILS] >= 1)
				best = 4;
			break;
		case 5:
			if (cl.items & IT_SUPER_NAILGUN && cl.stats[STAT_NAILS] >= 2)
				best = 5;
			break;
		case 6:
			if (cl.items & IT_GRENADE_LAUNCHER && cl.stats[STAT_ROCKETS] >= 1)
				best = 6;
			break;
		case 7:
			if (cl.items & IT_ROCKET_LAUNCHER && cl.stats[STAT_ROCKETS] >= 1)
				best = 7;
			break;
		case 8:
			if (cl.items & IT_LIGHTNING && cl.stats[STAT_CELLS] > 0)
				best = 8;
		}
	}

	if (best)
	{
		in_impulse = best;
	}
}
//... Tonik

/*
===============
CL_KeyState

Returns 0.25 if a key was pressed and released during the frame,
0.5 if it was pressed and held
0 if held then released, and
1.0 if held for the entire time
===============
*/
float CL_KeyState (kbutton_t *key, qboolean isfinal)
{
	float		val;
	qboolean	impulsedown, impulseup, down;

	impulsedown = key->state & 2;
	impulseup = key->state & 4;
	down = key->state & 1;
	val = 0;

	if (impulsedown && !impulseup)
	{
		if (down)
			val = 0.5;	// pressed and held this frame
		else
			val = 0;	//	I_Error ();
	}
	if (impulseup && !impulsedown)
	{
		if (down)
			val = 0;	//	I_Error ();
		else
			val = 0;	// released this frame
	}
	if (!impulsedown && !impulseup)
	{
		if (down)
			val = 1.0;	// held the entire frame
		else
			val = 0;	// up the entire frame
	}
	if (impulsedown && impulseup)
	{
		if (down)
			val = 0.75;	// released and re-pressed this frame
		else
			val = 0.25;	// pressed and released this frame
	}

	if (isfinal)
		key->state &= 1;		// clear impulses

	return val;
}


//==========================================================================

cvar_t	cl_upspeed = {"cl_upspeed","200",CVAR_ARCHIVE};
cvar_t	cl_forwardspeed = {"cl_forwardspeed","200", CVAR_ARCHIVE};
cvar_t	cl_backspeed = {"cl_backspeed","200", CVAR_ARCHIVE};
cvar_t	cl_sidespeed = {"cl_sidespeed","350",CVAR_ARCHIVE};

cvar_t	cl_movespeedkey = {"cl_movespeedkey","2.0",CVAR_NONE};

cvar_t	cl_yawspeed = {"cl_yawspeed","140",CVAR_ARCHIVE};
cvar_t	cl_pitchspeed = {"cl_pitchspeed","150",CVAR_ARCHIVE};

cvar_t	cl_anglespeedkey = {"cl_anglespeedkey","1.5",CVAR_NONE};

cvar_t	cl_alwaysrun = {"cl_alwaysrun","0",CVAR_ARCHIVE}; // QuakeSpasm -- new always run

cvar_t	pq_lag = { "pq_lag", "0" };			// JPG - synthetic lag // woods #pqlag
cvar_t	cl_smartspawn = {"cl_smartspawn", "0", CVAR_ARCHIVE}; // woods #spawntrainer

/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles (void)
{
	float	speed;
	float	up, down;

	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0))
		speed = host_frametime * cl_anglespeedkey.value;
	else
		speed = host_frametime;

	if (!(in_strafe.state & 1))
	{
		cl.viewangles[YAW] -= speed*cl_yawspeed.value*CL_KeyState (&in_right, true);
		cl.viewangles[YAW] += speed*cl_yawspeed.value*CL_KeyState (&in_left, true);
		cl.viewangles[YAW] = anglemod(cl.viewangles[YAW]);
	}
	if (in_klook.state & 1)
	{
		V_StopPitchDrift ();
		cl.viewangles[PITCH] -= speed*cl_pitchspeed.value * CL_KeyState (&in_forward, true);
		cl.viewangles[PITCH] += speed*cl_pitchspeed.value * CL_KeyState (&in_back, true);
	}

	up = CL_KeyState (&in_lookup, true);
	down = CL_KeyState(&in_lookdown, true);

	cl.viewangles[PITCH] -= speed*cl_pitchspeed.value * up;
	cl.viewangles[PITCH] += speed*cl_pitchspeed.value * down;

	if (up || down)
		V_StopPitchDrift ();

	if (cl.fullpitch == 0) // woods #pqfullpitch force client to adapt when not allowed
	{
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		//johnfitz -- variable pitch clamping
		if (cl.viewangles[PITCH] > cl_maxpitch.value)
			cl.viewangles[PITCH] = cl_maxpitch.value;
		if (cl.viewangles[PITCH] < cl_minpitch.value)
			cl.viewangles[PITCH] = cl_minpitch.value;
		//johnfitz

		if (cl.viewangles[ROLL] > 50)
			cl.viewangles[ROLL] = 50;
		if (cl.viewangles[ROLL] < -50)
			cl.viewangles[ROLL] = -50;
	}
}

/*
================
CL_BaseMove

Send the intended movement message to the server
================
*/
void CL_BaseMove (usercmd_t *cmd, qboolean isfinal)
{
	Q_memset (cmd, 0, sizeof(*cmd));

	VectorCopy(cl.viewangles, cmd->viewangles);

	if (cls.signon != SIGNONS)
		return;

	if (cl_iDrive.value) // woods #idrive
	{
		float s1, s2;
	if (in_strafe.state & 1)
	{
			s1 = CL_KeyState (&in_right, isfinal);
			s2 = CL_KeyState (&in_left, isfinal);

			if (s1 && s2)
			{
				if (in_right.downtime > in_left.downtime)
					s2 = 0;
				if (in_right.downtime < in_left.downtime)
					s1 = 0;
			}
			cmd->sidemove += cl_sidespeed.value * s1;
			cmd->sidemove -= cl_sidespeed.value * s2;
		}
		s1 = CL_KeyState (&in_moveright, isfinal);
		s2 = CL_KeyState (&in_moveleft, isfinal);
		if (s1 && s2)
		{
			if (in_moveright.downtime > in_moveleft.downtime)
				s2 = 0;
			if (in_moveright.downtime < in_moveleft.downtime)
				s1 = 0;
		}
		cmd->sidemove += cl_sidespeed.value * s1;
		cmd->sidemove -= cl_sidespeed.value * s2;
		s1 = CL_KeyState (&in_up, isfinal);
		s2 = CL_KeyState (&in_down, isfinal);
		if (s1 && s2)
		{
			if (in_up.downtime > in_down.downtime)
				s2 = 0;
			if (in_up.downtime < in_down.downtime)
				s1 = 0;
		}

		cmd->upmove += cl_upspeed.value * s1;
		cmd->upmove -= cl_upspeed.value * s2;

		if (!(in_klook.state & 1)) 
		{
			s1 = CL_KeyState (&in_forward, isfinal);
			s2 = CL_KeyState (&in_back, isfinal);
			if (s1 && s2)
			{
				if (in_forward.downtime > in_back.downtime)
					s2 = 0;
				if (in_forward.downtime < in_back.downtime)
					s1 = 0;
			}
			cmd->forwardmove += cl_forwardspeed.value * s1;
			cmd->forwardmove -= cl_backspeed.value * s2;
		}
	}
	else
	{
		if (in_strafe.state & 1)
		{
		cmd->sidemove += cl_sidespeed.value * CL_KeyState (&in_right, isfinal);
		cmd->sidemove -= cl_sidespeed.value * CL_KeyState (&in_left, isfinal);
	}

	cmd->sidemove += cl_sidespeed.value * CL_KeyState (&in_moveright, isfinal);
	cmd->sidemove -= cl_sidespeed.value * CL_KeyState (&in_moveleft, isfinal);

	cmd->upmove += cl_upspeed.value * CL_KeyState (&in_up, isfinal);
	cmd->upmove -= cl_upspeed.value * CL_KeyState (&in_down, isfinal);

	if (! (in_klook.state & 1) )
	{
		cmd->forwardmove += cl_forwardspeed.value * CL_KeyState (&in_forward, isfinal);
		cmd->forwardmove -= cl_backspeed.value * CL_KeyState (&in_back, isfinal);
	}
	}

//
// adjust for speed key
//
	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0))
	{
		cmd->forwardmove *= cl_movespeedkey.value;
		cmd->sidemove *= cl_movespeedkey.value;
		cmd->upmove *= cl_movespeedkey.value;
	}
}

void CL_FinishMove(usercmd_t *cmd, qboolean isfinal)
{
	static kbutton_t *buttons[] = {
		&in_attack,
		&in_jump,
		&in_button3,
		&in_button4,
		&in_button3,
		&in_button6,
		&in_button7,
		&in_button8,
	};
	unsigned int bits, i;
	//
	// send button bits
	//
	bits = 0;

	for (i = 0; i < countof(buttons); i++)
	{
		if ( buttons[i]->state & 3 )
		{
			if (i == 0 && cl.stats[STAT_HEALTH] <= 0 && cl_smartspawn.value) // special case for in_attack
				bits = 0;
			else
				bits |= 1<<i;
				
			if (isfinal)
				buttons[i]->state &= ~2;
		}
	}

	cmd->buttons = bits;
	cmd->impulse = in_impulse;

	if (isfinal)
		in_impulse = 0;

	cmd->forwardmove+= cl.accummoves[0];
	cmd->sidemove	+= cl.accummoves[1];
	cmd->upmove		+= cl.accummoves[2];
	if (isfinal)
		cl.accummoves[0]=cl.accummoves[1]=cl.accummoves[2]=0;

	cmd->sequence	= cl.movemessages;
	cmd->servertime	= cl.time;
	cmd->seconds	= cmd->servertime - cl.lastcmdtime;
}

// woods #pqlag

sizebuf_t lag_buff[32]; // JPG - support for synthetic lag
byte lag_data[32][128];  // JPG - support for synthetic lag
unsigned int lag_head, lag_tail; // JPG - support for synthetic lag
double lag_sendtime[32]; // JPG - support for synthetic lag

/* JPG - this function sends delayed move messages
==============
CL_SendLagMove
==============
*/
void CL_SendLagMove(void)
{
	if (cls.demoplayback || (cls.state != ca_connected) /* || (cls.signon != SIGNONS)*/)
		return;

	while ((lag_tail < lag_head) && (lag_sendtime[lag_tail & 31] <= realtime))
	{
		lag_tail++;
		if (++cl.movemessages <= 2)
		{
			lag_head = lag_tail = 0;  // JPG - hack: if cl.movemessages has been reset, we should reset these too
			continue;	// return -> continue
		}

		if (NET_SendUnreliableMessage(cls.netcon, &lag_buff[(lag_tail - 1) & 31]) == -1)
		{
			Con_Printf("CL_SendMove: lost server connection\n");
			CL_Disconnect();
		}
	}
}

/*
==============
CL_SendMove
==============
*/
void CL_SendMove(const usercmd_t* cmd)
{
	unsigned int	i;
	sizebuf_t		buf;
	byte			data[1024];

	buf.maxsize = sizeof(data);
	buf.cursize = 0;
	buf.data = data;

	for (i = 0; i < cl.ackframes_count; i++)
	{
		MSG_WriteByte(&buf, clcdp_ackframe);
		MSG_WriteLong(&buf, cl.ackframes[i]);
	}
	cl.ackframes_count = 0;

	if (cmd)
	{
		int dump = buf.cursize;
		unsigned int bits = cmd->buttons;

		//
		// send the movement message
		//
		MSG_WriteByte(&buf, clc_move);

		if (cl.protocol == PROTOCOL_VERSION_DP7)
		{
			if (0)
			{
				MSG_WriteLong(&buf, 0);
				MSG_WriteFloat(&buf, cl.mtime[0]);	// so server can get ping times
			}
			else
			{
				MSG_WriteLong(&buf, cl.movemessages);
				MSG_WriteFloat(&buf, cmd->servertime);	// for input timing
			}
		}
		else if (cl.protocol_pext2 & PEXT2_PREDINFO)
		{
			MSG_WriteShort(&buf, cl.movemessages & 0xffff);	//server will ack this once it has been applied to the player's entity state
			MSG_WriteFloat(&buf, cmd->servertime);	// so server can get cmd timing (pings will be calculated by entframe acks).
		}
		else
			MSG_WriteFloat(&buf, cl.mtime[0]);	// so server can get ping times

		for (i = 0; i < 3; i++)
			//johnfitz -- 16-bit angles for PROTOCOL_FITZQUAKE
			//spike -- nq+bjp3 use 8bit angles. all other supported protocols use 16bit ones.
			//spike -- proquake servers bump client->server angles up to at least 16bit. this is safe because it only happens when both client+server advertise it, and because it never actually gets recorded into demos anyway.
			//spike -- predinfo also always means 16bit angles, even if for some reason the server doesn't advertise proquake (like dp).
			if ((cl.protocol == PROTOCOL_NETQUAKE || cl.protocol == PROTOCOL_VERSION_BJP3) && !NET_QSocketGetProQuakeAngleHack(cls.netcon) && !(cl.protocol_pext2 & PEXT2_PREDINFO))
				MSG_WriteAngle(&buf, cl.viewangles[i], cl.protocolflags);
			else
				MSG_WriteAngle16(&buf, cl.viewangles[i], cl.protocolflags);
		//johnfitz

		MSG_WriteShort(&buf, cmd->forwardmove);
		MSG_WriteShort(&buf, cmd->sidemove);
		MSG_WriteShort(&buf, cmd->upmove);

		if (cl.protocol_pext2 & PEXT2_PRYDONCURSOR)
		{
			if (cmd->weapon)
				bits |= (1u << 30);
			if (cmd->cursor_screen[0] || cmd->cursor_screen[1] ||
				cmd->cursor_start[0] || cmd->cursor_start[1] || cmd->cursor_start[2] ||
				cmd->cursor_impact[0] || cmd->cursor_impact[1] || cmd->cursor_impact[2] ||
				cmd->cursor_entitynumber)
				bits |= (1u << 31);
			MSG_WriteLong(&buf, bits);
		}
		else if (cl.protocol == PROTOCOL_VERSION_DP7)
		{
			MSG_WriteLong(&buf, bits);
			bits |= (1u << 31);
		}
		else
			MSG_WriteByte(&buf, bits);
		MSG_WriteByte(&buf, cmd->impulse);
		if (bits & (1u << 30))
			MSG_WriteLong(&buf, cmd->weapon);
		if (bits & (1u << 31))
		{
			MSG_WriteShort(&buf, cmd->cursor_screen[0] * 32767);
			MSG_WriteShort(&buf, cmd->cursor_screen[1] * 32767);
			MSG_WriteFloat(&buf, cmd->cursor_start[0]);	//start (view pos)
			MSG_WriteFloat(&buf, cmd->cursor_start[1]);
			MSG_WriteFloat(&buf, cmd->cursor_start[2]);
			MSG_WriteFloat(&buf, cmd->cursor_impact[0]);	//impact
			MSG_WriteFloat(&buf, cmd->cursor_impact[1]);
			MSG_WriteFloat(&buf, cmd->cursor_impact[2]);
			MSG_WriteEntity(&buf, cmd->cursor_entitynumber, cl.protocol_pext2);
		}
		in_impulse = 0;

		cl.movecmds[(cl.movemessages++)&MOVECMDS_MASK] = *cmd;

	//
	// allways dump the first two message, because it may contain leftover inputs
	// from the last level
	//
		if (cl.movemessages <= 2)
		{
			buf.cursize = dump;	//don't actually send it...
			cl.movecmds[(cl.movemessages-1)&MOVECMDS_MASK].seconds = 0;	//and don't predict forwards. should fix prediction going weird at the start of the map.
		}
		else
			S_Voip_Transmit(clcfte_voicechat, &buf);/*Spike: Add voice data*/
	}
	else
		S_Voip_Transmit(clcfte_voicechat, NULL);/*Spike: Add voice data (with cl_voip_test anyway)*/

	//fixme: nops if we're still connecting, or something.

	//
	// deliver the message
	//
	if (cls.demoplayback || !buf.cursize)
		return;

	if (NET_SendUnreliableMessage(cls.netcon, &buf) == -1)
	{
		Con_Printf("CL_SendMove: lost server connection\n");
		CL_Disconnect();
	}
}

/*
==============
CL_SendMove2 - woods modified for #pqlag
==============
*/
void CL_SendMove2 (const usercmd_t *cmd)
{
	unsigned int	i;
	sizebuf_t* buf;	// JPG - turned into a pointer (made corresponding changes below) 
	//byte		data[1024];

	buf = &lag_buff[lag_head & 31];
	buf->maxsize = 1024;
	buf->cursize = 0;
	buf->data = lag_data[lag_head & 31]; // JPG - added head index
	lag_sendtime[lag_head++ & 31] = realtime + (pq_lag.value / 1000.0);

	for (i = 0; i < cl.ackframes_count; i++)
	{
		MSG_WriteByte(buf, clcdp_ackframe);
		MSG_WriteLong(buf, cl.ackframes[i]);
	}
	cl.ackframes_count = 0;

	if (cmd)
	{
		int dump = buf->cursize;
		unsigned int bits = cmd->buttons;

	//
	// send the movement message
	//
		MSG_WriteByte (buf, clc_move);

		if (cl.protocol == PROTOCOL_VERSION_DP7)
		{
			if (0)
			{
				MSG_WriteLong(buf, 0);
				MSG_WriteFloat (buf, cl.mtime[0]);	// so server can get ping times
			}
			else
			{
				MSG_WriteLong(buf, cl.movemessages);
				MSG_WriteFloat (buf, cmd->servertime);	// for input timing
			}
		}
		else if (cl.protocol_pext2 & PEXT2_PREDINFO)
		{
			MSG_WriteShort(buf, cl.movemessages&0xffff);	//server will ack this once it has been applied to the player's entity state
			MSG_WriteFloat (buf, cmd->servertime);	// so server can get cmd timing (pings will be calculated by entframe acks).
		}
		else
			MSG_WriteFloat (buf, cl.mtime[0]);	// so server can get ping times

		for (i=0 ; i<3 ; i++)
			//johnfitz -- 16-bit angles for PROTOCOL_FITZQUAKE
			//spike -- nq+bjp3 use 8bit angles. all other supported protocols use 16bit ones.
			//spike -- proquake servers bump client->server angles up to at least 16bit. this is safe because it only happens when both client+server advertise it, and because it never actually gets recorded into demos anyway.
			//spike -- predinfo also always means 16bit angles, even if for some reason the server doesn't advertise proquake (like dp).
			if ((cl.protocol == PROTOCOL_NETQUAKE || cl.protocol == PROTOCOL_VERSION_BJP3) && !NET_QSocketGetProQuakeAngleHack(cls.netcon) && !(cl.protocol_pext2 & PEXT2_PREDINFO))
				MSG_WriteAngle (buf, cl.viewangles[i], cl.protocolflags);
			else
				MSG_WriteAngle16 (buf, cl.viewangles[i], cl.protocolflags);
			//johnfitz

		MSG_WriteShort (buf, cmd->forwardmove);
		MSG_WriteShort (buf, cmd->sidemove);
		MSG_WriteShort (buf, cmd->upmove);

		if (cl.protocol_pext2 & PEXT2_PRYDONCURSOR)
		{
			if (cmd->weapon)
				bits |= (1u<<30);
			if (cmd->cursor_screen[0] || cmd->cursor_screen[1] ||
				cmd->cursor_start[0] || cmd->cursor_start[1] || cmd->cursor_start[2] ||
				cmd->cursor_impact[0] || cmd->cursor_impact[1] || cmd->cursor_impact[2] ||
				cmd->cursor_entitynumber)
				bits |= (1u<<31);
			MSG_WriteLong (buf, bits);
		}
		else if (cl.protocol == PROTOCOL_VERSION_DP7)
		{
			MSG_WriteLong (buf, bits);
			bits |= (1u<<31);
		}
		else
			MSG_WriteByte (buf, bits);
		MSG_WriteByte (buf, cmd->impulse);
		if (bits & (1u<<30))
			MSG_WriteLong (buf, cmd->weapon);
		if (bits & (1u<<31))
		{
			MSG_WriteShort(buf, cmd->cursor_screen[0] * 32767);
			MSG_WriteShort(buf, cmd->cursor_screen[1] * 32767);
			MSG_WriteFloat(buf, cmd->cursor_start[0]);	//start (view pos)
			MSG_WriteFloat(buf, cmd->cursor_start[1]);
			MSG_WriteFloat(buf, cmd->cursor_start[2]);
			MSG_WriteFloat(buf, cmd->cursor_impact[0]);	//impact
			MSG_WriteFloat(buf, cmd->cursor_impact[1]);
			MSG_WriteFloat(buf, cmd->cursor_impact[2]);
			MSG_WriteEntity(buf, cmd->cursor_entitynumber, cl.protocol_pext2);
		}
		in_impulse = 0;

		cl.movecmds[cl.movemessages&MOVECMDS_MASK] = *cmd;

	//
	// allways dump the first two message, because it may contain leftover inputs
	// from the last level
	//
		if (++cl.movemessages <= 2)
			buf->cursize = dump;
		else
			S_Voip_Transmit(clcfte_voicechat, buf);/*Spike: Add voice data*/
	}
	else
		S_Voip_Transmit(clcfte_voicechat, NULL);/*Spike: Add voice data (with cl_voip_test anyway)*/

	//fixme: nops if we're still connecting, or something.

	//
	// deliver the message
	//
	if (cls.demoplayback || !buf->cursize)
		return;

	/*if (NET_SendUnreliableMessage(cls.netcon, buf) == -1)
	{
		Con_Printf ("CL_SendMove: lost server connection\n");
		CL_Disconnect ();
	}*/

	CL_SendLagMove();
}

/*
============
CL_InitInput
============
*/
void CL_InitInput (void)
{
#undef Cmd_AddCommand
#define Cmd_AddCommand(cmd,fnc) Cmd_AddCommand2(cmd,fnc,src_command,true)
	Cmd_AddCommand ("+moveup",IN_UpDown);
	Cmd_AddCommand ("-moveup",IN_UpUp);
	Cmd_AddCommand ("+movedown",IN_DownDown);
	Cmd_AddCommand ("-movedown",IN_DownUp);
	Cmd_AddCommand ("+left",IN_LeftDown);
	Cmd_AddCommand ("-left",IN_LeftUp);
	Cmd_AddCommand ("+right",IN_RightDown);
	Cmd_AddCommand ("-right",IN_RightUp);
	Cmd_AddCommand ("+forward",IN_ForwardDown);
	Cmd_AddCommand ("-forward",IN_ForwardUp);
	Cmd_AddCommand ("+back",IN_BackDown);
	Cmd_AddCommand ("-back",IN_BackUp);
	Cmd_AddCommand ("+lookup", IN_LookupDown);
	Cmd_AddCommand ("-lookup", IN_LookupUp);
	Cmd_AddCommand ("+lookdown", IN_LookdownDown);
	Cmd_AddCommand ("-lookdown", IN_LookdownUp);
	Cmd_AddCommand ("+strafe", IN_StrafeDown);
	Cmd_AddCommand ("-strafe", IN_StrafeUp);
	Cmd_AddCommand ("+moveleft", IN_MoveleftDown);
	Cmd_AddCommand ("-moveleft", IN_MoveleftUp);
	Cmd_AddCommand ("+moveright", IN_MoverightDown);
	Cmd_AddCommand ("-moveright", IN_MoverightUp);
	Cmd_AddCommand ("+speed", IN_SpeedDown);
	Cmd_AddCommand ("-speed", IN_SpeedUp);
	Cmd_AddCommand ("+attack", IN_AttackDown);
	Cmd_AddCommand ("-attack", IN_AttackUp);
	Cmd_AddCommand ("+use", IN_UseDown);
	Cmd_AddCommand ("-use", IN_UseUp);
	Cmd_AddCommand ("+button3", IN_Button3Down);
	Cmd_AddCommand ("-button3", IN_Button3Up);
	Cmd_AddCommand ("+button4", IN_Button4Down);
	Cmd_AddCommand ("-button4", IN_Button4Up);
	Cmd_AddCommand ("+button5", IN_Button5Down);
	Cmd_AddCommand ("-button5", IN_Button5Up);
	Cmd_AddCommand ("+button6", IN_Button6Down);
	Cmd_AddCommand ("-button6", IN_Button6Up);
	Cmd_AddCommand ("+button7", IN_Button7Down);
	Cmd_AddCommand ("-button7", IN_Button7Up);
	Cmd_AddCommand ("+button8", IN_Button8Down);
	Cmd_AddCommand ("-button8", IN_Button8Up);
	Cmd_AddCommand ("+jump", IN_JumpDown);
	Cmd_AddCommand ("-jump", IN_JumpUp);
	Cmd_AddCommand ("impulse", IN_Impulse);
	Cmd_AddCommand ("+klook", IN_KLookDown);
	Cmd_AddCommand ("-klook", IN_KLookUp);
	Cmd_AddCommand ("+mlook", IN_MLookDown);
	Cmd_AddCommand ("-mlook", IN_MLookUp);

	Cvar_RegisterVariable (&pq_lag); // JPG - synthetic lag // woods #pqlag
	Cvar_RegisterVariable (&cl_iDrive); // woods #idrive

	Wheel_Init ();
}


/*
=============================================================================

WEAPON WHEEL  (Quake remaster style, stock/mission-pack .lmps plus generated axe icon)

=============================================================================
*/

#define WHEEL_RING_STEPS	64
#define WHEEL_STRINGIFY_(x)	#x
#define WHEEL_STRINGIFY(x)	WHEEL_STRINGIFY_(x)
#define WHEEL_AXE_ICON_DISPLAY_WIDTH	32
#define WHEEL_AXE_ICON_DISPLAY_HEIGHT	16
#define WHEEL_AXE_ICON_TEXTURE_SCALE	4
#define WHEEL_AXE_ICON_WIDTH	(WHEEL_AXE_ICON_DISPLAY_WIDTH * WHEEL_AXE_ICON_TEXTURE_SCALE)
#define WHEEL_AXE_ICON_HEIGHT	(WHEEL_AXE_ICON_DISPLAY_HEIGHT * WHEEL_AXE_ICON_TEXTURE_SCALE)
#define WHEEL_AXE_ICON_TRANSPARENT	255
#define WHEEL_AXE_ICON_AXIS_H	2	// model Z: handle length
#define WHEEL_AXE_ICON_AXIS_V	1	// model Y: blade height
#define WHEEL_AXE_ICON_AXIS_D	0	// model X: draw order
#define WHEEL_CONCHARS_GOLD_ZERO	18
#define WHEEL_AXE_LABEL_CHAR		(WHEEL_CONCHARS_GOLD_ZERO + 1)
#define WHEEL_AXE_UNSELECTED_ALPHA	0.45f
#define WHEEL_DEADZONE			0.35f
#define WHEEL_SCALE			0.28f
#define WHEEL_ICON_SCALE		2.75f
#define WHEEL_CENTER_X			0.76f
#define WHEEL_CENTER_Y			0.50f
#define WHEEL_SCREEN_MARGIN		8.0f
#define WHEEL_MOUSE_SENSITIVITY		0.040f
#define WHEEL_DEBUG			0
#define WHEEL_SHADER			1
#define WHEEL_GLOW			30.0f
#define WHEEL_GLOW_OPACITY		0.40
#define WHEEL_FALLBACK_GLOW_OPACITY	0.20f
#define WHEEL_NOISE			0.90f
#define WHEEL_BEVEL			7.0f
#define WHEEL_BEVEL_OPACITY		0.05f
#define WHEEL_OPACITY			0.85f
#define WHEEL_PACK_BASE		0
#define WHEEL_PACK_HIPNOTIC	1
#define WHEEL_PACK_ROGUE	2
#define WHEEL_FLAG_AXE		1
#define WHEEL_SELECT_NORMAL	0
#define WHEEL_SELECT_HIP_PROX	1

typedef struct
{
	const char	*name;
	const char	*icon;			// inv_*  (dim/inactive)
	const char	*icon_active;	// inv2_* (highlighted)
	int		impulse;
	int		item_bit;
	int		rogue_item_bit;		// Rogue changes the axe bit; 0 means item_bit
	int		ammo_stat;
	int		ammo_min;
	int		pack;
	int		flags;
	int		select_mode;
	int		fallback_char;
} wheel_weapon_t;

static const wheel_weapon_t wheel_weapons[] = {
	// Quake remaster visual wheel order: shotgun at top, then 3/4/5/6/7/8/axe
	// counter-clockwise.  Wheel_SlotAngle advances clockwise, so store the
	// clockwise order here.
	{"Shotgun",                "inv_shotgun",  "inv2_shotgun",  2, IT_SHOTGUN,            0,       STAT_SHELLS,  1, WHEEL_PACK_BASE,     0,              WHEEL_SELECT_NORMAL,   '2'},
	{"Axe",                    NULL,           NULL,            1, IT_AXE,                RIT_AXE, 0,            0, WHEEL_PACK_BASE,     WHEEL_FLAG_AXE, WHEEL_SELECT_NORMAL,   WHEEL_AXE_LABEL_CHAR},
	{"Thunderbolt",            "inv_lightng",  "inv2_lightng",  8, IT_LIGHTNING,          0,       STAT_CELLS,   1, WHEEL_PACK_BASE,     0,              WHEEL_SELECT_NORMAL,   '8'},
	{"Rocket Launcher",        "inv_srlaunch", "inv2_srlaunch", 7, IT_ROCKET_LAUNCHER,    0,       STAT_ROCKETS, 1, WHEEL_PACK_BASE,     0,              WHEEL_SELECT_NORMAL,   '7'},
	{"Grenade Launcher",       "inv_rlaunch",  "inv2_rlaunch",  6, IT_GRENADE_LAUNCHER,   0,       STAT_ROCKETS, 1, WHEEL_PACK_BASE,     0,              WHEEL_SELECT_NORMAL,   '6'},
	{"Super Nailgun",          "inv_snailgun", "inv2_snailgun", 5, IT_SUPER_NAILGUN,      0,       STAT_NAILS,   2, WHEEL_PACK_BASE,     0,              WHEEL_SELECT_NORMAL,   '5'},
	{"Nailgun",                "inv_nailgun",  "inv2_nailgun",  4, IT_NAILGUN,            0,       STAT_NAILS,   1, WHEEL_PACK_BASE,     0,              WHEEL_SELECT_NORMAL,   '4'},
	{"Super Shotgun",          "inv_sshotgun", "inv2_sshotgun", 3, IT_SUPER_SHOTGUN,      0,       STAT_SHELLS,  2, WHEEL_PACK_BASE,     0,              WHEEL_SELECT_NORMAL,   '3'},
	{"Proximity Gun",          "inv_prox",     "inv2_prox",     6, HIT_PROXIMITY_GUN,     0,       STAT_ROCKETS, 1, WHEEL_PACK_HIPNOTIC, 0,              WHEEL_SELECT_HIP_PROX, 0},
	{"Laser Cannon",           "inv_laser",    "inv2_laser",  225, HIT_LASER_CANNON,     0,       STAT_CELLS,   1, WHEEL_PACK_HIPNOTIC, 0,              WHEEL_SELECT_NORMAL,   0},
	{"Mjolnir",                "inv_mjolnir",  "inv2_mjolnir",226, HIT_MJOLNIR,          0,       STAT_CELLS,   1, WHEEL_PACK_HIPNOTIC, 0,              WHEEL_SELECT_NORMAL,   0},
	{"Lava Nailgun",           "r_lava",       NULL,           60, RIT_LAVA_NAILGUN,      0,       STAT_NAILS,   1, WHEEL_PACK_ROGUE,    0,              WHEEL_SELECT_NORMAL,   '4'},
	{"Lava Super Nailgun",     "r_superlava",  NULL,           61, RIT_LAVA_SUPER_NAILGUN,0,       STAT_NAILS,   2, WHEEL_PACK_ROGUE,    0,              WHEEL_SELECT_NORMAL,   '5'},
	{"Multi Grenade Launcher", "r_gren",       NULL,           62, RIT_MULTI_GRENADE,     0,       STAT_ROCKETS, 1, WHEEL_PACK_ROGUE,    0,              WHEEL_SELECT_NORMAL,   '6'},
	{"Multi Rocket Launcher",  "r_multirock",  NULL,           63, RIT_MULTI_ROCKET,      0,       STAT_ROCKETS, 1, WHEEL_PACK_ROGUE,    0,              WHEEL_SELECT_NORMAL,   '7'},
	{"Plasma Gun",             "r_plasma",     NULL,           64, RIT_PLASMA_GUN,        0,       STAT_CELLS,   1, WHEEL_PACK_ROGUE,    0,              WHEEL_SELECT_NORMAL,   '8'}
};
#define WHEEL_WEAPON_COUNT ((int)(sizeof(wheel_weapons) / sizeof(wheel_weapons[0])))

static qboolean	wheel_open;
static qboolean	wheel_block_b;		// swallow B until release after cancel
static int	wheel_open_refs;	// held +weaponwheel bindings
static int	wheel_pick;		// 0..Wheel_WeaponCount()-1
static float	wheel_mouse_x;		// virtual stick built from accumulated mouse delta
static float	wheel_mouse_y;
static qboolean	wheel_icons_loaded;
static qpic_t	*wheel_icons[WHEEL_WEAPON_COUNT];
static qpic_t	*wheel_icons_active[WHEEL_WEAPON_COUNT];
static qpic_t	*wheel_box_pic = NULL;	// backtile from gfx.wad -- tiled body texture

// Local mirror of gl_draw.c's file-static glpic_t so we can pull the
// texture and atlas UVs out of a qpic_t->data blob.  Layout must match.
typedef struct { gltexture_t *gltexture; float sl, tl, sh, th; } wheel_glpic_t;
typedef struct { qpic_t pic; byte padding[32]; } wheel_generated_pic_t;
typedef struct
{
	const byte	*skin;
	const stvert_t	*stverts;
	const dtriangle_t *triangles;
	const trivertx_t *verts;
	float		scale[3];
	float		scale_origin[3];
	int		skinwidth;
	int		skinheight;
	int		numverts;
	int		numtris;
} wheel_axe_model_t;

static wheel_generated_pic_t wheel_axe_icon_storage;
static byte	wheel_axe_icon_pixels[WHEEL_AXE_ICON_WIDTH * WHEEL_AXE_ICON_HEIGHT];
static byte	wheel_axe_icon_rgba_pixels[WHEEL_AXE_ICON_WIDTH * WHEEL_AXE_ICON_HEIGHT * 4];
static float	wheel_axe_icon_zbuf[WHEEL_AXE_ICON_WIDTH * WHEEL_AXE_ICON_HEIGHT];
static qpic_t	*wheel_axe_icon_pic;
static qboolean	wheel_axe_icon_attempted;
static const plcolour_t wheel_white = { .type = 2, .rgb = { 0xff, 0xff, 0xff }, .basic = 0 };

static GLuint	wheel_shader_program = 0;
static qboolean	wheel_shader_init_attempted = false;
static GLint	wheel_shader_u_center = -1;
static GLint	wheel_shader_u_radii = -1;
static GLint	wheel_shader_u_glow = -1;
static GLint	wheel_shader_u_body = -1;
static GLint	wheel_shader_u_rim = -1;
static GLint	wheel_shader_u_noise = -1;	// (scale, amount) -- legacy, drives box-texture amount
static GLint	wheel_shader_u_bevel = -1;	// rim Gaussian width in pixels
static GLint	wheel_shader_u_box_uv = -1;	// atlas remap (sl, tl, sh, th)
static GLint	wheel_shader_u_box_scale = -1;	// tile frequency (1/px)

static void Wheel_DebugMsg (const char *msg)
{
	if (WHEEL_DEBUG)
		Con_Printf ("weaponwheel: %s\n", msg);
}

static qboolean Wheel_Allowed (qboolean quiet)
{
	if (key_dest != key_game)
	{
		if (!quiet) Wheel_DebugMsg ("blocked (not in game)");
		return false;
	}
	if (cls.state != ca_connected || cls.signon < SIGNONS)
	{
		if (!quiet) Wheel_DebugMsg ("blocked (still loading)");
		return false;
	}
	if (cl.paused || cl.match_pause_time > 0)
	{
		if (!quiet) Wheel_DebugMsg ("blocked (paused)");
		return false;
	}
	if (cls.demoplayback)
	{
		if (!quiet) Wheel_DebugMsg ("blocked (demo)");
		return false;
	}
	if (cl.intermission)
	{
		if (!quiet) Wheel_DebugMsg ("blocked (intermission)");
		return false;
	}
	if (cl.stats[STAT_HEALTH] <= 0)
	{
		if (!quiet) Wheel_DebugMsg ("blocked (dead)");
		return false;
	}
	return true;
}

static qboolean Wheel_WeaponAvailable (const wheel_weapon_t *weapon)
{
	if (weapon->pack == WHEEL_PACK_HIPNOTIC)
		return hipnotic;
	if (weapon->pack == WHEEL_PACK_ROGUE)
		return rogue;
	return true;
}

static int Wheel_WeaponCount (void)
{
	int i, count = 0;

	for (i = 0; i < WHEEL_WEAPON_COUNT; i++)
	{
		if (Wheel_WeaponAvailable (&wheel_weapons[i]))
			count++;
	}
	return count;
}

static int Wheel_WeaponIndexForSlot (int slot)
{
	int i, count = 0;

	if (slot < 0)
		return -1;
	for (i = 0; i < WHEEL_WEAPON_COUNT; i++)
	{
		if (!Wheel_WeaponAvailable (&wheel_weapons[i]))
			continue;
		if (count == slot)
			return i;
		count++;
	}
	return -1;
}

static const wheel_weapon_t *Wheel_WeaponForSlot (int slot)
{
	int index = Wheel_WeaponIndexForSlot (slot);
	return (index >= 0) ? &wheel_weapons[index] : NULL;
}

static int Wheel_WeaponItemBit (const wheel_weapon_t *weapon)
{
	if (rogue && weapon->rogue_item_bit)
		return weapon->rogue_item_bit;
	return weapon->item_bit;
}

static qboolean Wheel_WeaponIsCurrent (const wheel_weapon_t *weapon)
{
	return cl.stats[STAT_ACTIVEWEAPON] == Wheel_WeaponItemBit (weapon);
}

static qboolean Wheel_WeaponOwned (int slot)
{
	const wheel_weapon_t *weapon = Wheel_WeaponForSlot (slot);
	if (!weapon)
		return false;
	return (cl.items & Wheel_WeaponItemBit (weapon)) != 0;
}

static qboolean Wheel_WeaponSelectable (int slot)
{
	const wheel_weapon_t *weapon = Wheel_WeaponForSlot (slot);

	if (!weapon)
		return false;
	if (!Wheel_WeaponOwned (slot))
		return false;
	if (!weapon->ammo_stat || weapon->ammo_min <= 0)
		return true;
	return cl.stats[weapon->ammo_stat] >= weapon->ammo_min;
}

static int Wheel_NearestSelectableSlot (int slot, int prefer_direction)
{
	int count, step, dir;

	count = Wheel_WeaponCount ();
	if (count <= 0)
		return slot;
	if (slot < 0 || slot >= count)
		slot = 0;
	if (Wheel_WeaponSelectable (slot))
		return slot;

	dir = (prefer_direction < 0) ? -1 : 1;
	for (step = 1; step < count; step++)
	{
		int forward = (slot + dir * step + count) % count;
		int backward = (slot - dir * step + count) % count;

		if (Wheel_WeaponSelectable (forward))
			return forward;
		if (Wheel_WeaponSelectable (backward))
			return backward;
	}

	return slot;
}

static int Wheel_SlotDistance (int a, int b, int count)
{
	int dist;

	if (count <= 0)
		return 0;
	dist = abs (a - b) % count;
	return q_min (dist, count - dist);
}

static int Wheel_CurrentWeaponSlot (void)
{
	int weap = cl.stats[STAT_ACTIVEWEAPON];
	int i, slot = 0;

	for (i = 0; i < WHEEL_WEAPON_COUNT; i++)
	{
		if (!Wheel_WeaponAvailable (&wheel_weapons[i]))
			continue;
		if (weap == Wheel_WeaponItemBit (&wheel_weapons[i]))
			return Wheel_NearestSelectableSlot (slot, 1);
		slot++;
	}
	return Wheel_NearestSelectableSlot (0, 1);
}

static float Wheel_Radius (void)
{
	float dim = (float)q_min (glwidth, glheight);
	float scale = CLAMP (0.12f, WHEEL_SCALE, 0.45f);
	return dim * scale;
}

static void Wheel_Position (float radius, float *cx, float *cy)
{
	float margin = WHEEL_SCREEN_MARGIN + CLAMP (0.0f, WHEEL_GLOW, 96.0f);
	float min_x = radius + margin;
	float max_x = (float)glwidth - radius - margin;
	float min_y = radius + margin;
	float max_y = (float)glheight - radius - margin;

	*cx = (float)glwidth * WHEEL_CENTER_X;
	*cy = (float)glheight * WHEEL_CENTER_Y;

	if (max_x >= min_x)
		*cx = CLAMP (min_x, *cx, max_x);
	else
		*cx = (float)glwidth * 0.5f;

	if (max_y >= min_y)
		*cy = CLAMP (min_y, *cy, max_y);
	else
		*cy = (float)glheight * 0.5f;
}

static float Wheel_SlotAngle (int slot)
{
	// sector 0 at top (12 o'clock), advance clockwise.  Screen Y grows downward
	// so "up" is -y; that matches Wheel_SetPickFromVector below.
	int count = q_max (1, Wheel_WeaponCount ());
	float span = 2.0f * (float)M_PI / (float)count;
	return -0.5f * (float)M_PI + slot * span;
}

static void Wheel_SetPickFromVector (float x, float y)
{
	float deadzone, mag, angle, span;
	int sector, count;

	mag = sqrtf (x * x + y * y);
	deadzone = CLAMP (0.0f, WHEEL_DEADZONE, 0.95f);
	if (mag < deadzone)
		return;

	// stick +x = right, stick +y = up.  Want sector 0 at top, clockwise.
	angle = atan2f (x, y);
	if (angle < 0)
		angle += 2.0f * (float)M_PI;

	count = Wheel_WeaponCount ();
	if (count <= 0)
		return;
	span = 2.0f * (float)M_PI / count;
	sector = (int)((angle + span * 0.5f) / span) % count;
	if (!Wheel_WeaponSelectable (sector) &&
		wheel_pick >= 0 && wheel_pick < count &&
		Wheel_WeaponSelectable (wheel_pick))
	{
		// Keep an equally near current pick stable instead of oscillating
		// between both neighbors of an unavailable sector.
		int nearest = Wheel_NearestSelectableSlot (sector, 1);
		if (Wheel_SlotDistance (sector, wheel_pick, count) <=
			Wheel_SlotDistance (sector, nearest, count))
			return;
	}
	{
		int prefer_direction = 1;
		if (wheel_pick >= 0 && wheel_pick < count)
		{
			int delta = sector - wheel_pick;
			if (delta > count / 2)
				delta -= count;
			else if (delta < -count / 2)
				delta += count;
			if (delta < 0)
				prefer_direction = -1;
		}
		wheel_pick = Wheel_NearestSelectableSlot (sector, prefer_direction);
	}
}

static qboolean Wheel_RangeOK (const byte *base, const byte *end, const byte *ptr, size_t len)
{
	return ptr >= base && ptr <= end && len <= (size_t)(end - ptr);
}

static qboolean Wheel_ParseAxeModel (const byte *model, size_t filesize, wheel_axe_model_t *out)
{
	const mdl_t *pinmodel;
	const byte *end, *p;
	size_t skin_size;
	int i, j, type, numskins, groupskins;

	memset (out, 0, sizeof(*out));

	if (filesize < sizeof(*pinmodel))
		return false;

	pinmodel = (const mdl_t *)model;
	if (LittleLong (pinmodel->ident) != IDPOLYHEADER ||
		LittleLong (pinmodel->version) != ALIAS_VERSION)
		return false;

	out->skinwidth = LittleLong (pinmodel->skinwidth);
	out->skinheight = LittleLong (pinmodel->skinheight);
	numskins = LittleLong (pinmodel->numskins);
	out->numverts = LittleLong (pinmodel->numverts);
	out->numtris = LittleLong (pinmodel->numtris);
	for (i = 0; i < 3; i++)
	{
		out->scale[i] = LittleFloat (pinmodel->scale[i]);
		out->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
	}
	if (out->skinwidth <= 0 || out->skinheight <= 0 || numskins <= 0 ||
		numskins > MAX_SKINS || LittleLong (pinmodel->numframes) <= 0 ||
		out->skinwidth > 4096 || out->skinheight > 4096 ||
		out->numverts <= 0 || out->numverts > MAXALIASVERTS ||
		out->numtris <= 0 || out->numtris > 65535)
		return false;

	skin_size = (size_t)out->skinwidth * (size_t)out->skinheight;
	if (skin_size / (size_t)out->skinwidth != (size_t)out->skinheight)
		return false;

	end = model + filesize;
	p = model + sizeof(*pinmodel);

	for (i = 0; i < numskins; i++)
	{
		if (!Wheel_RangeOK (model, end, p, sizeof(daliasskintype_t)))
			return false;

		type = LittleLong (((const daliasskintype_t *)p)->type);
		p += sizeof(daliasskintype_t);

		if (type == ALIAS_SKIN_SINGLE)
		{
			if (!Wheel_RangeOK (model, end, p, skin_size))
				return false;
			if (i == 0)
				out->skin = p;
			p += skin_size;
		}
		else if (type == ALIAS_SKIN_GROUP)
		{
			if (!Wheel_RangeOK (model, end, p, sizeof(daliasskingroup_t)))
				return false;
			groupskins = LittleLong (((const daliasskingroup_t *)p)->numskins);
			if (groupskins <= 0 || groupskins > MAX_SKINS)
				return false;

			p += sizeof(daliasskingroup_t);
			if (!Wheel_RangeOK (model, end, p, sizeof(daliasskininterval_t) * (size_t)groupskins))
				return false;
			p += sizeof(daliasskininterval_t) * (size_t)groupskins;

			for (j = 0; j < groupskins; j++)
			{
				if (!Wheel_RangeOK (model, end, p, skin_size))
					return false;
				if (i == 0 && j == 0)
					out->skin = p;
				p += skin_size;
			}
		}
		else
			return false;
	}

	if (!out->skin)
		return false;

	if (!Wheel_RangeOK (model, end, p, sizeof(stvert_t) * (size_t)out->numverts))
		return false;
	out->stverts = (const stvert_t *)p;
	p += sizeof(stvert_t) * (size_t)out->numverts;

	if (!Wheel_RangeOK (model, end, p, sizeof(dtriangle_t) * (size_t)out->numtris))
		return false;
	out->triangles = (const dtriangle_t *)p;
	p += sizeof(dtriangle_t) * (size_t)out->numtris;

	if (!Wheel_RangeOK (model, end, p, sizeof(daliasframetype_t)))
		return false;
	type = LittleLong (((const daliasframetype_t *)p)->type);
	p += sizeof(daliasframetype_t);

	if (type == ALIAS_SINGLE)
	{
		if (!Wheel_RangeOK (model, end, p, sizeof(daliasframe_t) + sizeof(trivertx_t) * (size_t)out->numverts))
			return false;
		out->verts = (const trivertx_t *)((const daliasframe_t *)p + 1);
	}
	else if (type == ALIAS_GROUP)
	{
		int groupframes;

		if (!Wheel_RangeOK (model, end, p, sizeof(daliasgroup_t)))
			return false;
		groupframes = LittleLong (((const daliasgroup_t *)p)->numframes);
		if (groupframes <= 0 || groupframes > MAXALIASFRAMES)
			return false;

		p += sizeof(daliasgroup_t);
		if (!Wheel_RangeOK (model, end, p, sizeof(daliasinterval_t) * (size_t)groupframes))
			return false;
		p += sizeof(daliasinterval_t) * (size_t)groupframes;

		if (!Wheel_RangeOK (model, end, p, sizeof(daliasframe_t) + sizeof(trivertx_t) * (size_t)out->numverts))
			return false;
		out->verts = (const trivertx_t *)((const daliasframe_t *)p + 1);
	}
	else
		return false;

	return true;
}

static float Wheel_AxeUVEdge (float ax, float ay, float bx, float by, float px, float py)
{
	return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static qboolean Wheel_AxeTriangleData (const wheel_axe_model_t *mdl, const dtriangle_t *tri,
	int vertindex[3], float s[3], float t[3])
{
	int facesfront = LittleLong (tri->facesfront);
	int j;

	for (j = 0; j < 3; j++)
	{
		int st_s, st_t, onseam;

		vertindex[j] = LittleLong (tri->vertindex[j]);
		if (vertindex[j] < 0 || vertindex[j] >= mdl->numverts)
			return false;

		st_s = LittleLong (mdl->stverts[vertindex[j]].s);
		st_t = LittleLong (mdl->stverts[vertindex[j]].t);
		onseam = LittleLong (mdl->stverts[vertindex[j]].onseam);
		if (!facesfront && onseam)
			st_s += mdl->skinwidth / 2;

		s[j] = (float)st_s;
		t[j] = (float)st_t;
	}

	return true;
}

static qboolean Wheel_AxeTriangleSelected (const wheel_axe_model_t *mdl, const float s[3], const float t[3])
{
	float cx = (s[0] + s[1] + s[2]) * (1.0f / 3.0f);
	float cy = (t[0] + t[1] + t[2]) * (1.0f / 3.0f);

	return cx < (float)mdl->skinwidth * 0.5f &&
		cy < (float)mdl->skinheight * 0.5f;
}

static float Wheel_AxeVertexAxis (const wheel_axe_model_t *mdl, int vertindex, int axis)
{
	return (float)mdl->verts[vertindex].v[axis] * mdl->scale[axis] + mdl->scale_origin[axis];
}

static qboolean Wheel_AxeDrawModelTriangle (const wheel_axe_model_t *mdl, const int vertindex[3],
	const float st_s[3], const float st_t[3], float center_h, float center_v, float icon_scale,
	const byte *skin, int skinwidth, int skinheight, qboolean skin_rgba, float *zbuf, byte *out)
{
	float sx[3], sy[3], depth[3];
	float area;
	int minx, maxx, miny, maxy;
	int x, y;
	qboolean drew = false;

	for (int i = 0; i < 3; i++)
	{
		float h = Wheel_AxeVertexAxis (mdl, vertindex[i], WHEEL_AXE_ICON_AXIS_H);
		float v = Wheel_AxeVertexAxis (mdl, vertindex[i], WHEEL_AXE_ICON_AXIS_V);

		sx[i] = (h - center_h) * icon_scale + (float)WHEEL_AXE_ICON_WIDTH * 0.5f;
		sy[i] = (float)(WHEEL_AXE_ICON_HEIGHT - 1) -
			((v - center_v) * icon_scale + (float)WHEEL_AXE_ICON_HEIGHT * 0.5f);
		depth[i] = Wheel_AxeVertexAxis (mdl, vertindex[i], WHEEL_AXE_ICON_AXIS_D);
	}

	area = Wheel_AxeUVEdge (sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
	if (fabsf (area) < 0.01f)
		return false;

	minx = (int)floorf (q_min (q_min (sx[0], sx[1]), sx[2])) - 1;
	maxx = (int)ceilf  (q_max (q_max (sx[0], sx[1]), sx[2])) + 1;
	miny = (int)floorf (q_min (q_min (sy[0], sy[1]), sy[2])) - 1;
	maxy = (int)ceilf  (q_max (q_max (sy[0], sy[1]), sy[2])) + 1;

	minx = CLAMP (0, minx, WHEEL_AXE_ICON_WIDTH - 1);
	maxx = CLAMP (0, maxx, WHEEL_AXE_ICON_WIDTH - 1);
	miny = CLAMP (0, miny, WHEEL_AXE_ICON_HEIGHT - 1);
	maxy = CLAMP (0, maxy, WHEEL_AXE_ICON_HEIGHT - 1);

	for (y = miny; y <= maxy; y++)
	{
		for (x = minx; x <= maxx; x++)
		{
			float px = (float)x + 0.5f;
			float py = (float)y + 0.5f;
			float e0 = Wheel_AxeUVEdge (sx[1], sy[1], sx[2], sy[2], px, py);
			float e1 = Wheel_AxeUVEdge (sx[2], sy[2], sx[0], sy[0], px, py);
			float e2 = Wheel_AxeUVEdge (sx[0], sy[0], sx[1], sy[1], px, py);
			float w0, w1, w2, d, us, vt;
			int idx, si, ti;

			if (!((area > 0.0f && e0 >= -0.01f && e1 >= -0.01f && e2 >= -0.01f) ||
				(area < 0.0f && e0 <=  0.01f && e1 <=  0.01f && e2 <=  0.01f)))
				continue;

			w0 = e0 / area;
			w1 = e1 / area;
			w2 = e2 / area;
			d = w0 * depth[0] + w1 * depth[1] + w2 * depth[2];

			idx = y * WHEEL_AXE_ICON_WIDTH + x;
			if (d < zbuf[idx])
				continue;

			us = w0 * st_s[0] + w1 * st_s[1] + w2 * st_s[2];
			vt = w0 * st_t[0] + w1 * st_t[1] + w2 * st_t[2];

			if (skin_rgba)
			{
				const byte *rgba;
				byte *dst;

				si = CLAMP (0, (int)(((us + 0.5f) / (float)mdl->skinwidth) * (float)skinwidth), skinwidth - 1);
				ti = CLAMP (0, (int)(((vt + 0.5f) / (float)mdl->skinheight) * (float)skinheight), skinheight - 1);
				rgba = skin + (ti * skinwidth + si) * 4;
				if (!rgba[3])
					continue;

				zbuf[idx] = d;
				dst = out + idx * 4;
				dst[0] = rgba[0];
				dst[1] = rgba[1];
				dst[2] = rgba[2];
				dst[3] = rgba[3];
				drew = true;
			}
			else
			{
				byte c;

				si = CLAMP (0, (int)(us + 0.5f), skinwidth - 1);
				ti = CLAMP (0, (int)(vt + 0.5f), skinheight - 1);
				c = skin[ti * skinwidth + si];
				if (c == WHEEL_AXE_ICON_TRANSPARENT)
					continue;

				zbuf[idx] = d;
				out[idx] = c;
				drew = true;
			}
		}
	}

	return drew;
}

static qboolean Wheel_BuildAxeIconPixels (const wheel_axe_model_t *mdl,
	const byte *skin, int skinwidth, int skinheight, qboolean skin_rgba, byte *out)
{
	float min_h =  999999.0f, max_h = -999999.0f;
	float min_v =  999999.0f, max_v = -999999.0f;
	float center_h, center_v, icon_scale, hscale, vscale, pad;
	float *zbuf = wheel_axe_icon_zbuf;
	int i, j, selected = 0;
	qboolean drew = false;

	if (!skin || skinwidth <= 0 || skinheight <= 0)
		return false;

	if (skin_rgba)
		memset (out, 0, WHEEL_AXE_ICON_WIDTH * WHEEL_AXE_ICON_HEIGHT * 4);
	else
		memset (out, WHEEL_AXE_ICON_TRANSPARENT, WHEEL_AXE_ICON_WIDTH * WHEEL_AXE_ICON_HEIGHT);

	for (i = 0; i < mdl->numtris; i++)
	{
		int vertindex[3];
		float s[3], t[3];

		if (!Wheel_AxeTriangleData (mdl, &mdl->triangles[i], vertindex, s, t) ||
			!Wheel_AxeTriangleSelected (mdl, s, t))
			continue;

		selected++;
		for (j = 0; j < 3; j++)
		{
			float h = Wheel_AxeVertexAxis (mdl, vertindex[j], WHEEL_AXE_ICON_AXIS_H);
			float v = Wheel_AxeVertexAxis (mdl, vertindex[j], WHEEL_AXE_ICON_AXIS_V);

			if (h < min_h) min_h = h;
			if (h > max_h) max_h = h;
			if (v < min_v) min_v = v;
			if (v > max_v) max_v = v;
		}
	}

	if (!selected || max_h <= min_h || max_v <= min_v)
		return false;

	pad = (max_h - min_h) * 0.08f;
	min_h -= pad; max_h += pad;
	pad = (max_v - min_v) * 0.08f;
	min_v -= pad; max_v += pad;

	center_h = (min_h + max_h) * 0.5f;
	center_v = (min_v + max_v) * 0.5f;
	hscale = (float)(WHEEL_AXE_ICON_WIDTH - 1) / (max_h - min_h);
	vscale = (float)(WHEEL_AXE_ICON_HEIGHT - 1) / (max_v - min_v);
	icon_scale = q_min (hscale, vscale);

	for (i = 0; i < WHEEL_AXE_ICON_WIDTH * WHEEL_AXE_ICON_HEIGHT; i++)
		zbuf[i] = -999999.0f;

	for (i = 0; i < mdl->numtris; i++)
	{
		int vertindex[3];
		float s[3], t[3];

		if (!Wheel_AxeTriangleData (mdl, &mdl->triangles[i], vertindex, s, t) ||
			!Wheel_AxeTriangleSelected (mdl, s, t))
			continue;

		if (Wheel_AxeDrawModelTriangle (mdl, vertindex, s, t, center_h, center_v, icon_scale,
			skin, skinwidth, skinheight, skin_rgba, zbuf, out))
			drew = true;
	}

	return drew;
}

static qpic_t *Wheel_LoadAxeIcon (void)
{
	byte *model;
	const char *model_path = "progs/v_axe.mdl";
	wheel_axe_model_t mdl;
	wheel_glpic_t gl;
	size_t filesize;
	unsigned int flags;
	enum srcformat icon_format = SRC_INDEXED;
	byte *icon_pixels = wheel_axe_icon_pixels;

	if (wheel_axe_icon_attempted)
		return wheel_axe_icon_pic;
	wheel_axe_icon_attempted = true;

	model = COM_LoadTempFile (model_path, NULL);
	if (!model)
	{
		model_path = "v_axe.mdl";
		model = COM_LoadTempFile (model_path, NULL);
	}
	if (!model || com_filesize < (qofs_t)sizeof(mdl_t))
		return NULL;

	filesize = (size_t)com_filesize;
	if (!Wheel_ParseAxeModel (model, filesize, &mdl))
		return NULL;

	if (gl_load24bit.value > 0)
	{
		int mark = Hunk_LowMark ();
		int fwidth = 0, fheight = 0;
		qboolean malloced = false;
		enum srcformat fmt = SRC_RGBA;
		char filename[MAX_QPATH];
		byte *data;

		q_snprintf (filename, sizeof(filename), "%s_0", model_path);
		data = Image_LoadImage (filename, &fwidth, &fheight, &fmt, &malloced);
		if (data && fmt == SRC_RGBA &&
			Wheel_BuildAxeIconPixels (&mdl, data, fwidth, fheight, true, wheel_axe_icon_rgba_pixels))
		{
			icon_format = SRC_RGBA;
			icon_pixels = wheel_axe_icon_rgba_pixels;
		}

		if (malloced)
			free (data);
		Hunk_FreeToLowMark (mark);
	}

	if (icon_format != SRC_RGBA &&
		!Wheel_BuildAxeIconPixels (&mdl, mdl.skin, mdl.skinwidth, mdl.skinheight, false, wheel_axe_icon_pixels))
		return NULL;

	flags = TEXPREF_LINEAR | TEXPREF_ALPHA | TEXPREF_PERSIST |
		TEXPREF_NOPICMIP | TEXPREF_PAD | TEXPREF_OVERWRITE;
	gl.gltexture = TexMgr_LoadImage (NULL, "weaponwheel_axe", WHEEL_AXE_ICON_WIDTH,
		WHEEL_AXE_ICON_HEIGHT, icon_format, icon_pixels, "",
		(src_offset_t)icon_pixels, flags);
	if (!gl.gltexture)
		return NULL;

	gl.sl = 0;
	gl.sh = (float)WHEEL_AXE_ICON_WIDTH / (float)TexMgr_PadConditional (WHEEL_AXE_ICON_WIDTH);
	gl.tl = 0;
	gl.th = (float)WHEEL_AXE_ICON_HEIGHT / (float)TexMgr_PadConditional (WHEEL_AXE_ICON_HEIGHT);

	wheel_axe_icon_storage.pic.width = WHEEL_AXE_ICON_DISPLAY_WIDTH;
	wheel_axe_icon_storage.pic.height = WHEEL_AXE_ICON_DISPLAY_HEIGHT;
	memcpy (wheel_axe_icon_storage.pic.data, &gl, sizeof(gl));

	wheel_axe_icon_pic = &wheel_axe_icon_storage.pic;
	return wheel_axe_icon_pic;
}

static void Wheel_LoadIcons (void)
{
	int i;
	if (wheel_icons_loaded)
		return;
	for (i = 0; i < WHEEL_WEAPON_COUNT; i++)
	{
		if (!Wheel_WeaponAvailable (&wheel_weapons[i]))
		{
			wheel_icons[i] = NULL;
			wheel_icons_active[i] = NULL;
			continue;
		}
		wheel_icons[i] = wheel_weapons[i].icon ?
			Draw_PicFromWad2 (wheel_weapons[i].icon, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP) : NULL;
		wheel_icons_active[i] = wheel_weapons[i].icon_active ?
			Draw_PicFromWad2 (wheel_weapons[i].icon_active, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP) : NULL;
		if (wheel_icons[i] == pic_nul)
			wheel_icons[i] = NULL;
		if (wheel_icons_active[i] == pic_nul)
			wheel_icons_active[i] = NULL;
		if ((wheel_weapons[i].flags & WHEEL_FLAG_AXE) && !wheel_icons[i])
			wheel_icons[i] = Wheel_LoadAxeIcon ();
		if (!wheel_icons_active[i])
			wheel_icons_active[i] = wheel_icons[i];
	}
	// BACKTILE from gfx.wad: loaded without PAD/ALPHA so it tiles cleanly
	// (same flags the engine uses for draw_backtile in gl_draw.c).
	wheel_box_pic = Draw_PicFromWad2 ("backtile", TEXPREF_NOPICMIP);
	if (wheel_box_pic == pic_nul)
		wheel_box_pic = NULL;
	wheel_icons_loaded = true;
}

qboolean Wheel_IsOpen (void)
{
	return wheel_open;
}

qboolean Wheel_BlockBButton (void)
{
	return wheel_block_b;
}

void Wheel_BlockBButtonRelease (void)
{
	wheel_block_b = true;
}

void Wheel_ClearBBlock (void)
{
	wheel_block_b = false;
}

void Wheel_UpdateSelection (float stick_x, float stick_y)
{
	if (!wheel_open)
		return;
	Wheel_SetPickFromVector (stick_x, stick_y);
}

void Wheel_ScrollSelection (int direction)
{
	int count, start, i;

	if (!wheel_open || !direction)
		return;

	count = Wheel_WeaponCount ();
	if (count <= 0)
		return;

	start = (wheel_pick >= 0 && wheel_pick < count) ? wheel_pick : Wheel_CurrentWeaponSlot ();
	direction = (direction > 0) ? 1 : -1;

	for (i = 1; i <= count; i++)
	{
		int slot = (start + direction * i + count) % count;
		if (Wheel_WeaponSelectable (slot))
		{
			wheel_pick = slot;
			return;
		}
	}
}

void Wheel_UpdateMouse (float dx, float dy)
{
	float sens, mag;

	if (!wheel_open)
		return;

	sens = WHEEL_MOUSE_SENSITIVITY;
	// invert dy: screen-space delta y grows downward, but we want "mouse up = top sector".
	wheel_mouse_x += dx * sens;
	wheel_mouse_y -= dy * sens;

	mag = sqrtf (wheel_mouse_x * wheel_mouse_x + wheel_mouse_y * wheel_mouse_y);
	if (mag > 1.0f)
	{
		wheel_mouse_x /= mag;
		wheel_mouse_y /= mag;
	}
	Wheel_SetPickFromVector (wheel_mouse_x, wheel_mouse_y);
}

static void Wheel_SelectCommand (const wheel_weapon_t *weapon, char *cmd, size_t cmdsize)
{
	cmd[0] = 0;

	if (weapon->select_mode == WHEEL_SELECT_HIP_PROX)
	{
		if (cl.stats[STAT_ACTIVEWEAPON] == HIT_PROXIMITY_GUN)
			return;

		// Hipnotic has no direct proximity impulse.  The official progs toggle
		// proximity from the grenade launcher on a second impulse 6.
		if ((cl.items & IT_GRENADE_LAUNCHER) &&
			cl.stats[STAT_ACTIVEWEAPON] != IT_GRENADE_LAUNCHER)
			q_snprintf (cmd, cmdsize, "impulse 6\nwait\nimpulse 6\n");
		else
			q_snprintf (cmd, cmdsize, "impulse 6\n");
		return;
	}

	if (Wheel_WeaponIsCurrent (weapon))
		return;
	q_snprintf (cmd, cmdsize, "impulse %i\n", weapon->impulse);
}

static qboolean Wheel_Open (void)
{
	const wheel_weapon_t *weapon;

	if (wheel_open)
		return true;
	if (!Wheel_Allowed (false))
		return false;

	Wheel_LoadIcons ();
	wheel_pick = Wheel_CurrentWeaponSlot ();
	wheel_mouse_x = 0.0f;
	wheel_mouse_y = 0.0f;

	wheel_open = true;
	weapon = Wheel_WeaponForSlot (wheel_pick);
	Wheel_DebugMsg (va ("open (pick=%i %s)", wheel_pick, weapon ? weapon->name : "none"));
	return true;
}

static void Wheel_CloseSelect (void)
{
	const wheel_weapon_t *weapon;
	char cmd[64];
	int count;

	if (!wheel_open)
		return;
	if (!Wheel_Allowed (true))
	{
		Wheel_Reset ();
		return;
	}

	count = Wheel_WeaponCount ();
	if (count <= 0)
	{
		Wheel_Reset ();
		return;
	}
	if (wheel_pick < 0 || wheel_pick >= count || !Wheel_WeaponSelectable (wheel_pick))
		wheel_pick = Wheel_NearestSelectableSlot (wheel_pick, 1);

	wheel_open = false;
	wheel_open_refs = 0;

	weapon = Wheel_WeaponForSlot (wheel_pick);
	if (!weapon)
	{
		Wheel_DebugMsg (va ("close (slot %i invalid)", wheel_pick));
		return;
	}
	if (!Wheel_WeaponSelectable (wheel_pick))
	{
		Wheel_DebugMsg (va ("close (slot %i not selectable)", wheel_pick));
		return;
	}

	Wheel_SelectCommand (weapon, cmd, sizeof(cmd));
	if (!cmd[0])
	{
		Wheel_DebugMsg (va ("close (%s already active)", weapon->name));
		return;
	}

	Cbuf_AddText (cmd);
	Wheel_DebugMsg (va ("commit %s", weapon->name));
}

void Wheel_Cancel (void)
{
	if (!wheel_open)
		return;
	wheel_open = false;
	wheel_open_refs = 0;
	Wheel_DebugMsg ("cancel");
}

void Wheel_ClearIcons (void)
{
	memset (wheel_icons, 0, sizeof(wheel_icons));
	memset (wheel_icons_active, 0, sizeof(wheel_icons_active));
	wheel_box_pic = NULL;
	wheel_icons_loaded = false;
	wheel_axe_icon_pic = NULL;
	wheel_axe_icon_attempted = false;
}

void Wheel_Reset (void)
{
	wheel_open = false;
	wheel_open_refs = 0;
	wheel_block_b = false;
	wheel_mouse_x = 0.0f;
	wheel_mouse_y = 0.0f;
}

static void Wheel_OpenDown (void)
{
	if (wheel_open)
	{
		wheel_open_refs++;
		return;
	}
	if (Wheel_Open ())
		wheel_open_refs = 1;
}

static void Wheel_OpenUp (void)
{
	if (!wheel_open)
	{
		wheel_open_refs = 0;
		return;
	}
	if (wheel_open_refs > 1)
	{
		wheel_open_refs--;
		return;
	}
	wheel_open_refs = 0;
	Wheel_CloseSelect ();
}

void Wheel_Init (void)
{
	Cmd_AddCommand ("+weaponwheel", Wheel_OpenDown);
	Cmd_AddCommand ("-weaponwheel", Wheel_OpenUp);

	Wheel_Reset ();
}

/*
===============
Wheel rendering: remaster-style translucent ring + small arrow pointer.
No fullscreen dim, no sector wedges.
===============
*/

/*
===============
GLSL wheel: one quad, fragment shader does body + outer glow + uniform bevel
rims + procedural noise tint.  Falls back to the primitive path below when
GLSL is unavailable or WHEEL_SHADER is 0.
===============
*/

static qboolean Wheel_InitShader (void)
{
	static const GLchar *vertSrc =
		"varying vec2 vPos;\n"
		"void main(void) {\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		"	vPos = gl_Vertex.xy;\n"
		"}\n";

	static const GLchar *fragSrc =
		"uniform vec2  uCenter;\n"
		"uniform vec2  uRadii;       // (r_in, r_out) in pixels\n"
		"uniform float uGlow;        // outer glow falloff radius in pixels\n"
		"uniform vec4  uBody;        // ring body RGBA\n"
		"uniform vec4  uRim;         // bevel rim RGBA (color multiplied by intensity)\n"
		"uniform vec2  uNoise;       // (unused, amount 0..1) -- box texture blend\n"
		"uniform float uBevel;       // bevel Gaussian width in pixels\n"
		"uniform sampler2D uBoxTex;  // gfx/backtile atlas\n"
		"uniform vec4  uBoxUV;       // (sl, tl, sh, th) sub-rect in atlas\n"
		"uniform float uBoxScale;    // tile frequency (1/px)\n"
		"varying vec2  vPos;\n"
		"\n"
		"void main(void) {\n"
		"	float d = length(vPos - uCenter);\n"
		"	float r_in = uRadii.x;\n"
		"	float r_out = uRadii.y;\n"
		"	float feather = 1.0;\n"
		"\n"
		"	// 1px AA annulus\n"
		"	float outer = smoothstep(r_out, r_out - feather, d);\n"
		"	float hole  = smoothstep(r_in + feather, r_in - feather, d);\n"
		"	float ring  = clamp(outer - hole, 0.0, 1.0);\n"
		"\n"
		"	// outer halo: fades from 1 at r_out to 0 at r_out+uGlow, only beyond the ring\n"
		"	float glow_out = (1.0 - smoothstep(r_out, r_out + uGlow, d)) * (1.0 - outer);\n"
		"	// inner halo: peaks at r_in, fades toward center over uGlow pixels, only inside the hole\n"
		"	float glow_in  = smoothstep(r_in - uGlow, r_in, d) * hole;\n"
		"	float glow = glow_out + glow_in;\n"
		"\n"
		"	// backtile from gfx.wad, tiled across the ring body, atlas-remapped\n"
		"	vec2 uv = fract(vPos * uBoxScale);\n"
		"	uv = mix(uBoxUV.xy, uBoxUV.zw, uv);\n"
		"	vec3 box  = texture2D(uBoxTex, uv).rgb;\n"
		"	vec3 body = mix(uBody.rgb, box, clamp(uNoise.y, 0.0, 1.0));\n"
		"\n"
		"	// uniform bright rim near both edges (Gaussian band, clipped to ring)\n"
		"	float band = max(uBevel, 0.5);\n"
		"	float dOut = (d - r_out) / band;\n"
		"	float dIn  = (d - r_in)  / band;\n"
		"	float rim  = (exp(-dOut * dOut) + exp(-dIn * dIn)) * ring;\n"
		"\n"
		"	vec3  col  = mix(vec3(0.0), body, ring);\n"
		"	float a    = ring * uBody.a + glow * " WHEEL_STRINGIFY(WHEEL_GLOW_OPACITY) ";\n"
		"	col = mix(col, uRim.rgb, uRim.a * rim);\n"
		"	a   = max(a, uRim.a * rim);\n"
		"\n"
		"	if (a <= 0.0) discard;\n"
		"	gl_FragColor = vec4(col, a);\n"
		"}\n";

	GLuint program;

	if (wheel_shader_program)
		return true;
	if (wheel_shader_init_attempted)
		return false;
	wheel_shader_init_attempted = true;

	if (!gl_glsl_able || !GL_UseProgramFunc || !GL_Uniform4fFunc || !GL_Uniform2fFunc || !GL_Uniform1fFunc)
		return false;

	program = GL_CreateProgram (vertSrc, fragSrc, 0, NULL);
	if (!program)
		return false;

	wheel_shader_u_center = GL_GetUniformLocation (&program, "uCenter");
	wheel_shader_u_radii  = GL_GetUniformLocation (&program, "uRadii");
	wheel_shader_u_glow   = GL_GetUniformLocation (&program, "uGlow");
	wheel_shader_u_body   = GL_GetUniformLocation (&program, "uBody");
	wheel_shader_u_rim    = GL_GetUniformLocation (&program, "uRim");
	wheel_shader_u_noise  = GL_GetUniformLocation (&program, "uNoise");
	wheel_shader_u_bevel  = GL_GetUniformLocation (&program, "uBevel");
	wheel_shader_u_box_uv = GL_GetUniformLocation (&program, "uBoxUV");
	wheel_shader_u_box_scale = GL_GetUniformLocation (&program, "uBoxScale");

	wheel_shader_program = program;
	return true;
}

void Wheel_ShutdownGL (void)
{
	wheel_shader_program = 0;
	wheel_shader_init_attempted = false;
	wheel_shader_u_center = wheel_shader_u_radii = wheel_shader_u_glow = -1;
	wheel_shader_u_body = wheel_shader_u_rim = wheel_shader_u_noise = -1;
	wheel_shader_u_bevel = -1;
	wheel_shader_u_box_uv = wheel_shader_u_box_scale = -1;
}

// Draws the wheel body + glow + bevel in a single shader pass.  Returns
// false if the shader path is unavailable so the caller can fall back.
static qboolean Wheel_DrawBodyShader (float cx, float cy, float r_in, float r_out, float glow_px)
{
	float left, right, top, bottom;
	wheel_glpic_t *box;

	if (!WHEEL_SHADER)
		return false;
	if (!Wheel_InitShader ())
		return false;
	if (!wheel_box_pic)
		return false;

	box = (wheel_glpic_t *)wheel_box_pic->data;
	if (!box->gltexture)
		return false;

	left   = cx - r_out - glow_px - 2.f;
	right  = cx + r_out + glow_px + 2.f;
	top    = cy - r_out - glow_px - 2.f;
	bottom = cy + r_out + glow_px + 2.f;

	glEnable (GL_TEXTURE_2D);
	glEnable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	GL_Bind (box->gltexture);

	GL_UseProgramFunc (wheel_shader_program);
	GL_Uniform2fFunc (wheel_shader_u_center, cx, cy);
	GL_Uniform2fFunc (wheel_shader_u_radii, r_in, r_out);
	GL_Uniform1fFunc (wheel_shader_u_glow, glow_px);
	GL_Uniform4fFunc (wheel_shader_u_body,
		0.10f, 0.08f, 0.07f,
		CLAMP (0.0f, WHEEL_OPACITY, 1.0f));
	GL_Uniform4fFunc (wheel_shader_u_rim,
		0.95f, 0.88f, 0.72f,
		CLAMP (0.0f, WHEEL_BEVEL_OPACITY, 1.0f));
	GL_Uniform2fFunc (wheel_shader_u_noise,
		0.08f,
		CLAMP (0.0f, WHEEL_NOISE, 1.0f));
	GL_Uniform1fFunc (wheel_shader_u_bevel,
		CLAMP (0.5f, WHEEL_BEVEL, 40.0f));
	GL_Uniform4fFunc (wheel_shader_u_box_uv, box->sl, box->tl, box->sh, box->th);
	GL_Uniform1fFunc (wheel_shader_u_box_scale, 1.0f / 64.0f);	// backtile is 64x64; 1:1 tiling

	glBegin (GL_QUADS);
	glVertex2f (left,  top);
	glVertex2f (right, top);
	glVertex2f (right, bottom);
	glVertex2f (left,  bottom);
	glEnd ();

	GL_UseProgramFunc (0);
	return true;
}

static void Wheel_DrawAnnulus (float cx, float cy, float r_in, float r_out, float r, float g, float b, float a)
{
	int i;
	float ang, c, s;

	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glColor4f (r, g, b, a);

	glBegin (GL_TRIANGLE_STRIP);
	for (i = 0; i <= WHEEL_RING_STEPS; i++)
	{
		ang = (float)i * (2.0f * (float)M_PI / (float)WHEEL_RING_STEPS);
		c = cosf (ang); s = sinf (ang);
		glVertex2f (cx + c * r_out, cy + s * r_out);
		glVertex2f (cx + c * r_in,  cy + s * r_in);
	}
	glEnd ();
}

// Uniform outer glow: stack concentric annuli outside r_out, each thicker
// and fainter than the last, to darken the wall in a radial halo around the
// ring.  Black with low alpha so it tints the underlying texture.
static void Wheel_DrawOuterGlow (float cx, float cy, float r_out)
{
	int pass;
	const int passes = 6;
	const float step = 5.0f;
	float prev = r_out;

	for (pass = 0; pass < passes; pass++)
	{
		float t     = (float)(pass + 1) / (float)passes;	// 0.16 .. 1.0
		float r     = r_out + step * (pass + 1);
		float alpha = WHEEL_FALLBACK_GLOW_OPACITY * (1.0f - t * 0.9f);
		Wheel_DrawAnnulus (cx, cy, prev, r, 0.f, 0.f, 0.f, alpha);
		prev = r;
	}
}

// Subtle uniform rim: a single thin line loop in a soft warm color, drawn
// all the way around at low alpha so the edge reads as a faint highlight
// without picking a light direction.
static void Wheel_DrawSubtleRim (float cx, float cy, float r, float alpha)
{
	int i;
	float ang;

	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glLineWidth (1.0f);
	glColor4f (0.95f, 0.88f, 0.72f, alpha);

	glBegin (GL_LINE_LOOP);
	for (i = 0; i < WHEEL_RING_STEPS; i++)
	{
		ang = (float)i * (2.0f * (float)M_PI / (float)WHEEL_RING_STEPS);
		glVertex2f (cx + cosf (ang) * r, cy + sinf (ang) * r);
	}
	glEnd ();
}

// Pointer: same arrow character (141) the menu uses for pinned bookmarks,
// reused via Draw_Character_Rotation.  We push our own matrix to scale and
// position around the slot direction; Draw_Character_Rotation handles its
// own pivot/rotate/restore internally.
//
// Char 141 is the menu pin; keep the rotation aligned to the same screen-space
// slot angle used for positioning around the wheel.
static void Wheel_DrawPointerChar (float cx, float cy, float r_in, int slot, float scale)
{
	float a = Wheel_SlotAngle (slot);
	float pointer_r = r_in * 0.85f;
	float px = cx + cosf (a) * pointer_r;
	float py = cy + sinf (a) * pointer_r;
	float rot = RAD2DEG (a);

	glEnable (GL_TEXTURE_2D);
	glEnable (GL_ALPHA_TEST);
	glDisable (GL_BLEND);
	glColor4f (1.f, 1.f, 1.f, 1.f);

	glPushMatrix ();
	glTranslatef (px, py, 0.f);
	glScalef (scale, scale, 1.f);
	Draw_Character_Rotation (-4, -4, 141, (int)rot);
	glPopMatrix ();
}

static void Wheel_DrawAxeSlotLabel (float ix, float iy, qpic_t *pic, float icon_scale, float alpha)
{
	float label_scale = CLAMP (1.0f, icon_scale * 0.75f, 2.5f);
	float label_x = ix - 4.0f * label_scale;
	float label_y = iy + (float)pic->height * icon_scale * 0.5f + 0.5f * label_scale;

	glPushMatrix ();
	glTranslatef ((float)((int)(label_x + 0.5f)), (float)((int)(label_y + 0.5f)), 0.f);
	glScalef (label_scale, label_scale, 1.f);
	Draw_CharacterRGBA (0, 0, WHEEL_AXE_LABEL_CHAR, wheel_white, alpha);
	glPopMatrix ();
}

void Wheel_Draw (void)
{
	int i, count;
	float cx, cy, radius, r_in, r_out, icon_r, iscale;

	if (!wheel_open)
		return;

	// soft-close if state changed (menu, disconnect, death, intermission, ...)
	if (!Wheel_Allowed (true))
	{
		Wheel_Reset ();
		return;
	}

	Wheel_LoadIcons ();
	GL_SetCanvas (CANVAS_DEFAULT);
	count = Wheel_WeaponCount ();
	if (count <= 0)
	{
		Wheel_Reset ();
		return;
	}
	if (wheel_pick < 0 || wheel_pick >= count)
		wheel_pick = Wheel_CurrentWeaponSlot ();
	if (!Wheel_WeaponSelectable (wheel_pick))
		wheel_pick = Wheel_NearestSelectableSlot (wheel_pick, 1);

	radius = Wheel_Radius ();
	Wheel_Position (radius, &cx, &cy);
	r_out = radius;
	r_in  = radius * 0.50f;
	icon_r = (r_in + r_out) * 0.5f;
	iscale = CLAMP (1.0f, WHEEL_ICON_SCALE, 5.0f);

	{
		float glow_px = CLAMP (0.f, WHEEL_GLOW, 96.f);
		if (!Wheel_DrawBodyShader (cx, cy, r_in, r_out, glow_px))
		{
			// fallback: immediate-mode glow + body + uniform rim
			Wheel_DrawOuterGlow (cx, cy, r_out);
			Wheel_DrawAnnulus (cx, cy, r_in, r_out, 0.10f, 0.08f, 0.07f, 0.60f);
			Wheel_DrawSubtleRim (cx, cy, r_out, 0.35f);
			Wheel_DrawSubtleRim (cx, cy, r_in,  0.28f);
		}
	}

	// icons / labels
	glEnable (GL_TEXTURE_2D);
	for (i = 0; i < count; i++)
	{
		int weapon_index = Wheel_WeaponIndexForSlot (i);
		const wheel_weapon_t *weapon = &wheel_weapons[weapon_index];
		float a = Wheel_SlotAngle (i);
		float ix = cx + cosf (a) * icon_r;
		float iy = cy + sinf (a) * icon_r;
		qboolean owned = Wheel_WeaponOwned (i);
		qboolean selectable = Wheel_WeaponSelectable (i);
		float alpha = selectable ? 1.0f : (owned ? 0.45f : 0.28f);
		qpic_t *pic = (i == wheel_pick && selectable && wheel_icons_active[weapon_index]) ?
			wheel_icons_active[weapon_index] : wheel_icons[weapon_index];

		if ((weapon->flags & WHEEL_FLAG_AXE) && i != wheel_pick)
			alpha = q_min (alpha, WHEEL_AXE_UNSELECTED_ALPHA);

		if (pic)
		{
			float w = pic->width * iscale;
			float h = pic->height * iscale;
			Draw_ScaledPicAlpha ((int)(ix - w * 0.5f), (int)(iy - h * 0.5f), pic, iscale, alpha);
			if (weapon->flags & WHEEL_FLAG_AXE)
				Wheel_DrawAxeSlotLabel (ix, iy, pic, iscale, alpha);
		}
		else
		{
			// fallback: impulse digit, dimmed consistently with generated icons.
			int ch = weapon->fallback_char;
			if (ch && (owned || i == wheel_pick))
				Draw_CharacterRGBA ((int)(ix - 4), (int)(iy - 4), ch, wheel_white, alpha);
		}
	}

	// arrow pointing at current pick (char 141, scaled with the wheel)
	if (Wheel_WeaponSelectable (wheel_pick))
		Wheel_DrawPointerChar (cx, cy, r_in, wheel_pick, iscale);

	// restore GL state for the rest of the HUD
	glColor4f (1.f, 1.f, 1.f, 1.f);
	glDisable (GL_BLEND);
	glEnable (GL_ALPHA_TEST);
	glEnable (GL_TEXTURE_2D);

	if (WHEEL_DEBUG)
	{
		const wheel_weapon_t *weapon = Wheel_WeaponForSlot (wheel_pick);
		Draw_String (8, 8, va ("wheel: %s", weapon ? weapon->name : "none"));
	}
}
