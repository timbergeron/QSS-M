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
// cvar.c -- dynamic variable tracking

#include "quakedef.h"

static cvar_t	*cvar_vars;
static char	cvar_null_string[] = "";

static struct cvaralias_s
{	//spike -- tbh mostly for _cl_name -> name, but useful for future reasons too. can't handle different values though.
	const char			*name;
	cvar_t				*cvar;
	struct cvaralias_s	*next;
} *cvar_aliases;

typedef struct mapcvar_s
{
	cvar_t				*cvar;
	char				*old_value;
	unsigned int		old_flags;
	struct mapcvar_s	*next;
} mapcvar_t;

static mapcvar_t	*map_locked_cvars;
static mapcvar_t	*map_restoring_cvars;

static void Cvar_MapLock_Status_f (void);

//==============================================================================
//
//  USER COMMANDS
//
//==============================================================================

void Cvar_Reset (const char *name); //johnfitz

/*
============
Cvar_List_f -- johnfitz
============
*/
void Cvar_List_f (void)
{
	cvar_t	*cvar;
	const char 	*partial;
	int		i, len, count, modified, archived, serverinfo;
	qboolean	filter_modified, filter_archived, filter_serverinfo;

	partial = NULL;
	len = 0;
	filter_modified = filter_archived = filter_serverinfo = false;

	// args may be filter flags (any combination of !, *, s — combined like "*!s" or as separate args)
	// and/or a partial name prefix. A token made up entirely of flag chars is treated as flags.
	for (i = 1; i < Cmd_Argc(); i++)
	{
		const char	*arg = Cmd_Argv(i);
		const char	*c;
		qboolean	flags_only = (*arg != '\0');

		for (c = arg; *c; c++)
		{
			if (*c != '!' && *c != '*' && *c != 's')
			{
				flags_only = false;
				break;
			}
		}

		if (flags_only)
		{
			for (c = arg; *c; c++)
			{
				if (*c == '!') filter_modified = true;
				else if (*c == '*') filter_archived = true;
				else if (*c == 's') filter_serverinfo = true;
			}
		}
		else if (!partial && *arg)
		{
			partial = arg;
			len = Q_strlen(partial);
		}
	}

	count = modified = archived = serverinfo = 0;
	for (cvar = cvar_vars ; cvar ; cvar = cvar->next)
	{
		qboolean is_modified, is_archived, is_serverinfo;

		if (partial && Q_strncmp(partial, cvar->name, len))
		{
			continue;
		}
		is_modified   = (strcmp(cvar->string, cvar->default_string) != 0);
		is_archived   = (cvar->flags & CVAR_ARCHIVE) != 0;
		is_serverinfo = (cvar->flags & CVAR_NOTIFY)  != 0;

		if (filter_modified   && !is_modified)   continue;
		if (filter_archived   && !is_archived)   continue;
		if (filter_serverinfo && !is_serverinfo) continue;

		Con_SafePrintf ("%s%s%s %s \"%s\"\n",
			is_modified  ? "!" : " ",
			is_archived  ? "*" : " ",
			is_serverinfo ? "s" : " ",
			cvar->name,
			cvar->string);
		count++;
		if (is_modified)   modified++;
		if (is_archived)   archived++;
		if (is_serverinfo) serverinfo++;
	}

	Con_SafePrintf ("\n");
	Con_SafePrintf (" ^m!^m modified   ^m*^m archived   ^ms^m serverinfo\n");
	Con_SafePrintf ("\n");
	Con_SafePrintf (" ^m%i^m cvars", count);
	if (filter_modified || filter_archived || filter_serverinfo)
	{
		Con_SafePrintf (" with ^m%s%s%s^m",
			filter_modified   ? "!" : "",
			filter_archived   ? "*" : "",
			filter_serverinfo ? "s" : "");
	}
	if (partial)
	{
		Con_SafePrintf (" matching \"^m%s^m\"", partial);
	}
	Con_SafePrintf ("  (^m%i^m modified, ^m%i^m archived, ^m%i^m serverinfo)\n", modified, archived, serverinfo);
}

/*
============
Cvar_Inc_f -- johnfitz
============
*/
void Cvar_Inc_f (void)
{
	switch (Cmd_Argc())
	{
	default:
	case 1:
		Con_Printf("inc <cvar> [amount] : increment cvar\n");
		break;
	case 2:
		Cvar_SetValue (Cmd_Argv(1), Cvar_VariableValue(Cmd_Argv(1)) + 1);
		break;
	case 3:
		Cvar_SetValue (Cmd_Argv(1), Cvar_VariableValue(Cmd_Argv(1)) + Q_atof(Cmd_Argv(2)));
		break;
	}
}

/*
============
Cvar_Set_f -- spike

both set+seta commands
============
*/
void Cvar_Set_f (void)
{
	//q2: set name value flags
	//dp: set name value description
	//fte: set name some freeform value with spaces or whatever //description
	//to avoid politics, its easier to just stick with name+value only.
	//that leaves someone else free to pick a standard for what to do with extra args.
	const char *varname = Cmd_Argv(1);
	const char *varvalue = Cmd_Argv(2);
	cvar_t *var;
	int fl = 0;
	if (Cmd_Argc() < 3)
	{
		Con_Printf("%s <cvar> <value>\n", Cmd_Argv(0));
		return;
	}

	if (!strcmp(Cmd_Argv(0), "setfl") && Cmd_Argc() == 4)
	{
		const char *as = Cmd_Argv(3);
		for (; *as; as++)
		{
			switch(*as)
			{
			case 'a':
				fl |= CVAR_ARCHIVE|CVAR_SETA;	//will forget other flags, but that's probably okay because this should be more for default.cfg and the other flags will get re-asserted that way anyway
				break;
			case 'u':
				fl |= CVAR_USERINFO;
				break;
			case 's':
				fl |= CVAR_SERVERINFO;
				break;
			default:
				Con_Warning("%s \"%s\" unknown cvar flag '%c'\n", Cmd_Argv(0), varname, *as);
				return;
			}
		}
	}
	else if (Cmd_Argc() > 3)
	{	//dp conflicts with fte/q2. play safe and piss off anyone trying to use either. they should probably use setfl if that's what they really meant.
		Con_Warning("%s \"%s\" command with extra args\n", Cmd_Argv(0), varname);
		return;
	}
	else if (!strcmp(Cmd_Argv(0), "seta"))
		fl = CVAR_ARCHIVE|CVAR_SETA;

	var = Cvar_Create(varname, varvalue);
	if (!var)
	{
		Con_Warning("%s \"%s\" failed: name conflicts with an existing command\n", Cmd_Argv(0), varname);
		return;
	}

	if (var->flags & CVAR_LOCKED)
	{
		Con_Printf ("\"%s\" is locked by the current map\n", var->name);
		return;
	}

	var->flags |= fl;
	Cvar_SetQuick(var, varvalue);
}

/*
============
Cvar_Toggle_f -- johnfitz
============
*/
void Cvar_Toggle_f (void)
{
	cvar_t *v;
	if (Cmd_Argc()<2)
	{
		Con_Printf("toggle <cvar> [value] [altvalue]: toggle cvar\n");
		return;
	}
	v = Cvar_FindVar(Cmd_Argv(1));
	if (!v)
	{
		Con_Printf ("variable \"%s\" not found\n", Cmd_Argv(1));
		return;
	}
	if (v->flags & CVAR_LOCKED)
	{
		Con_Printf ("\"%s\" is locked by the current map\n", v->name);
		return;
	}

	if (Cmd_Argc() >= 3)
	{
		const char *newval = Cmd_Argv(2);
		const char *defval = (Cmd_Argc()>3)?Cmd_Argv(3):v->default_string;
		if (!defval) defval = "0";
		if (!strcmp(newval, v->string))
			Cvar_SetQuick(v, defval);
		else
			Cvar_SetQuick(v, newval);
	}
	else
	{
		if (v->value)
			Cvar_SetQuick(v, "0");
		else
			Cvar_SetQuick(v, "1");
	}
}

int cmdtoggle; // woods #cmdtoggle

/*
============
Cmd_Toggle_f -- woods #cmdtoggle
============
*/
void Cmd_Toggle_f(void)
{
	if ((Cmd_Argc() < 3) || (Cmd_Argc() > 3))
	{
		Con_Printf("\n");
		Con_Printf("%s <command1> <command2>\n", Cmd_Argv(0));
		Con_Printf("\n");
		return;
	}

	if (cmdtoggle == 0)
	{ 
		cmdtoggle = 1;
		Cbuf_AddText(Cmd_Argv(1));
	}
	else
	{ 
		cmdtoggle = 0;
		Cbuf_AddText(Cmd_Argv(2));
	}
}

/*
============
Cvar_Cycle_f -- johnfitz
============
*/
void Cvar_Cycle_f (void)
{
	int i;

	if (Cmd_Argc() < 3)
	{
		Con_Printf("cycle <cvar> <value list>: cycle cvar through a list of values\n");
		return;
	}

	//loop through the args until you find one that matches the current cvar value.
	//yes, this will get stuck on a list that contains the same value twice.
	//it's not worth dealing with, and i'm not even sure it can be dealt with.
	for (i = 2; i < Cmd_Argc(); i++)
	{
		//zero is assumed to be a string, even though it could actually be zero.  The worst case
		//is that the first time you call this command, it won't match on zero when it should, but after that,
		//it will be comparing strings that all had the same source (the user) so it will work.
		if (Q_atof(Cmd_Argv(i)) == 0)
		{
			if (!strcmp(Cmd_Argv(i), Cvar_VariableString(Cmd_Argv(1))))
				break;
		}
		else
		{
			if (Q_atof(Cmd_Argv(i)) == Cvar_VariableValue(Cmd_Argv(1)))
				break;
		}
	}

	if (i == Cmd_Argc())
		Cvar_Set (Cmd_Argv(1), Cmd_Argv(2)); // no match
	else if (i + 1 == Cmd_Argc())
		Cvar_Set (Cmd_Argv(1), Cmd_Argv(2)); // matched last value in list
	else
		Cvar_Set (Cmd_Argv(1), Cmd_Argv(i+1)); // matched earlier in list
}

/*
============
Cvar_Reset_f -- johnfitz
============
*/
void Cvar_Reset_f (void)
{
	switch (Cmd_Argc())
	{
	default:
	case 1:
		Con_Printf ("reset <cvar> : reset cvar to default\n");
		break;
	case 2:
		Cvar_Reset (Cmd_Argv(1));
		break;
	}
}

/*
============
Cvar_ResetAll_f -- johnfitz
============
*/
void Cvar_ResetAll_f (void)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
		Cvar_Reset (var->name);
}

/*
============
Cvar_ResetCfg_f -- QuakeSpasm
============
*/
void Cvar_ResetCfg_f (void)
{
	cvar_t	*var;

	for (var = cvar_vars ; var ; var = var->next)
		if (var->flags & CVAR_ARCHIVE) Cvar_Reset (var->name);
}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
============
Cvar_Init -- johnfitz
============
*/

void Cvar_Init (void)
{
	Cmd_AddCommand ("cvarlist", Cvar_List_f);
	Cmd_AddCommand ("toggle", Cvar_Toggle_f);
	Cmd_AddCommand ("cmdtoggle", Cmd_Toggle_f); // woods #cmdtoggle
	Cmd_AddCommand ("cycle", Cvar_Cycle_f);
	Cmd_AddCommand ("inc", Cvar_Inc_f);
	Cmd_AddCommand ("reset", Cvar_Reset_f);
	Cmd_AddCommand ("resetall", Cvar_ResetAll_f);
	Cmd_AddCommand ("resetcfg", Cvar_ResetCfg_f);
	Cmd_AddCommand ("set", Cvar_Set_f);
	Cmd_AddCommand ("seta", Cvar_Set_f);
	Cmd_AddCommand ("setfl", Cvar_Set_f);
	Cmd_AddCommand ("maplock_status", Cvar_MapLock_Status_f);
}

//==============================================================================
//
//  CVAR FUNCTIONS
//
//==============================================================================

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar (const char *var_name)
{
	cvar_t	*var;
	struct cvaralias_s	*varalias;

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (!Q_strcmp(var_name, var->name))
			return var;
	}

	for (varalias = cvar_aliases ; varalias ; varalias = varalias->next)
	{
		if (!Q_strcmp(var_name, varalias->name))
			return varalias->cvar;
	}

	return NULL;
}

cvar_t *Cvar_FindVarAfter (const char *prev_name, unsigned int with_flags)
{
	cvar_t	*var;

	if (*prev_name)
	{
		var = Cvar_FindVar (prev_name);
		if (!var)
			return NULL;
		var = var->next;
	}
	else
		var = cvar_vars;

	// search for the next cvar matching the needed flags
	while (var)
	{
		if ((var->flags & with_flags) || !with_flags)
			break;
		var = var->next;
	}
	return var;
}

/*
============
Cvar_LockVar
============
*/
void Cvar_LockVar (const char *var_name)
{
	cvar_t	*var = Cvar_FindVar (var_name);
	if (var)
		var->flags |= CVAR_LOCKED;
}

void Cvar_UnlockVar (const char *var_name)
{
	cvar_t	*var = Cvar_FindVar (var_name);
	if (var)
		var->flags &= ~CVAR_LOCKED;
}

void Cvar_UnlockAll (void)
{
	cvar_t	*var;

	// Map locks carry saved values, so restoring is part of fully unlocking them.
	Cvar_MapLock_RestoreAll ();

	for (var = cvar_vars ; var ; var = var->next)
	{
		var->flags &= ~CVAR_LOCKED;
	}
}

static mapcvar_t *Cvar_MapLock_FindInList (mapcvar_t *list, const cvar_t *var)
{
	mapcvar_t	*curr;

	for (curr = list ; curr ; curr = curr->next)
		if (curr->cvar == var)
			return curr;

	return NULL;
}

static mapcvar_t *Cvar_MapLock_FindActive (const cvar_t *var)
{
	return Cvar_MapLock_FindInList (map_locked_cvars, var);
}

static mapcvar_t *Cvar_MapLock_FindSaved (const cvar_t *var)
{
	mapcvar_t	*curr;

	curr = Cvar_MapLock_FindActive (var);
	if (curr)
		return curr;

	return Cvar_MapLock_FindInList (map_restoring_cvars, var);
}

static qboolean Cvar_MapLock_DeniedName (const char *name)
{
	// Some names are also covered by prefixes; keep them explicit to document high-risk cvars.
	static const char *const denied_names[] =
	{
		"cmdline",
		"registered",
		"developer",
		"sv_cheats",
		"host_framerate",
		"host_timescale",
		"host_maxfps",
		"deathmatch",
		"coop",
		"skill",
		"teamplay",
		"fraglimit",
		"timelimit",
		"samelevel",
		"noexit",
		"pausable",
		"serverprofile",
		"devstats",
		"campaign",
		"horde",
		"temp1",
		"hostname",
		"password",
		"rcon_password",
		"allow_download",
		"allow_download_sky",
		"cl_web_download_url",
		"cl_web_download_url2",
		"cl_autodemo",
		"cl_autovote",
		"cl_autovote_list",
		"cl_contentfilter",
		"cl_migration_schema",
		"cl_nocsqc",
		"cl_nopext",
		"cl_onload",
		"sensitivity",
		"lookspring",
		"lookstrafe",
		"m_pitch",
		"m_yaw",
		"cl_minpitch",
		"cl_maxpitch",
		"volume",
		"bgmvolume",
		"nosound",
		"sndspeed",
		"precache",
		"loadas8bit",
		"max_edicts",
		"nomonsters",
		"edgefriction",
		"gl_max_size",
		"gl_picmip",
		"vid_fullscreen",
		"vid_width",
		"vid_height",
		"vid_desktopfullscreen",
		"vid_refreshrate",
		"vid_bpp",
		"vid_borderless",
		"vid_saveresize",
		"vid_vsync",
		"vid_fsaa",
		"vid_fxaa",
		NULL
	};
	static const char *const denied_prefixes[] =
	{
		"_",
		"cfg_",
		"cmd_",
		"com_",
		"con_",
		"fs_",
		"gyro_",
		"host_",
		"in_",
		"joy_",
		"log_",
		"m_",
		"net_",
		"pm_",
		"pq_",
		"pr_",
		"snd_",
		"sys_",
		"sv_",
		"cl_demo",
		"cl_download",
		"cl_portpingprobe_",
		"cl_voip_",
		NULL
	};
	int		i;

	for (i = 0 ; denied_names[i] ; i++)
		if (!q_strcasecmp (name, denied_names[i]))
			return true;

	for (i = 0 ; denied_prefixes[i] ; i++)
	{
		size_t	len = strlen (denied_prefixes[i]);

		if (!q_strncasecmp (name, denied_prefixes[i], len))
			return true;
	}

	return false;
}

static qboolean Cvar_MapLock_CanSet (const cvar_t *var)
{
	if (!var)
		return false;
	if (!(var->flags & CVAR_REGISTERED))
		return false;
	if (var->flags & (CVAR_ROM | CVAR_LOCKED | CVAR_NOTIFY | CVAR_SERVERINFO | CVAR_USERINFO | CVAR_USERDEFINED | CVAR_AUTOCVAR))
		return false;
	if (Cvar_MapLock_DeniedName (var->name))
		return false;
	return true;
}

void Cvar_MapLock_Set (const char *var_name, const char *value)
{
	cvar_t		*var;
	mapcvar_t	*curr;
	unsigned int	apply_flags;

	if (!var_name || !*var_name || !value)
		return;

	var = Cvar_FindVar (var_name);
	if (!var)
		return;

	curr = Cvar_MapLock_FindActive (var);
	if (!curr)
	{
		// Only first-lock validates CVAR_LOCKED; duplicate worldspawn keys keep the saved original.
		if (!Cvar_MapLock_CanSet (var))
			return;

		curr = (mapcvar_t *) Z_Malloc (sizeof(*curr));
		curr->cvar = var;
		curr->old_value = Z_Strdup (var->string);
		curr->old_flags = var->flags;
		curr->next = map_locked_cvars;
		map_locked_cvars = curr;
	}

	apply_flags = curr->old_flags;
	// Clearing ROM/LOCKED is defensive; first-lock filters reject those flags.
	var->flags = apply_flags & ~(CVAR_ROM | CVAR_LOCKED);
	Cvar_SetQuick (var, value);
	var->flags = apply_flags | CVAR_LOCKED;
}

void Cvar_MapLock_RestoreAll (void)
{
	mapcvar_t	*curr, *next;

	if (!map_locked_cvars)
		return;

	curr = map_locked_cvars;
	map_locked_cvars = NULL;
	map_restoring_cvars = curr;

	while (curr)
	{
		next = curr->next;

		curr->cvar->flags = curr->old_flags & ~(CVAR_ROM | CVAR_LOCKED);
		Cvar_SetQuick (curr->cvar, curr->old_value);
		curr->cvar->flags = curr->old_flags;

		map_restoring_cvars = next;
		Z_Free (curr->old_value);
		Z_Free (curr);
		curr = next;
	}

	map_restoring_cvars = NULL;
}

qboolean Cvar_MapLock_ParseWorldspawnKey (const char *key, const char *value)
{
	if (q_strncasecmp (key, "cvar_", 5))
		return false;

	Cvar_MapLock_Set (key + 5, value);
	return true;
}

qboolean Cvar_MapLock_GetSavedState (const cvar_t *var, const char **value, unsigned int *flags)
{
	mapcvar_t	*curr;

	curr = Cvar_MapLock_FindSaved (var);
	if (!curr)
		return false;

	if (value)
		*value = curr->old_value;
	if (flags)
		*flags = curr->old_flags;
	return true;
}

static void Cvar_MapLock_Status_f (void)
{
	mapcvar_t	*curr;
	int		count;

	if (!map_locked_cvars)
	{
		Con_Printf ("No map-locked cvars.\n");
		return;
	}

	Con_Printf ("Map-locked cvars:\n");
	count = 0;
	for (curr = map_locked_cvars ; curr ; curr = curr->next)
	{
		Con_Printf ("  %s saved \"%s\" effective \"%s\"\n",
			curr->cvar->name, curr->old_value, curr->cvar->string);
		count++;
	}
	Con_Printf ("%i map-locked cvar%s.\n", count, count == 1 ? "" : "s");
}

/*
============
Cvar_VariableValue
============
*/
float	Cvar_VariableValue (const char *var_name)
{
	cvar_t	*var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return 0;
	return Q_atof (var->string);
}


/*
============
Cvar_VariableString
============
*/
const char *Cvar_VariableString (const char *var_name)
{
	cvar_t *var;

	var = Cvar_FindVar (var_name);
	if (!var)
		return cvar_null_string;
	return var->string;
}


/*
============
Cvar_CompleteVariable
============
*/
const char *Cvar_CompleteVariable (const char *partial)
{
	cvar_t	*cvar;
	struct cvaralias_s	*cvaralias;
	int	len;

	len = Q_strlen(partial);
	if (!len)
		return NULL;

// check functions
	for (cvar = cvar_vars ; cvar ; cvar = cvar->next)
	{
		if (!Q_strncmp(partial, cvar->name, len))
			return cvar->name;
	}

	for (cvaralias = cvar_aliases ; cvaralias ; cvaralias = cvaralias->next)
	{
		if (!Q_strncmp(partial, cvaralias->name, len))
			return cvaralias->name;
	}

	return NULL;
}

/*
============
Cvar_Reset -- johnfitz
============
*/
void Cvar_Reset (const char *name)
{
	cvar_t	*var;

	var = Cvar_FindVar (name);
	if (!var)
		Con_Printf ("variable \"%s\" not found\n", name);
	else if (var->flags & CVAR_LOCKED)
		Con_Printf ("\"%s\" is locked by the current map\n", var->name);
	else
		Cvar_SetQuick (var, var->default_string);
}

void Cvar_SetQuick (cvar_t *var, const char *value)
{
	if (var->flags & (CVAR_ROM|CVAR_LOCKED))
		return;
	if (!(var->flags & CVAR_REGISTERED))
		return;

	if (!var->string)
		var->string = Z_Strdup (value);
	else
	{
		int	len;

		if (!strcmp(var->string, value))
			return;	// no change

		var->flags |= CVAR_CHANGED;
		len = Q_strlen (value);
		if (len != Q_strlen(var->string))
		{
			Z_Free ((void *)var->string);
			var->string = (char *) Z_Malloc (len + 1);
		}
		memcpy ((char *)var->string, value, len + 1);
	}

	var->value = Q_atof (var->string);

	//johnfitz -- save initial value for "reset" command
	if (!var->default_string)
		var->default_string = Z_Strdup (var->string);
	//johnfitz -- during initialization, update default too
	else if (!host_initialized)
	{
	//	Sys_Printf("changing default of %s: %s -> %s\n",
	//		   var->name, var->default_string, var->string);
		Z_Free ((void *)var->default_string);
		var->default_string = Z_Strdup (var->string);
	}
	//johnfitz

	if (var->callback)
		var->callback (var);
	if (var->flags & CVAR_AUTOCVAR)
		PR_AutoCvarChanged(var);
	if (var->flags & CVAR_SERVERINFO)
	{	//replicate the cvar change into the serverinfo string and let clients know.
		client_t *cl;
		Info_SetKey(svs.serverinfo, sizeof(svs.serverinfo), var->name, var->string);
		for (cl = svs.clients; cl < svs.clients+svs.maxclients; cl++)
		{
			if (cl->active)
			{
				MSG_WriteByte (&cl->message, svc_stufftext);
				MSG_WriteString (&cl->message, va("%s \"%s\" \"%s\"\n", "//svi", var->name, var->string));
			}
		}
	}
	if (var->flags & CVAR_USERINFO)
	{	//replicate the cvar change into the userinfo.
		Info_SetKey(cls.userinfo, sizeof(cls.userinfo), var->name, var->string);

		//let the server know.
		if (cls.state == ca_connected)
		{
			MSG_WriteByte (&cls.message, clc_stringcmd);
			if (var == &cl_name)	//some hacks for legacy settings.
				MSG_WriteString(&cls.message,va("name \"%s\"\n", var->string));
			else if (var == &cl_topcolor || var == &cl_bottomcolor)
				MSG_WriteString(&cls.message,va("color \"%s\" \"%s\"\n", cl_topcolor.string, cl_bottomcolor.string));
			else
				MSG_WriteString(&cls.message,va("setinfo \"%s\" \"%s\"\n", var->name, var->string));
		}
	}
}

void Cvar_SetValueQuick (cvar_t *var, const float value)
{
	char	val[32], *ptr = val;

	if (value == (float)((int)value))
		q_snprintf (val, sizeof(val), "%i", (int)value);
	else
	{
		q_snprintf (val, sizeof(val), "%f", value);
		// kill trailing zeroes
		while (*ptr)
			ptr++;
		while (--ptr > val && *ptr == '0' && ptr[-1] != '.')
			*ptr = '\0';
	}

	Cvar_SetQuick (var, val);
}

/*
============
Cvar_Set
============
*/
void Cvar_Set (const char *var_name, const char *value)
{
	cvar_t		*var;

	var = Cvar_FindVar (var_name);
	if (!var)
	{	// there is an error in C code if this happens
		Con_Printf ("Cvar_Set: variable %s not found\n", var_name);
		return;
	}

	Cvar_SetQuick (var, value);


	// JPG - there's probably a better place for this, but it works. // woods #pqlag
	if (!strcmp(var_name, "pq_lag"))
	{
		if (var->value < 0)
		{
			Cvar_Set("pq_lag", "0");
			return;
		}
		if (var->value > 400)
		{
			Cvar_Set("pq_lag", "400");
			return;
		}
		Cbuf_AddText(va("say \"%cping +%d%c\"\n", 157, (int)var->value, 159));
	}
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue (const char *var_name, const float value)
{
	char	val[32], *ptr = val;

	if (value == (float)((int)value))
		q_snprintf (val, sizeof(val), "%i", (int)value);
	else
	{
		q_snprintf (val, sizeof(val), "%f", value);
		// kill trailing zeroes
		while (*ptr)
			ptr++;
		while (--ptr > val && *ptr == '0' && ptr[-1] != '.')
			*ptr = '\0';
	}

	Cvar_Set (var_name, val);
}

/*
============
Cvar_SetROM
============
*/
void Cvar_SetROM (const char *var_name, const char *value)
{
	cvar_t *var = Cvar_FindVar (var_name);
	if (var)
	{
		var->flags &= ~CVAR_ROM;
		Cvar_SetQuick (var, value);
		var->flags |= CVAR_ROM;
	}
}

/*
============
Cvar_SetValueROM
============
*/
void Cvar_SetValueROM (const char *var_name, const float value)
{
	cvar_t *var = Cvar_FindVar (var_name);
	if (var)
	{
		var->flags &= ~CVAR_ROM;
		Cvar_SetValueQuick (var, value);
		var->flags |= CVAR_ROM;
	}
}

/*
============
Cvar_RegisterAlias

Adds a special alias for an existing cvar. this allows for cfg/gamecode compat with other engines.
============
*/
void Cvar_RegisterAlias(cvar_t *variable, const char *newname)
{
	struct cvaralias_s *alias;
	//no dupes/conflicts please.
	if (Cvar_FindVar (newname))
	{
		Con_Printf ("Can't register variable %s, already defined\n", newname);
		return;
	}
	if (Cvar_FindVar (variable->name) != variable)
	{
		Con_Printf ("Can't register pseudo-variable %s, variable %s not registered properly\n", newname, variable->name);
		return;
	}
	if (Cmd_Exists (newname))
	{
		Con_Printf ("Cvar_RegisterAlias: %s is a command\n", newname);
		return;
	}

	//create it
	alias = Z_Malloc(sizeof(*alias) + strlen(newname)+1);
	alias->cvar = variable;
	alias->name = (char*)(alias+1);
	strcpy((char*)(alias+1), newname);

	//link it in.
	alias->next = cvar_aliases;
	cvar_aliases = alias;
}

/*
============
Cvar_RegisterVariable

Adds a freestanding variable to the variable list.
============
*/
void Cvar_RegisterVariable (cvar_t *variable)
{
	char	value[800]; // woods #obmodelslist raise for lists
	qboolean	set_rom;
	cvar_t	*cursor,*prev; //johnfitz -- sorted list insert

// first check to see if it has already been defined
	if (Cvar_FindVar (variable->name))
	{
		Con_Printf ("Can't register variable %s, already defined\n", variable->name);
		return;
	}

// check for overlap with a command
	if (Cmd_Exists (variable->name))
	{
		Con_Printf ("Cvar_RegisterVariable: %s is a command\n", variable->name);
		return;
	}

// link the variable in
	//johnfitz -- insert each entry in alphabetical order
	if (cvar_vars == NULL ||
	    strcmp(variable->name, cvar_vars->name) < 0) // insert at front
	{
		variable->next = cvar_vars;
		cvar_vars = variable;
	}
	else //insert later
	{
		prev = cvar_vars;
		cursor = cvar_vars->next;
		while (cursor && (strcmp(variable->name, cursor->name) > 0))
		{
			prev = cursor;
			cursor = cursor->next;
		}
		variable->next = prev->next;
		prev->next = variable;
	}
	//johnfitz
	variable->flags |= CVAR_REGISTERED;

// copy the value off, because future sets will Z_Free it
	q_strlcpy (value, variable->string, sizeof(value));
	variable->string = NULL;
	variable->default_string = NULL;

	if (!(variable->flags & CVAR_CALLBACK))
		variable->callback = NULL;

// set it through the function to be consistent
	set_rom = (variable->flags & CVAR_ROM);
	variable->flags &= ~CVAR_ROM;
	Cvar_SetQuick (variable, value);
	if (set_rom)
		variable->flags |= CVAR_ROM;
}

/*
============
Cvar_Create -- spike

Creates a cvar if it does not already exist, otherwise does nothing.
Must not be used until after all other cvars are registered.
Cvar will be persistent.
============
*/
cvar_t *Cvar_Create (const char *name, const char *value)
{
	cvar_t *newvar;
	newvar = Cvar_FindVar(name);
	if (newvar)
		return newvar;	//already exists.
	if (Cmd_Exists (name))
		return NULL;	//error! panic! oh noes!

	newvar = Z_Malloc(sizeof(cvar_t) + strlen(name)+1);
	newvar->name = (char*)(newvar+1);
	strcpy((char*)(newvar+1), name);
	newvar->flags = CVAR_USERDEFINED;

	newvar->string = value;
	Cvar_RegisterVariable(newvar);
	return newvar;
}

/*
============
Cvar_SetCallback

Set a callback function to the var
============
*/
void Cvar_SetCallback (cvar_t *var, cvarcallback_t func)
{
	var->callback = func;
	if (func)
		var->flags |= CVAR_CALLBACK;
	else	var->flags &= ~CVAR_CALLBACK;
}

/*
============
Cvar_SetCompletion -- woods #iwtabcomplete
Set a completion function to the var
============
*/
void Cvar_SetCompletion (cvar_t* var, cvarcompletion_t func)
{
	var->completion = func;
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean	Cvar_Command (void)
{
	cvar_t			*v;

// check variables
	v = Cvar_FindVar (Cmd_Argv(0));
	if (!v)
		return false;

// perform a variable print or set
	if (Cmd_Argc() == 1)
	{
		if (v->default_string) // woods from https://github.com/andrei-drexler/quakespasm print defaults
		{
			if (!Q_strcmp(v->string, v->default_string))
				Con_Printf("\"%s\" is \"%s\" (default)\n", v->name, v->string);
			else
				Con_Printf("\"%s\" is \"%s\" (default: \"%s\")\n", v->name, v->string, v->default_string);
		}
		else
			Con_Printf("\"%s\" is \"%s\"\n", v->name, v->string);
		return true;
	}

	if (v->flags & CVAR_LOCKED)
	{
		Con_Printf ("\"%s\" is locked by the current map\n", v->name);
		return true;
	}

	if (Con_IsRedirected())
		Con_Printf ("changing \"%s\" from \"%s\" to \"%s\"\n", v->name, v->string, Cmd_Argv(1));

	Cvar_Set (v->name, Cmd_Argv(1));
	return true;
}


/*
============
Cvar_WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables (FILE *f)
{
	cvar_t	*var;
	const char	*value;
	unsigned int	flags;

	for (var = cvar_vars ; var ; var = var->next)
	{
		if (!Cvar_MapLock_GetSavedState (var, &value, &flags))
		{
			value = var->string;
			flags = var->flags;
		}

		if (flags & CVAR_ARCHIVE)
		{
			if (flags & (CVAR_USERDEFINED|CVAR_SETA))
				fprintf (f, "seta ");
			fprintf (f, "%s \"%s\"\n", var->name, value);
		}
	}
}
