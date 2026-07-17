/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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
#include "q_stdinc.h" // woods for #iplog
#include "q_ctype.h"
#include "arch_def.h" // woods for #iplog
#include "net_sys.h" // woods for #iplog
#include "net_defs.h" // woods for #iplog
#include "net_ws.h"
#include <time.h> // woods #demolistsort
#include <sys/stat.h> // woods #demolistsort
#include "bgmusic.h" // woods #musiclist
#include "json.h" // woods #mapdescriptions
#ifdef _WIN32 // woods #udplist
	#include "wsaerror.h"
#else
#include <dirent.h>
#endif
#include <ctype.h> // woods #udplist
#include <errno.h>

extern cvar_t	pausable;
extern cvar_t	nomonsters; // woods #nomonsters (ironwail)

// 0 = no, 1 = ask, 2 = when dead, 3 = always
cvar_t sv_autoload = {"sv_autoload", "2", CVAR_ARCHIVE}; // woods #autoload (iw)

int	current_skill;

Uint64		mpservertime;	// woods #servertime
extern		char afk_name[16]; // woods #smartafk

cvar_t sv_adminnick = {"sv_adminnick", "server admin", CVAR_ARCHIVE}; // woods (darkpaces) #adminnick
cvar_t sv_modvote = {"sv_modvote", "0", CVAR_ARCHIVE | CVAR_SERVERINFO};
extern char lastconnected[3]; // woods -- #identify+
extern qboolean ctrlpressed; // woods #saymodifier

void CL_ManualDownload_f (const char* filename); // woods #manualdownload
extern char unfun[129];
int unfun_match(const char* s1, char* s2);

static void Host_Goto_f (void);
static void Host_Resurrect_f (void);

static char cl_chat_ignored_names[MAX_SCOREBOARD][MAX_SCOREBOARDNAME];
static int cl_chat_ignored_slots[MAX_SCOREBOARD];
static qboolean cl_chat_ignored_active[MAX_SCOREBOARD];

static qboolean unfun_equal(const char* s1, const char* s2)
{
	int i;

	for (i = 0; s1[i] && s2[i]; i++)
	{
		if (unfun[s1[i] & 127] != unfun[s2[i] & 127])
			return false;
	}

	return (!s1[i] && !s2[i]);
}

static void Host_Ignore_Printf(const char *fmt, ...)
{
	char msg[1024];
	va_list ap;

	va_start(ap, fmt);
	q_vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	if (cmd_source == src_client)
		SV_ClientPrintf("%s", msg);
	else
		Con_Printf("%s", msg);
}

static int Host_FindServerPlayerSlot(const char *arg, char *resolved_name, size_t resolved_name_size, qboolean *ambiguous)
{
	int exact = -1;
	int exact_matches = 0;
	int partial = -1;
	int partial_matches = 0;
	int slot = Q_atoi(arg) - 1;

	*ambiguous = false;

	if (slot >= 0 && slot < svs.maxclients && svs.clients[slot].active)
	{
		q_strlcpy(resolved_name, svs.clients[slot].name, resolved_name_size);
		return slot;
	}

	for (slot = 0; slot < svs.maxclients; slot++)
	{
		if (!svs.clients[slot].active)
			continue;
		if (!unfun_equal(arg, svs.clients[slot].name))
			continue;

		exact = slot;
		exact_matches++;
		if (exact_matches > 1)
			break;
	}

	if (exact_matches == 1)
	{
		q_strlcpy(resolved_name, svs.clients[exact].name, resolved_name_size);
		return exact;
	}
	else if (exact_matches > 1)
	{
		*ambiguous = true;
		return -1;
	}

	for (slot = 0; slot < svs.maxclients; slot++)
	{
		if (!svs.clients[slot].active)
			continue;
		if (!unfun_match(arg, svs.clients[slot].name))
			continue;

		partial = slot;
		partial_matches++;
		if (partial_matches > 1)
			break;
	}

	if (partial_matches == 1)
	{
		q_strlcpy(resolved_name, svs.clients[partial].name, resolved_name_size);
		return partial;
	}

	*ambiguous = (partial_matches > 1);
	return -1;
}

static int Host_FindLocalPlayerSlot(const char *arg, char *resolved_name, size_t resolved_name_size, qboolean *ambiguous)
{
	int exact = -1;
	int exact_matches = 0;
	int partial = -1;
	int partial_matches = 0;
	int slot = Q_atoi(arg) - 1;

	*ambiguous = false;

	if (slot >= 0 && slot < cl.maxclients && cl.scores[slot].name[0])
	{
		q_strlcpy(resolved_name, cl.scores[slot].name, resolved_name_size);
		return slot;
	}

	for (slot = 0; slot < cl.maxclients; slot++)
	{
		if (!cl.scores[slot].name[0])
			continue;
		if (!unfun_equal(arg, cl.scores[slot].name))
			continue;

		exact = slot;
		exact_matches++;
		if (exact_matches > 1)
			break;
	}

	if (exact_matches == 1)
	{
		q_strlcpy(resolved_name, cl.scores[exact].name, resolved_name_size);
		return exact;
	}
	else if (exact_matches > 1)
	{
		*ambiguous = true;
		return -1;
	}

	for (slot = 0; slot < cl.maxclients; slot++)
	{
		if (!cl.scores[slot].name[0])
			continue;
		if (!unfun_match(arg, cl.scores[slot].name))
			continue;

		partial = slot;
		partial_matches++;
		if (partial_matches > 1)
			break;
	}

	if (partial_matches == 1)
	{
		q_strlcpy(resolved_name, cl.scores[partial].name, resolved_name_size);
		return partial;
	}

	*ambiguous = (partial_matches > 1);
	return -1;
}

static int CL_FindIgnoredNameSlot(const char *arg, char *resolved_name, size_t resolved_name_size, qboolean *ambiguous)
{
	int exact = -1;
	int exact_matches = 0;
	int partial = -1;
	int partial_matches = 0;
	int i;

	*ambiguous = false;

	for (i = 0; i < MAX_SCOREBOARD; i++)
	{
		if (!cl_chat_ignored_active[i])
			continue;
		if (!unfun_equal(arg, cl_chat_ignored_names[i]))
			continue;

		exact = i;
		exact_matches++;
		if (exact_matches > 1)
			break;
	}

	if (exact_matches == 1)
	{
		q_strlcpy(resolved_name, cl_chat_ignored_names[exact], resolved_name_size);
		return exact;
	}
	else if (exact_matches > 1)
	{
		*ambiguous = true;
		return -1;
	}

	for (i = 0; i < MAX_SCOREBOARD; i++)
	{
		if (!cl_chat_ignored_active[i])
			continue;
		if (!unfun_match(arg, cl_chat_ignored_names[i]))
			continue;

		partial = i;
		partial_matches++;
		if (partial_matches > 1)
			break;
	}

	if (partial_matches == 1)
	{
		q_strlcpy(resolved_name, cl_chat_ignored_names[partial], resolved_name_size);
		return partial;
	}

	*ambiguous = (partial_matches > 1);
	return -1;
}

static qboolean CL_AddIgnoredName(const char *name, int player_slot)
{
	int free_slot = -1;
	int i;

	for (i = 0; i < MAX_SCOREBOARD; i++)
	{
		if (!cl_chat_ignored_active[i])
		{
			if (free_slot < 0)
				free_slot = i;
			continue;
		}

		if ((player_slot >= 0 && cl_chat_ignored_slots[i] == player_slot) ||
			!q_strcasecmp(cl_chat_ignored_names[i], name))
			return false;
	}

	if (free_slot < 0)
		return false;

	q_strlcpy(cl_chat_ignored_names[free_slot], name, sizeof(cl_chat_ignored_names[free_slot]));
	cl_chat_ignored_slots[free_slot] = player_slot;
	cl_chat_ignored_active[free_slot] = true;
	return true;
}

static qboolean CL_RemoveIgnoredName(const char *name)
{
	int i;

	for (i = 0; i < MAX_SCOREBOARD; i++)
	{
		if (!cl_chat_ignored_active[i])
			continue;
		if (q_strcasecmp(cl_chat_ignored_names[i], name))
			continue;

		cl_chat_ignored_active[i] = false;
		cl_chat_ignored_slots[i] = -1;
		cl_chat_ignored_names[i][0] = '\0';
		return true;
	}

	return false;
}

void CL_ClearIgnoredChats(void)
{
	int i;

	memset(cl_chat_ignored_names, 0, sizeof(cl_chat_ignored_names));
	for (i = 0; i < MAX_SCOREBOARD; i++)
		cl_chat_ignored_slots[i] = -1;
	memset(cl_chat_ignored_active, 0, sizeof(cl_chat_ignored_active));
}

void CL_UpdateIgnoredChatSlot(int slot, const char *name)
{
	int i;

	if (slot < 0 || slot >= MAX_SCOREBOARD)
		return;

	if (!name)
		name = "";

	for (i = 0; i < MAX_SCOREBOARD; i++)
	{
		if (!cl_chat_ignored_active[i] || cl_chat_ignored_slots[i] != slot)
			continue;

		if (!name[0])
		{
			cl_chat_ignored_slots[i] = -1;
			continue;
		}

		q_strlcpy(cl_chat_ignored_names[i], name, sizeof(cl_chat_ignored_names[i]));
	}
}

static qboolean CL_IsIgnoredChatPrefix(const char *text, const char *name, const char *prefix_format)
{
	char prefix[MAX_SCOREBOARDNAME + 8];

	q_snprintf(prefix, sizeof(prefix), prefix_format, name);
	return !strncmp(text, prefix, strlen(prefix));
}

qboolean CL_ShouldIgnoreChatPrint(const char *text)
{
	int i;

	for (i = 0; i < MAX_SCOREBOARD; i++)
	{
		if (!cl_chat_ignored_active[i])
			continue;

		if (CL_IsIgnoredChatPrefix(text, cl_chat_ignored_names[i], "\001%s:") ||
			CL_IsIgnoredChatPrefix(text, cl_chat_ignored_names[i], "\001(%s):") ||
			CL_IsIgnoredChatPrefix(text, cl_chat_ignored_names[i], "%s:") ||
			CL_IsIgnoredChatPrefix(text, cl_chat_ignored_names[i], "(%s):") ||
			CL_IsIgnoredChatPrefix(text, cl_chat_ignored_names[i], "dm [%s]:"))
			return true;
	}

	return false;
}

static void Host_BuildPlayerTarget(char *buffer, size_t buffer_size)
{
	int i;

	buffer[0] = '\0';
	for (i = 1; i < Cmd_Argc(); i++)
	{
		if (i > 1)
			q_strlcat(buffer, " ", buffer_size);
		q_strlcat(buffer, Cmd_Argv(i), buffer_size);
	}
}

static qboolean Host_ServerChatIgnored(const client_t *receiver, int sender_slot)
{
	if (!receiver || sender_slot < 0 || sender_slot >= MAX_SCOREBOARD)
		return false;

	return !!(receiver->chat_ignore[sender_slot >> 3] & (1u << (sender_slot & 7)));
}

static void Host_ServerSetChatIgnored(client_t *receiver, int sender_slot, qboolean ignored)
{
	if (!receiver || sender_slot < 0 || sender_slot >= MAX_SCOREBOARD)
		return;

	if (ignored)
		receiver->chat_ignore[sender_slot >> 3] |= (1u << (sender_slot & 7));
	else
		receiver->chat_ignore[sender_slot >> 3] &= ~(1u << (sender_slot & 7));
}

static void Host_PrintIgnoredList(void)
{
	int i;
	qboolean any = false;

	if (cmd_source == src_client)
	{
		SV_ClientPrintf("Server ignore list:\n");
		for (i = 0; i < svs.maxclients; i++)
		{
			if (!svs.clients[i].active)
				continue;
			if (!Host_ServerChatIgnored(host_client, i))
				continue;

			SV_ClientPrintf("  %s\n", svs.clients[i].name);
			any = true;
		}

		if (!any)
			SV_ClientPrintf("  (empty)\n");
	}
	else
	{
		Con_Printf("Local ignore list:\n");
		for (i = 0; i < MAX_SCOREBOARD; i++)
		{
			if (!cl_chat_ignored_active[i])
				continue;

			Con_Printf("  %s\n", cl_chat_ignored_names[i]);
			any = true;
		}

		if (!any)
			Con_Printf("  (empty)\n");
	}
}

static void Host_Ignore_Common(qboolean ignored)
{
	char target_arg[MAX_SCOREBOARDNAME];
	char resolved_name[MAX_SCOREBOARDNAME];
	qboolean ambiguous = false;

	if (Cmd_Argc() < 2)
	{
		if (ignored)
			Host_PrintIgnoredList();
		else
			Host_Ignore_Printf("usage: %s <player>\n", Cmd_Argv(0));
		return;
	}

	Host_BuildPlayerTarget(target_arg, sizeof(target_arg));

	if (cmd_source == src_client)
	{
		int target_slot;
		int sender_slot;

		target_slot = Host_FindServerPlayerSlot(target_arg, resolved_name, sizeof(resolved_name), &ambiguous);
		if (target_slot < 0)
		{
			Host_Ignore_Printf("%s player match for %s\n", ambiguous ? "No unique" : "No", target_arg);
			return;
		}

		sender_slot = (int)(host_client - svs.clients);
		if (target_slot == sender_slot)
		{
			Host_Ignore_Printf("You cannot ignore yourself.\n");
			return;
		}

		if (ignored)
		{
			if (Host_ServerChatIgnored(host_client, target_slot))
			{
				Host_Ignore_Printf("Already ignoring %s on this server.\n", resolved_name);
				return;
			}

			Host_ServerSetChatIgnored(host_client, target_slot, true);
			Host_Ignore_Printf("Ignoring %s on this server.\n", resolved_name);
		}
		else
		{
			if (!Host_ServerChatIgnored(host_client, target_slot))
			{
				Host_Ignore_Printf("%s is not ignored on this server.\n", resolved_name);
				return;
			}

			Host_ServerSetChatIgnored(host_client, target_slot, false);
			Host_Ignore_Printf("No longer ignoring %s on this server.\n", resolved_name);
		}

		return;
	}

	if (cmd_source != src_command)
		return;

	if (cls.state != ca_connected || cl.maxclients < 1)
	{
		Con_Printf("Not connected.\n");
		return;
	}

	if (ignored)
	{
		int target_slot = Host_FindLocalPlayerSlot(target_arg, resolved_name, sizeof(resolved_name), &ambiguous);
		int local_slot = cl.realviewentity > 0 ? cl.realviewentity - 1 : cl.viewentity - 1;

		if (target_slot < 0)
		{
			Host_Ignore_Printf("%s player match for %s\n", ambiguous ? "No unique" : "No", target_arg);
			return;
		}
		if (local_slot >= 0 && local_slot < cl.maxclients && target_slot == local_slot)
		{
			Host_Ignore_Printf("You cannot ignore yourself.\n");
			return;
		}

		if (!CL_AddIgnoredName(resolved_name, target_slot))
		{
			Host_Ignore_Printf("Already ignoring %s locally.\n", resolved_name);
			return;
		}

		Host_Ignore_Printf("Ignoring %s locally.\n", resolved_name);
		return;
	}
	else
	{
		int stored_slot = CL_FindIgnoredNameSlot(target_arg, resolved_name, sizeof(resolved_name), &ambiguous);

		if (stored_slot < 0)
		{
			int target_slot = Host_FindLocalPlayerSlot(target_arg, resolved_name, sizeof(resolved_name), &ambiguous);

			if (target_slot < 0)
			{
				Host_Ignore_Printf("%s ignored player match for %s\n", ambiguous ? "No unique" : "No", target_arg);
				return;
			}
		}
		else
		{
			(void)stored_slot;
		}

		if (!CL_RemoveIgnoredName(resolved_name))
		{
			Host_Ignore_Printf("%s is not ignored locally.\n", resolved_name);
			return;
		}

		Host_Ignore_Printf("No longer ignoring %s locally.\n", resolved_name);
	}
}

static void Host_Ignore_f(void)
{
	Host_Ignore_Common(true);
}

static void Host_Unignore_f(void)
{
	Host_Ignore_Common(false);
}

/*
==================
Host_Quit_f
==================
*/
static qboolean host_quit_confirmed = false;

void Host_Quit_f (void)
{
	static qboolean quit_in_progress = false;
	qboolean confirmed = host_quit_confirmed;

	host_quit_confirmed = false;

	if (!confirmed && key_dest == key_console && cls.state != ca_dedicated && !cls.menu_qcvm.progs && cl.matchinp) // woods #matchquit
		M_Menu_Quit_f ();
	
	if (!confirmed && key_dest != key_console && cls.state != ca_dedicated && !cls.menu_qcvm.progs)
	{
		M_Menu_Quit_f ();
		return;
	}

	if (quit_in_progress)
		return;
	quit_in_progress = true;

	SCR_QuitFade ();

	CL_Disconnect ();
	Host_ShutdownServer(false);

	if (!cl_afk.value) // if I disable it, lets delete it
		remove(va("%s/id1/backups/name.txt", com_basedir));

	Sys_Quit ();
}

void Host_Quit_Confirmed_f (void)
{
	host_quit_confirmed = true;
	Host_Quit_f ();
}

//==============================================================================
//johnfitz -- extramaps management
//==============================================================================

/*
==================
FileList_Add
==================
*/
void FileList_Add (const char *name, const char* data, filelist_item_t **list) // woods #demolistsort add arg, remove static
{
	filelist_item_t	*item,*cursor,*prev;

	// ignore duplicate
	for (item = *list; item; item = item->next)
	{
		if (!q_strcasecmp (name, item->name))
			return;
	}

	item = (filelist_item_t *) Z_Malloc(sizeof(filelist_item_t));
	if (!item)
		Sys_Error("FileList_Add: out of memory on %lu bytes (%s)",
			(unsigned long)sizeof(filelist_item_t), name);
	memset(item, 0, sizeof(*item));
	q_strlcpy (item->name, name, sizeof(item->name));
	if (data)
		q_strlcpy(item->data, data, sizeof(item->data)); // woods #demolistsort add arg

	// insert each entry in alphabetical order
	if (*list == NULL ||
		q_strnaturalcmp(item->name, (*list)->name) < 0) //insert at front
	{
		item->next = *list;
		*list = item;
	}
	else //insert later
	{
		prev = *list;
		cursor = (*list)->next;
		while (cursor && (q_strnaturalcmp(item->name, cursor->name) > 0))
		{
			prev = cursor;
			cursor = cursor->next;
		}
		item->next = prev->next;
		prev->next = item;
	}
}

/*
==================
FileList_Subtract -- woods
==================
*/
void FileList_Subtract (const char* name, filelist_item_t** list)
{
	filelist_item_t* cursor, * prev = NULL;

	for (cursor = *list; cursor != NULL; prev = cursor, cursor = cursor->next)
	{
		if (!Q_strcmp(name, cursor->name))
		{
			if (cursor == *list) // If it's the first item in the list
			{
				*list = cursor->next;
			}
			else // If it's in the middle or end
			{
				prev->next = cursor->next;
			}

			Z_Free(cursor);
			return;
		}
	}
}

static void FileList_Clear (filelist_item_t **list)
{
	filelist_item_t *blah;

	while (*list)
	{
		blah = (*list)->next;
		Z_Free(*list);
		*list = blah;
	}
}

filelist_item_t	*extralevels;

void ExtraMaps_Add (const char *name)
{
	FileList_Add(name, NULL, &extralevels); // woods #demolistsort add arg
}

void ExtraMaps_Init (void)
{
#ifdef _WIN32
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
#else
	DIR		*dir_p;
	struct dirent	*dir_t;
#endif
	char		filestring[MAX_OSPATH];
	char		mapname[32];
	//char		ignorepakdir[32]; // woods, no lets search in paks
	searchpath_t	*search;
	pack_t		*pak;
	int		i;

	// we don't want to list the maps in id1 pakfiles,
	// because these are not "add-on" levels
	//q_snprintf (ignorepakdir, sizeof(ignorepakdir), "/%s/", GAMENAME); // woods, no lets search in paks

	for (search = com_searchpaths; search; search = search->next)
	{
		if (!search->pack) //directory
		{
#ifdef _WIN32
			q_snprintf (filestring, sizeof(filestring), "%s/maps/*.bsp", search->filename);
			fhnd = FindFirstFile(filestring, &fdat);
			if (fhnd == INVALID_HANDLE_VALUE)
				continue;
			do
			{
				COM_StripExtension(fdat.cFileName, mapname, sizeof(mapname));
				ExtraMaps_Add (mapname);
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
#else
			q_snprintf (filestring, sizeof(filestring), "%s/maps/", search->filename);
			dir_p = opendir(filestring);
			if (dir_p == NULL)
				continue;
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "bsp") != 0)
					continue;
				COM_StripExtension(dir_t->d_name, mapname, sizeof(mapname));
				ExtraMaps_Add (mapname);
			}
			closedir(dir_p);
#endif
		}
		else //pakfile
		{
			//if (!strstr(search->pack->filename, ignorepakdir)) // woods, no lets search in paks
			//{ //don't list standard id maps
				for (i = 0, pak = search->pack; i < pak->numfiles; i++)
				{
					if (!strcmp(COM_FileGetExtension(pak->files[i].name), "bsp"))
					{
						COM_StripExtension(pak->files[i].name + 5, mapname, sizeof(mapname));

						if (pak->files[i].filelen > 32*1024 && !isSpecialMap(mapname))
						{ // don't list files under 32k (ammo boxes etc) or certain names (ex. authmdl are larger) -- woods
							ExtraMaps_Add (mapname);
						}
					}
				}
			//}
		}
	}
}

static void ExtraMaps_Clear (void)
{
	FileList_Clear(&extralevels);
	descriptionsParsed = false;
}

void ExtraMaps_NewGame (void)
{
	ExtraMaps_Clear ();
	ExtraMaps_Init ();
}

//==============================================================================
// woods -- worldspawn map description support #mapdescriptions
//==============================================================================

#define	MAXDESC	50

int max_word_length = 0;
qboolean descriptionsParsed = false;

void FreeLevelList(filelist_item_t* list)
{
	filelist_item_t* level = list;
	while (level)
	{
		filelist_item_t* next = level->next;
		free(level);
		level = next;
	}
}

filelist_item_t* FindLevelInList(filelist_item_t* list, const char* name)
{
	filelist_item_t* level;
	for (level = list; level; level = level->next)
	{
		// Add case-insensitive comparison
		if (q_strcasecmp(level->name, name) == 0)
			return level;
	}
	return NULL;
}

static void FileList_ClearMapSizeCache(filelist_item_t *item)
{
	if (!item)
		return;

	item->total_surface_area = 0.0f;
	item->floor_surface_area = 0.0f;
	item->wall_surface_area = 0.0f;
	item->ceiling_surface_area = 0.0f;
	item->counted_faces = 0;
	item->total_faces = 0;
	item->has_mapsize_cache = false;
}

static void FileList_CopyMapSizeCache(filelist_item_t *dst, const filelist_item_t *src)
{
	if (!dst)
		return;

	if (!src || !src->has_mapsize_cache)
	{
		FileList_ClearMapSizeCache(dst);
		return;
	}

	dst->total_surface_area = src->total_surface_area;
	dst->floor_surface_area = src->floor_surface_area;
	dst->wall_surface_area = src->wall_surface_area;
	dst->ceiling_surface_area = src->ceiling_surface_area;
	dst->counted_faces = src->counted_faces;
	dst->total_faces = src->total_faces;
	dst->has_mapsize_cache = true;
}

void InitializeMapDescJSON(void)
{
	char fname[MAX_OSPATH];
	FILE* file;

	q_snprintf(fname, sizeof(fname), "%s/id1/backups/mapdesc.json", com_basedir);

	file = fopen(fname, "r");
	if (file) {
		fclose(file);
		return;
	}

	file = fopen(fname, "w");
	if (!file) {
		Con_DPrintf("Failed to create mapdesc.json\n");
		return;
	}

	fprintf(file, "[\n]\n");
	fclose(file);
}

void SaveMapDescriptionsToJSON(filelist_item_t* extralevels)
{
	char fname[MAX_OSPATH];
	FILE* file;

	if (q_snprintf(fname, sizeof(fname), "%s/id1/backups/mapdesc.json", com_basedir) >= sizeof(fname)) {
		Con_DPrintf("Path too long for buffer\n");
		return;
	}

	file = fopen(fname, "w");
	if (!file) {
		Con_DPrintf("Failed to open mapdesc.json for writing\n");
		return;
	}

	fprintf(file, "[\n");

	filelist_item_t* level;
	qboolean first = true;
	for (level = extralevels; level; level = level->next)
	{
		if (!first)
			fprintf(file, ",\n");
		first = false;

		// If data is empty, explicitly mark it as empty-description
		const char* description = level->data[0] ? level->data : "empty-description";

		char* escaped_name = JSON_EscapeString(level->name);
		char* escaped_description = JSON_EscapeString(description);

		if (!escaped_name || !escaped_description) {
			Con_DPrintf("Failed to escape JSON string\n");
			free(escaped_name);
			free(escaped_description);
			fclose(file);
			return;
		}

			fprintf(file, "  {\n");
			fprintf(file, "    \"name\": \"%s\",\n", escaped_name);
			fprintf(file, "    \"description\": \"%s\"", escaped_description);
			if (level->has_mapsize_cache)
			{
				fprintf(file, ",\n");
				fprintf(file, "    \"total_surface_area\": %.9g,\n", (double)level->total_surface_area);
				fprintf(file, "    \"floor_surface_area\": %.9g,\n", (double)level->floor_surface_area);
				fprintf(file, "    \"wall_surface_area\": %.9g,\n", (double)level->wall_surface_area);
				fprintf(file, "    \"ceiling_surface_area\": %.9g,\n", (double)level->ceiling_surface_area);
				fprintf(file, "    \"counted_faces\": %d,\n", level->counted_faces);
				fprintf(file, "    \"total_faces\": %d\n", level->total_faces);
			}
			else
			{
				fprintf(file, "\n");
			}
			fprintf(file, "  }");

		free(escaped_name);
		free(escaped_description);
	}

	fprintf(file, "\n]\n");
	fclose(file);
}

void LoadMapDescriptionsFromJSON(filelist_item_t** extralevels_from_json)
{
	char fname[MAX_OSPATH];
	FILE* file;
	long file_size;
	char* jsonText;
	json_t* json;
	filelist_item_t* last = NULL;

	*extralevels_from_json = NULL;

	InitializeMapDescJSON();

	q_snprintf(fname, sizeof(fname), "%s/id1/backups/mapdesc.json", com_basedir);

	file = fopen(fname, "rb");
	if (!file) {
		Con_DPrintf("Failed to open mapdesc.json\n");
		return;
	}

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	rewind(file);

	if (file_size <= 0) {
		fclose(file);
		return;
	}

	jsonText = malloc(file_size + 1);
	if (!jsonText) {
		fclose(file);
		return;
	}

	if (fread(jsonText, 1, file_size, file) != (size_t)file_size) {
		Con_DPrintf("Failed to read entire file\n");
		free(jsonText);
		fclose(file);
		return;
	}

	jsonText[file_size] = '\0';
	fclose(file);

	json = JSON_Parse(jsonText);
	free(jsonText);

	if (!json || !json->root || json->root->type != JSON_ARRAY) {
		if (json) JSON_Free(json);
		// If JSON is invalid, reinitialize the file
		InitializeMapDescJSON();
		return;
	}

	// Parse entries
	const jsonentry_t* mapEntry;
	for (mapEntry = json->root->firstchild; mapEntry; mapEntry = mapEntry->next)
	{
		const char* name = JSON_FindString(mapEntry, "name");
		const char* description = JSON_FindString(mapEntry, "description");

		if (!name || !description || description[0] == '\0') continue;

		filelist_item_t* item = malloc(sizeof(filelist_item_t));
		if (!item) {
			Con_DPrintf("Memory allocation failed\n");
			FreeLevelList(*extralevels_from_json);
			*extralevels_from_json = NULL;	//callers walk and re-free this list
			JSON_Free(json);
			return;
		}
		memset(item, 0, sizeof(*item));

		Q_strncpy(item->name, name, MAX_QPATH - 1);
		item->name[MAX_QPATH - 1] = '\0';

		Q_strncpy(item->data, description, 49);
		item->data[49] = '\0';

		{
			const double *total_surface_area = JSON_FindNumber(mapEntry, "total_surface_area");
			const double *floor_surface_area = JSON_FindNumber(mapEntry, "floor_surface_area");
			const double *wall_surface_area = JSON_FindNumber(mapEntry, "wall_surface_area");
			const double *ceiling_surface_area = JSON_FindNumber(mapEntry, "ceiling_surface_area");
			const double *counted_faces = JSON_FindNumber(mapEntry, "counted_faces");
			const double *total_faces = JSON_FindNumber(mapEntry, "total_faces");

			if (total_surface_area && floor_surface_area && wall_surface_area && ceiling_surface_area
				&& counted_faces && total_faces)
			{
				item->total_surface_area = (float)*total_surface_area;
				item->floor_surface_area = (float)*floor_surface_area;
				item->wall_surface_area = (float)*wall_surface_area;
				item->ceiling_surface_area = (float)*ceiling_surface_area;
				item->counted_faces = (int)*counted_faces;
				item->total_faces = (int)*total_faces;
				item->has_mapsize_cache = true;
			}
		}

		item->next = NULL;

		if (!*extralevels_from_json)
			*extralevels_from_json = item;
		else
			last->next = item;
		last = item;
	}

	JSON_Free(json);
}

void UpdateMaxWordLength (const char* word)
{
	int word_length = strlen(word);
	if (word_length > max_word_length)
		max_word_length = word_length;
}

void ExtraMaps_ParseDescriptions(void)
{
	filelist_item_t* level;
	filelist_item_t* extralevels_from_json = NULL;

	LoadMapDescriptionsFromJSON(&extralevels_from_json);

	for (level = extralevels; level; level = level->next)
		UpdateMaxWordLength(level->name);

	for (level = extralevels; level; level = level->next)
	{
		filelist_item_t* json_level = FindLevelInList(extralevels_from_json, level->name);
		if (json_level)
		{
			// Trust the cached empty-description status
			if (strcmp(json_level->data, "empty-description") == 0)
			{
				level->data[0] = '\0'; // Keep it empty
			}
			else
			{
				// Use cached description
				strncpy(level->data, json_level->data, sizeof(level->data) - 1);
				level->data[sizeof(level->data) - 1] = '\0';
			}

			FileList_CopyMapSizeCache(level, json_level);
		}
		else
		{
			// Only load from .bsp for new/uncached maps
			char mapdesc[MAXDESC];
			Mod_LoadMapDescription(mapdesc, sizeof(mapdesc), level->name);
			Q_strncpy(level->data, mapdesc, sizeof(level->data) - 1);
			Con_DPrintf("cached new map description %s\n", level->name);
			level->data[sizeof(level->data) - 1] = '\0';
			FileList_ClearMapSizeCache(level);
		}
	}

	SaveMapDescriptionsToJSON(extralevels);

	FreeLevelList(extralevels_from_json);
	extralevels_from_json = NULL;

	descriptionsParsed = true;
}

void FileList_Add_MapDesc (const char* levelName) // for a map download
{
	if (!descriptionsParsed)
		ExtraMaps_ParseDescriptions();
	
	UpdateMaxWordLength (levelName);

	char mapdesc[MAXDESC];
	Mod_LoadMapDescription (mapdesc, sizeof(mapdesc), levelName);

	FileList_Add (levelName, mapdesc, &extralevels);

	SaveMapDescriptionsToJSON(extralevels); // save the updated extralevels list to mapdesc.json
}

static void Host_Maps_f (void) // prints worldspawn map description
{
	if (!descriptionsParsed)
		ExtraMaps_ParseDescriptions();

	filelist_item_t* level;
	int count = 0;
	const char* filter = NULL;

	if (Cmd_Argc() >= 2)
		filter = Cmd_Argv(1);

	Con_SafePrintf("\n");

	for (level = extralevels; level; level = level->next)
	{
		char buf[MAX_CHAT_SIZE_EX];
		char combined[MAX_CHAT_SIZE_EX];

		int word_length = strlen(level->name);
		int num_spaces = (max_word_length + 2) - word_length;
		if (num_spaces < 1) num_spaces = 1;

		// Calculate available space for level->data
		int name_space = word_length + num_spaces;
		int remaining_space = sizeof(combined) - name_space - 2;
		if (remaining_space < 10)
		{
			remaining_space = 10;
			name_space = sizeof(combined) - remaining_space - 2;
		}
		q_snprintf(combined, sizeof(combined), "%-*s %.*s",
			name_space, level->name,
			remaining_space - 1, level->data);
		if (filter) 
		{
			if (!(q_strcasestr(level->name, filter) || q_strcasestr(level->data, filter)))
				continue;

			COM_TintSubstring(combined, filter, buf, sizeof(buf));
			q_strlcpy(combined, buf, sizeof(combined));
		}

		Con_SafePrintf("   %s\n", combined);
		count++;
	}

	if (filter) 
		Con_SafePrintf("\n%i map(s) found containing '%s'\n\n", count, filter);
	else
	{
		if (count)
			Con_SafePrintf("\n%i map(s)\n\n", count);
		else
			Con_SafePrintf("\nno maps found\n\n");
	}
}

//==============================================================================
// woods -- FolderList id1 directories management for open cmd #folderlist
//==============================================================================

filelist_item_t* folderlist;

static void FolderList_Add (const char* name)
{
	FileList_Add (name, NULL, &folderlist); // woods #demolistsort add arg
}

static void FolderList_Clear (void)
{
	FileList_Clear (&folderlist);
}

void FolderList_Rebuild (void)
{
	FolderList_Clear ();
	FolderList_Init ();
}

#ifdef _WIN32
void FolderList_Init(void)
{
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
	DWORD		attribs;
	char		dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf(dir_string, sizeof(dir_string), "%s/*", com_gamedir);
	fhnd = FindFirstFile(dir_string, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
			continue;
		q_snprintf(mod_string, sizeof(mod_string), "%s/%s", com_gamedir, fdat.cFileName);
		attribs = GetFileAttributes(mod_string);
		if (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
			/* don't bother testing for pak files / progs.dat */
			FolderList_Add(fdat.cFileName);
		}
	} while (FindNextFile(fhnd, &fdat));

	FolderList_Add ("id1");

	FindClose(fhnd);
}
#else
void FolderList_Init(void)
{
	DIR* dir_p, * mod_dir_p;
	struct dirent* dir_t;
	char		dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf(dir_string, sizeof(dir_string), "%s/", com_gamedir);
	dir_p = opendir(dir_string);
	if (dir_p == NULL)
		return;

	while ((dir_t = readdir(dir_p)) != NULL)
	{
		if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
			continue;
		if (!q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "app")) // skip .app bundles on macOS
			continue;
		q_snprintf(mod_string, sizeof(mod_string), "%s%s/", dir_string, dir_t->d_name);
		mod_dir_p = opendir(mod_string);
		if (mod_dir_p == NULL)
			continue;
		/* don't bother testing for pak files / progs.dat */
		FolderList_Add(dir_t->d_name);
		closedir(mod_dir_p);
	}

	FolderList_Add("id1");

	closedir(dir_p);
}
#endif

//==============================================================================
//johnfitz -- modlist management
//==============================================================================

filelist_item_t	*modlist;

static void Modlist_Add (const char *name)
{
	FileList_Add(name, NULL, &modlist); // woods #demolistsort add arg
}

#ifdef _WIN32
static void Modlist_AddDirectoriesInPath (const char *path)
{
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
	DWORD		attribs;
	char		dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf (dir_string, sizeof(dir_string), "%s/*", path);
	fhnd = FindFirstFile(dir_string, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
			continue;
		q_snprintf (mod_string, sizeof(mod_string), "%s/%s", path, fdat.cFileName);
		attribs = GetFileAttributes (mod_string);
		if (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
			/* don't bother testing for pak files / progs.dat */
			Modlist_Add(fdat.cFileName);
		}
	} while (FindNextFile(fhnd, &fdat));

	FindClose(fhnd);
}

void Modlist_Init (void)
{
	char modpath[MAX_OSPATH];

	Modlist_AddDirectoriesInPath(com_basedir);
	q_snprintf(modpath, sizeof(modpath), "%s/games", com_basedir);
	Modlist_AddDirectoriesInPath(modpath);
	q_snprintf(modpath, sizeof(modpath), "%s/mods", com_basedir);
	Modlist_AddDirectoriesInPath(modpath);
}
#else
static void Modlist_AddDirectoriesInPath (const char *path)
{
	DIR		*dir_p, *mod_dir_p;
	struct dirent	*dir_t;
	char		dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf (dir_string, sizeof(dir_string), "%s/", path);
	dir_p = opendir(dir_string);
	if (dir_p == NULL)
		return;

	while ((dir_t = readdir(dir_p)) != NULL)
	{
		if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
			continue;
		if (!q_strcasecmp (COM_FileGetExtension (dir_t->d_name), "app")) // skip .app bundles on macOS
			continue;
		q_snprintf(mod_string, sizeof(mod_string), "%s%s/", dir_string, dir_t->d_name);
		mod_dir_p = opendir(mod_string);
		if (mod_dir_p == NULL)
			continue;
		/* don't bother testing for pak files / progs.dat */
		Modlist_Add(dir_t->d_name);
		closedir(mod_dir_p);
	}

	closedir(dir_p);
}

void Modlist_Init (void)
{
	char modpath[MAX_OSPATH];

	Modlist_AddDirectoriesInPath(com_basedir);
	q_snprintf(modpath, sizeof(modpath), "%s/games", com_basedir);
	Modlist_AddDirectoriesInPath(modpath);
	q_snprintf(modpath, sizeof(modpath), "%s/mods", com_basedir);
	Modlist_AddDirectoriesInPath(modpath);
}
#endif

//==============================================================================
// coop mod voting
//==============================================================================

typedef struct modvote_choice_s
{
	char					name[MAX_QPATH];
	int						votes;
	struct modvote_choice_s *next;
} modvote_choice_t;

static modvote_choice_t *sv_modvote_choices;
static modvote_choice_t *sv_modvote_client_choices[MAX_SCOREBOARD];
static Uint32 sv_modvote_last_vote_time[MAX_SCOREBOARD];

#define MODVOTE_COOLDOWN_MS 1000U
#define MODVOTE_TAG "^m[vote]^m "
#define MODVOTE_MOTD_MIN_SECONDS 10.0
#define MODVOTE_MOTD_LEGACY_REFRESH_SECONDS 1.0

static void Host_Modvote_Core(client_t *voter, const char *modname, qboolean is_chat);
static void Host_Modvote_Apply(const char *modname);

static void Host_Modvote_StyleText(char *text)
{
	char *src = text;
	char *dst = text;
	qboolean masked = false;
	qboolean gold_digits = false;

	while (*src)
	{
		unsigned char ch = (unsigned char)*src++;

		if (ch == '^' && *src == '^')
		{
			src++;
			*dst++ = '^';
			continue;
		}
		if (ch == '^' && *src == 'm')
		{
			src++;
			masked = !masked;
			continue;
		}
		if (ch == '^' && *src == 'g')
		{
			src++;
			gold_digits = !gold_digits;
			continue;
		}

		if (gold_digits && ch >= '0' && ch <= '9')
			*dst++ = (char)(ch - 30);
		else if (masked && ch >= 32 && ch < 127)
			*dst++ = (char)(ch | 128);
		else
			*dst++ = (char)ch;
	}

	*dst = '\0';
}

static void Host_Modvote_EscapeMarkup(const char *src, char *dst, size_t dstsize)
{
	char *out = dst;
	char *end;

	if (!dstsize)
		return;

	if (!src)
		src = "";

	end = dst + dstsize - 1;
	while (*src && out < end)
	{
		if (*src == '^' && out + 1 < end)
			*out++ = '^';
		*out++ = *src++;
	}

	*out = '\0';
}

static void Host_Modvote_BroadcastPrintf(const char *fmt, ...)
{
	va_list argptr;
	char msg[1024];

	va_start(argptr, fmt);
	q_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);
	Host_Modvote_StyleText(msg);

	SV_BroadcastPrintf("%s", msg);
}

static void Host_Modvote_Printf(client_t *client, const char *fmt, ...)
{
	va_list argptr;
	char msg[1024];
	client_t *saved_client = host_client;

	va_start(argptr, fmt);
	q_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);
	Host_Modvote_StyleText(msg);

	if (client)
	{
		host_client = client;
		SV_ClientPrintf("%s", msg);
		host_client = saved_client;
	}
	else
	{
		Con_Printf("%s", msg);
	}
}

static void Host_Modvote_CenterPrintf(client_t *client, const char *fmt, ...)
{
	va_list argptr;
	char msg[1024];

	if (!client)
		return;

	va_start(argptr, fmt);
	q_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);
	Host_Modvote_StyleText(msg);

	MSG_WriteByte(&client->message, svc_centerprint);
	MSG_WriteString(&client->message, msg);
}

static qboolean Host_ClientIsQSSM(client_t *client)
{
	char ver[64];

	if (!client)
		return false;

	Info_GetKey(client->userinfo, "*ver", ver, sizeof(ver));
	return !q_strncasecmp(ver, "QSS-M", 5);
}

static qboolean Host_Modvote_ClientSupportsPersistentCenterprint(client_t *client)
{
	return Host_ClientIsQSSM(client);
}

static void Host_Modvote_SendJoinCenterprint(client_t *client)
{
	char safe_hostname[128];
	const char *servername;
	const char *coop_line;
	const char *prefix = "";

	if (!client)
		return;
	if (client->modvote_motd_centerprint_suppressed)
		return;

	Host_Modvote_EscapeMarkup(hostname.string, safe_hostname, sizeof(safe_hostname));
	servername = safe_hostname[0] ? safe_hostname : "this server";
	coop_line = (coop.value >= 1.0f) ?
		"Server mod voting is enabled for this coop game." :
		"Server mod voting is enabled and becomes available during coop games.";

	if (Host_Modvote_ClientSupportsPersistentCenterprint(client))
		prefix = "/P";

	Host_Modvote_CenterPrintf(client, "%s^mWelcome to %s^m\n\n%s\n\n^mVote with^m ^mmodvote <modname>^m\n\n^mChat:^m ^m/modvote <modname>^m or ^m!modvote <modname>^m",
		prefix, servername, coop_line);
}

void Host_Modvote_NotifyQCCenterprint(client_t *client)
{
	if (!client || !client->netconnection)
		return;

	client->modvote_motd_centerprint_suppressed = true;
	client->modvote_motd_active = false;
}

static qboolean Host_Modvote_ClientTriedJoinMotdInput(client_t *client)
{
	if (!client || !client->edict)
		return false;

	if (client->cmd.forwardmove || client->cmd.sidemove || client->cmd.upmove)
		return true;
	if (client->edict->v.button0 || client->edict->v.button2 || client->edict->v.impulse)
		return true;

	return false;
}

static void Host_Modvote_ClientReply(client_t *client, const char *fmt, ...)
{
	va_list argptr;
	char msg[1024];

	va_start(argptr, fmt);
	q_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	Host_Modvote_Printf(client, "\n%s\n", msg);
}

static qboolean Host_Modvote_IsValidModName(const char *modname)
{
	if (!modname || !modname[0])
		return false;
	if (modname[0] == '-' || !strcmp(modname, "."))
		return false;
	if (strstr(modname, "..") || strstr(modname, "/") || strstr(modname, "\\") || strstr(modname, ":") || strstr(modname, ";"))
		return false;
	return true;
}

static modvote_choice_t *Host_Modvote_FindChoice(const char *modname)
{
	modvote_choice_t *choice;

	for (choice = sv_modvote_choices; choice; choice = choice->next)
	{
		if (!q_strcasecmp(choice->name, modname))
			return choice;
	}

	return NULL;
}

static void Host_Modvote_UnlinkChoice(modvote_choice_t *choice)
{
	modvote_choice_t **link;

	for (link = &sv_modvote_choices; *link; link = &(*link)->next)
	{
		if (*link == choice)
		{
			*link = choice->next;
			Z_Free(choice);
			return;
		}
	}
}

static qboolean Host_Modvote_IsValidMod(const char *modname)
{
	filelist_item_t *mod;

	for (mod = modlist; mod; mod = mod->next)
	{
		if (!q_strcasecmp(mod->name, modname))
			return true;
	}

	return false;
}

static qboolean Host_Modvote_ClientIsEligible(client_t *cl)
{
	char chat_val[64];
	char spectator_val[16];

	// Botclients have no netconnection in this engine and should not affect
	// human vote thresholds or eligibility.
	if (!cl || !cl->active || !cl->spawned || !cl->netconnection)
		return false;

	Info_GetKey(cl->userinfo, "*spectator", spectator_val, sizeof(spectator_val));
	if (spectator_val[0] && Q_atoi(spectator_val))
		return false;

	Info_GetKey(cl->userinfo, "chat", chat_val, sizeof(chat_val));
	if (chat_val[0] && (!q_strcasecmp(chat_val, "afk") || (Q_atoi(chat_val) & CIF_AFK)))
		return false;

	return true;
}

static void Host_Modvote_PruneIneligibleVotes(void)
{
	int i;

	for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; ++i)
	{
		if (sv_modvote_client_choices[i] && !Host_Modvote_ClientIsEligible(&svs.clients[i]))
			Host_Modvote_RemoveClientVote(i);
	}
}

static int Host_Modvote_CountEligibleClients(void)
{
	int i;
	int count = 0;

	for (i = 0; i < svs.maxclients; ++i)
	{
		if (Host_Modvote_ClientIsEligible(&svs.clients[i]))
			++count;
	}

	return count;
}

static int Host_Modvote_VotesNeeded(int eligible)
{
	if (eligible <= 0)
		return 1;
	return (eligible / 2) + 1;
}

static qboolean Host_Modvote_CheckMajority(void)
{
	modvote_choice_t *choice;
	int eligible;
	int needed;

	if (!sv.active || sv_modvote.value < 1.0f || coop.value < 1.0f || !sv_modvote_choices)
		return false;

	Host_Modvote_PruneIneligibleVotes();
	eligible = Host_Modvote_CountEligibleClients();
	needed = Host_Modvote_VotesNeeded(eligible);

	for (choice = sv_modvote_choices; choice; choice = choice->next)
	{
		if (choice->votes >= needed)
		{
			char modname[MAX_QPATH];

			q_strlcpy(modname, choice->name, sizeof(modname));
			Host_Modvote_Apply(modname);
			return true;
		}
	}

	return false;
}

static void Host_Modvote_PrintAvailableMods(client_t *client, qboolean motd_format)
{
	filelist_item_t *mod;
	int count = 0;
	const char *active_mod = COM_GetGameNames(false);
	char safe_active_mod[2048];
	char safe_modname[MAX_QPATH * 2];

	if (motd_format)
		Host_Modvote_Printf(client, "Available vote options:\n");
	else
		Host_Modvote_Printf(client, MODVOTE_TAG "Available vote options:\n");

	if (!active_mod || !active_mod[0])
		active_mod = GAMENAME;

	Host_Modvote_EscapeMarkup(active_mod, safe_active_mod, sizeof(safe_active_mod));
	Host_Modvote_Printf(client, "  ^m%s^m ^m(active)^m\n", safe_active_mod);

	for (mod = modlist; mod; mod = mod->next)
	{
		if (COM_GameDirMatches(mod->name))
			continue;

		Host_Modvote_EscapeMarkup(mod->name, safe_modname, sizeof(safe_modname));
		Host_Modvote_Printf(client, "  ^m%s^m\n", safe_modname);
		count++;
	}

	if (!count)
		Host_Modvote_Printf(client, "  ^mNo other mods are available to vote for.^m\n");
}

static void Host_Modvote_PrintJoinMotd(client_t *client)
{
	char safe_hostname[128];
	const char *servername;

	if (!client || !client->netconnection || client->modvote_motd_shown || sv_modvote.value < 1.0f)
		return;

	client->modvote_motd_shown = true;
	client->modvote_motd_active = false;
	client->modvote_motd_input_seen = false;

	Host_Modvote_EscapeMarkup(hostname.string, safe_hostname, sizeof(safe_hostname));
	servername = safe_hostname[0] ? safe_hostname : "this server";

	Host_Modvote_Printf(client, "\n");
	if (!client->modvote_motd_centerprint_suppressed)
	{
		client->modvote_motd_active = true;
		client->modvote_motd_start_time = realtime;
		client->modvote_motd_next_refresh_time = realtime + MODVOTE_MOTD_LEGACY_REFRESH_SECONDS;
		Host_Modvote_SendJoinCenterprint(client);
	}

	Host_Modvote_Printf(client, "^mWelcome to %s.^m\n\n", servername);
	if (coop.value >= 1.0f)
		Host_Modvote_Printf(client, "Server mod voting is enabled for this coop game.\n\n");
	else
		Host_Modvote_Printf(client, "Server mod voting is enabled and becomes available during coop games.\n\n");
	Host_Modvote_Printf(client, "^mVote with^m ^mmodvote <modname>^m.\n\n");
	Host_Modvote_Printf(client, "^mChat:^m ^m/modvote <modname>^m or ^m!modvote <modname>^m.\n\n");
	Host_Modvote_Printf(client, "Use ^mmodvote^m with no mod name to view status and vote options.\n\n");
	Host_Modvote_PrintAvailableMods(client, true);
	Host_Modvote_Printf(client, "\n");
}

void Host_Modvote_UpdateJoinMotd(void)
{
	int i;

	if (Host_Modvote_CheckMajority())
		return;

	for (i = 0; i < svs.maxclients; ++i)
	{
		client_t *client = &svs.clients[i];

		if (!client->active || !client->spawned || !client->netconnection || !client->modvote_motd_active)
			continue;

		if (Host_Modvote_ClientTriedJoinMotdInput(client))
			client->modvote_motd_input_seen = true;

		if (client->modvote_motd_input_seen &&
			realtime - client->modvote_motd_start_time >= MODVOTE_MOTD_MIN_SECONDS)
		{
			Host_Modvote_CenterPrintf(client, "");
			client->modvote_motd_active = false;
			continue;
		}

		if (!Host_Modvote_ClientSupportsPersistentCenterprint(client) &&
			realtime >= client->modvote_motd_next_refresh_time)
		{
			Host_Modvote_SendJoinCenterprint(client);
			client->modvote_motd_next_refresh_time = realtime + MODVOTE_MOTD_LEGACY_REFRESH_SECONDS;
		}
	}
}

static void Host_Modvote_PrintStatus(client_t *client)
{
	modvote_choice_t *choice;
	int eligible;
	int needed;
	const char *player_plural;
	const char *vote_plural;
	qboolean framed = (client != NULL);

	if (framed)
		Host_Modvote_Printf(client, "\n");

	if (sv_modvote.value < 1.0f)
	{
		Host_Modvote_Printf(client, MODVOTE_TAG "Mod voting is disabled on this server.\n");
		goto done;
	}

	if (coop.value < 1.0f)
	{
		Host_Modvote_Printf(client, MODVOTE_TAG "Mod voting is only available in coop games.\n");
		goto done;
	}

	if (Host_Modvote_CheckMajority())
		return;

	eligible = Host_Modvote_CountEligibleClients();
	needed = Host_Modvote_VotesNeeded(eligible);
	player_plural = (eligible == 1) ? "" : "s";
	vote_plural = (needed == 1) ? "" : "s";

	Host_Modvote_Printf(client, MODVOTE_TAG "Status: ^g%d^g active coop player%s, ^g%d^g vote%s required for a majority.\n",
		eligible, player_plural, needed, vote_plural);

	if (!sv_modvote_choices)
		Host_Modvote_Printf(client, MODVOTE_TAG "No active tallies. Use ^m\"modvote <modname>\"^m to start one.\n");
	else
	{
		char safe_choice_name[MAX_QPATH * 2];

		Host_Modvote_Printf(client, MODVOTE_TAG "Current tallies:\n");
		for (choice = sv_modvote_choices; choice; choice = choice->next)
		{
			Host_Modvote_EscapeMarkup(choice->name, safe_choice_name, sizeof(safe_choice_name));
			Host_Modvote_Printf(client, "  ^m%s^m : ^g%d^g vote%s\n", safe_choice_name, choice->votes, (choice->votes == 1) ? "" : "s");
		}
	}

	Host_Modvote_PrintAvailableMods(client, false);

done:
	if (framed)
		Host_Modvote_Printf(client, "\n");
}

void Host_Modvote_Reset(void)
{
	modvote_choice_t *choice = sv_modvote_choices;

	while (choice)
	{
		modvote_choice_t *next = choice->next;
		Z_Free(choice);
		choice = next;
	}

	sv_modvote_choices = NULL;
	memset(sv_modvote_client_choices, 0, sizeof(sv_modvote_client_choices));
	memset(sv_modvote_last_vote_time, 0, sizeof(sv_modvote_last_vote_time));
}

void Host_Modvote_RemoveClientVote(int client_index)
{
	modvote_choice_t *choice;

	if (client_index < 0 || client_index >= MAX_SCOREBOARD)
		return;

	sv_modvote_last_vote_time[client_index] = 0;

	choice = sv_modvote_client_choices[client_index];
	if (!choice)
		return;

	sv_modvote_client_choices[client_index] = NULL;

	if (choice->votes > 0)
		--choice->votes;

	if (choice->votes <= 0)
		Host_Modvote_UnlinkChoice(choice);
}

static void Host_Modvote_Apply(const char *modname)
{
	int i;
	char modcopy[MAX_QPATH];
	char safe_modcopy[MAX_QPATH * 2];

	q_strlcpy(modcopy, modname, sizeof(modcopy));
	Host_Modvote_EscapeMarkup(modcopy, safe_modcopy, sizeof(safe_modcopy));

	Host_Modvote_Printf(NULL, MODVOTE_TAG "Passed for ^m\"%s\"^m.\n", safe_modcopy);
	Host_Modvote_BroadcastPrintf("\n" MODVOTE_TAG "Vote passed! Switching server to mod ^m\"%s\"^m.\n", safe_modcopy);

	Host_Modvote_Reset();

	for (i = 0; i < svs.maxclients; ++i)
	{
		client_t *cl = &svs.clients[i];

		if (!cl->active)
			continue;

		Host_Modvote_Printf(cl, MODVOTE_TAG "Vote passed. Server switching to mod ^m\"%s\"^m. You are being disconnected; load the mod client-side, then reconnect.\n\n", safe_modcopy);
	}

	for (i = 0; i < svs.maxclients; ++i)
	{
		client_t *cl = &svs.clients[i];

		if (!cl->active)
			continue;

		host_client = cl;
		SV_DropClient(false);
	}

	host_client = svs.clients;

	COM_SetModvoteAutostart();
	Cbuf_AddText(va("game %s\n", modcopy));
}

static void Host_Modvote_Core(client_t *voter, const char *modname_arg, qboolean is_chat)
{
	const char *modname = modname_arg ? modname_arg : "";
	char modname_token[MAX_QPATH];
	char safe_modname[MAX_QPATH * 2];
	modvote_choice_t *current;
	modvote_choice_t *choice;
	int client_index;
	int eligible;
	int needed;
	size_t i;
	Uint32 current_time;
	qboolean is_change;
	char old_name[MAX_QPATH];

	if (!sv.active)
	{
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "The server is not currently running a game.\n");
		return;
	}

	if (sv_modvote.value < 1.0f)
	{
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "Mod voting is disabled on this server.\n");
		return;
	}

	if (coop.value < 1.0f)
	{
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "Mod voting is only available in coop games.\n");
		return;
	}

	if (Host_Modvote_CheckMajority())
		return;

	if (!Host_Modvote_ClientIsEligible(voter))
	{
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "You must be an active in-game coop player to use modvote.\n");
		return;
	}

	while (q_isspace((unsigned char)*modname))
		modname++;
	if (*modname == '\"')
		modname++;
	while (q_isspace((unsigned char)*modname))
		modname++;

	if (!*modname)
	{
		Host_Modvote_PrintStatus(voter);
		return;
	}

	for (i = 0; modname[i] && !q_isspace((unsigned char)modname[i]) && modname[i] != '\"' && i + 1 < sizeof(modname_token); ++i)
		modname_token[i] = modname[i];
	modname_token[i] = '\0';
	modname = modname_token;
	Host_Modvote_EscapeMarkup(modname, safe_modname, sizeof(safe_modname));

	if (!Host_Modvote_IsValidModName(modname))
	{
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "Usage: ^m%s <modname>^m\n", is_chat ? "say /modvote" : "modvote");
		return;
	}

	if (!Host_Modvote_IsValidMod(modname))
	{
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "Mod ^m\"%s\"^m is not available on this server. Use ^m\"modvote\"^m with no mod name to list available vote options.\n", safe_modname);
		return;
	}

	if (COM_GameDirMatches(modname))
	{
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "Mod ^m\"%s\"^m is already active.\n", safe_modname);
		return;
	}

	eligible = Host_Modvote_CountEligibleClients();
	needed = Host_Modvote_VotesNeeded(eligible);

	client_index = (int)(voter - svs.clients);
	if (client_index < 0 || client_index >= svs.maxclients || client_index >= MAX_SCOREBOARD)
		return;

	current_time = SDL_GetTicks();
	if (!SDL_TICKS_PASSED(current_time, sv_modvote_last_vote_time[client_index] + MODVOTE_COOLDOWN_MS))
	{
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "Please wait a moment before changing your vote.\n");
		return;
	}
	sv_modvote_last_vote_time[client_index] = current_time;

	current = sv_modvote_client_choices[client_index];
	if (current && !q_strcasecmp(current->name, modname))
	{
		char safe_current_name[MAX_QPATH * 2];

		Host_Modvote_EscapeMarkup(current->name, safe_current_name, sizeof(safe_current_name));
		Host_Modvote_ClientReply(voter, MODVOTE_TAG "You have already voted for mod ^m\"%s\"^m (^g%d^g/^g%d^g).\n", safe_current_name, current->votes, needed);
		return;
	}

	is_change = (current != NULL);
	if (is_change)
	{
		q_strlcpy(old_name, current->name, sizeof(old_name));
		sv_modvote_client_choices[client_index] = NULL;
		if (current->votes > 0)
			--current->votes;
		if (current->votes <= 0)
			Host_Modvote_UnlinkChoice(current);
	}

	choice = Host_Modvote_FindChoice(modname);
	if (!choice)
	{
		choice = (modvote_choice_t *)Z_Malloc(sizeof(*choice));
		q_strlcpy(choice->name, modname, sizeof(choice->name));
		choice->votes = 0;
		choice->next = sv_modvote_choices;
		sv_modvote_choices = choice;
	}

	choice->votes++;
	sv_modvote_client_choices[client_index] = choice;

	{
		char safe_voter_name[MAX_QPATH * 2];
		char safe_choice_name[MAX_QPATH * 2];

		Host_Modvote_EscapeMarkup(voter->name, safe_voter_name, sizeof(safe_voter_name));
		Host_Modvote_EscapeMarkup(choice->name, safe_choice_name, sizeof(safe_choice_name));
		if (is_change)
		{
			char safe_old_name[MAX_QPATH * 2];
			Host_Modvote_EscapeMarkup(old_name, safe_old_name, sizeof(safe_old_name));
			Host_Modvote_BroadcastPrintf(MODVOTE_TAG "%s changed vote from ^m\"%s\"^m to ^m\"%s\"^m (^g%d^g/^g%d^g).\n", safe_voter_name, safe_old_name, safe_choice_name, choice->votes, needed);
		}
		else
		{
			Host_Modvote_BroadcastPrintf(MODVOTE_TAG "%s voted to switch to mod ^m\"%s\"^m (^g%d^g/^g%d^g).\n", safe_voter_name, safe_choice_name, choice->votes, needed);
		}
		Host_Modvote_BroadcastPrintf(MODVOTE_TAG "To vote: ^m\"modvote %s\"^m or chat ^m\"/modvote %s\"^m.\n", safe_choice_name, safe_choice_name);
		Host_Modvote_Printf(NULL, MODVOTE_TAG "%s voted for mod ^m\"%s\"^m (^g%d^g/^g%d^g).\n", safe_voter_name, safe_choice_name, choice->votes, needed);
	}

	if (choice->votes >= needed)
	{
		Host_Modvote_Apply(choice->name);
		return;
	}

	Host_Modvote_PrintStatus(NULL);
}

static void Host_Modvote_f(void)
{
	client_t *voter;

	if (cmd_source != src_client && cls.state == ca_connected && !cls.demoplayback)
	{
		Cmd_ForwardToServer();
		return;
	}

	if (!sv.active)
	{
		if (cmd_source == src_client)
			Host_Modvote_ClientReply(host_client, MODVOTE_TAG "The server is not currently running a game.\n");
		else
			Host_Modvote_Printf(NULL, MODVOTE_TAG "The server is not currently running a game.\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Host_Modvote_PrintStatus((cmd_source == src_client) ? host_client : NULL);
		return;
	}

	if (cmd_source == src_client)
	{
		voter = host_client;
	}
	else if (isDedicated)
	{
		Host_Modvote_Printf(NULL, MODVOTE_TAG "Only connected clients may cast ballots.\n");
		return;
	}
	else
	{
		voter = &svs.clients[0];
		if (!voter->active)
		{
			Host_Modvote_Printf(NULL, MODVOTE_TAG "No active local client is connected to cast a ballot.\n");
			return;
		}
	}

	Host_Modvote_Core(voter, Cmd_Argv(1), false);
}

//==============================================================================
// woods -- server list management #serverlist
//==============================================================================

filelist_item_t* serverlist;

#define SERVERHISTORY_TIME_LENGTH 21

typedef enum
{
	SERVERHISTORY_NOT_FOUND,
	SERVERHISTORY_LOADED,
	SERVERHISTORY_LOAD_ERROR
} serverhistory_load_result_t;

static void ServerHistory_GetPath (char *path, size_t path_size, const char *filename)
{
	q_snprintf(path, path_size, "%s/id1/backups/%s", com_basedir, filename);
}

/* Keep in sync with QSSValidServerAddress in macOS/Sources/Shared/QSSDockMenuContent.m,
   which applies the same rules to the same files from the Dock menu. */
static qboolean ServerHistory_ValidAddress (const char *address)
{
	qboolean alnum = false;
	size_t i;

	if (!address || !address[0] || strlen(address) >= sizeof(serverlist->name) ||
		!q_strcasecmp(address, "local") || !q_strcasecmp(address, "localhost"))
		return false;
	/* History entries are used by command-line and console reconnect paths. */
	if (address[0] == '+' || address[0] == '-' || strpbrk(address, ";\""))
		return false;

	for (i = 0; address[i]; i++)
	{
		if (q_isspace((unsigned char)address[i]) || (unsigned char)address[i] < 0x20)
			return false;
		if (q_isalnum((unsigned char)address[i]))
			alnum = true;
	}

	return alnum;	/* nothing resolvable is punctuation-only */
}

static qboolean ServerHistory_ValidTimestamp (const char *timestamp)
{
	static const char format[] = "dddd-dd-ddTdd:dd:ddZ";
	size_t i;

	if (!timestamp || strlen(timestamp) != sizeof(format) - 1)
		return false;
	for (i = 0; format[i]; i++)
	{
		if ((format[i] == 'd' && !q_isdigit((unsigned char)timestamp[i])) ||
			(format[i] != 'd' && timestamp[i] != format[i]))
			return false;
	}
	return true;
}

static qboolean ServerHistory_GetTimestamp (char *timestamp, size_t timestamp_size)
{
	time_t systime;
	struct tm *utc;

	if (!timestamp_size)
		return false;
	timestamp[0] = '\0';

	systime = time(NULL);
	utc = gmtime(&systime);
	if (!utc || strftime(timestamp, timestamp_size, "%Y-%m-%dT%H:%M:%SZ", utc) == 0)
		return false;
	return true;
}

static filelist_item_t *ServerHistory_Find (const char *address, filelist_item_t **prev_out)
{
	filelist_item_t *item, *prev = NULL;

	for (item = serverlist; item; item = item->next)
	{
		if (!q_strcasecmp(item->name, address))
		{
			if (prev_out)
				*prev_out = prev;
			return item;
		}
		prev = item;
	}

	if (prev_out)
		*prev_out = NULL;
	return NULL;
}

static filelist_item_t *ServerHistory_AllocItem (const char *address, const char *last_connected)
{
	filelist_item_t *item;

	item = (filelist_item_t *)Z_Malloc(sizeof(*item));	/* Sys_Errors on failure */
	memset(item, 0, sizeof(*item));
	q_strlcpy(item->name, address, sizeof(item->name));
	if (last_connected)
		q_strlcpy(item->data, last_connected, sizeof(item->data));
	return item;
}

static qboolean ServerHistory_Append (filelist_item_t **tail, const char *address,
	const char *last_connected)
{
	filelist_item_t *item;

	if (!ServerHistory_ValidAddress(address) || ServerHistory_Find(address, NULL))
		return false;

	item = ServerHistory_AllocItem(address, last_connected);
	if (!*tail && serverlist)
	{	/* Appending to a list we did not build: find its real tail rather
		   than dropping it by overwriting the head. */
		for (*tail = serverlist; (*tail)->next; *tail = (*tail)->next)
			;
	}
	if (*tail)
		(*tail)->next = item;
	else
		serverlist = item;
	*tail = item;
	return true;
}

/* Set when the history file exists but could not be read or preserved.
   Writing then would replace data we never managed to load. */
static qboolean serverhistory_write_blocked;

qboolean ServerHistory_Write (void)
{
	char path[MAX_OSPATH], tmp_path[MAX_OSPATH];
	FILE *file;
	filelist_item_t *item;
	qboolean ok = true;
	qboolean first = true;

	if (serverhistory_write_blocked)
		return false;

	ServerHistory_GetPath(path, sizeof(path), SERVERLIST);
	q_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%lu", path, Sys_GetProcessId());
	COM_CreatePath(tmp_path);

	file = fopen(tmp_path, "w");
	if (!file)
	{
		Con_DPrintf("ServerHistory_Write: unable to open %s for writing\n", tmp_path);
		return false;
	}

	if (fprintf(file, "[\n") < 0)
		ok = false;
	for (item = serverlist; ok && item; item = item->next)
	{
		char *escaped_address = JSON_EscapeString(item->name);
		char *escaped_date = item->data[0] ? JSON_EscapeString(item->data) : NULL;

		if (!escaped_address || (item->data[0] && !escaped_date))
		{
			free(escaped_address);
			free(escaped_date);
			ok = false;
			break;
		}

		if (!first && fprintf(file, ",\n") < 0)
			ok = false;
		first = false;
		if (ok && fprintf(file, "  {\n    \"address\": \"%s\",\n", escaped_address) < 0)
			ok = false;
		if (ok && item->data[0])
		{
			if (fprintf(file, "    \"last_connected\": \"%s\"\n  }", escaped_date) < 0)
				ok = false;
		}
		else if (ok && fprintf(file, "    \"last_connected\": null\n  }") < 0)
			ok = false;

		free(escaped_address);
		free(escaped_date);
	}
	if (ok && fprintf(file, "%s]\n", first ? "" : "\n") < 0)
		ok = false;
	if (fclose(file) != 0)
		ok = false;

	if (!ok)
	{
		Con_DPrintf("ServerHistory_Write: failed to flush %s, preserving existing file\n", tmp_path);
		remove(tmp_path);
		return false;
	}

#ifdef _WIN32
	if (!MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
	if (rename(tmp_path, path) != 0)
#endif
	{
		Con_DPrintf("ServerHistory_Write: unable to replace %s with %s\n", path, tmp_path);
		remove(tmp_path);
		return false;
	}

	return true;
}

static serverhistory_load_result_t ServerHistory_LoadJSONFile (const char *filename,
	filelist_item_t **tail, qboolean *needs_rewrite)
{
	char path[MAX_OSPATH];
	FILE *file;
	long file_size;
	char *buffer;
	json_t *json;
	const jsonentry_t *entry;

	ServerHistory_GetPath(path, sizeof(path), filename);
	file = fopen(path, "rb");
	if (!file)
		return errno == ENOENT ? SERVERHISTORY_NOT_FOUND : SERVERHISTORY_LOAD_ERROR;

	if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
		file_size > 4 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		Con_DPrintf("ServerHistory: unable to size %s\n", filename);
		return SERVERHISTORY_LOAD_ERROR;
	}

	buffer = (char *)malloc((size_t)file_size + 1);
	if (!buffer)
	{
		fclose(file);
		return SERVERHISTORY_LOAD_ERROR;
	}
	if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size)
	{
		free(buffer);
		fclose(file);
		Con_DPrintf("ServerHistory: unable to read %s\n", filename);
		return SERVERHISTORY_LOAD_ERROR;
	}
	buffer[file_size] = '\0';
	fclose(file);

	json = JSON_Parse(buffer);
	free(buffer);
	if (!json || !json->root || json->root->type != JSON_ARRAY)
	{
		if (json)
			JSON_Free(json);
		Con_DPrintf("ServerHistory: invalid %s\n", filename);
		return SERVERHISTORY_LOAD_ERROR;
	}

	for (entry = json->root->firstchild; entry; entry = entry->next)
	{
		const char *address = NULL;
		const char *last_connected = NULL;

		if (entry->type == JSON_OBJECT)
		{
			address = JSON_FindString(entry, "address");
			last_connected = JSON_FindString(entry, "last_connected");
			if (last_connected && !ServerHistory_ValidTimestamp(last_connected))
			{
				last_connected = NULL;
				if (needs_rewrite)
					*needs_rewrite = true;
			}
		}

		if (!ServerHistory_Append(tail, address, last_connected) && needs_rewrite)
			*needs_rewrite = true;
	}

	JSON_Free(json);
	return SERVERHISTORY_LOADED;
}

static serverhistory_load_result_t ServerHistory_LoadTextFile (const char *filename,
	filelist_item_t **tail)
{
	char path[MAX_OSPATH];
	char line[NET_NAMELEN + 2];
	FILE *file;

	ServerHistory_GetPath(path, sizeof(path), filename);
	file = fopen(path, "r");
	if (!file)
		return errno == ENOENT ? SERVERHISTORY_NOT_FOUND : SERVERHISTORY_LOAD_ERROR;

	while (fgets(line, sizeof(line), file))
	{
		char *start = line;
		char *end;
		int c;

		/* Reject rather than partially importing an overlong legacy line. */
		if (!strpbrk(line, "\r\n") && !feof(file))
		{
			while ((c = fgetc(file)) != '\n' && c != EOF)
				;
			continue;
		}

		line[strcspn(line, "\r\n")] = '\0';
		while (*start && q_isspace((unsigned char)*start))
			start++;
		end = start + strlen(start);
		while (end > start && q_isspace((unsigned char)end[-1]))
			*--end = '\0';
		ServerHistory_Append(tail, start, NULL);
	}

	if (ferror(file))
	{
		fclose(file);
		Con_DPrintf("ServerHistory: unable to read %s\n", filename);
		return SERVERHISTORY_LOAD_ERROR;
	}
	fclose(file);
	return SERVERHISTORY_LOADED;
}

/* Ordered most-recent-first: entries land in list order, and the head is what
   Host_GetLastServer reports, so the file naming the last server used has to
   be read before the alphabetically ordered bulk history. */
static const char *serverhistory_legacy_sources[] = {
	"lastserver.txt",
	SERVERLIST_LEGACY
};

/*
================
ServerHistory_PreserveUnreadable

The history file exists but would not load. Move it aside so a later write
cannot silently destroy it, and only start a fresh history once it is safe.
================
*/
static void ServerHistory_PreserveUnreadable (void)
{
	char path[MAX_OSPATH], bad_path[MAX_OSPATH];

	ServerHistory_GetPath(path, sizeof(path), SERVERLIST);
	q_snprintf(bad_path, sizeof(bad_path), "%s.bad", path);

	remove(bad_path);
	if (rename(path, bad_path) == 0)
		Con_Printf("Couldn't read %s; kept a copy as %s.bad and started a new history\n",
			SERVERLIST, SERVERLIST);
	else
	{
		serverhistory_write_blocked = true;
		Con_Printf("Couldn't read or move %s; server history will not be saved this session\n",
			SERVERLIST);
	}
}

static void ServerHistory_RemoveLegacyFiles (void)
{
	char path[MAX_OSPATH];
	size_t i;

	for (i = 0; i < countof(serverhistory_legacy_sources); i++)
	{
		ServerHistory_GetPath(path, sizeof(path), serverhistory_legacy_sources[i]);
		remove(path);
	}
}

void ServerList_Init(void)
{
	char	name[MAX_OSPATH];
	filelist_item_t *tail = NULL;
	serverhistory_load_result_t result;
	qboolean rewrite = false;
	qboolean migrated = false;
	qboolean migration_error = false;
	size_t i;

	q_snprintf(name, sizeof(name), "%s/id1", com_basedir); //  make an id1 folder if it doesnt exist already #smartafk
	Sys_mkdir(name);

	q_snprintf(name, sizeof(name), "%s/id1/backups", com_basedir); //  create backups folder if not there
	Sys_mkdir(name);

	result = ServerHistory_LoadJSONFile(SERVERLIST, &tail, &rewrite);
	if (result == SERVERHISTORY_LOADED)
	{
		if (rewrite)
			ServerHistory_Write();
		return;
	}
	if (result == SERVERHISTORY_LOAD_ERROR)
	{	/* Never migrate on top of a history we failed to read. */
		ServerHistory_PreserveUnreadable();
		return;
	}

	for (i = 0; i < countof(serverhistory_legacy_sources); i++)
	{
		result = ServerHistory_LoadTextFile(serverhistory_legacy_sources[i], &tail);
		migrated |= result == SERVERHISTORY_LOADED;
		migration_error |= result == SERVERHISTORY_LOAD_ERROR;
	}

	if (migrated && !migration_error && ServerHistory_Write())
		ServerHistory_RemoveLegacyFiles();
	else if (migration_error)
		Con_DPrintf("ServerList_Init: preserving legacy history after a read error\n");
}

void ServerHistory_Record (const char *server)
{
	filelist_item_t *item, *prev;
	char timestamp[SERVERHISTORY_TIME_LENGTH];

	if (!ServerHistory_ValidAddress(server))
		return;

	if (!ServerHistory_GetTimestamp(timestamp, sizeof(timestamp)))
		timestamp[0] = '\0';
	item = ServerHistory_Find(server, &prev);
	if (!item)
	{
		item = ServerHistory_AllocItem(server, timestamp[0] ? timestamp : NULL);
		item->next = serverlist;
		serverlist = item;
	}
	else
	{
		q_strlcpy(item->name, server, sizeof(item->name));
		if (timestamp[0])	/* keep the known date if the clock read failed */
			q_strlcpy(item->data, timestamp, sizeof(item->data));
		if (prev)
		{
			prev->next = item->next;
			item->next = serverlist;
			serverlist = item;
		}
	}

	ServerHistory_Write();
}

//==============================================================================
// woods -- name history management #namehistory
//==============================================================================

filelist_item_t* namehistorylist;

#define NAMEHISTORY_FILE "names.json"
#define NAMEHISTORY_FILE_LEGACY "names.txt"
#define NAMEHISTORY_TIME_LENGTH 20

static void NameHistory_EnsureDir (void)
{
	char path[MAX_OSPATH];

	q_snprintf(path, sizeof(path), "%s/id1", com_basedir);
	Sys_mkdir(path);

	q_snprintf(path, sizeof(path), "%s/id1/backups", com_basedir);
	Sys_mkdir(path);
}

static void NameHistory_GetPath (char *path, size_t path_size)
{
	q_snprintf(path, path_size, "%s/id1/backups/%s", com_basedir, NAMEHISTORY_FILE);
}

static void NameHistory_GetLegacyPath (char *path, size_t path_size)
{
	q_snprintf(path, path_size, "%s/id1/backups/%s", com_basedir, NAMEHISTORY_FILE_LEGACY);
}

static void NameHistory_GetTimestamp (char *last_used, size_t last_used_size)
{
	time_t systime;
	struct tm *loct;

	if (!last_used_size)
		return;

	last_used[0] = '\0';

	systime = time(NULL);
	loct = localtime(&systime);
	if (!loct)
	{
		q_strlcpy(last_used, "unknown", last_used_size);
		return;
	}

	strftime(last_used, last_used_size, "%Y-%m-%d %H:%M:%S", loct);
}

static int NameHistory_HexValue (char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void NameHistory_EncodeHexName (const char *name, char *hex, size_t hex_size)
{
	static const char digits[] = "0123456789abcdef";
	size_t i, j;

	for (i = 0, j = 0; name[i] && j + 2 < hex_size; i++)
	{
		unsigned char c = (unsigned char)name[i];
		hex[j++] = digits[c >> 4];
		hex[j++] = digits[c & 0x0f];
	}

	hex[j] = '\0';
}

static qboolean NameHistory_DecodeHexName (const char *hex, char *name, size_t name_size)
{
	size_t i, j;

	if (!hex || !name || !name_size)
		return false;

	for (i = 0, j = 0; hex[i]; i += 2)
	{
		int hi, lo;

		if (!hex[i + 1] || j + 1 >= name_size)
			return false;

		hi = NameHistory_HexValue(hex[i]);
		lo = NameHistory_HexValue(hex[i + 1]);
		if (hi < 0 || lo < 0)
			return false;

		name[j++] = (char)((hi << 4) | lo);
	}

	name[j] = '\0';
	return j > 0;
}

static qboolean NameHistory_DecodeLegacyJSONName (const char *src, char *dst, size_t dst_size)
{
	const unsigned char *in;
	size_t out;

	if (!src || !dst || !dst_size)
		return false;

	in = (const unsigned char *)src;
	out = 0;

	while (*in)
	{
		if (out + 1 >= dst_size)
			return false;

		// Old JSON used \u00XX for raw Quake bytes; JSON_Parse expanded those to UTF-8.
		if ((in[0] & 0xe0) == 0xc0 && (in[1] & 0xc0) == 0x80)
		{
			unsigned int codepoint = ((in[0] & 0x1f) << 6) | (in[1] & 0x3f);
			if (codepoint >= 0x80 && codepoint <= 0xff)
			{
				dst[out++] = (char)codepoint;
				in += 2;
				continue;
			}
		}

		dst[out++] = (char)*in++;
	}

	dst[out] = '\0';
	return out > 0;
}

static filelist_item_t *NameHistory_AllocItem (const char *name, const char *last_used)
{
	filelist_item_t *item;

	item = (filelist_item_t *)Z_Malloc(sizeof(*item));
	if (!item)
		Sys_Error("NameHistory_AllocItem: out of memory on %lu bytes (%s)",
			(unsigned long)sizeof(*item), name);

	memset(item, 0, sizeof(*item));
	q_strlcpy(item->name, name, sizeof(item->name));
	if (last_used)
		q_strlcpy(item->data, last_used, sizeof(item->data));

	return item;
}

static filelist_item_t *NameHistory_Find (const char *name, filelist_item_t **prev_out)
{
	filelist_item_t *item, *prev;

	prev = NULL;
	for (item = namehistorylist; item; item = item->next)
	{
		if (!q_strcasecmp(name, item->name))
		{
			if (prev_out)
				*prev_out = prev;
			return item;
		}
		prev = item;
	}

	if (prev_out)
		*prev_out = NULL;

	return NULL;
}

static void NameHistory_SkipLine (FILE *file)
{
	int c;

	while ((c = fgetc(file)) != '\n' && c != EOF)
		;
}

static void NameHistory_AppendLoadedItem (filelist_item_t **tail, const char *name, const char *last_used, qboolean *rewrite)
{
	filelist_item_t *item;

	if (!name || !name[0])
	{
		if (rewrite)
			*rewrite = true;
		return;
	}

	if (NameHistory_Find(name, NULL))
	{
		if (rewrite)
			*rewrite = true;
		return;
	}

	item = NameHistory_AllocItem(name, last_used);
	if (*tail)
		(*tail)->next = item;
	else
		namehistorylist = item;
	*tail = item;
}

static qboolean NameHistory_Write (void)
{
	char path[MAX_OSPATH];
	char tmp_path[MAX_OSPATH];
	FILE *file;
	filelist_item_t *item;
	qboolean ok = true;
	qboolean first = true;

	NameHistory_EnsureDir();
	NameHistory_GetPath(path, sizeof(path));
	q_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

	file = fopen(tmp_path, "w");
	if (!file)
	{
		Con_DPrintf("NameHistory_Write: unable to open %s for writing\n", tmp_path);
		return false;
	}

	fprintf(file, "[\n");

	for (item = namehistorylist; item; item = item->next)
	{
		char hex_name[MAX_QPATH * 2 + 1];
		char *escaped_last_used = item->data[0] ? JSON_EscapeString(item->data) : NULL;

		NameHistory_EncodeHexName(item->name, hex_name, sizeof(hex_name));

		if (item->data[0] && !escaped_last_used)
		{
			free(escaped_last_used);
			ok = false;
			break;
		}

		if (!first)
			fprintf(file, ",\n");
		first = false;

		fprintf(file, "  {\n");
		fprintf(file, "    \"name_hex\": \"%s\"", hex_name);
		if (item->data[0])
			fprintf(file, ",\n    \"last_used\": \"%s\"\n", escaped_last_used);
		else
			fprintf(file, "\n");
		fprintf(file, "  }");

		free(escaped_last_used);
	}

	if (ok)
	{
		if (!first)
			fprintf(file, "\n");
		fprintf(file, "]\n");
	}

	if (fclose(file) != 0)
		ok = false;

	if (!ok)
	{
		Con_DPrintf("NameHistory_Write: failed to flush %s, preserving existing file\n", tmp_path);
		remove(tmp_path);
		return false;
	}

	remove(path);
	if (rename(tmp_path, path) != 0)
	{
		Con_DPrintf("NameHistory_Write: unable to replace %s with %s\n", path, tmp_path);
		remove(tmp_path);
		return false;
	}

	return true;
}

static qboolean NameHistory_LoadJSON (const char *path)
{
	FILE *file;
	long file_size;
	char *buffer;
	json_t *json;
	filelist_item_t *tail = NULL;
	qboolean rewrite = false;

	file = fopen(path, "rb");
	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	rewind(file);

	if (file_size <= 0)
	{
		fclose(file);
		return true;
	}

	buffer = (char *)malloc((size_t)file_size + 1);
	if (!buffer)
	{
		fclose(file);
		return true;
	}

	if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size)
	{
		free(buffer);
		fclose(file);
		return true;
	}

	buffer[file_size] = '\0';
	fclose(file);

	json = JSON_Parse(buffer);
	free(buffer);

	if (!json || !json->root || json->root->type != JSON_ARRAY)
	{
		if (json)
			JSON_Free(json);
		Con_DPrintf("NameHistory_Init: invalid %s\n", NAMEHISTORY_FILE);
		return true;
	}

	{
		const jsonentry_t *entry;

		for (entry = json->root->firstchild; entry; entry = entry->next)
		{
			char decoded_name[MAX_QPATH];
			const char *name;
			const char *name_hex;
			const char *legacy_name;
			const char *last_used;

			if (!entry || entry->type != JSON_OBJECT)
			{
				rewrite = true;
				continue;
			}

			name_hex = JSON_FindString(entry, "name_hex");
			legacy_name = JSON_FindString(entry, "name");
			last_used = JSON_FindString(entry, "last_used");

			if (name_hex)
			{
				if (!NameHistory_DecodeHexName(name_hex, decoded_name, sizeof(decoded_name)))
				{
					rewrite = true;
					continue;
				}
				name = decoded_name;
			}
			else if (legacy_name)
			{
				if (!NameHistory_DecodeLegacyJSONName(legacy_name, decoded_name, sizeof(decoded_name)))
				{
					rewrite = true;
					continue;
				}
				name = decoded_name;
				rewrite = true;
			}
			else
			{
				rewrite = true;
				continue;
			}

			NameHistory_AppendLoadedItem(&tail, name, last_used, &rewrite);
		}
	}

	JSON_Free(json);

	if (rewrite)
		NameHistory_Write();

	return true;
}

static qboolean NameHistory_LoadLegacy (const char *path)
{
	FILE *file;
	char buffer[MAX_QPATH];
	char *newline;
	char *carriage;
	filelist_item_t *tail = NULL;

	file = fopen(path, "r");
	if (!file)
		return false;

	while (fgets(buffer, sizeof(buffer), file) != NULL)
	{
		newline = strchr(buffer, '\n');
		if (!newline && !feof(file))
		{
			NameHistory_SkipLine(file);
			continue;
		}

		if (newline)
			*newline = '\0';

		carriage = strchr(buffer, '\r');
		if (carriage)
			*carriage = '\0';

		NameHistory_AppendLoadedItem(&tail, buffer, NULL, NULL);
	}

	fclose(file);
	NameHistory_Write();

	return true;
}

void NameHistory_Init (void)
{
	char path[MAX_OSPATH];

	FileList_Clear(&namehistorylist);
	NameHistory_EnsureDir();

	NameHistory_GetPath(path, sizeof(path));
	if (NameHistory_LoadJSON(path))
		return;

	NameHistory_GetLegacyPath(path, sizeof(path));
	NameHistory_LoadLegacy(path);
}

void NameHistory_Add (const char *name)
{
	filelist_item_t *item, *prev;
	qboolean changed;
	char last_used[NAMEHISTORY_TIME_LENGTH];

	if (!name || !name[0])
		return;

	NameHistory_GetTimestamp(last_used, sizeof(last_used));
	item = NameHistory_Find(name, &prev);
	if (item)
	{
		changed = false;

		if (Q_strcmp(item->name, name))
		{
			q_strlcpy(item->name, name, sizeof(item->name));
			changed = true;
		}

		if (Q_strcmp(item->data, last_used))
		{
			q_strlcpy(item->data, last_used, sizeof(item->data));
			changed = true;
		}

		if (prev)
		{
			prev->next = item->next;
			item->next = namehistorylist;
			namehistorylist = item;
			changed = true;
		}

		if (changed)
			NameHistory_Write();

		return;
	}

	item = NameHistory_AllocItem(name, last_used);
	item->next = namehistorylist;
	namehistorylist = item;
	NameHistory_Write();
}

static void NameHistory_Clear (void)
{
	FileList_Clear(&namehistorylist);
	NameHistory_Write();
}

static const char *NameHistory_LastUsedString (const filelist_item_t *item)
{
	if (!item || !item->data[0])
		return "unknown";

	return item->data;
}

static qboolean NameHistory_DateAliasAvailable (void)
{
	return NameHistory_Find("date", NULL) == NULL;
}

static qboolean NameHistory_ClearAliasAvailable (void)
{
	return NameHistory_Find("clear", NULL) == NULL;
}

static void Host_NameHistory_Completion_f (const char *partial)
{
	if (Cmd_Argc() != 2)
		return;

	Con_AddToTabList("-d", partial, "show dates", NULL);
	Con_AddToTabList("-c", partial, "clear history", NULL);
	if (NameHistory_DateAliasAvailable())
		Con_AddToTabList("date", partial, "show dates", NULL);
	if (NameHistory_ClearAliasAvailable())
		Con_AddToTabList("clear", partial, "clear history", NULL);
}

static void Host_NameHistory_PrintDates (int count)
{
	extern int con_linewidth;

	filelist_item_t *item;
	filelist_item_t **items;
	int i;
	int max_name_len;
	int max_date_len;
	int name_width;
	qboolean multiline;

	items = (filelist_item_t **)Z_Malloc(count * sizeof(*items));
	i = 0;
	max_name_len = 0;
	max_date_len = 0;

	for (item = namehistorylist; item; item = item->next)
	{
		items[i++] = item;
		max_name_len = q_max(max_name_len, (int)strlen(item->name));
		max_date_len = q_max(max_date_len, (int)strlen(NameHistory_LastUsedString(item)));
	}

	multiline = (con_linewidth < max_date_len + 12);
	name_width = q_min(max_name_len, q_max(con_linewidth - max_date_len - 2, 8));

	for (i = count - 1; i >= 0; i--)
	{
		item = items[i];
		if (multiline)
		{
			Con_Printf("%s\n", item->name);
			Con_Printf("  %s\n", NameHistory_LastUsedString(item));
		}
		else
		{
			Con_Printf("%-*.*s  %s\n", name_width, name_width, item->name, NameHistory_LastUsedString(item));
		}
	}

	Con_Printf("%d %s found\n", count, (count == 1) ? "entry" : "entries");
	Z_Free(items);
}

static void Host_NameHistory_f (void)
{
	extern int con_linewidth;

	filelist_item_t *item;
	int count, maxlen, colwidth, cols, col;
	qboolean show_dates;
	qboolean clear_history;
	const char *arg;

	show_dates = false;
	clear_history = false;

	if (Cmd_Argc() > 2)
	{
		Con_Printf("usage: namehistory [-d|date|-c|clear]\n");
		return;
	}

	if (Cmd_Argc() == 2)
	{
		arg = Cmd_Argv(1);
		if (!q_strcasecmp(arg, "-d"))
		{
			show_dates = true;
		}
		else if (!q_strcasecmp(arg, "date"))
		{
			if (NameHistory_DateAliasAvailable())
				show_dates = true;
			else
			{
				Con_Printf("namehistory date is ambiguous because \"date\" exists in history\n");
				Con_Printf("use \"namehistory -d\" to show timestamps\n");
				return;
			}
		}
		else if (!q_strcasecmp(arg, "-c"))
		{
			clear_history = true;
		}
		else if (!q_strcasecmp(arg, "clear"))
		{
			if (NameHistory_ClearAliasAvailable())
				clear_history = true;
			else
			{
				Con_Printf("namehistory clear is ambiguous because \"clear\" exists in history\n");
				Con_Printf("use \"namehistory -c\" to clear history\n");
				return;
			}
		}
		else
		{
			Con_Printf("usage: namehistory [-d|date|-c|clear]\n");
			return;
		}
	}

	if (clear_history)
	{
		NameHistory_Clear();
		Con_Printf("Name history cleared\n");
		return;
	}

	count = 0;
	maxlen = 0;
	for (item = namehistorylist; item; item = item->next)
	{
		count++;
		maxlen = q_max(maxlen, (int)strlen(item->name));
	}

	if (!count)
	{
		Con_Printf("No name history found\n");
		return;
	}

	if (show_dates)
	{
		Host_NameHistory_PrintDates(count);
		return;
	}

	colwidth = q_max(maxlen + 2, 8);
	cols = q_max(con_linewidth / colwidth, 1);
	cols = q_min(cols, count);

	col = 0;
	for (item = namehistorylist; item; item = item->next)
	{
		Con_Printf("%-*.*s", colwidth, colwidth, item->name);
		col++;
		if (col == cols)
		{
			Con_Printf("\n");
			col = 0;
		}
	}

	if (col)
		Con_Printf("\n");

	Con_Printf("%d %s found\n", count, (count == 1) ? "entry" : "entries");
}

//==============================================================================
// woods -- bookmarks list management #bookmarksmenu #bookmarksjson
//==============================================================================

filelist_item_t* bookmarkslist;

static void BookmarksList_Clear(void)
{
	FileList_Clear(&bookmarkslist);
}

void BookmarksList_Rebuild(void)
{
	BookmarksList_Clear();
	BookmarksList_Init();
}

/*
================
BookmarkData_Parse

Parse the data field from a bookmark entry.
Handles both JSON format: {"alias":"...", "pinned":bool}
and legacy format: alias |pin
================
*/
void BookmarkData_Parse(const char* data, char* alias, size_t alias_size, qboolean* pinned)
{
	char local_alias[BOOKMARK_DATA_LENGTH];
	char* dest = alias;
	size_t dest_size = alias_size;

	if (!dest || dest_size == 0)
	{
		dest = local_alias;
		dest_size = sizeof(local_alias);
	}

	dest[0] = '\0';
	if (alias && alias_size)
		alias[0] = '\0';
	if (pinned)
		*pinned = false;

	if (!data)
		return;

	// Skip leading whitespace
	while (*data == ' ' || *data == '\t')
		++data;

	// Try JSON parsing first
	if (*data == '{')
	{
		json_t* json = JSON_Parse(data);
		if (json && json->root && json->root->type == JSON_OBJECT)
		{
			const char* alias_value = JSON_FindString(json->root, "alias");
			const qboolean* pinned_value = JSON_FindBoolean(json->root, "pinned");

			if (alias_value)
				q_strlcpy(dest, alias_value, dest_size);
			else
				dest[0] = '\0';

			if (alias != dest && alias && alias_size)
				q_strlcpy(alias, dest, alias_size);

			if (pinned)
				*pinned = pinned_value ? *pinned_value : false;

			JSON_Free(json);
			return;
		}

		if (json)
			JSON_Free(json);
	}

	// Legacy format: "alias |pin" or just "alias"
	q_strlcpy(dest, data, dest_size);

	// Trim trailing whitespace
	size_t len = strlen(dest);
	while (len > 0 && (dest[len - 1] == ' ' || dest[len - 1] == '\t'))
		dest[--len] = '\0';

	// Check for |pin suffix (with space before it)
	if (len >= strlen(BOOKMARK_PIN_SUFFIX))
	{
		const size_t suffix_len = strlen(BOOKMARK_PIN_SUFFIX);
		char* marker = dest + len - suffix_len;

		if (marker > dest && (marker[-1] == ' ' || marker[-1] == '\t') &&
		    !q_strcasecmp(marker, BOOKMARK_PIN_SUFFIX))
		{
			if (pinned)
				*pinned = true;

			*marker = '\0';

			// Trim trailing whitespace before the suffix
			while (marker > dest && (marker[-1] == ' ' || marker[-1] == '\t'))
				*--marker = '\0';
		}
		else if (pinned)
		{
			*pinned = false;
		}
	}

	if (alias != dest && alias && alias_size)
		q_strlcpy(alias, dest, alias_size);
}

/*
================
BookmarkData_Format

Format bookmark data as JSON string.
Output: {"alias":"escaped_alias","pinned":true/false}
================
*/
void BookmarkData_Format(char* dest, size_t dest_size, const char* alias, qboolean pinned)
{
	if (!dest || dest_size == 0)
		return;

	dest[0] = '\0';

	if (!alias)
		alias = "";

	char* escaped_alias = JSON_EscapeString(alias);
	if (!escaped_alias)
	{
		// Fallback to legacy format if escape fails
		if (pinned && alias[0])
			q_snprintf(dest, dest_size, "%s %s", alias, BOOKMARK_PIN_SUFFIX);
		else if (pinned)
			q_snprintf(dest, dest_size, "%s", BOOKMARK_PIN_SUFFIX);
		else
			q_strlcpy(dest, alias, dest_size);
		return;
	}

	q_snprintf(dest, dest_size, "{\"alias\":\"%s\",\"pinned\":%s}", escaped_alias, pinned ? "true" : "false");

	free(escaped_alias);
}

/*
================
BookmarksList_Write

Write bookmarks list to JSON file with atomic write (tmp + rename).
================
*/
void BookmarksList_Write(void)
{
	char fname[MAX_OSPATH];
	char tmpfname[MAX_OSPATH];
	FILE* file;
	qboolean ok = true;

	q_snprintf(fname, sizeof(fname), "%s/id1", com_basedir);
	Sys_mkdir(fname);

	q_snprintf(fname, sizeof(fname), "%s/id1/backups", com_basedir);
	Sys_mkdir(fname);

	q_snprintf(fname, sizeof(fname), "%s/id1/backups/%s", com_basedir, BOOKMARKSLIST);
	q_snprintf(tmpfname, sizeof(tmpfname), "%s.tmp", fname);

	file = fopen(tmpfname, "w");
	if (!file)
	{
		Con_DPrintf("BookmarksList_Write: Unable to open %s for writing\n", tmpfname);
		return;
	}

	fprintf(file, "[\n");

	filelist_item_t* item;
	qboolean first = true;
	for (item = bookmarkslist; item; item = item->next)
	{
		char alias[BOOKMARK_DATA_LENGTH];
		qboolean pinned = false;
		BookmarkData_Parse(item->data, alias, sizeof(alias), &pinned);

		char* escaped_name = JSON_EscapeString(item->name);
		char* escaped_alias = JSON_EscapeString(alias);

		if (!escaped_name || !escaped_alias)
		{
			Con_Printf("BookmarksList_Write: skipping entry due to allocation failure for %s\n",
			           item->name[0] ? item->name : "<null>");
			free(escaped_name);
			free(escaped_alias);
			ok = false;
			break;
		}

		if (!first)
			fprintf(file, ",\n");
		first = false;

		fprintf(file, "  {\n");
		fprintf(file, "    \"address\": \"%s\",\n", escaped_name);
		fprintf(file, "    \"alias\": \"%s\",\n", escaped_alias);
		fprintf(file, "    \"pinned\": %s\n", pinned ? "true" : "false");
		fprintf(file, "  }");

		free(escaped_name);
		free(escaped_alias);
	}

	if (ok)
	{
		if (!first)
			fprintf(file, "\n");
		fprintf(file, "]\n");
	}

	if (fclose(file) != 0)
		ok = false;

	if (!ok)
	{
		Con_Printf("BookmarksList_Write: failed to flush %s, preserving existing file\n", tmpfname);
		remove(tmpfname);
		return;
	}

	remove(fname);  // Windows rename() fails if destination exists
	if (rename(tmpfname, fname) != 0)
	{
		Con_Printf("BookmarksList_Write: unable to replace %s with %s\n", fname, tmpfname);
		remove(tmpfname);
	}
}

/*
================
BookmarksList_Init

Read bookmarks from file. Supports JSON format and legacy CSV format.
Legacy files are automatically migrated to JSON on first load.
================
*/
void BookmarksList_Init(void)
{
	char fname[MAX_OSPATH];
	FILE* file;
	long file_size;
	char* buffer;

	q_snprintf(fname, sizeof(fname), "%s/id1", com_basedir);
	Sys_mkdir(fname);

	q_snprintf(fname, sizeof(fname), "%s/id1/backups", com_basedir);
	Sys_mkdir(fname);

	q_snprintf(fname, sizeof(fname), "%s/id1/backups/%s", com_basedir, BOOKMARKSLIST);

	file = fopen(fname, "rb");
	if (!file)
	{
		// Try legacy filename for migration
		q_snprintf(fname, sizeof(fname), "%s/id1/backups/%s", com_basedir, BOOKMARKSLIST_LEGACY);
		file = fopen(fname, "rb");
		if (!file)
			return;
	}

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	rewind(file);

	if (file_size <= 0)
	{
		fclose(file);
		return;
	}

	buffer = (char*)malloc(file_size + 1);
	if (!buffer)
	{
		fclose(file);
		return;
	}

	if (fread(buffer, 1, file_size, file) != (size_t)file_size)
	{
		free(buffer);
		fclose(file);
		return;
	}

	buffer[file_size] = '\0';
	fclose(file);

	// Try JSON parsing first
	json_t* json = JSON_Parse(buffer);
	if (json && json->root && json->root->type == JSON_ARRAY)
	{
		const jsonentry_t* entry;
		for (entry = json->root->firstchild; entry; entry = entry->next)
		{
			if (!entry || entry->type != JSON_OBJECT)
				continue;

			const char* address = JSON_FindString(entry, "address");
			const char* alias = JSON_FindString(entry, "alias");
			const qboolean* pinned_ptr = JSON_FindBoolean(entry, "pinned");
			qboolean pinned = pinned_ptr ? *pinned_ptr : false;

			if (!address || !alias)
				continue;

			char data[BOOKMARK_DATA_LENGTH];
			BookmarkData_Format(data, sizeof(data), alias, pinned);
			FileList_Add(address, data, &bookmarkslist);
		}

		JSON_Free(json);
		free(buffer);
		return;
	}

	if (json)
		JSON_Free(json);

	// Legacy CSV format: "address,alias" or "address,alias |pin"
	qboolean legacy_format = false;
	char* buffer_copy = (char*)malloc(file_size + 1);
	if (!buffer_copy)
	{
		free(buffer);
		return;
	}
	memcpy(buffer_copy, buffer, file_size + 1);

	char* line = strtok(buffer_copy, "\n");
	while (line)
	{
		char* trimmed = line;
		while (*trimmed == ' ' || *trimmed == '\t')
			++trimmed;

		if (*trimmed)
		{
			char* end = trimmed + strlen(trimmed);
			while (end > trimmed && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
				*--end = '\0';

			char* comma = strchr(trimmed, ',');
			char* name = trimmed;
			const char* raw_data = NULL;

			if (comma)
			{
				*comma = '\0';
				raw_data = comma + 1;
			}

			char alias[BOOKMARK_DATA_LENGTH];
			qboolean pinned = false;
			BookmarkData_Parse(raw_data, alias, sizeof(alias), &pinned);

			char data[BOOKMARK_DATA_LENGTH];
			BookmarkData_Format(data, sizeof(data), alias, pinned);
			FileList_Add(name, data, &bookmarkslist);
			legacy_format = true;
		}

		line = strtok(NULL, "\n");
	}

	free(buffer_copy);
	free(buffer);

	// Auto-migrate legacy format to JSON and delete old file
	if (legacy_format)
	{
		BookmarksList_Write();
		// Delete old bookmarks.txt after successful migration
		char legacy_fname[MAX_OSPATH];
		q_snprintf(legacy_fname, sizeof(legacy_fname), "%s/id1/backups/%s", com_basedir, BOOKMARKSLIST_LEGACY);
		remove(legacy_fname);
		Con_Printf("Migrated bookmarks from %s to %s\n", BOOKMARKSLIST_LEGACY, BOOKMARKSLIST);
	}
}

//==============================================================================
// woods -- exec list management (adapted from demolist) #execlist
//			search in id1, configs, aliases, names folders
//==============================================================================

filelist_item_t* execlist;

static void ExecList_Clear (void)
{
	FileList_Clear (&execlist);
}

void ExecList_Rebuild(void)
{
	ExecList_Clear ();
	ExecList_Init ();
}

// TODO: Factor out to a general-purpose file searching function
void ExecList_Init(void)
{
#ifdef _WIN32
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
#else
	DIR* dir_p;
	struct dirent* dir_t;
#endif
	char		filestring[MAX_OSPATH];
	char		cfgname[MAX_OSPATH];
	char		cfgnamedir[MAX_OSPATH];
	searchpath_t* search;

	for (search = com_searchpaths; search; search = search->next)
	{
#ifdef _WIN32
		q_snprintf(filestring, sizeof(filestring), "%s/*.cfg", search->filename); // search gamedir
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				strncpy(cfgname, fdat.cFileName, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				FileList_Add(cfgname, NULL, &execlist); // woods #demolistsort add arg
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
		}

		q_snprintf(filestring, sizeof(filestring), "%s/aliases/*.cfg", search->filename);
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				strncpy(cfgname, fdat.cFileName, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				q_snprintf(cfgnamedir, sizeof(cfgnamedir), "aliases/%s", cfgname);
				FileList_Add(cfgnamedir, NULL, &execlist); // woods #demolistsort add arg
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
		}

		q_snprintf(filestring, sizeof(filestring), "%s/names/*.cfg", search->filename);
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				strcpy(cfgname, fdat.cFileName);
				q_snprintf(cfgnamedir, sizeof(cfgnamedir), "names/%s", cfgname);
				FileList_Add(cfgnamedir, NULL, &execlist); // woods #demolistsort add arg
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
		}

		q_snprintf(filestring, sizeof(filestring), "%s/backups/*.cfg", search->filename);
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				strncpy(cfgname, fdat.cFileName, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				q_snprintf(cfgnamedir, sizeof(cfgnamedir), "backups/%s", cfgname);
				FileList_Add(cfgnamedir, NULL, &execlist); // woods #demolistsort add arg
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
		}

		q_snprintf(filestring, sizeof(filestring), "%s/configs/*.cfg", search->filename);
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				strncpy(cfgname, fdat.cFileName, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				q_snprintf(cfgnamedir, sizeof(cfgnamedir), "configs/%s", cfgname);
				FileList_Add(cfgnamedir, NULL, &execlist); // woods #demolistsort add arg
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
		}
#else
		q_snprintf(filestring, sizeof(filestring), "%s/", search->filename); // search gamedir
		dir_p = opendir(filestring);
		if (dir_p != NULL)
		{
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "cfg") != 0)
					continue;

				strncpy(cfgname, dir_t->d_name, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				FileList_Add(cfgname, NULL, &execlist); // woods #demolistsort add arg
			}
			closedir(dir_p);
		}


		q_snprintf(filestring, sizeof(filestring), "%s/aliases/", search->filename); // search aliases folder
		dir_p = opendir(filestring);
		if (dir_p != NULL)
		{

			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "cfg") != 0)
					continue;

				strncpy(cfgname, dir_t->d_name, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				q_snprintf(cfgnamedir, sizeof(cfgnamedir), "aliases/%s", cfgname);
				FileList_Add(cfgnamedir, NULL, &execlist); // woods #demolistsort add arg
			}
			closedir(dir_p);
		}


		q_snprintf(filestring, sizeof(filestring), "%s/names/", search->filename); // search names folder
		dir_p = opendir(filestring);
		if (dir_p != NULL)
		{
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "cfg") != 0)
					continue;

				strncpy(cfgname, dir_t->d_name, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				q_snprintf(cfgnamedir, sizeof(cfgnamedir), "names/%s", cfgname);
				FileList_Add(cfgnamedir, NULL, &execlist); // woods #demolistsort add arg
			}
			closedir(dir_p);
		}

		q_snprintf(filestring, sizeof(filestring), "%s/configs", search->filename); // search configs folder
		dir_p = opendir(filestring);
		if (dir_p != NULL)
		{
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "cfg") != 0)
					continue;

				strncpy(cfgname, dir_t->d_name, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				q_snprintf(cfgnamedir, sizeof(cfgnamedir), "configs/%s", cfgname);
				FileList_Add(cfgnamedir, NULL, &execlist); // woods #demolistsort add arg
			}
			closedir(dir_p);
		}

		q_snprintf(filestring, sizeof(filestring), "%s/backups", search->filename); // search backups folder
		dir_p = opendir(filestring);
		if (dir_p != NULL)
		{

			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "cfg") != 0)
					continue;

				strncpy(cfgname, dir_t->d_name, sizeof(cfgname) - 1);
				cfgname[sizeof(cfgname) - 1] = '\0';
				q_snprintf(cfgnamedir, sizeof(cfgnamedir), "backups/%s", cfgname);
				FileList_Add(cfgnamedir, NULL, &execlist); // woods #demolistsort add arg
			}
			closedir(dir_p);
	}
#endif
	}
}

//==============================================================================
// woods -- r_particledesc completion #particlelist
//==============================================================================

filelist_item_t* particlelist;

static void ParticleList_Clear(void)
{
	FileList_Clear (&particlelist);
}

void ParticleList_Rebuild (void)
{
	ParticleList_Clear ();
	ParticleList_Init ();
}

void ParticleList_Init (void)
{
#ifdef _WIN32
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
#else
	DIR* dir_p;
	struct dirent* dir_t;
#endif
	char		filestring[MAX_OSPATH];
	char		cfgname[MAX_OSPATH];
	char		cfgnamedir[MAX_OSPATH];
	searchpath_t* search;
	pack_t* pak;
	int		i;

	FileList_Add ("classic", NULL, &particlelist); // woods #demolistsort add arg

	for (search = com_searchpaths; search; search = search->next)
	{
		if (!search->pack) //directory
		{
#ifdef _WIN32


			q_snprintf(filestring, sizeof(filestring), "%s/particles/*.cfg", search->filename);
			fhnd = FindFirstFile(filestring, &fdat);
			if (fhnd != INVALID_HANDLE_VALUE)
			{
				do
				{
					strcpy(cfgname, fdat.cFileName);
					COM_StripExtension(cfgname, cfgname, sizeof(cfgname));
					sprintf(cfgnamedir, "%s", cfgname);
					FileList_Add(cfgnamedir, NULL, &particlelist); // woods #demolistsort add arg
				} while (FindNextFile(fhnd, &fdat));
				FindClose(fhnd);
			}
#else
			q_snprintf(filestring, sizeof(filestring), "%s/particles/", search->filename);
			dir_p = opendir(filestring);
			if (dir_p != NULL)
			{

				while ((dir_t = readdir(dir_p)) != NULL)
				{
					if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "cfg") != 0)
						continue;

					strcpy(cfgname, dir_t->d_name);
					COM_StripExtension(cfgname, cfgname, sizeof(cfgname));
					sprintf(cfgnamedir, "%s", cfgname);
					FileList_Add(cfgnamedir, NULL, &particlelist); // woods #demolistsort add arg
				}
				closedir(dir_p);
			}
#endif
		}
		else //pakfile
		{
			for (i = 0, pak = search->pack; i < pak->numfiles; i++)
			{
				const char* pakfilename = pak->files[i].name;

				if (!strcmp(COM_FileGetExtension (pakfilename), "cfg") && strstr (pakfilename, "particles"))
				{
					COM_StripExtension(pakfilename, cfgname, sizeof(cfgname));

					const char* particlePrefix = "particles/";
					char* found = strstr(cfgname, particlePrefix);
					if (found != NULL)
					{
						memmove (found, found + strlen (particlePrefix), strlen (found) - strlen (particlePrefix) + 1);
					}

					FileList_Add (cfgname, NULL, &particlelist); // woods #demolistsort add arg
				}
			}

		}
	}
}

//==============================================================================
//ericw -- demo list management
//==============================================================================

filelist_item_t	*demolist;

static void DemoList_Clear (void)
{
	FileList_Clear (&demolist);
}

void DemoList_Rebuild (void)
{
	DemoList_Clear ();
	DemoList_Init ();
}

// TODO: Factor out to a general-purpose file searching function
void DemoList_Init (void)
{
#ifdef _WIN32
	WIN32_FIND_DATA	fdat;
	SYSTEMTIME stUTC, stLocal;
	HANDLE		fhnd;
	const char		*patterns[] = {"*.dem", "*.dz"};
	int			pattern_idx;
#else
	DIR		*dir_p;
	struct dirent	*dir_t;
	struct stat file_stat;
	struct tm* tm;
#endif
	char		filestring[MAX_OSPATH];
	char		dateStr[80]; // To store the date string
	searchpath_t	*search;
	pack_t		*pak;
	int		i;
	
	for (search = com_searchpaths; search; search = search->next)
	{
		if (!search->pack) //directory
		{
#ifdef _WIN32
			for (pattern_idx = 0; pattern_idx < (int)(sizeof(patterns) / sizeof(patterns[0])); ++pattern_idx)
			{
				q_snprintf(filestring, sizeof(filestring), "%s/demos/%s", search->filename, patterns[pattern_idx]); // woods #demosfolder
				fhnd = FindFirstFile(filestring, &fdat);
				if (fhnd != INVALID_HANDLE_VALUE)
				{
					do
					{
						if (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
							continue;

						// Convert the last-write time to local time
						FileTimeToSystemTime(&fdat.ftLastWriteTime, &stUTC);
						SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

						// Build a string showing the date
						sprintf(dateStr, "%04d-%02d-%02d %02d:%02d:%02d",
							stLocal.wYear, stLocal.wMonth, stLocal.wDay,
							stLocal.wHour, stLocal.wMinute, stLocal.wSecond);

						FileList_Add(fdat.cFileName, dateStr, &demolist);
					} while (FindNextFile(fhnd, &fdat));
					FindClose(fhnd);
				}

				q_snprintf(filestring, sizeof(filestring), "%s/%s", search->filename, patterns[pattern_idx]);
				fhnd = FindFirstFile(filestring, &fdat);
				if (fhnd != INVALID_HANDLE_VALUE)
				{
					do
					{
						char listname[MAX_QPATH];

						if (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
							continue;

						FileTimeToSystemTime(&fdat.ftLastWriteTime, &stUTC);
						SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);
						sprintf(dateStr, "%04d-%02d-%02d %02d:%02d:%02d",
							stLocal.wYear, stLocal.wMonth, stLocal.wDay,
							stLocal.wHour, stLocal.wMinute, stLocal.wSecond);

						q_snprintf(listname, sizeof(listname), "./%s", fdat.cFileName);
						FileList_Add(listname, dateStr, &demolist);
					} while (FindNextFile(fhnd, &fdat));
					FindClose(fhnd);
				}
			}
#else
			q_snprintf (filestring, sizeof(filestring), "%s/demos/", search->filename); // woods #demosfolder
			dir_p = opendir(filestring);
			if (dir_p != NULL)
			{
				while ((dir_t = readdir(dir_p)) != NULL)
				{
					const char *ext = COM_FileGetExtension(dir_t->d_name);

					if (q_strcasecmp(ext, "dem") != 0 && q_strcasecmp(ext, "dz") != 0)
						continue;

					char fullpath[MAX_OSPATH];

					// Calculate the lengths
					size_t filestring_len = strlen(filestring);

					// Truncate dir_t->d_name to fit into fullpath
					size_t max_dname_len = MAX_OSPATH - filestring_len - 1; // Subtract 1 for null terminator
					char truncated_dname[max_dname_len + 1]; // +1 for null terminator
					strncpy(truncated_dname, dir_t->d_name, max_dname_len);
					truncated_dname[max_dname_len] = '\0'; // Ensure null termination

					snprintf(fullpath, sizeof(fullpath), "%s%s", filestring, truncated_dname);

					if (stat(fullpath, &file_stat) < 0 || !S_ISREG(file_stat.st_mode))
						continue;

					tm = localtime(&file_stat.st_mtime);
					if (tm) { // Check for NULL
						snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d %02d:%02d:%02d",
							tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
							tm->tm_hour, tm->tm_min, tm->tm_sec);
					}
					else {
						strncpy(dateStr, "Unknown Date", sizeof(dateStr) - 1);
						dateStr[sizeof(dateStr) - 1] = '\0'; // Ensure null termination
					}

					FileList_Add(dir_t->d_name, dateStr, &demolist);
				}
				closedir(dir_p);
			}

			q_snprintf (filestring, sizeof(filestring), "%s/", search->filename);
			dir_p = opendir(filestring);
			if (dir_p != NULL)
			{
				while ((dir_t = readdir(dir_p)) != NULL)
				{
					char fullpath[MAX_OSPATH];
					char listname[MAX_QPATH];
					const char *ext = COM_FileGetExtension(dir_t->d_name);

					if (q_strcasecmp(ext, "dem") != 0 && q_strcasecmp(ext, "dz") != 0)
						continue;

					q_snprintf(fullpath, sizeof(fullpath), "%s%s", filestring, dir_t->d_name);
					if (stat(fullpath, &file_stat) < 0 || !S_ISREG(file_stat.st_mode))
						continue;

					tm = localtime(&file_stat.st_mtime);
					if (tm)
					{
						snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d %02d:%02d:%02d",
							tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
							tm->tm_hour, tm->tm_min, tm->tm_sec);
					}
					else
					{
						strncpy(dateStr, "Unknown Date", sizeof(dateStr) - 1);
						dateStr[sizeof(dateStr) - 1] = '\0';
					}

					q_snprintf(listname, sizeof(listname), "./%s", dir_t->d_name);
					FileList_Add(listname, dateStr, &demolist);
				}
				closedir(dir_p);
			}
#endif
		}
		else //pakfile
		{
			for (i = 0, pak = search->pack; i < pak->numfiles; i++)
			{
				const char *pakname = pak->files[i].name;
				const char *ext = COM_FileGetExtension(pakname);
				if (!q_strcasecmp(ext, "dem") || !q_strcasecmp(ext, "dz"))
				{
					char listname[MAX_QPATH];

					if (!strchr(pakname, '/') && !strchr(pakname, '\\'))
					{
						q_snprintf(listname, sizeof(listname), "./%s", pakname);
						FileList_Add(listname, "Unknown Date", &demolist);
					}
					else
						FileList_Add(pakname, "Unknown Date", &demolist);
				}
			}
		}
	}
}

//==============================================================================
//woods -- sky list management #skylist
//==============================================================================

filelist_item_t* skylist;

static void SkyList_Clear (void)
{
	FileList_Clear (&skylist);
}

void SkyList_Rebuild(void)
{
	SkyList_Clear ();
	SkyList_Init ();
}

int SkyhasValidExtension (char* filename) 
{
	size_t len = strlen(filename);
	return (len > 2 &&
		(!strcmp(filename + len - 6, "bk.tga") ||
			!strcmp(filename + len - 6, "bk.png") ||
			!strcmp(filename + len - 6, "bk.jpg") ||
			!strcmp(filename + len - 8, "bk.dds")));
}

void SkyList_Recurse(const char* basePath)
{
#ifdef _WIN32
	char filestring[MAX_OSPATH], newBasePath[MAX_OSPATH], fullSkyName[MAX_OSPATH], skyname[32];
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;
	q_snprintf(filestring, sizeof(filestring), "%s/*", basePath);
	fhnd = FindFirstFile(filestring, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && strcmp(fdat.cFileName, ".") != 0 && strcmp(fdat.cFileName, "..") != 0) {
			q_snprintf(newBasePath, sizeof(newBasePath), "%s/%s", basePath, fdat.cFileName);
			SkyList_Recurse (newBasePath);
		}
		else if (SkyhasValidExtension(fdat.cFileName)) {
			COM_StripExtension(fdat.cFileName, skyname, sizeof(skyname));
			skyname[strlen(skyname) - 2] = '\0'; // remove "bk" part
			char* lastSlash = strrchr(basePath, '/');
			const char* parentDirectory = lastSlash ? lastSlash + 1 : basePath;
			if (strcmp(parentDirectory, "env") != 0)
				q_snprintf(fullSkyName, sizeof(fullSkyName), "%s/%s", parentDirectory, skyname);
			else
				q_snprintf(fullSkyName, sizeof(fullSkyName), "%s", skyname);
			FileList_Add (fullSkyName, NULL, &skylist); // woods #demolistsort add arg
		}
	} while (FindNextFile(fhnd, &fdat));
	FindClose(fhnd);
#else
	char newBasePath[MAX_OSPATH], fullSkyName[MAX_OSPATH], skyname[32];
	DIR* dir_p = opendir(basePath);
	struct dirent* dir_t;
	if (dir_p == NULL)
		return;
	while ((dir_t = readdir(dir_p)) != NULL)
	{
		if (dir_t->d_type == DT_DIR && strcmp(dir_t->d_name, ".") != 0 && strcmp(dir_t->d_name, "..") != 0) {
			q_snprintf(newBasePath, sizeof(newBasePath), "%s/%s", basePath, dir_t->d_name);
			SkyList_Recurse (newBasePath);
		}
		else if (SkyhasValidExtension (dir_t->d_name)) {
			COM_StripExtension (dir_t->d_name, skyname, sizeof(skyname));
			skyname[strlen(skyname) - 2] = '\0'; // remove "bk" part
			char* lastSlash = strrchr(basePath, '/');
			const char* parentDirectory = lastSlash ? lastSlash + 1 : basePath;			if (strcmp(parentDirectory, "env") != 0)
				q_snprintf(fullSkyName, sizeof(fullSkyName), "%s/%s", parentDirectory, skyname);
			else
				q_snprintf(fullSkyName, sizeof(fullSkyName), "%s", skyname);
			FileList_Add (fullSkyName, NULL, &skylist); // woods #demolistsort add arg
		}
	}
	closedir(dir_p);
#endif
}

void SkyList_Init (void)
{
	searchpath_t* search;
	char filestring[MAX_OSPATH];
	for (search = com_searchpaths; search; search = search->next)
	{
		if (!search->pack) //directory
		{
			q_snprintf(filestring, sizeof(filestring), "%s/gfx/env", search->filename);
			SkyList_Recurse (filestring);
		}
		else //pakfile
		{
			pack_t* pak;
			int i;
			const char prefix[] = "gfx/env/";
			const size_t prefix_len = sizeof(prefix) - 1;
			for (i = 0, pak = search->pack; i < pak->numfiles; i++)
			{
				char* name = pak->files[i].name;
				if (strlen(name) <= prefix_len || q_strncasecmp(name, prefix, prefix_len))
					continue;
				if (SkyhasValidExtension (name)) {
					char skyname[32];
					name += prefix_len;
					COM_StripExtension (name, skyname, sizeof(skyname));
					skyname[strlen(skyname) - 2] = '\0'; // remove "bk" part
					FileList_Add (skyname, NULL, &skylist); // woods #demolistsort add arg
				}
			}
		}
	}
}

//==============================================================================
// woods  -- music list management #musiclist
//==============================================================================

filelist_item_t* musiclist;

static void MusicList_Clear (void)
{
	FileList_Clear (&musiclist);
}

void MusicList_Rebuild (void)
{
	MusicList_Clear ();
	MusicList_Init ();
}

void MusicList_Init(void)
{
#ifdef _WIN32
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;
#else
	DIR* dir_p;
	struct dirent* dir_t;
#endif
	char filestring[MAX_OSPATH];
	char musicname[32];
	searchpath_t* search;
	pack_t* pak;
	int i;

	for (search = com_searchpaths; search; search = search->next)
	{
		if (!search->pack) //directory
		{
#ifdef _WIN32
			for (int i = 0; wanted_handlers[i].ext != NULL; ++i)
			{
				q_snprintf(filestring, sizeof(filestring), "%s/music/*.%s", search->filename, wanted_handlers[i].ext);
				fhnd = FindFirstFile(filestring, &fdat);
				if (fhnd == INVALID_HANDLE_VALUE)
					continue;
				do
				{
					COM_StripExtension(fdat.cFileName, musicname, sizeof(musicname));
					FileList_Add(musicname, NULL, &musiclist);
				} while (FindNextFile(fhnd, &fdat));
				FindClose(fhnd);
			}
#else
			for (int i = 0; wanted_handlers[i].ext != NULL; ++i)
			{
				q_snprintf(filestring, sizeof(filestring), "%s/music/", search->filename);
				dir_p = opendir(filestring);
				if (dir_p == NULL)
					continue;
				while ((dir_t = readdir(dir_p)) != NULL)
				{
					if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
						continue;

					const char* ext = COM_FileGetExtension(dir_t->d_name);

					if (q_strcasecmp(ext, wanted_handlers[i].ext) == 0)
					{
						COM_StripExtension(dir_t->d_name, musicname, sizeof(musicname));
						FileList_Add(musicname, NULL, &musiclist);
					}
				}
				closedir(dir_p);
			}
#endif
		}
		else //pakfile
		{
			for (i = 0, pak = search->pack; i < pak->numfiles; i++)
			{
				if (strncmp(pak->files[i].name, "music/", 6) == 0)
				{
					const char* ext = COM_FileGetExtension(pak->files[i].name);

					for (int j = 0; wanted_handlers[j].ext != NULL; ++j)
					{
						if (q_strcasecmp(ext, wanted_handlers[j].ext) == 0)
						{
							char* startOfName = pak->files[i].name + 6; // Skip the 'music/' part
							COM_StripExtension(startOfName, musicname, sizeof(musicname));
							FileList_Add(musicname, NULL, &musiclist);
							break;
						}
					}
				}
			}
		}
	}
}

//==============================================================================
//woods -- text list management #textlist
//==============================================================================

filelist_item_t* textlist;

static void TextList_Clear(void)
{
	FileList_Clear(&textlist);
}

void TextList_Rebuild(void)
{
	TextList_Clear();
	TextList_Init();
}

int FileHasValidExtension(const char* filename)
{
	size_t len = strlen(filename);
	if (len <= 2)
		return 0;

	const char* extensions[] = { ".txt", ".cfg", ".ent", ".json", ".loc" };
	const size_t num_extensions = sizeof(extensions) / sizeof(extensions[0]);

	for (size_t i = 0; i < num_extensions; ++i)
	{
		size_t ext_len = strlen(extensions[i]);
		if (len >= ext_len)
		{
			const char* file_ext = filename + len - ext_len;
			if (q_strcasecmp(file_ext, extensions[i]) == 0)
				return 1;
		}
	}
	return 0;
}


void FileList_Recurse(const char* basePath, int depth, const char* initialBasePath)
{
#ifdef _WIN32
	char currentBasePath[MAX_OSPATH], searchPath[MAX_OSPATH], fullFileName[MAX_OSPATH];
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;

	if (depth > 2)
		return; // Only go two directories deep

	// Copy basePath to currentBasePath
	q_strlcpy(currentBasePath, basePath, sizeof(currentBasePath));

	q_snprintf(searchPath, sizeof(searchPath), "%s/*", currentBasePath);

	fhnd = FindFirstFile(searchPath, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (strcmp(fdat.cFileName, ".") != 0 && strcmp(fdat.cFileName, "..") != 0)
		{
			if (fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				// Build the path to the new directory
				char newBasePath[MAX_OSPATH];
				q_snprintf(newBasePath, sizeof(newBasePath), "%s/%s", currentBasePath, fdat.cFileName);
				// Recurse into the directory
				FileList_Recurse(newBasePath, depth + 1, initialBasePath);
			}
			else if (FileHasValidExtension(fdat.cFileName))
			{
				// Build the full file path
				q_snprintf(fullFileName, sizeof(fullFileName), "%s/%s", currentBasePath, fdat.cFileName);
				// Get the path relative to initialBasePath
				const char* relativePath = fullFileName + strlen(initialBasePath);
				if (*relativePath == '/' || *relativePath == '\\')
					relativePath++; // Skip the leading '/' or '\'
				FileList_Add(relativePath, NULL, &textlist); // Add file with proper path to list
			}
		}
	} while (FindNextFile(fhnd, &fdat));

	FindClose(fhnd);
#else
	char currentBasePath[MAX_OSPATH], fullFileName[MAX_OSPATH];
	DIR* dir_p;
	struct dirent* dir_t;

	if (depth > 2)
		return; // Only go two directories deep

	// Copy basePath to currentBasePath
	q_strlcpy(currentBasePath, basePath, sizeof(currentBasePath));

	dir_p = opendir(currentBasePath);
	if (dir_p == NULL)
		return;

	while ((dir_t = readdir(dir_p)) != NULL)
	{
		if (strcmp(dir_t->d_name, ".") != 0 && strcmp(dir_t->d_name, "..") != 0)
		{
			// Build the path to the item
			char itemPath[MAX_OSPATH];
			q_snprintf(itemPath, sizeof(itemPath), "%s/%s", currentBasePath, dir_t->d_name);

			// Get file info to check if it's a directory
			struct stat st;
			if (stat(itemPath, &st) == -1)
				continue;

			if (S_ISDIR(st.st_mode))
			{
				// Recurse into the directory
				FileList_Recurse(itemPath, depth + 1, initialBasePath);
			}
			else if (FileHasValidExtension(dir_t->d_name))
			{
				// Build the full file path
				q_snprintf(fullFileName, sizeof(fullFileName), "%s/%s", currentBasePath, dir_t->d_name);
				// Get the path relative to initialBasePath
				const char* relativePath = fullFileName + strlen(initialBasePath);
				if (*relativePath == '/')
					relativePath++; // Skip the leading '/'
				FileList_Add(relativePath, NULL, &textlist); // Add file with proper path to list
			}
		}
	}
	closedir(dir_p);
#endif
}

void TextList_Init(void)
{
	TextList_Clear();

	if (com_basedir[0] == '\0' || com_gamedir[0] == '\0')
		return;

	const char* initialBasePath = com_basedir; // set initialBasePath to com_basedir

	char id1Path[MAX_OSPATH]; // always search the "id1" directory
#ifdef _WIN32
	q_snprintf(id1Path, sizeof(id1Path), "%s\\id1", com_basedir);
#else
	q_snprintf(id1Path, sizeof(id1Path), "%s/id1", com_basedir);
#endif
	FileList_Recurse(id1Path, 0, initialBasePath);

	char gameDirName[MAX_OSPATH]; // extract the game directory name from com_gamedir
	strncpy(gameDirName, com_gamedir, sizeof(gameDirName));
	gameDirName[sizeof(gameDirName) - 1] = '\0'; // ensure null-termination

	size_t len = strlen(gameDirName); 	// remove trailing path separator if present
	if (len > 0 && (gameDirName[len - 1] == '/' || gameDirName[len - 1] == '\\'))
		gameDirName[len - 1] = '\0';

	const char* lastSep = strrchr(gameDirName, '/');
	if (!lastSep)
		lastSep = strrchr(gameDirName, '\\');
	if (lastSep)
		memmove(gameDirName, lastSep + 1, strlen(lastSep));

	if (q_strcasecmp(gameDirName, "id1") != 0) // if com_gamedir is not "id1", also search com_gamedir
	{
		FileList_Recurse(com_gamedir, 0, initialBasePath);
	}
}

/*
==================
Host_Mods_f -- johnfitz

list all potential mod directories (contain either a pak file or a progs.dat)
==================
*/
static void Host_Mods_f (void)
{
	int i;
	filelist_item_t	*mod;

	for (mod = modlist, i=0; mod; mod = mod->next, i++)
		Con_SafePrintf ("   %s\n", mod->name);

	if (i)
		Con_SafePrintf ("%i mod(s)\n", i);
	else
		Con_SafePrintf ("no mods found\n");
}

//==============================================================================

/*
=============
Host_Mapname_f -- johnfitz
=============
*/
static void Host_Mapname_f (void)
{
	if (sv.active)
	{
		Con_Printf ("\"mapname\" is \"%s\"\n", sv.name);
		return;
	}

	if (cls.state == ca_connected)
	{
		Con_Printf ("\"mapname\" is \"%s\"\n", cl.mapname);
		return;
	}

	Con_Printf ("no map loaded\n");
}

#define PLAYER_HEIGHT_QU	56.0f		// standing hull: (-16 -16 -24) to (16 16 32)
#define HUMAN_HEIGHT_FT		5.75f		// 5'9"
#define HUMAN_HEIGHT_M		1.7526f
#define QU2_PER_SQFT		((PLAYER_HEIGHT_QU / HUMAN_HEIGHT_FT) * (PLAYER_HEIGHT_QU / HUMAN_HEIGHT_FT))
#define QU2_PER_SQM			((PLAYER_HEIGHT_QU / HUMAN_HEIGHT_M) * (PLAYER_HEIGHT_QU / HUMAN_HEIGHT_M))
#define MAX_MAPSIZE_COMPARE	80	// matches cmd.c MAX_ARGS

typedef struct mapsize_result_s
{
	char		mapname[MAX_QPATH];
	float		total_surface_area;
	float		floor_surface_area;
	float		wall_surface_area;
	float		ceiling_surface_area;
	int			counted_faces;
	int			total_faces;
} mapsize_result_t;

static qboolean Host_MapSize_IsLegacyUnitsArg (const char *arg)
{
	return (!q_strcasecmp(arg, "ft") || !q_strcasecmp(arg, "imperial") ||
		!q_strcasecmp(arg, "m") || !q_strcasecmp(arg, "metric"));
}

static qboolean Host_MapSize_IsListArg (const char *arg)
{
	return (!q_strcasecmp(arg, "-l") || !q_strcasecmp(arg, "list"));
}

static qboolean Host_MapSize_IsClearAllArg (const char *arg)
{
	return !q_strcasecmp(arg, "-clearall");
}

static void Host_MapSize_SetResultFromModel (mapsize_result_t *result, const char *mapname, const qmodel_t *mod)
{
	memset(result, 0, sizeof(*result));
	q_strlcpy(result->mapname, mapname, sizeof(result->mapname));
	result->total_surface_area = mod->total_surface_area;
	result->floor_surface_area = mod->floor_surface_area;
	result->wall_surface_area = mod->wall_surface_area;
	result->ceiling_surface_area = mod->ceiling_surface_area;
	result->counted_faces = mod->counted_faces;
	result->total_faces = (mod->submodels && mod->numsubmodels > 0) ? mod->submodels[0].numfaces : 0;
}

static void Host_MapSize_SetResultFromAreas (mapsize_result_t *result, const char *mapname, const map_surface_areas_t *areas)
{
	memset(result, 0, sizeof(*result));
	q_strlcpy(result->mapname, mapname, sizeof(result->mapname));
	result->total_surface_area = areas->total_surface_area;
	result->floor_surface_area = areas->floor_surface_area;
	result->wall_surface_area = areas->wall_surface_area;
	result->ceiling_surface_area = areas->ceiling_surface_area;
	result->counted_faces = areas->counted_faces;
	result->total_faces = areas->total_faces;
}

static void Host_MapSize_SetResultFromCache (mapsize_result_t *result, const filelist_item_t *level)
{
	memset(result, 0, sizeof(*result));
	q_strlcpy(result->mapname, level->name, sizeof(result->mapname));
	result->total_surface_area = level->total_surface_area;
	result->floor_surface_area = level->floor_surface_area;
	result->wall_surface_area = level->wall_surface_area;
	result->ceiling_surface_area = level->ceiling_surface_area;
	result->counted_faces = level->counted_faces;
	result->total_faces = level->total_faces;
}

static qboolean Host_MapSize_ResultMatchesCache (const mapsize_result_t *result, const filelist_item_t *level)
{
	return level->has_mapsize_cache
		&& level->total_surface_area == result->total_surface_area
		&& level->floor_surface_area == result->floor_surface_area
		&& level->wall_surface_area == result->wall_surface_area
		&& level->ceiling_surface_area == result->ceiling_surface_area
		&& level->counted_faces == result->counted_faces
		&& level->total_faces == result->total_faces;
}

static void Host_MapSize_StoreCacheOnLevel (filelist_item_t *level, const mapsize_result_t *result)
{
	level->total_surface_area = result->total_surface_area;
	level->floor_surface_area = result->floor_surface_area;
	level->wall_surface_area = result->wall_surface_area;
	level->ceiling_surface_area = result->ceiling_surface_area;
	level->counted_faces = result->counted_faces;
	level->total_faces = result->total_faces;
	level->has_mapsize_cache = true;
}

static qboolean Host_MapSize_LoadCachedResult (const char *mapname, mapsize_result_t *result)
{
	qboolean found = false;

	if (descriptionsParsed)
	{
		filelist_item_t *level = FindLevelInList(extralevels, mapname);
		if (level && level->has_mapsize_cache)
		{
			Host_MapSize_SetResultFromCache(result, level);
			return true;
		}
	}
	else
	{
		filelist_item_t *json_levels = NULL;
		filelist_item_t *level;

		LoadMapDescriptionsFromJSON(&json_levels);
		level = FindLevelInList(json_levels, mapname);
		if (level && level->has_mapsize_cache)
		{
			Host_MapSize_SetResultFromCache(result, level);
			found = true;
		}
		FreeLevelList(json_levels);
	}

	return found;
}

static void Host_MapSize_UpdateCache (const mapsize_result_t *result)
{
	filelist_item_t *level;

	if (!result->mapname[0])
		return;

	if (!descriptionsParsed)
		ExtraMaps_ParseDescriptions();

	level = FindLevelInList(extralevels, result->mapname);
	if (!level || Host_MapSize_ResultMatchesCache(result, level))
		return;

	Host_MapSize_StoreCacheOnLevel(level, result);
	SaveMapDescriptionsToJSON(extralevels);
}

static void Host_MapSize_RefreshAllMissingCaches (void)
{
	filelist_item_t *level;
	int missing = 0;
	int cached = 0;
	qboolean updated = false;

	if (!descriptionsParsed)
		ExtraMaps_ParseDescriptions();

	for (level = extralevels; level; level = level->next)
		if (!level->has_mapsize_cache)
			missing++;

	if (!missing)
		return;

	Con_Printf("mapsize: caching size data for %d map%s...\n",
		missing, (missing == 1) ? "" : "s");

	for (level = extralevels; level; level = level->next)
	{
		char mappath[MAX_QPATH];
		map_surface_areas_t areas;
		mapsize_result_t result;

		if (level->has_mapsize_cache)
			continue;

		if ((size_t)q_snprintf(mappath, sizeof(mappath), "maps/%s.bsp", level->name) >= sizeof(mappath))
			continue;

		if (!Mod_CalcBSPFileSurfaceAreas(mappath, &areas))
		{
			Con_DPrintf("mapsize: can't cache map \"%s\"\n", level->name);
			continue;
		}

		Host_MapSize_SetResultFromAreas(&result, level->name, &areas);
		Host_MapSize_StoreCacheOnLevel(level, &result);
		cached++;
		updated = true;
	}

	if (updated)
		SaveMapDescriptionsToJSON(extralevels);

	Con_Printf("mapsize: cached %d of %d map%s\n",
		cached, missing, (missing == 1) ? "" : "s");
}

static void Host_MapSize_ClearAllCache (void)
{
	filelist_item_t *level;
	filelist_item_t *json_levels = NULL;
	int cleared = 0;

	if (descriptionsParsed)
	{
		for (level = extralevels; level; level = level->next)
		{
			if (!level->has_mapsize_cache)
				continue;

			FileList_ClearMapSizeCache(level);
			cleared++;
		}

		SaveMapDescriptionsToJSON(extralevels);
	}
	else
	{
		LoadMapDescriptionsFromJSON(&json_levels);
		for (level = json_levels; level; level = level->next)
		{
			if (!level->has_mapsize_cache)
				continue;

			FileList_ClearMapSizeCache(level);
			cleared++;
		}

		SaveMapDescriptionsToJSON(json_levels);
		FreeLevelList(json_levels);
	}

	if (cleared)
		Con_Printf("mapsize: cleared cached size data for %d map%s\n\n",
			cleared, (cleared == 1) ? "" : "s");
	else
		Con_Printf("mapsize: no cached size data to clear\n\n");
}

static qboolean Host_MapSize_LoadMapByName (const char *arg, mapsize_result_t *result)
{
	char mapbase[MAX_QPATH];
	char mapname[MAX_QPATH];
	char mappath[MAX_QPATH];
	map_surface_areas_t areas;

	if (strstr(arg, ".."))
	{
		Con_Printf("invalid map name \"%s\"\n", arg);
		return false;
	}

	q_strlcpy(mapbase, COM_SkipPath(arg), sizeof(mapbase));
	COM_StripExtension(mapbase, mapname, sizeof(mapname));

	if (!*mapname)
	{
		Con_Printf("invalid map name \"%s\"\n", arg);
		return false;
	}

	if ((size_t)q_snprintf(mappath, sizeof(mappath), "maps/%s.bsp", mapname) >= sizeof(mappath))
	{
		Con_Printf("map name \"%s\" is too long\n", mapname);
		return false;
	}

	if (!COM_FileExists(mappath, NULL))
	{
		if (Host_MapSize_IsLegacyUnitsArg(arg))
			Con_Printf("mapsize now prints both sq ft and sq m; usage is: mapsize [mapname]\n");
		Con_Printf("map \"%s\" not found\n", mapname);
		return false;
	}

	if (Host_MapSize_LoadCachedResult(mapname, result))
		return true;

	if (!Mod_CalcBSPFileSurfaceAreas(mappath, &areas))
	{
		Con_Printf("can't load map \"%s\"\n", mapname);
		return false;
	}

	Host_MapSize_SetResultFromAreas(result, mapname, &areas);
	Host_MapSize_UpdateCache(result);
	return true;
}

static const char *Host_FormatUnsignedWithCommas (unsigned long long value)
{
	static char buffers[8][32];
	static int buffer_index;
	char *out;
	int group;

	buffer_index = (buffer_index + 1) % Q_COUNTOF(buffers);
	out = buffers[buffer_index] + sizeof(buffers[0]) - 1;
	*out-- = '\0';
	group = 0;

	if (value == 0)
	{
		*out-- = '0';
	}
	else while (value > 0)
	{
		if (group == 3)
		{
			*out-- = ',';
			group = 0;
		}
		*out-- = '0' + (int)(value % 10);
		value /= 10;
		group++;
	}

	return out + 1;
}

static const char *Host_FormatMapArea (float area_qu2, float qu2_per_unit)
{
	double converted = (double)area_qu2 / (double)qu2_per_unit;

	if (converted <= 0.0)
		return "0";

	return Host_FormatUnsignedWithCommas((unsigned long long)(converted + 0.5));
}

static void Host_PrintMapAreaLine (const char *label, float area_qu2)
{
	Con_Printf("  %-13s %s sq ft / %s sq m\n",
		label,
		Host_FormatMapArea(area_qu2, QU2_PER_SQFT),
		Host_FormatMapArea(area_qu2, QU2_PER_SQM));
}

static void Host_PrintMapSizeReport (const mapsize_result_t *result)
{
	Con_Printf("\n=== Map Size: %s ===\n\n", result->mapname);
	Con_Printf("  Scale: 56 qu player height = 5'9\" human height\n\n");
	Host_PrintMapAreaLine("Floor area:", result->floor_surface_area);
	Host_PrintMapAreaLine("Wall area:", result->wall_surface_area);
	Host_PrintMapAreaLine("Ceiling area:", result->ceiling_surface_area);
	Host_PrintMapAreaLine("Total area:", result->total_surface_area);
	Con_Printf("  Faces counted: %s of %s\n",
		Host_FormatUnsignedWithCommas((result->counted_faces > 0) ? (unsigned long long)result->counted_faces : 0),
		Host_FormatUnsignedWithCommas((result->total_faces > 0) ? (unsigned long long)result->total_faces : 0));
	Con_Printf("\n");
}

static void Host_PrintMapSizeFloorComparison (mapsize_result_t *results, int count)
{
	int i, j;
	int name_width = 0;

	for (i = 1; i < count; ++i)
	{
		mapsize_result_t key = results[i];
		float key_floor = key.floor_surface_area;

		for (j = i - 1; j >= 0; --j)
		{
			float floor_area = results[j].floor_surface_area;

			if (floor_area < key_floor)
				break;
			if (floor_area == key_floor && q_strcasecmp(results[j].mapname, key.mapname) <= 0)
				break;

			results[j + 1] = results[j];
		}

		results[j + 1] = key;
	}

	for (i = 0; i < count; ++i)
		name_width = q_max(name_width, (int)strlen(results[i].mapname));

	Con_Printf("\n=== Floor Compare: Smallest to Largest ===\n\n");
	Con_Printf("  Scale: 56 qu player height = 5'9\" human height\n\n");
	for (i = 0; i < count; ++i)
	{
		const float floor_area = results[i].floor_surface_area;

		Con_Printf("  %-*s  %s sq ft / %s sq m",
			name_width,
			results[i].mapname,
			Host_FormatMapArea(floor_area, QU2_PER_SQFT),
			Host_FormatMapArea(floor_area, QU2_PER_SQM));

		if (i > 0)
		{
			const float diff_area = floor_area - results[i - 1].floor_surface_area;

			Con_Printf("  ^m(+%s sq ft / +%s sq m)^m",
				Host_FormatMapArea(diff_area, QU2_PER_SQFT),
				Host_FormatMapArea(diff_area, QU2_PER_SQM));
		}

		Con_Printf("\n");
	}

	Con_Printf("\n");
}

static int Host_MapSize_CompareLevelPtrs (const void *lhs, const void *rhs)
{
	const filelist_item_t *a = *(const filelist_item_t * const *)lhs;
	const filelist_item_t *b = *(const filelist_item_t * const *)rhs;

	if (a->has_mapsize_cache != b->has_mapsize_cache)
		return a->has_mapsize_cache ? -1 : 1;
	if (!a->has_mapsize_cache)
		return q_strcasecmp(a->name, b->name);
	if (a->floor_surface_area < b->floor_surface_area)
		return -1;
	if (a->floor_surface_area > b->floor_surface_area)
		return 1;
	return q_strcasecmp(a->name, b->name);
}

static void Host_MapSize_List_f (const char *filter)
{
	filelist_item_t *level;
	filelist_item_t **sorted;
	int numlevels = 0;
	int count = 0;
	int name_width = 0;
	int sqft_width = 3;
	int sqm_width = 3;
	int i;

	if (!descriptionsParsed)
		ExtraMaps_ParseDescriptions();

	Host_MapSize_RefreshAllMissingCaches();

	for (level = extralevels; level; level = level->next)
		++numlevels;

	if (!numlevels)
	{
		Con_SafePrintf("\nno maps found\n\n");
		return;
	}

	sorted = (filelist_item_t **)malloc(sizeof(*sorted) * numlevels);
	if (!sorted)
	{
		Con_Printf("mapsize: out of memory\n");
		return;
	}

	for (level = extralevels, i = 0; level; level = level->next)
		sorted[i++] = level;

	qsort(sorted, numlevels, sizeof(*sorted), Host_MapSize_CompareLevelPtrs);

	for (i = 0; i < numlevels; ++i)
	{
		level = sorted[i];
		if (filter && !(q_strcasestr(level->name, filter) || q_strcasestr(level->data, filter)))
			continue;

		name_width = q_max(name_width, (int)strlen(level->name));
		if (level->has_mapsize_cache)
		{
			sqft_width = q_max(sqft_width, (int)strlen(Host_FormatMapArea(level->floor_surface_area, QU2_PER_SQFT)));
			sqm_width = q_max(sqm_width, (int)strlen(Host_FormatMapArea(level->floor_surface_area, QU2_PER_SQM)));
		}
	}

	Con_SafePrintf("\n");
	Con_SafePrintf("   floor area, smallest to largest (56 qu player height = 5'9\")\n\n");

	for (i = 0; i < numlevels; ++i)
	{
		char buf[MAX_CHAT_SIZE_EX];
		char combined[MAX_CHAT_SIZE_EX];
		const char *sqft = "n/a";
		const char *sqm = "n/a";
		int desc_space;

		level = sorted[i];
		if (filter && !(q_strcasestr(level->name, filter) || q_strcasestr(level->data, filter)))
			continue;

		if (level->has_mapsize_cache)
		{
			sqft = Host_FormatMapArea(level->floor_surface_area, QU2_PER_SQFT);
			sqm = Host_FormatMapArea(level->floor_surface_area, QU2_PER_SQM);
		}

		desc_space = (int)sizeof(combined) - name_width - sqft_width - sqm_width - 24;
		if (desc_space < 0)
			desc_space = 0;

		q_snprintf(combined, sizeof(combined), "^m%-*s^m  %*s sq ft / %*s sq m  %.*s",
			name_width, level->name,
			sqft_width, sqft,
			sqm_width, sqm,
			desc_space, level->data);

		if (filter)
		{
			COM_TintSubstring(combined, filter, buf, sizeof(buf));
			q_strlcpy(combined, buf, sizeof(combined));
		}

		Con_SafePrintf("   %s\n", combined);
		count++;
	}

	free(sorted);

	if (filter)
		Con_SafePrintf("\n%i map(s) found containing '%s'\n\n", count, filter);
	else if (count)
		Con_SafePrintf("\n%i map(s)\n\n", count);
	else
		Con_SafePrintf("\nno maps found\n\n");
}

static void Host_MapSize_f (void)
{
	mapsize_result_t results[MAX_MAPSIZE_COMPARE];
	int result_count = 0;
	int i;

	if (Cmd_Argc() >= 2 && Host_MapSize_IsClearAllArg(Cmd_Argv(1)))
	{
		if (Cmd_Argc() != 2)
		{
			Con_Printf("usage: mapsize -clearall\n");
			return;
		}

		Host_MapSize_ClearAllCache();
		return;
	}

	if (Cmd_Argc() >= 2 && Host_MapSize_IsListArg(Cmd_Argv(1)))
	{
		if (Cmd_Argc() > 3)
		{
			Con_Printf("usage: mapsize [-l|list] [filter]\n");
			return;
		}

		Host_MapSize_List_f((Cmd_Argc() == 3) ? Cmd_Argv(2) : NULL);
		return;
	}

	if (Cmd_Argc() > MAX_MAPSIZE_COMPARE)
	{
		Con_Printf("mapsize supports up to %d maps at once\n", MAX_MAPSIZE_COMPARE - 1);
		return;
	}

	if (Cmd_Argc() == 1)
	{
		mapsize_result_t *result = &results[result_count];
		qmodel_t *mod;
		const char *mapname;

		if (sv.active)
		{
			mod = sv.qcvm.worldmodel;
			mapname = sv.name;
		}
		else if (cls.state == ca_connected)
		{
			mod = cl.worldmodel;
			mapname = cl.mapname;
		}
		else
		{
			Con_Printf("no map loaded\n");
			return;
		}

		if (!mod || !mapname[0])
		{
			Con_Printf("no map loaded\n");
			return;
		}

		Host_MapSize_SetResultFromModel(result, mapname, mod);
		Host_MapSize_UpdateCache(result);
		result_count = 1;
	}
	else
	{
		for (i = 1; i < Cmd_Argc(); ++i)
		{
			mapsize_result_t *result = &results[result_count];

			if (!Host_MapSize_LoadMapByName(Cmd_Argv(i), result))
				return;

			result_count++;
		}
	}

	for (i = 0; i < result_count; ++i)
		Host_PrintMapSizeReport(&results[i]);

	if (result_count > 1)
		Host_PrintMapSizeFloorComparison(results, result_count);
}

/*
==================
Host_Status_f
==================
*/
static void Host_Status_f (void)
{
	void	(*print_fn) (const char *fmt, ...)
				 FUNCP_PRINTF(1,2);
	client_t	*client;
	int			seconds;
	int			minutes;
	int			hours = 0;
	int			j, i;

	qhostaddr_t addresses[32];
	int numaddresses;

	if (cmd_source != src_client)
	{
		if (!sv.active)
		{
			cl.console_status = true;	// JPG 1.05 - added this; woods for #iplog
			Cmd_ForwardToServer ();
			return;
		}
		print_fn = Con_Printf;
	}
	else
		print_fn = SV_ClientPrintf;

	print_fn (    "host:    %s\n", Cvar_VariableString ("hostname"));
	print_fn (    "version: "ENGINE_NAME_AND_VER"\n");

#if 1
	numaddresses = NET_ListAddresses(addresses, sizeof(addresses)/sizeof(addresses[0]));
	for (i = 0; i < numaddresses; i++)
	{
		if (*addresses[i] == '[')
			print_fn ("ipv6:    %s\n", addresses[i]);	//Spike -- FIXME: we should really have ports displayed here or something
		else
			print_fn ("tcp/ip:  %s\n", addresses[i]);	//Spike -- FIXME: we should really have ports displayed here or something
	}
#else
	if (ipv4Available)
		print_fn ("tcp/ip:  %s\n", my_ipv4_address);	//Spike -- FIXME: we should really have ports displayed here or something
	if (ipv6Available)
		print_fn ("ipv6:    %s\n", my_ipv6_address);
	if (ipxAvailable)
		print_fn ("ipx:     %s\n", my_ipx_address);
#endif
	print_fn (    "map:     %s\n", sv.name);

	for (i = 1,j=0; i < MAX_MODELS; i++)
		if (sv.model_precache[i])
			j++;
	print_fn (    "models:  %i/%i\n", j, MAX_MODELS-1);
	for (i = 1,j=0; i < MAX_SOUNDS; i++)
		if (sv.sound_precache[i])
			j++;
	print_fn (    "sounds:  %i/%i\n", j, MAX_SOUNDS-1);
	for (i = 0,j=0; i < MAX_PARTICLETYPES; i++)
		if (sv.particle_precache[i])
			j++;
	if (j)
		print_fn (    "effects: %i/%i\n", j, MAX_PARTICLETYPES-1);
	for (i = 1,j=1; i < sv.qcvm.num_edicts; i++)
		if (!sv.qcvm.edicts[i].free)
			j++;
	print_fn (    "entities:%i/%i\n", j, sv.qcvm.max_edicts);

	print_fn (    "players: %i active (%i max)\n\n", net_activeconnections, svs.maxclients);
	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active)
			continue;
		if (client->netconnection)
			seconds = (int)(net_time - NET_QSocketGetTime(client->netconnection));
		else
			seconds = 0;
		minutes = seconds / 60;
		if (minutes)
		{
			seconds -= (minutes * 60);
			hours = minutes / 60;
			if (hours)
				minutes -= (hours * 60);
		}
		else
			hours = 0;
		print_fn ("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n", j+1, client->name, (int)client->edict->v.frags, hours, minutes, seconds);
		
		if (cmd_source != src_command)
			print_fn("   %s\n", client->netconnection ? NET_QSocketGetMaskedAddressStringForDisplay(client->netconnection) : "botclient");
		else
			print_fn("   %s\n", client->netconnection ? NET_QSocketGetTrueAddressString(client->netconnection) : "botclient");
	}

}

/*
==================
Host_God_f

Sets client to godmode
==================
*/
static void Host_God_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	if (sv_player->v.deadflag != DEAD_NO || sv_player->v.health <= 0)
	{
		Host_Resurrect_f ();
		sv_player->v.flags = (int)sv_player->v.flags | FL_GODMODE;
		SV_ClientPrintf ("godmode ON\n");
		return;
	}

	//johnfitz -- allow user to explicitly set god mode to on or off
	switch (Cmd_Argc())
	{
	case 1:
		sv_player->v.flags = (int)sv_player->v.flags ^ FL_GODMODE;
		if (!((int)sv_player->v.flags & FL_GODMODE) )
			SV_ClientPrintf ("godmode OFF\n");
		else
			SV_ClientPrintf ("godmode ON\n");
		break;
	case 2:
		if (Q_atof(Cmd_Argv(1)))
		{
			sv_player->v.flags = (int)sv_player->v.flags | FL_GODMODE;
			SV_ClientPrintf ("godmode ON\n");
		}
		else
		{
			sv_player->v.flags = (int)sv_player->v.flags & ~FL_GODMODE;
			SV_ClientPrintf ("godmode OFF\n");
		}
		break;
	default:
		Con_Printf("god [value] : toggle god mode. values: 0 = off, 1 = on\n");
		break;
	}
	//johnfitz
}

/*
==================
Host_Notarget_f
==================
*/
static void Host_Notarget_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	//johnfitz -- allow user to explicitly set notarget to on or off
	switch (Cmd_Argc())
	{
	case 1:
		sv_player->v.flags = (int)sv_player->v.flags ^ FL_NOTARGET;
		if (!((int)sv_player->v.flags & FL_NOTARGET) )
			SV_ClientPrintf ("notarget OFF\n");
		else
			SV_ClientPrintf ("notarget ON\n");
		break;
	case 2:
		if (Q_atof(Cmd_Argv(1)))
		{
			sv_player->v.flags = (int)sv_player->v.flags | FL_NOTARGET;
			SV_ClientPrintf ("notarget ON\n");
		}
		else
		{
			sv_player->v.flags = (int)sv_player->v.flags & ~FL_NOTARGET;
			SV_ClientPrintf ("notarget OFF\n");
		}
		break;
	default:
		Con_Printf("notarget [value] : toggle notarget mode. values: 0 = off, 1 = on\n");
		break;
	}
	//johnfitz
}

qboolean noclip_anglehack;

/*
==================
Host_Noclip_f
==================
*/
static void Host_Noclip_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	//johnfitz -- allow user to explicitly set noclip to on or off
	switch (Cmd_Argc())
	{
	case 1:
		if (sv_player->v.movetype != MOVETYPE_NOCLIP)
		{
			noclip_anglehack = true;
			sv_player->v.movetype = MOVETYPE_NOCLIP;
			SV_ClientPrintf ("noclip ON\n");
		}
		else
		{
			noclip_anglehack = false;
			sv_player->v.movetype = MOVETYPE_WALK;
			SV_ClientPrintf ("noclip OFF\n");
		}
		break;
	case 2:
		if (Q_atof(Cmd_Argv(1)))
		{
			noclip_anglehack = true;
			sv_player->v.movetype = MOVETYPE_NOCLIP;
			SV_ClientPrintf ("noclip ON\n");
		}
		else
		{
			noclip_anglehack = false;
			sv_player->v.movetype = MOVETYPE_WALK;
			SV_ClientPrintf ("noclip OFF\n");
		}
		break;
	default:
		Con_Printf("noclip [value] : toggle noclip mode. values: 0 = off, 1 = on\n");
		break;
	}
	//johnfitz
}

#define VectorClear(v) ((v)[0] = (v)[1] = (v)[2] = 0) // woods #setlast
#define HOST_SETPOS_TRACE_UP			256.0f
#define HOST_SETPOS_TRACE_DOWN			2048.0f
#define HOST_SETPOS_MIN_FLOOR_Z			0.7f
#define HOST_SETPOS_END_RING_COUNT		8
#define HOST_SETPOS_END_TRIGGER_MARGIN	96.0f
#define HOST_SETPOS_MIDDLE_RING_STEP	128.0f
#define HOST_SETPOS_MIDDLE_MAX_RINGS	48

typedef struct host_setpos_item_target_s
{
	const char *alias;
	const char *classname;
	int spawnflags;
} host_setpos_item_target_t;

static const host_setpos_item_target_t host_setpos_item_targets[] =
{
	{ "quad",	"item_artifact_super_damage",		0 },
	{ "pent",	"item_artifact_invulnerability",	0 },
	{ "rl",		"weapon_rocketlauncher",			0 },
	{ "gl",		"weapon_grenadelauncher",			0 },
	{ "sg",		"weapon_shotgun",					0 },
	{ "ssg",	"weapon_supershotgun",				0 },
	{ "sng",	"weapon_supernailgun",				0 },
	{ "ng",		"weapon_nailgun",					0 },
	{ "lg",		"weapon_lightning",					0 },
	{ "mega",	"item_health",						2 },
	{ "ring",	"item_artifact_invisibility",		0 },
	{ "eyes",	"item_artifact_invisibility",		0 },
	{ "suit",	"item_artifact_envirosuit",			0 }
};

static qboolean Host_SetPosClassnameIs(const edict_t *ent, const char *classname)
{
	if (!ent || ent->free || !ent->v.classname)
		return false;

	return !strcmp(PR_GetString(ent->v.classname), classname);
}

static edict_t *Host_SetPosFindClassname(const char *classname)
{
	int entnum;

	for (entnum = 0; entnum < qcvm->num_edicts; entnum++)
	{
		edict_t *ent = EDICT_NUM(entnum);

		if (Host_SetPosClassnameIs(ent, classname))
			return ent;
	}

	return NULL;
}

static void Host_SetPosEntityCenter(const edict_t *ent, vec3_t out)
{
	VectorAdd(ent->v.absmin, ent->v.absmax, out);
	VectorScale(out, 0.5f, out);

	if (DotProduct(out, out) == 0.0f)
		VectorCopy(ent->v.origin, out);
}

static float Host_SetPosDistanceSquared(const vec3_t a, const vec3_t b)
{
	vec3_t delta;

	VectorSubtract(a, b, delta);
	return DotProduct(delta, delta);
}

static const host_setpos_item_target_t *Host_SetPosFindItemTarget(const char *alias)
{
	size_t i;

	for (i = 0; i < Q_COUNTOF(host_setpos_item_targets); i++)
		if (!q_strcasecmp(alias, host_setpos_item_targets[i].alias))
			return &host_setpos_item_targets[i];

	return NULL;
}

static qboolean Host_SetPosItemTargetMatches(const edict_t *ent, const host_setpos_item_target_t *target)
{
	if (!Host_SetPosClassnameIs(ent, target->classname))
		return false;

	if (target->spawnflags && (((int)ent->v.spawnflags & target->spawnflags) != target->spawnflags))
		return false;

	return true;
}

static qboolean Host_SetPosFindItem(const host_setpos_item_target_t *target, const char *alias, vec3_t origin, int *match_index, int *match_count)
{
	static char last_alias[16];
	static char last_map[MAX_QPATH];
	static int last_entnum = -1;
	qboolean reset_cycle;
	int entnum, first_entnum = -1, selected_entnum = -1;
	int selected_index = 0, count = 0;
	int start_after;
	edict_t *ent;

	reset_cycle = q_strcasecmp(last_alias, alias) || strcmp(last_map, sv.name);
	start_after = reset_cycle ? -1 : last_entnum;

	for (entnum = 0; entnum < qcvm->num_edicts; entnum++)
	{
		ent = EDICT_NUM(entnum);
		if (!Host_SetPosItemTargetMatches(ent, target))
			continue;

		count++;
		if (first_entnum < 0)
			first_entnum = entnum;
		if (selected_entnum < 0 && entnum > start_after)
		{
			selected_entnum = entnum;
			selected_index = count;
		}
	}

	if (count <= 0)
		return false;

	if (selected_entnum < 0)
	{
		selected_entnum = first_entnum;
		selected_index = 1;
	}

	ent = EDICT_NUM(selected_entnum);
	VectorCopy(ent->v.origin, origin);
	if (DotProduct(origin, origin) == 0.0f)
		Host_SetPosEntityCenter(ent, origin);

	q_strlcpy(last_alias, alias, sizeof(last_alias));
	q_strlcpy(last_map, sv.name, sizeof(last_map));
	last_entnum = selected_entnum;

	if (match_index)
		*match_index = selected_index;
	if (match_count)
		*match_count = count;

	return true;
}

static qboolean Host_SetPosFindStart(vec3_t origin, vec3_t angles)
{
	static const char *start_classnames[] = {
		"info_player_start",
		"info_player_coop",
		"info_player_deathmatch"
	};
	size_t i;

	for (i = 0; i < Q_COUNTOF(start_classnames); i++)
	{
		edict_t *ent = Host_SetPosFindClassname(start_classnames[i]);

		if (ent)
		{
			VectorCopy(ent->v.origin, origin);
			VectorCopy(ent->v.angles, angles);
			return true;
		}
	}

	return false;
}

static float Host_SetPosVerticalMaxOffset(void)
{
	float height, max_offset;

	if (!qcvm->worldmodel)
		return 512.0f;

	height = qcvm->worldmodel->maxs[2] - qcvm->worldmodel->mins[2];
	max_offset = q_max(512.0f, height * 0.5f);
	return max_offset;
}

static qboolean Host_SetPosTryFloorProbe(edict_t *mover, const vec3_t base, float vertical_offset, vec3_t out)
{
	vec3_t old_origin, probe, above, below, spot;
	trace_t trace;

	VectorCopy(mover->v.origin, old_origin);

	VectorCopy(base, probe);
	probe[2] += vertical_offset;

	VectorCopy(probe, above);
	above[2] += HOST_SETPOS_TRACE_UP;
	VectorCopy(probe, below);
	below[2] -= HOST_SETPOS_TRACE_DOWN;

	trace = SV_Move(above, vec3_origin, vec3_origin, below,
		MOVE_NOMONSTERS, mover);
	if (trace.allsolid || trace.startsolid ||
		trace.fraction >= 1.0f ||
		trace.plane.normal[2] < HOST_SETPOS_MIN_FLOOR_Z)
	{
		VectorCopy(old_origin, mover->v.origin);
		return false;
	}

	VectorCopy(trace.endpos, spot);
	spot[2] += -mover->v.mins[2] + 1.0f;

	VectorCopy(spot, mover->v.origin);
	if (!SV_TestEntityPosition(mover) && SV_CheckBottom(mover))
	{
		VectorCopy(spot, out);
		VectorCopy(old_origin, mover->v.origin);
		return true;
	}

	VectorCopy(old_origin, mover->v.origin);
	return false;
}

static qboolean Host_SetPosProbeFloorSpot(edict_t *mover, const vec3_t base, vec3_t out)
{
	static const float close_vertical_offsets[] = {
		0.0f, 64.0f, -64.0f, 128.0f, -128.0f, 256.0f, -256.0f
	};
	float max_offset, offset;
	size_t i;

	for (i = 0; i < Q_COUNTOF(close_vertical_offsets); i++)
		if (Host_SetPosTryFloorProbe(mover, base, close_vertical_offsets[i], out))
			return true;

	max_offset = Host_SetPosVerticalMaxOffset();
	for (offset = 512.0f; offset <= max_offset; offset *= 2.0f)
	{
		if (Host_SetPosTryFloorProbe(mover, base, offset, out))
			return true;
		if (Host_SetPosTryFloorProbe(mover, base, -offset, out))
			return true;
	}

	return false;
}

static qboolean Host_SetPosFindSafeSpotNear(edict_t *mover, const vec3_t base, vec3_t out, int ring_count, float ring_step)
{
	static const float ring_xy[][2] = {
		{ 1.0f,  0.0f},
		{-1.0f,  0.0f},
		{ 0.0f,  1.0f},
		{ 0.0f, -1.0f},
		{ 0.7071f,  0.7071f},
		{-0.7071f,  0.7071f},
		{ 0.7071f, -0.7071f},
		{-0.7071f, -0.7071f},
		{ 0.9239f,  0.3827f},
		{-0.9239f,  0.3827f},
		{ 0.9239f, -0.3827f},
		{-0.9239f, -0.3827f},
		{ 0.3827f,  0.9239f},
		{-0.3827f,  0.9239f},
		{ 0.3827f, -0.9239f},
		{-0.3827f, -0.9239f}
	};
	vec3_t old_origin;
	float old_solid, old_movetype;
	size_t i;
	int ring;

	VectorCopy(mover->v.origin, old_origin);
	old_solid = mover->v.solid;
	old_movetype = mover->v.movetype;
	mover->v.solid = SOLID_SLIDEBOX;
	mover->v.movetype = MOVETYPE_WALK;

	if (Host_SetPosProbeFloorSpot(mover, base, out))
		goto found;

	for (ring = 1; ring <= ring_count; ring++)
	{
		float radius = ring * ring_step;

		for (i = 0; i < Q_COUNTOF(ring_xy); i++)
		{
			vec3_t probe;

			VectorCopy(base, probe);
			probe[0] += ring_xy[i][0] * radius;
			probe[1] += ring_xy[i][1] * radius;

			if (Host_SetPosProbeFloorSpot(mover, probe, out))
				goto found;
		}
	}

	VectorCopy(old_origin, mover->v.origin);
	mover->v.solid = old_solid;
	mover->v.movetype = old_movetype;
	return false;

found:
	VectorCopy(old_origin, mover->v.origin);
	mover->v.solid = old_solid;
	mover->v.movetype = old_movetype;
	return true;
}

static int Host_SetPosMiddleRingCount(void)
{
	float width, depth, max_extent;
	int rings;

	if (!qcvm->worldmodel)
		return HOST_SETPOS_MIDDLE_MAX_RINGS;

	width = qcvm->worldmodel->maxs[0] - qcvm->worldmodel->mins[0];
	depth = qcvm->worldmodel->maxs[1] - qcvm->worldmodel->mins[1];
	max_extent = q_max(width, depth) * 0.5f;
	rings = (int)(max_extent / HOST_SETPOS_MIDDLE_RING_STEP) + 1;
	rings = q_max(rings, 1);
	rings = q_min(rings, HOST_SETPOS_MIDDLE_MAX_RINGS);
	return rings;
}

static void Host_SetPosTriggerOutsidePoint(const edict_t *trigger, const vec3_t reference, vec3_t out)
{
	vec3_t center, dir;
	float len, half_x, half_y, dist_x, dist_y, edge_dist;

	Host_SetPosEntityCenter(trigger, center);
	VectorSubtract(reference, center, dir);
	dir[2] = 0.0f;
	len = VectorLength(dir);
	if (len < 1.0f)
	{
		dir[0] = -1.0f;
		dir[1] = 0.0f;
		dir[2] = 0.0f;
	}
	else
	{
		VectorScale(dir, 1.0f / len, dir);
	}

	half_x = q_max(fabs(center[0] - trigger->v.absmin[0]), fabs(trigger->v.absmax[0] - center[0]));
	half_y = q_max(fabs(center[1] - trigger->v.absmin[1]), fabs(trigger->v.absmax[1] - center[1]));
	dist_x = (fabs(dir[0]) > 0.001f) ? half_x / fabs(dir[0]) : 999999.0f;
	dist_y = (fabs(dir[1]) > 0.001f) ? half_y / fabs(dir[1]) : 999999.0f;
	edge_dist = q_min(dist_x, dist_y);
	if (edge_dist > 999998.0f)
		edge_dist = 0.0f;

	VectorMA(center, edge_dist + HOST_SETPOS_END_TRIGGER_MARGIN, dir, out);
	out[2] = center[2];
}

static qboolean Host_SetPosFindEnd(vec3_t origin)
{
	edict_t *start = Host_SetPosFindClassname("info_player_start");
	vec3_t reference;
	edict_t *best = NULL;
	float best_dist = -1.0f;
	int entnum;

	if (start)
		VectorCopy(start->v.origin, reference);
	else
		VectorCopy(sv_player->v.origin, reference);

	for (entnum = 0; entnum < qcvm->num_edicts; entnum++)
	{
		edict_t *ent = EDICT_NUM(entnum);
		vec3_t center;
		float dist;

		if (!Host_SetPosClassnameIs(ent, "trigger_changelevel"))
			continue;

		Host_SetPosEntityCenter(ent, center);
		dist = Host_SetPosDistanceSquared(center, reference);
		if (!best || dist > best_dist)
		{
			best = ent;
			best_dist = dist;
			VectorCopy(center, origin);
		}
	}

	if (!best)
		return false;

	Host_SetPosTriggerOutsidePoint(best, reference, origin);
	if (Host_SetPosFindSafeSpotNear(sv_player, origin, origin,
		HOST_SETPOS_END_RING_COUNT, HOST_SETPOS_MIDDLE_RING_STEP))
		return true;

	return true;
}

static qboolean Host_SetPosFindMiddle(vec3_t origin)
{
	if (!qcvm->worldmodel)
		return false;

	/* Geometric BSP bounds center; this is not aware of playable route flow. */
	VectorAdd(qcvm->worldmodel->mins, qcvm->worldmodel->maxs, origin);
	VectorScale(origin, 0.5f, origin);

	return Host_SetPosFindSafeSpotNear(sv_player, origin, origin,
		Host_SetPosMiddleRingCount(), HOST_SETPOS_MIDDLE_RING_STEP);
}

static void Host_SetPosApply(const vec3_t origin, const vec3_t angles, qboolean use_angles, qboolean force_noclip)
{
	vec3_t forward, right, up;

	if (force_noclip && sv_player->v.movetype != MOVETYPE_NOCLIP)
	{
		noclip_anglehack = true;
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		SV_ClientPrintf ("noclip ON\n");
	}

	VectorClear(sv_player->v.velocity);
	VectorCopy(origin, sv_player->v.origin);

	if (use_angles)
	{
		VectorCopy(angles, sv_player->v.angles);
		sv_player->v.fixangle = 1;
	}

	AngleVectors(sv_player->v.angles, forward, right, up);
	S_Update(sv_player->v.origin, forward, right, up);

	SV_LinkEdict(sv_player, false);
}

/*
====================
Host_SetPos_f

adapted from fteqw, originally by Alex Shadowalker
====================
*/
static void Host_SetPos_f(void)
{
	int     i, numargs;
	float   args[6];
	const char *target;
	const host_setpos_item_target_t *item_target;
	vec3_t origin, angles;
	qboolean use_angles;
	int match_index, match_count;

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	extern vec3_t last_viewpos; // woods #setlast
	extern vec3_t last_viewangles; // woods  #setlast
	extern qboolean has_last_viewpos; // woods #setlast

	if (Cmd_Argc() == 2)
	{
		target = Cmd_Argv(1);
		item_target = NULL;
		use_angles = false;
		VectorCopy(sv_player->v.angles, angles);

		if (!q_strcasecmp(target, "last")) // woods #setlast
		{
			if (!has_last_viewpos)
			{
				SV_ClientPrintf("\nno previous viewpos available\n\n");
				return;
			}

			Host_SetPosApply(last_viewpos, last_viewangles, true, false);
			return;
		}

		if (!q_strcasecmp(target, "start"))
		{
			if (!Host_SetPosFindStart(origin, angles))
			{
				SV_ClientPrintf("setpos start: no player start found\n");
				return;
			}
			use_angles = true;
		}
		else if (!q_strcasecmp(target, "end"))
		{
			if (!Host_SetPosFindEnd(origin))
			{
				SV_ClientPrintf("setpos end: no changelevel trigger found\n");
				return;
			}
		}
		else if (!q_strcasecmp(target, "middle"))
		{
			if (!Host_SetPosFindMiddle(origin))
			{
				SV_ClientPrintf("setpos middle: no safe middle spot found\n");
				return;
			}
		}
		else if ((item_target = Host_SetPosFindItemTarget(target)) != NULL)
		{
			if (!Host_SetPosFindItem(item_target, target, origin, &match_index, &match_count))
			{
				SV_ClientPrintf("setpos %s: no matching item found\n", target);
				return;
			}
		}
		else
			goto parse_coords;

		Host_SetPosApply(origin, angles, use_angles, true);
		if (item_target && match_count > 1)
			SV_ClientPrintf("setpos %s %i/%i: %i %i %i\n", target, match_index, match_count,
				(int)sv_player->v.origin[0],
				(int)sv_player->v.origin[1],
				(int)sv_player->v.origin[2]);
		else
			SV_ClientPrintf("setpos %s: %i %i %i\n", target,
				(int)sv_player->v.origin[0],
				(int)sv_player->v.origin[1],
				(int)sv_player->v.origin[2]);
		return;
	}

parse_coords:

	for (i = 1, numargs = 0; i < Cmd_Argc(); i++)
	{
		const char* str = Cmd_Argv(i);
		if (strcmp(str, "(") == 0 || strcmp(str, ")") == 0)
			continue;
		if (++numargs <= 6)
			args[numargs - 1] = atof(str);
	}

	if (numargs != 6 && numargs != 3)
	{
		SV_ClientPrintf("usage:\n");
		SV_ClientPrintf("   setpos <x> <y> <z>\n");
		SV_ClientPrintf("   setpos <x> <y> <z> <pitch> <yaw> <roll>\n");
		SV_ClientPrintf("   setpos start | end | middle | last\n");
		SV_ClientPrintf("   setpos quad | pent | ring | eyes | suit | mega | rl | gl | lg | sng | ng | ssg | sg\n");
		SV_ClientPrintf("current values:\n");
		SV_ClientPrintf("   %i %i %i %i %i %i\n",
			(int)sv_player->v.origin[0],
			(int)sv_player->v.origin[1],
			(int)sv_player->v.origin[2],
			(int)sv_player->v.v_angle[0],
			(int)sv_player->v.v_angle[1],
			(int)sv_player->v.v_angle[2]);
		return;
	}

	origin[0] = args[0];
	origin[1] = args[1];
	origin[2] = args[2];
	VectorCopy(sv_player->v.angles, angles);
	if (numargs == 6)
	{
		angles[0] = args[3];
		angles[1] = args[4];
		angles[2] = args[5];
	}

	Host_SetPosApply(origin, angles, numargs == 6, true);
}

/*
==================
Host_Fly_f

Sets client to flymode
==================
*/
static void Host_Fly_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	//johnfitz -- allow user to explicitly set noclip to on or off
	switch (Cmd_Argc())
	{
	case 1:
		if (sv_player->v.movetype != MOVETYPE_FLY)
		{
			sv_player->v.movetype = MOVETYPE_FLY;
			SV_ClientPrintf ("flymode ON\n");
		}
		else
		{
			sv_player->v.movetype = MOVETYPE_WALK;
			SV_ClientPrintf ("flymode OFF\n");
		}
		break;
	case 2:
		if (Q_atof(Cmd_Argv(1)))
		{
			sv_player->v.movetype = MOVETYPE_FLY;
			SV_ClientPrintf ("flymode ON\n");
		}
		else
		{
			sv_player->v.movetype = MOVETYPE_WALK;
			SV_ClientPrintf ("flymode OFF\n");
		}
		break;
	default:
		Con_Printf("fly [value] : toggle fly mode. values: 0 = off, 1 = on\n");
		break;
	}
	//johnfitz
}

/*
==================
Host_GetDamageFunction
==================
*/
static int Host_GetDamageFunction(void)
{
	dfunction_t *func;

	if (!qcvm || !qcvm->progs) // VM present?
		return -1;

	if (deathmatch.value || coop.value)
		return -1;

	func = ED_FindFunction("T_Damage");
	return func ? (int)(func - qcvm->functions) : -1;
}

static qboolean Host_EdictClassnameIs(const edict_t *ent, const char *classname)
{
	return ent && !ent->free && ent->v.classname
		&& !strcmp(PR_GetString(ent->v.classname), classname);
}

static qboolean Host_EdictStringIs(string_t value, const char *string)
{
	return value && string && !strcmp(PR_GetString(value), string);
}

static int Host_FindEntityGlobalOffset(const char *name)
{
	ddef_t *def = ED_FindGlobal(name);

	if (!def || ((def->type & ~DEF_SAVEGLOBAL) != ev_entity))
		return -1;

	return def->ofs;
}

static void Host_SetEntityGlobal(int ofs, edict_t *ent)
{
	if (ofs >= 0)
		G_INT(ofs) = ent ? EDICT_TO_PROG(ent) : 0;
}

static qboolean Host_DoDamage(int func, edict_t* target, qboolean gib)
{
	float health;
	const int old_self = pr_global_struct->self;
	const int old_other = pr_global_struct->other;

	if (!target || target->free)
		return false;

	health = target->v.health;
	if (health <= 0.0f || target->v.takedamage <= 0.0f) // Skip corpses or invulnerable entities
		return false;

	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG(sv_player);
	pr_global_struct->other = EDICT_TO_PROG(sv_player);

	/* Parameter setup. */
	G_INT(OFS_PARM0) = EDICT_TO_PROG(target);   /* target    */
	G_INT(OFS_PARM1) = EDICT_TO_PROG(sv_player);/* inflictor */
	G_INT(OFS_PARM2) = G_INT(OFS_PARM1);        /* attacker  */
	G_FLOAT(OFS_PARM3) = health + (gib ? 99.0f : 1.0f);
	memset(&qcvm->globals[OFS_PARM4], 0, sizeof(float) * 3 * 4);

	PR_ExecuteProgram(func);

	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;

	return true;
}

static qboolean Host_RunEdictFunction(func_t func, edict_t *self, edict_t *other)
{
	const int old_self = pr_global_struct->self;
	const int old_other = pr_global_struct->other;
	const int activator_ofs = Host_FindEntityGlobalOffset("activator");
	const int old_activator = (activator_ofs >= 0) ? G_INT(activator_ofs) : 0;

	if (!func || !self || self->free)
		return false;

	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG(self);
	pr_global_struct->other = other ? EDICT_TO_PROG(other) : 0;
	Host_SetEntityGlobal(activator_ofs, other);

	PR_ExecuteProgram(func);

	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;
	if (activator_ofs >= 0)
		G_INT(activator_ofs) = old_activator;

	return true;
}

static qboolean Host_RunEntityFunction(const char *funcname, edict_t *self, edict_t *other)
{
	dfunction_t *func;

	func = ED_FindFunction(funcname);
	if (!func)
		return false;

	return Host_RunEdictFunction((func_t)(func - qcvm->functions), self, other);
}

static qboolean Host_MassacreBoss(edict_t *ent, int damage_func, qboolean gib)
{
	if (Host_EdictClassnameIs(ent, "monster_boss"))
		return Host_RunEntityFunction("boss_death10", ent, sv_player);

	if (Host_EdictClassnameIs(ent, "monster_oldone"))
	{
		if (ent->v.health <= 0.0f || ent->v.takedamage <= 0.0f)
			return false;

		ent->v.flags = (int)ent->v.flags | FL_MONSTER;
		return Host_DoDamage(damage_func, ent, gib);
	}

	return false;
}

static qboolean Host_MassacreClassnameIsMonster(const char *classname)
{
	return classname && !strncmp(classname, "monster_", 8);
}

static qboolean Host_MassacreIsMonster(const edict_t *ent)
{
	const char *classname;

	if (!ent || ent->free || ent->v.health <= 0.0f)
		return false;

	classname = PR_GetString(ent->v.classname);
	if (Host_MassacreClassnameIsMonster(classname))
		return true;

	if (!((int)ent->v.flags & FL_MONSTER))
		return false;
	if (ent->v.takedamage <= 0.0f)
		return false;

	if (!classname || !classname[0])
		return true;

	/* Rogue r2m8's time machine is a damageable item with FL_MONSTER set.
	   Killing it through massacre fires the finale camera script instead of
	   just clearing the real monster count. */
	if (!strncmp(classname, "item_", 5) ||
		!strncmp(classname, "func_", 5) ||
		!strncmp(classname, "trigger_", 8) ||
		!strncmp(classname, "info_", 5) ||
		!strncmp(classname, "path_", 5))
		return false;

	return true;
}

static qboolean Host_MassacrePrepareMonster(edict_t *ent)
{
	const char *classname;

	if (!ent || ent->free || ent->v.health <= 0.0f)
		return false;

	if (((int)ent->v.flags & FL_MONSTER) && ent->v.takedamage > 0.0f)
		return true;

	classname = PR_GetString(ent->v.classname);
	/* Targetname monsters can sit dormant until their use function runs. */
	if (Host_MassacreClassnameIsMonster(classname) && ent->v.use)
		Host_RunEdictFunction(ent->v.use, ent, sv_player);

	return ent && !ent->free && ent->v.health > 0.0f && ent->v.takedamage > 0.0f;
}

static void Host_MassacreUseCounterToZero(edict_t *ent, int count_ofs)
{
	eval_t *count;
	float old_count;
	int guard = 64;

	while (ent && !ent->free && ent->v.use && guard-- > 0)
	{
		count = GetEdictFieldValue(ent, count_ofs);
		if (!count || count->_float <= 0.0f)
			break;

		old_count = count->_float;
		if (!Host_RunEdictFunction(ent->v.use, ent, sv_player))
			break;
		count = ent->free ? NULL : GetEdictFieldValue(ent, count_ofs);
		if (ent->free || !count || count->_float >= old_count)
			break;
	}
}

static void Host_MassacreFinishMGEndBossCounters(void)
{
	size_t i;
	int count_ofs;

	if (q_strcasecmp(sv.name, "mgend"))
		return;

	count_ofs = ED_FindFieldOffset("count");
	if (count_ofs < 0)
		return;

	/* mgend's Chthon fight advances doors through staged boss_counter uses. */
	for (i = 1; i < qcvm->num_edicts; ++i)
	{
		edict_t *ent = EDICT_NUM((int)i);
		eval_t *count;

		if (ent->free)
			continue;
		if (!Host_EdictClassnameIs(ent, "trigger_counter"))
			continue;
		if (!Host_EdictStringIs(ent->v.targetname, "boss_counter"))
			continue;
		count = GetEdictFieldValue(ent, count_ofs);
		if (!count || count->_float <= 0.0f)
			continue;

		Host_MassacreUseCounterToZero(ent, count_ofs);
	}
}

static void Host_Massacre_f (void) // alexey-lysiuk/quakespasm-exp/commit/af0833c
{
	const qboolean gib = (Cmd_Argc() > 1);   /* any 2nd arg toggles gibs */
	int            func;
	size_t         i;
	int            count = 0;

	/* Forward if typed on the host console of a listen server. */
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer();
		return;
	}

	/* Abort if T_Damage() not found (unusual custom progs). */
	if ((func = Host_GetDamageFunction()) == -1)
		return;

	for (i = 1; i < qcvm->num_edicts; ++i)     /* edict 0 = world */
	{
		edict_t* ent = EDICT_NUM((int)i);
		if (ent->free)
			continue;
		if (Host_MassacreBoss(ent, func, gib))
		{
			count++;
			continue;
		}
		if (!Host_MassacreIsMonster(ent))
			continue;
		if (!Host_MassacrePrepareMonster(ent))
			continue;

		if (Host_DoDamage(func, ent, gib))
			count++;
	}
	Host_MassacreFinishMGEndBossCounters();

	SV_ClientPrintf("Massacred all %d monster%s (%s)\n",
		count, count == 1 ? "" : "s", gib ? "gibbed" : "no gibs");
}

static qboolean Host_CoopMoveClientLiving(const client_t *client)
{
	edict_t *ent;

	if (!client || !client->active || !client->spawned)
		return false;

	ent = client->edict;
	if (!ent || ent->free)
		return false;

	return ent->v.deadflag == DEAD_NO && ent->v.health > 0.0f;
}

static qboolean Host_CoopMoveIntermissionActive(void)
{
	ddef_t *def;

	if (svs.changelevel_issued)
		return true;

	/* Do not infer this from PF_sv_WriteByte: QC writes arbitrary payload
	   bytes, and 30/31 are valid non-command byte values. */
	def = ED_FindGlobal("intermission_running");
	return def && G_FLOAT(def->ofs) != 0.0f;
}

#define HOST_COOP_GOTO_COOLDOWN		2.0
#define HOST_COOP_MOVE_TRACE_UP		64.0f
#define HOST_COOP_MOVE_TRACE_DOWN	128.0f
#define HOST_COOP_MOVE_MIN_FLOOR_Z	0.7f
#define HOST_COOP_MOVE_RING_RADIUS	32.0f
#define HOST_COOP_MOVE_RING_COUNT	4

static int Host_CoopMoveCoordSize(void)
{
	if (sv.protocolflags & PRFL_FLOATCOORD)
		return 4;
	if (sv.protocolflags & PRFL_INT32COORD)
		return 4;
	if (sv.protocolflags & PRFL_24BITCOORD)
		return 3;
	return 2;
}

static qboolean Host_CoopMoveSpotVisible(edict_t *anchor, const vec3_t spot)
{
	vec3_t end;
	trace_t trace;

	VectorCopy(spot, end);
	trace = SV_Move(anchor->v.origin, vec3_origin, vec3_origin, end,
		MOVE_NOMONSTERS, anchor);
	return !trace.allsolid && !trace.startsolid && trace.fraction >= 1.0f;
}

static qboolean Host_CoopMoveTrySpot(edict_t *mover, const vec3_t base, vec3_t out, edict_t **ground)
{
	static const float vertical_offsets[] = {0.0f, 8.0f, 16.0f, 24.0f, -8.0f, -16.0f, -24.0f};
	size_t i;

	if (ground)
		*ground = NULL;

	for (i = 0; i < Q_COUNTOF(vertical_offsets); i++)
	{
		vec3_t probe, above, below, spot;
		trace_t ground_trace;

		VectorCopy(base, probe);
		probe[2] += vertical_offsets[i];

		VectorCopy(probe, above);
		above[2] += HOST_COOP_MOVE_TRACE_UP;
		VectorCopy(probe, below);
		below[2] -= HOST_COOP_MOVE_TRACE_DOWN;

		ground_trace = SV_Move(above, vec3_origin, vec3_origin, below,
			MOVE_NOMONSTERS, mover);
		if (ground_trace.allsolid || ground_trace.startsolid ||
			ground_trace.fraction >= 1.0f ||
			ground_trace.plane.normal[2] < HOST_COOP_MOVE_MIN_FLOOR_Z)
			continue;

		VectorCopy(ground_trace.endpos, spot);
		spot[2] += -mover->v.mins[2] + 1.0f;

		VectorCopy(spot, mover->v.origin);
		if (!SV_TestEntityPosition(mover) && SV_CheckBottom(mover))
		{
			VectorCopy(spot, out);
			if (ground)
				*ground = ground_trace.ent;
			return true;
		}
	}

	return false;
}

static qboolean Host_CoopMoveFindSafeSpot(edict_t *mover, edict_t *anchor, vec3_t out, edict_t **ground)
{
	static const float preferred_offsets[][2] = {
		{-48.0f,   0.0f},
		{-48.0f,  32.0f},
		{-48.0f, -32.0f},
		{  0.0f,  48.0f},
		{  0.0f, -48.0f},
		{-72.0f,   0.0f},
		{-72.0f,  32.0f},
		{-72.0f, -32.0f}
	};
	static const float ring_xy[][2] = {
		{ 1.0f,  0.0f},
		{-1.0f,  0.0f},
		{ 0.0f,  1.0f},
		{ 0.0f, -1.0f},
		{ 1.0f,  1.0f},
		{-1.0f,  1.0f},
		{ 1.0f, -1.0f},
		{-1.0f, -1.0f}
	};
	vec3_t old_origin;
	float old_solid, old_movetype;
	vec3_t angles, forward, right, up;
	edict_t *candidate_ground;
	size_t i;
	int ring;

	VectorCopy(mover->v.origin, old_origin);
	old_solid = mover->v.solid;
	old_movetype = mover->v.movetype;
	mover->v.solid = SOLID_SLIDEBOX;
	mover->v.movetype = MOVETYPE_WALK;
	if (ground)
		*ground = NULL;

	/* Test probes temporarily write mover->v.origin; do not relink until the
	   original origin is restored below. */
	VectorCopy(vec3_origin, angles);
	angles[YAW] = anchor->v.angles[YAW];
	AngleVectors(angles, forward, right, up);

	for (i = 0; i < Q_COUNTOF(preferred_offsets); i++)
	{
		vec3_t base;

		VectorMA(anchor->v.origin, preferred_offsets[i][0], forward, base);
		VectorMA(base, preferred_offsets[i][1], right, base);

		if (Host_CoopMoveTrySpot(mover, base, out, &candidate_ground) &&
			Host_CoopMoveSpotVisible(anchor, out))
		{
			if (ground)
				*ground = candidate_ground;
			goto found;
		}
	}

	for (ring = 1; ring <= HOST_COOP_MOVE_RING_COUNT; ring++)
	{
		float radius = ring * HOST_COOP_MOVE_RING_RADIUS;

		for (i = 0; i < Q_COUNTOF(ring_xy); i++)
		{
			vec3_t base;

			VectorCopy(anchor->v.origin, base);
			base[0] += ring_xy[i][0] * radius;
			base[1] += ring_xy[i][1] * radius;

			if (Host_CoopMoveTrySpot(mover, base, out, &candidate_ground) &&
				Host_CoopMoveSpotVisible(anchor, out))
			{
				if (ground)
					*ground = candidate_ground;
				goto found;
			}
		}
	}

	VectorCopy(old_origin, mover->v.origin);
	mover->v.solid = old_solid;
	mover->v.movetype = old_movetype;
	if (ground)
		*ground = NULL;
	return false;

found:
	VectorCopy(old_origin, mover->v.origin);
	mover->v.solid = old_solid;
	mover->v.movetype = old_movetype;
	return true;
}

static void Host_CoopMoveTeleportSplash(const vec3_t origin)
{
	int msgsize = 2 + 3 * Host_CoopMoveCoordSize();

	if (sv.datagram.cursize > MAX_DATAGRAM - msgsize)
		return;

	MSG_WriteByte(&sv.datagram, svc_temp_entity);
	MSG_WriteByte(&sv.datagram, TE_TELEPORT);
	MSG_WriteCoord(&sv.datagram, origin[0], sv.protocolflags);
	MSG_WriteCoord(&sv.datagram, origin[1], sv.protocolflags);
	MSG_WriteCoord(&sv.datagram, origin[2], sv.protocolflags);
}

static void Host_CoopMoveApply(edict_t *player, const vec3_t origin, edict_t *ground)
{
	eval_t *val;
	vec3_t old_origin;
	int ofs;

	VectorCopy(player->v.origin, old_origin);
	SV_StartSound(player, old_origin, 0, "misc/r_tele1.wav", 255, 1.0f);
	Host_CoopMoveTeleportSplash(old_origin);

	VectorCopy(origin, player->v.origin);
	VectorClear(player->v.velocity);
	player->v.movetype = MOVETYPE_WALK;
	player->v.solid = SOLID_SLIDEBOX;
	player->v.flags = (int)player->v.flags & ~FL_WATERJUMP;
	if (ground && ground->v.solid == SOLID_BSP)
	{
		player->v.flags = (int)player->v.flags | FL_ONGROUND;
		player->v.groundentity = EDICT_TO_PROG(ground);
	}
	else
	{
		player->v.flags = (int)player->v.flags & ~FL_ONGROUND;
		player->v.groundentity = 0;
	}

	ofs = ED_FindFieldOffset("waterjump_time");
	if (ofs >= 0 && (val = GetEdictFieldValue(player, ofs)))
		val->_float = 0.0f;

	SV_CheckWater(player);

	/* Assist moves are positioning help, not map teleports, so do not fire triggers. */
	SV_LinkEdict(player, false);
	Host_CoopMoveTeleportSplash(player->v.origin);
	SV_StartSound(player, player->v.origin, 0, "misc/r_tele1.wav", 255, 1.0f);
}

/*
==================
Host_Goto_f -- woods #goto

Move the calling coop player to a safe spot near another living coop player.
==================
*/
static void Host_Goto_f (void)
{
	char target_name[MAX_SCOREBOARDNAME];
	char target_arg_buffer[MAX_SCOREBOARDNAME];
	const char *target_arg;
	qboolean ambiguous = false;
	int target_slot;
	client_t *target_client;
	vec3_t safe_origin;
	edict_t *safe_ground;

	if (cmd_source != src_client)
	{
		if (Con_IsRedirected())
			Con_Printf("goto is only available as a client command\n");
		else if (cls.state == ca_connected && !cls.demoplayback)
			Cmd_ForwardToServer();
		else
			Con_Printf("goto is only available as a client command\n");
		return;
	}

	if (!sv.active)
	{
		SV_ClientPrintf("No active server\n");
		return;
	}

	if (coop.value <= 0.0f)
	{
		SV_ClientPrintf("goto is only available in coop\n");
		return;
	}

	if (pr_global_struct->deathmatch)
	{
		SV_ClientPrintf("goto is not available in deathmatch\n");
		return;
	}

	if (Host_CoopMoveIntermissionActive())
	{
		SV_ClientPrintf("goto is not available during intermission or level changes\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		SV_ClientPrintf("usage: goto <player>\n");
		return;
	}

	if (!Host_CoopMoveClientLiving(host_client))
	{
		SV_ClientPrintf("You must be alive to use goto\n");
		return;
	}

	if (realtime < host_client->coop_goto_next_time)
	{
		SV_ClientPrintf("goto is available in %.1f seconds\n",
			host_client->coop_goto_next_time - realtime);
		return;
	}

	Host_BuildPlayerTarget(target_arg_buffer, sizeof(target_arg_buffer));
	target_arg = target_arg_buffer;
	target_slot = Host_FindServerPlayerSlot(target_arg, target_name, sizeof(target_name), &ambiguous);
	if (target_slot < 0)
	{
		SV_ClientPrintf("%s player match for %s\n", ambiguous ? "No unique" : "No", target_arg);
		return;
	}

	target_client = &svs.clients[target_slot];
	if (target_client == host_client)
	{
		SV_ClientPrintf("You cannot goto yourself\n");
		return;
	}

	if (!Host_CoopMoveClientLiving(target_client))
	{
		SV_ClientPrintf("%s is not alive\n", target_name);
		return;
	}

	if (!Host_CoopMoveFindSafeSpot(host_client->edict, target_client->edict, safe_origin, &safe_ground))
	{
		SV_ClientPrintf("No safe spot near %s\n", target_name);
		return;
	}

	Host_CoopMoveApply(host_client->edict, safe_origin, safe_ground);
	host_client->coop_goto_next_time = realtime + HOST_COOP_GOTO_COOLDOWN;
	SV_BroadcastPrintf("%s joined %s\n", host_client->name, target_client->name);
	Con_DPrintf("coop move: goto: %s near %s\n", host_client->name, target_client->name);
}

/*
==================
Host_Resurrect_f -- woods #resurrect

Bring the local player back to life exactly where they died,
preserving inventory and giving brief invulnerability when supported.
==================
*/

static void Host_Resurrect_f (void)
{
	eval_t* val;
	int     ofs;

	/*----------------------------------------------------------------
	 * 1. Guard rails
	 *----------------------------------------------------------------*/
	if (cmd_source != src_client)          /* typed in host console?  */
	{
		Cmd_ForwardToServer();
		return;
	}
	if (pr_global_struct->deathmatch)      /* no cheats in deathmatch */
		return;

	if (sv_player->v.deadflag == DEAD_NO && sv_player->v.health > 0)
	{
		SV_ClientPrintf("You are not dead\n");
		return;
	}

	/*----------------------------------------------------------------
	 * 2. Snapshot the current state
	 *----------------------------------------------------------------*/
	vec3_t death_origin, safe_origin = {0, 0, 0};

	float saved_weapon = sv_player->v.weapon;
	float saved_ammo_shells = sv_player->v.ammo_shells;
	float saved_ammo_nails = sv_player->v.ammo_nails;
	float saved_ammo_rockets = sv_player->v.ammo_rockets;
	float saved_ammo_cells = sv_player->v.ammo_cells;
	float saved_currentammo = sv_player->v.currentammo;
	string_t saved_weaponmodel = sv_player->v.weaponmodel;
	/* items can carry IT_SIGIL4 (1u<<31), so signed casts can saturate
	   or overflow on high-bit item sets and poison unrelated bits. */
	unsigned saved_items = (unsigned)sv_player->v.items;
	float saved_armortype = sv_player->v.armortype;
	float saved_armorvalue = sv_player->v.armorvalue;

	/* PutClientInServer would otherwise snap the view to spawnpoint angles. */
	vec3_t saved_angles, saved_v_angle;
	VectorCopy(sv_player->v.angles, saved_angles);
	VectorCopy(sv_player->v.v_angle, saved_v_angle);

	/* items2 is mission-pack/mod-only; snapshot only if defined */
	int   saved_items2 = 0;
	int   items2_ofs = ED_FindFieldOffset("items2");
	if (items2_ofs >= 0)
	{
		val = GetEdictFieldValue(sv_player, items2_ofs);
		if (val)
			saved_items2 = (int)val->_float;
		else
			items2_ofs = -1;
	}

	VectorCopy(sv_player->v.origin, death_origin);

	/*----------------------------------------------------------------
	 * 3. Find a safe spot near the death position
	 *----------------------------------------------------------------*/
#define STEP_RINGS   4               /* 0, 32, 64, 96 */
#define STEP_RADIUS  32.0f
	static const float ring_xy[9][2] = {
		{  0,  0}, { 1,  0}, {-1,  0}, { 0,  1},
		{  0, -1}, { 1,  1}, {-1,  1}, { 1, -1}, {-1, -1}
	};
	int ring, dir, found = 0, found_ring = 0;

	for (ring = 0; ring < STEP_RINGS && !found; ++ring)
	{
		float r = ring * STEP_RADIUS;

		for (dir = 0; dir < 9 && !found; ++dir)
		{
			vec3_t try_xy, above, below, impact;
			trace_t ground_trace;

			/* XY offset in this ring */
			try_xy[0] = death_origin[0] + ring_xy[dir][0] * r;
			try_xy[1] = death_origin[1] + ring_xy[dir][1] * r;
			try_xy[2] = death_origin[2];

			/* Trace 64 down from 64 up to find ground. Use the server trace
			   path; client TraceLine relies on cl.worldmodel and is wrong for
			   dedicated/remote server command handling. */
			VectorCopy(try_xy, above);  above[2] += 64.0f;
			VectorCopy(try_xy, below);  below[2] -= 64.0f;
			ground_trace = SV_Move(above, vec3_origin, vec3_origin, below,
				MOVE_NOMONSTERS, sv_player);
			if (ground_trace.allsolid || ground_trace.startsolid ||
				ground_trace.fraction >= 1.0f ||
				ground_trace.plane.normal[2] <= 0.0f)
				continue;
			VectorCopy(ground_trace.endpos, impact);

			/* Place the player 18 units above impact point */
			VectorCopy(impact, safe_origin);
			safe_origin[2] += 18.0f;

			VectorCopy(safe_origin, sv_player->v.origin);
			if (!SV_TestEntityPosition(sv_player))
			{
				found = 1;
				found_ring = ring;
			}
		}
	}

#undef STEP_RADIUS
#undef STEP_RINGS

	/* Probing left sv_player->v.origin at the last test position; restore it
	   so PutClientInServer sees the original spot. We reposition afterwards. */
	VectorCopy(death_origin, sv_player->v.origin);

	if (!found)
	{
		/* Fallback: original position, nudged up 18 */
		VectorCopy(death_origin, safe_origin);
		safe_origin[2] += 18.0f;
	}

	/*----------------------------------------------------------------
	 * 4. Call QC PutClientInServer to reset player state
	 *----------------------------------------------------------------*/
	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG(sv_player);
	PR_ExecuteProgram(pr_global_struct->PutClientInServer);

	/* PutClientInServer chose a spawnpoint; remember it as a final fallback
	   in case our safe_origin doesn't fit the live player bbox (e.g. corpse
	   was crushed against geometry, or a mod shrinks the corpse bbox). */
	vec3_t spawnpoint_origin;
	qboolean used_spawnpoint = false;
	VectorCopy(sv_player->v.origin, spawnpoint_origin);

	/*----------------------------------------------------------------
	 * 5. Restore inventory, position, and health
	 *----------------------------------------------------------------*/
	VectorCopy(safe_origin, sv_player->v.origin);
	if (SV_TestEntityPosition(sv_player))
	{
		VectorCopy(spawnpoint_origin, sv_player->v.origin);
		used_spawnpoint = true;
		found_ring = 0;
	}

	sv_player->v.weapon = saved_weapon;
	sv_player->v.ammo_shells = saved_ammo_shells;
	sv_player->v.ammo_nails = saved_ammo_nails;
	sv_player->v.ammo_rockets = saved_ammo_rockets;
	sv_player->v.ammo_cells = saved_ammo_cells;
	sv_player->v.armortype = saved_armortype;
	sv_player->v.armorvalue = saved_armorvalue;
	/* The matching timers are cleared below. Drop temporary powerup item bits
	   here so HUD/effect state cannot survive without a valid expiry timer. */
	sv_player->v.items = saved_items &
		~(IT_INVISIBILITY | IT_INVULNERABILITY | IT_SUIT | IT_QUAD);

	if (items2_ofs >= 0)
	{
		val = GetEdictFieldValue(sv_player, items2_ofs);
		if (val)
			val->_float = (float)saved_items2;
	}

	sv_player->v.health = 100;
	sv_player->v.max_health = 100;
	sv_player->v.deadflag = DEAD_NO;
	sv_player->v.takedamage = DAMAGE_AIM;
	sv_player->v.movetype = MOVETYPE_WALK;
	sv_player->v.solid = SOLID_SLIDEBOX;
	sv_player->v.flags = (int)sv_player->v.flags | (FL_CLIENT | FL_ONGROUND);
	{
		unsigned powerup_effects = EF_DIMLIGHT | EF_RED | EF_BLUE;
		if (qcvm->brokeneffects)
			powerup_effects |= EFQE_QUADLIGHT | EFQE_PENTLIGHT;

		/* PutClientInServer usually clears effects, but keep this in step
		   with the timer/item cleanup for mods that leave glow bits intact. */
		sv_player->v.effects = (unsigned)sv_player->v.effects & ~powerup_effects;
	}
	sv_player->v.weaponframe = 0;

	/* Restore facing so the resurrected player keeps looking where they died
	   instead of snapping to the spawnpoint orientation. fixangle=1 makes
	   the engine push the angles to the client. */
	VectorCopy(saved_angles, sv_player->v.angles);
	VectorCopy(saved_v_angle, sv_player->v.v_angle);
	sv_player->v.fixangle = 1;

	/*----------------------------------------------------------------
	 * 6. Reset miscellaneous QC-only fields
	 *----------------------------------------------------------------*/
	static const struct { const char* name; float value; } scalars[] = {
		{"show_hostile",     0},
		{"air_finished",     12.0f},         /* seconds from now */
		{"dmg",              2},
		{"attack_finished",  0},
	};
	for (size_t s = 0; s < Q_COUNTOF(scalars); ++s)
	{
		ofs = ED_FindFieldOffset(scalars[s].name);
		if (ofs >= 0 && (val = GetEdictFieldValue(sv_player, ofs)))
			val->_float = (scalars[s].name[0] == 'a')
				? qcvm->time + scalars[s].value
				: scalars[s].value;
	}

	/* -----------------------------------------------------------------
	 * clear temporary power-up timers
	 * -----------------------------------------------------------------*/
	static const char* timers[] = {
		"super_damage_finished",
		"radsuit_finished",
		"invisible_finished",
		"trif_time",
		"trif_finished"
	};

	for (size_t k = 0; k < Q_COUNTOF(timers); ++k)
	{
		ofs = ED_FindFieldOffset(timers[k]);
		if (ofs >= 0)                          /* field exists in this progs */
		{
			val = GetEdictFieldValue(sv_player, ofs);
			if (val)                           /* field is addressable       */
				val->_float = 0.0f;
		}
	}

	/* Arm a 5s invulnerability via timer fields. Only set IT_INVULNERABILITY
	   if invincible_finished exists; otherwise CheckPowerups has nothing to
	   consult and the icon would persist indefinitely. */
	qboolean invuln_armed = false;
	ofs = ED_FindFieldOffset("invincible_finished");
	if (ofs >= 0 && (val = GetEdictFieldValue(sv_player, ofs)))
	{
		val->_float = qcvm->time + 5.0f;
		invuln_armed = true;
	}

	if (invuln_armed)
	{
		/* invincible_time is a stock-QC warning-cadence marker, not an expiry:
		   pickups set it to 1 and use invincible_finished as the actual timer. */
		ofs = ED_FindFieldOffset("invincible_time");
		if (ofs >= 0 && (val = GetEdictFieldValue(sv_player, ofs)))
			val->_float = 1.0f;
	}

	/* pain cooldown reset */
	ofs = ED_FindFieldOffset("pain_finished");
	if (ofs >= 0 && (val = GetEdictFieldValue(sv_player, ofs)))
		val->_float = 0.0f;

	/*----------------------------------------------------------------
	 * 7. Sync currentammo and weaponmodel to the restored weapon
	 *----------------------------------------------------------------*/
	if (!Host_RunEntityFunction("W_SetCurrentAmmo", sv_player, NULL))
	{
		sv_player->v.currentammo = saved_currentammo;
		sv_player->v.weaponmodel = saved_weaponmodel;

		/* Progs without W_SetCurrentAmmo: stock weapons can still derive the
		   correct ammo counter. Custom weapons keep their saved values. */
		switch ((int)sv_player->v.weapon)
		{
		case IT_SHOTGUN:
		case IT_SUPER_SHOTGUN:
			sv_player->v.currentammo = sv_player->v.ammo_shells;  break;

		case IT_NAILGUN:
		case IT_SUPER_NAILGUN:
		case RIT_LAVA_SUPER_NAILGUN:
			sv_player->v.currentammo = sv_player->v.ammo_nails;   break;

		case IT_GRENADE_LAUNCHER:
		case IT_ROCKET_LAUNCHER:
		case RIT_MULTI_GRENADE:
		case RIT_MULTI_ROCKET:
			sv_player->v.currentammo = sv_player->v.ammo_rockets; break;

		case IT_LIGHTNING:
		case HIT_LASER_CANNON:
		case HIT_MJOLNIR:
			sv_player->v.currentammo = sv_player->v.ammo_cells;   break;
		}
	}

	if (invuln_armed)
	{
		sv_player->v.items = (unsigned)sv_player->v.items | IT_INVULNERABILITY;
		if (qcvm->brokeneffects)
			sv_player->v.effects = (unsigned)sv_player->v.effects | EFQE_PENTLIGHT;
		else
			sv_player->v.effects = (unsigned)sv_player->v.effects | EF_DIMLIGHT;
		host_client->powerup_warn_flags |= PWARN_GIVE | PWARN_RESURRECT_INVULN;
	}

	/*----------------------------------------------------------------
	 * 8. Finalise: relink, play sound, force client update, print
	 *----------------------------------------------------------------*/
	SV_LinkEdict(sv_player, false);      /* update physics box */

	/* Sound emanates from the final position, not the death origin. */
	SV_StartSound(sv_player, sv_player->v.origin, 0,
		"items/protect.wav", 255, 1.0f);

	MSG_WriteByte(&host_client->message, svc_stufftext);
	MSG_WriteString(&host_client->message, "bf\n"); /* refresh pics and icons */

	if (used_spawnpoint)
		SV_ClientPrintf("Death position unsafe; respawned at spawnpoint\n");
	else if (found && found_ring > 0)
		SV_ClientPrintf("Moved to safe position (%d units)\n", found_ring * 32);

	if (invuln_armed)
		SV_ClientPrintf("Resurrected with 100 health and 5 seconds of invulnerability!\n");
	else
		SV_ClientPrintf("Resurrected with 100 health\n");
	SV_BroadcastPrintf("%s was resurrected\n",
		PR_GetString(sv_player->v.netname));
}

/*
==================
ParseServerAddress -- woods #udplist
==================
*/
static qboolean ParseServerAddress(const char* input, char* hostbuf, size_t hostbufsize, int* portout)
{
	net_endpoint_t endpoint;
	int default_port;

	if (!input || !*input || !hostbuf || !hostbufsize || !portout)
		return false;

	default_port = (net_hostport > 0 && net_hostport <= 65535) ? net_hostport : DEFAULTnet_hostport;
	if (default_port <= 0 || default_port > 65535)
		default_port = 26000;
	/* Diagnostic commands reject malformed endpoints before invoking the resolver. */
	if (!NET_ParseEndpoint(input, default_port, &endpoint))
		return false;
	if (strlen(endpoint.host) + 1 > hostbufsize)
		return false;

	q_strlcpy(hostbuf, endpoint.host, hostbufsize);
	*portout = endpoint.port;
	return true;
}

static size_t BuildNetQuakePingQuery(unsigned char* buffer, size_t bufsize) // woods #udplist
{
	const char gamename[] = "QUAKE";
	unsigned char* out = buffer;
	size_t required = 4 /* header */ + 1 /* command */ + sizeof(gamename) /* string incl. null */ + 1 /* protocol */;

	if (bufsize < required)
		return 0;

	out += 4; // leave space for the header
	*out++ = CCREQ_SERVER_INFO;
	memcpy(out, gamename, sizeof(gamename));
	out += sizeof(gamename);
	*out++ = NET_PROTOCOL_VERSION;

	{
		unsigned int header = NETFLAG_CTL | ((unsigned int)((out - buffer)) & NETFLAG_LENGTH_MASK);
		int beheader = BigLong((int)header);
		memcpy(buffer, &beheader, sizeof(beheader));
	}

	return (size_t)(out - buffer);
	}

typedef struct ping_query_s // woods #udplist
{
	const unsigned char* payload;
	size_t payload_len;
	const char* label;
} ping_query_t;

static int NormalizePingResult(int ping_ms) // woods #udplist
{
	const int normalization_bias = 14;

	if (ping_ms < 0)
		return ping_ms;

	ping_ms -= normalization_bias;
	if (ping_ms < 0)
		ping_ms = 0;

	return ping_ms;
}

static qboolean SendPingPacket(sys_socket_t sock, const struct sockaddr* addr, socklen_t addrlen, const ping_query_t* query, qboolean is_retry) // woods #udplist
{
	if (!query || !query->payload || query->payload_len == 0)
		return false;

	for (;;)
{
		if (sendto(sock, (const char*)query->payload, (int)query->payload_len, 0, addr, addrlen) != SOCKET_ERROR)
			return true;

		{
			int err = SOCKETERRNO;
#ifdef _WIN32
			if (err == WSAEINTR)
#else
			if (err == EINTR)
#endif
				continue; // interrupted, retry immediately

			Con_DPrintf("sendto() %s%s failed: %s\n", is_retry ? "retry " : "", query->label, socketerror(err));
		}
		return false;
	}
}

/*
==================
Socket_Ping_Host -- woods #udplist
==================
*/
static void FormatResolvedServerAddress(const struct sockaddr* addr, socklen_t addrlen,
	int port, char* out, size_t outsize)
{
	char numeric[NI_MAXHOST];

	if (!out || !outsize || !addr ||
		getnameinfo(addr, addrlen, numeric, sizeof(numeric), NULL, 0, NI_NUMERICHOST) != 0)
		return;

	if (strchr(numeric, ':'))
		q_snprintf(out, outsize, "[%s]:%d", numeric, port);
	else
		q_snprintf(out, outsize, "%s:%d", numeric, port);
}

static int Socket_Ping_HostResolved(const char* host, int port, char* resolved, size_t resolvedsize)
{
	struct addrinfo hints;
	struct addrinfo* res = NULL;
	struct addrinfo* rp;
	char portstr[16];
	int ping_result = -1;
	unsigned char nq_query[32];
	size_t nq_query_len;
	static const unsigned char qw_getinfo[] = { 0xFF, 0xFF, 0xFF, 0xFF, 'g', 'e', 't', 'i', 'n', 'f', 'o', '\n' };
	static const unsigned char qw_status[] = { 0xFF, 0xFF, 0xFF, 0xFF, 's', 't', 'a', 't', 'u', 's', '\n' };
	static const unsigned char dp_getchallenge[] = { 0xFF, 0xFF, 0xFF, 0xFF, 'g', 'e', 't', 'c', 'h', 'a', 'l', 'l', 'e', 'n', 'g', 'e', '\n' };
	int ret;

	if (resolved && resolvedsize)
		resolved[0] = '\0';

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_family = AF_UNSPEC;

	q_snprintf(portstr, sizeof(portstr), "%d", port);

	nq_query_len = BuildNetQuakePingQuery(nq_query, sizeof(nq_query));

	ret = getaddrinfo(host, portstr, &hints, &res);
	if (ret != 0)
	{
#ifdef _WIN32
		Con_DPrintf("getaddrinfo failed for %s:%s (%d)\n", host, portstr, ret);
#else
		Con_DPrintf("getaddrinfo failed for %s:%s (%s)\n", host, portstr, gai_strerror(ret));
#endif
		return -1;
	}

	for (rp = res; rp; rp = rp->ai_next)
	{
		sys_socket_t sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (resolved && resolvedsize && !resolved[0])
			FormatResolvedServerAddress(rp->ai_addr, (socklen_t)rp->ai_addrlen,
				port, resolved, resolvedsize);
		if (sock == INVALID_SOCKET)
		{
			Con_DPrintf("socket() failed: %s\n", socketerror(SOCKETERRNO));
			continue;
		}

		{
			const ping_query_t base_queries[] = {
				{qw_getinfo,      sizeof(qw_getinfo),      "getinfo"},
				{qw_status,       sizeof(qw_status),       "status"},
				{dp_getchallenge, sizeof(dp_getchallenge), "getchallenge"},
			};
			ping_query_t queries[Q_COUNTOF(base_queries) + 1];
			size_t   query_count = 0;
			qboolean sent_any = false;
			double   start_time = 0.0;
			double   next_resend_time = 0.0;
			const double resend_interval = 0.5;
			const double max_wait_time = 1.5;

			if (nq_query_len > 0)
			{
				queries[query_count].payload = nq_query;
				queries[query_count].payload_len = nq_query_len;
				queries[query_count].label = "netquake";
				++query_count;
			}

			for (size_t i = 0; i < Q_COUNTOF(base_queries); ++i)
				queries[query_count++] = base_queries[i];

			for (size_t i = 0; i < query_count; ++i)
	{ 
				if (SendPingPacket(sock, rp->ai_addr, (socklen_t)rp->ai_addrlen, &queries[i], false))
				{
					if (!sent_any)
					{
						start_time = Sys_DoubleTime();
						next_resend_time = start_time + resend_interval;
						sent_any = true;
					}
				}
			}

			if (sent_any)
			{
				double deadline = start_time + max_wait_time; // allow a little longer for servers to answer

				while (ping_result < 0)
				{
					double now = Sys_DoubleTime();
					double remaining = deadline - now;
					fd_set readfds;
					struct timeval tv;
					int sel;

					if (remaining <= 0)
			break;

					if (now >= next_resend_time && now < deadline)
					{
						for (size_t i = 0; i < query_count; ++i)
							SendPingPacket(sock, rp->ai_addr, (socklen_t)rp->ai_addrlen, &queries[i], true);

						next_resend_time = now + resend_interval;
						continue;
					}

					FD_ZERO(&readfds);
					FD_SET(sock, &readfds);

					if (remaining >= 1.0)
					{
						tv.tv_sec = (int)remaining;
						tv.tv_usec = (int)((remaining - tv.tv_sec) * 1000000.0);
		}
					else
					{
						tv.tv_sec = 0;
						tv.tv_usec = (int)(remaining * 1000000.0);
						if (tv.tv_usec <= 0)
							tv.tv_usec = 1000;
	}

#ifdef _WIN32
					sel = selectsocket(0, &readfds, NULL, NULL, &tv);
#else
					sel = selectsocket((int)(sock + 1), &readfds, NULL, NULL, &tv);
#endif
					if (sel > 0 && FD_ISSET(sock, &readfds))
					{
						unsigned char buffer[2048];
						struct sockaddr_storage from;
						socklen_t fromlen = sizeof(from);
						int received;

						for (;;)
						{
							received = recvfrom(sock, (char*)buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &fromlen);
							if (received >= 0)
								break;

							{
								int err = SOCKETERRNO;
#ifdef _WIN32
								if (err == WSAEINTR)
#else
								if (err == EINTR)
#endif
									continue; // interrupted, try again immediately

								Con_DPrintf("recvfrom() failed: %s\n", socketerror(err));
							}
							break;
	}

						if (received > 0)
						{
							double elapsed = (Sys_DoubleTime() - start_time) * 1000.0;
							if (elapsed < 0)
								elapsed = 0;

							ping_result = NormalizePingResult((int)(elapsed + 0.5));
							break;
	}
}
					else if (sel == SOCKET_ERROR)
					{
						int err = SOCKETERRNO;
#ifdef _WIN32
						if (err == WSAEINTR)
#else
						if (err == EINTR)
#endif
							continue; // interrupted, keep waiting within the deadline

						Con_DPrintf("select() failed: %s\n", socketerror(err));
						break;
					}
				}
			}
		}

		closesocket(sock);

		if (ping_result >= 0)
		{
			FormatResolvedServerAddress(rp->ai_addr, (socklen_t)rp->ai_addrlen,
				port, resolved, resolvedsize);
			break;
		}
	}

	freeaddrinfo(res);
	return ping_result;
}

static int Socket_Ping_Host(const char* host, int port)
{
	return Socket_Ping_HostResolved(host, port, NULL, 0);
}

/*
==================
UDP_Ping_Host -- woods #udplist
==================
*/
int UDP_Ping_HostResolved(const char* host, char* resolved, size_t resolvedsize)
{
	char hostbuf[MAX_SERVER_ADDRESS_LEN];
	int port;

	if (resolved && resolvedsize)
		resolved[0] = '\0';
	if (!ParseServerAddress(host, hostbuf, sizeof(hostbuf), &port))
		return -1;

	return Socket_Ping_HostResolved(hostbuf, port, resolved, resolvedsize);
}

int UDP_Ping_Host(const char* host)
{
	return UDP_Ping_HostResolved(host, NULL, 0);
}

/*
==================
UDP_QueryPlayers -- query player names from a server via CCREQ_PLAYER_INFO
Returns a malloc'd comma-separated string, or NULL on failure.
==================
*/
char *UDP_QueryPlayers(const char *host, int maxslots)
{
	char hostbuf[MAX_SERVER_ADDRESS_LEN];
	int port;
	struct addrinfo hints, *res, *rp;
	char portstr[16];
	sys_socket_t sock = INVALID_SOCKET;
	char names[512];
	int nameslen = 0;
	int ret;

	if (!host || !*host || maxslots <= 0)
		return NULL;
	if (maxslots > MAX_SCOREBOARD)
		maxslots = MAX_SCOREBOARD;

	if (!ParseServerAddress(host, hostbuf, sizeof(hostbuf), &port))
		return NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_family = AF_UNSPEC;

	q_snprintf(portstr, sizeof(portstr), "%d", port);
	ret = getaddrinfo(hostbuf, portstr, &hints, &res);
	if (ret != 0)
		return NULL;

	for (rp = res; rp; rp = rp->ai_next)
	{
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock != INVALID_SOCKET)
			break;
	}
	if (sock == INVALID_SOCKET || !rp)
	{
		freeaddrinfo(res);
		return NULL;
	}

	/* send CCREQ_PLAYER_INFO for each slot */
	{
		unsigned char pkt[8];
		int i;
		for (i = 0; i < maxslots; i++)
		{
			unsigned int hdr;
			int behdr;
			size_t pktlen = 6; /* 4 header + 1 command + 1 slot */

			pkt[4] = CCREQ_PLAYER_INFO;
			pkt[5] = (unsigned char)i;
			hdr = NETFLAG_CTL | ((unsigned int)pktlen & NETFLAG_LENGTH_MASK);
			behdr = BigLong((int)hdr);
			memcpy(pkt, &behdr, 4);

			sendto(sock, (const char *)pkt, (int)pktlen, 0, rp->ai_addr, (socklen_t)rp->ai_addrlen);
		}
	}

	/* collect responses with timeout */
	{
		double deadline = Sys_DoubleTime() + 1.5;
		double idle_deadline = Sys_DoubleTime() + 0.3; /* give up if no response within 300ms */
		int received_count = 0;

		while (received_count < maxslots)
		{
			double now = Sys_DoubleTime();
			double wait_until = (idle_deadline < deadline) ? idle_deadline : deadline;
			double remaining = wait_until - now;
			fd_set readfds;
			struct timeval tv;
			int sel;
			unsigned char buf[512];
			struct sockaddr_storage from;
			socklen_t fromlen = sizeof(from);
			ssize_t recv_len;
			int len;

			if (remaining <= 0)
				break;

			FD_ZERO(&readfds);
			FD_SET(sock, &readfds);
			if (remaining >= 1.0)
			{
				tv.tv_sec = (int)remaining;
				tv.tv_usec = (int)((remaining - tv.tv_sec) * 1000000.0);
			}
			else
			{
				tv.tv_sec = 0;
				tv.tv_usec = (int)(remaining * 1000000.0);
				if (tv.tv_usec <= 0)
					tv.tv_usec = 1000;
			}

#ifdef _WIN32
			sel = selectsocket(0, &readfds, NULL, NULL, &tv);
#else
			sel = selectsocket((int)(sock + 1), &readfds, NULL, NULL, &tv);
#endif
			if (sel <= 0)
				break;
			if (!FD_ISSET(sock, &readfds))
				break;

			recv_len = recvfrom(sock, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
			if (recv_len > (ssize_t)sizeof(buf))
				continue;
			len = (int)recv_len;
			if (len < 6) /* minimum: 4 header + 1 type + 1 slot */
				continue;

			{
				int control = BigLong(*(int *)buf);
				int pktlen;
				const unsigned char *p;
				const char *pname;
				int namelen;

				if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
					continue;
				pktlen = control & NETFLAG_LENGTH_MASK;
				if (pktlen != len)
					continue;
				if (buf[4] != CCREP_PLAYER_INFO)
					continue;

				/* buf[5] = player number, name starts at buf[6] as null-terminated string */
				p = buf + 6;
				pname = (const char *)p;

				/* ensure name is null-terminated within received data */
				{
					const unsigned char *end = buf + len;
					const unsigned char *scan = p;
					while (scan < end && *scan)
						scan++;
					if (scan >= end)
						continue; /* no null terminator found, skip */
					namelen = (int)(scan - p);
				}

				/* trim trailing spaces */
				while (namelen > 0 && pname[namelen - 1] == ' ')
					namelen--;

				if (namelen > 0 && nameslen + namelen + 2 < (int)sizeof(names))
				{
					if (nameslen > 0)
					{
						names[nameslen++] = ',';
						names[nameslen++] = ' ';
					}
					memcpy(names + nameslen, pname, namelen);
					nameslen += namelen;
					names[nameslen] = '\0';
				}
				received_count++;
				idle_deadline = Sys_DoubleTime() + 0.3; /* extend idle window after each response */
			}
		}
	}

	closesocket(sock);
	freeaddrinfo(res);

	if (nameslen > 0)
		return strdup(names);
	return NULL;
}

/*
==================
Host_Ping_f -- woods add support for external ping command #icmp

==================
*/
static void Host_Ping_f (void)
{
	int		i, j;
	float		total;
	client_t	*client;
	const char* n;	// JPG - for ping +N // woods #pqlag (add const)

	n = Cmd_Argv(1);

	// JPG - check for ping +N // woods #pqlag
	if (Cmd_Argc() == 2)
	{
		if (*n == '+')
		{
			if (cls.state != ca_connected)
			{
				Con_Printf("You must be connected to a server to use ping +N\n");
				return;
			}
			
			Cvar_Set("pq_lag", n + 1);
			return;
		}

		{
			char host_only[MAX_SERVER_ADDRESS_LEN];
			int ping_port;

			if (!ParseServerAddress(n, host_only, sizeof(host_only), &ping_port))
		{
				Con_Printf("address not valid %s\n", n);
				return;
			}

			if (Valid_IP(host_only) || Valid_Domain(host_only))
		{
				int rtt = Socket_Ping_Host(host_only, ping_port);
			if (rtt >= 0)
				Con_Printf("%i ms\n", rtt);
			else
					Con_Printf("ping failed, server did not respond\n");
			return;
		}

			Con_Printf("address not valid %s\n", n);
			return;
		}
	}

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	SV_ClientPrintf ("Client ping times:\n");
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->spawned || !client->netconnection)
			continue;
		total = 0;
		for (j = 0; j < NUM_PING_TIMES; j++)
			total+=client->ping_times[j];
		total /= NUM_PING_TIMES;
		SV_ClientPrintf ("%4i %s\n", (int)(total*1000), client->name);
	}
}

#define PINGS_CHUNK_CLIENTS 32

static int Host_ClientPingMS(client_t *client)
{
	int j;
	float total;

	if (!client || !client->spawned || !client->netconnection)
		return 0;

	total = 0;
	for (j = 0; j < NUM_PING_TIMES; j++)
		total += client->ping_times[j];
	total /= NUM_PING_TIMES;

	return CLAMP(0, (int)(total * 1000), 9999);
}

#define NETDROPS_MIN_WINDOW_SECS 5.0
#define NETDROPS_REMOTE_TRUNCATE_RESERVE 128

typedef struct netdrops_client_s
{
	int slot;
	const char *name;
	int ping;
	int packetloss;
	unsigned int map_drops;
	double map_drops_per_min;
	double sort_drops_per_min;
	unsigned int total_drops;
	int connected_secs;
	qboolean rate_valid;
} netdrops_client_t;

static int Host_NetDropsSortClass(const netdrops_client_t *client)
{
	if (client->rate_valid && client->map_drops == 0)
		return 0;
	if (!client->rate_valid && client->map_drops == 0)
		return 1;
	return 2;
}

static int Host_NetDropsCompare(const void *lhs, const void *rhs)
{
	const netdrops_client_t *a = (const netdrops_client_t *)lhs;
	const netdrops_client_t *b = (const netdrops_client_t *)rhs;
	int class_a = Host_NetDropsSortClass(a);
	int class_b = Host_NetDropsSortClass(b);

	if (class_a != class_b)
		return class_a - class_b;

	if (a->sort_drops_per_min < b->sort_drops_per_min)
		return -1;
	if (a->sort_drops_per_min > b->sort_drops_per_min)
		return 1;

	if (a->map_drops != b->map_drops)
		return a->map_drops < b->map_drops ? -1 : 1;
	if (a->total_drops != b->total_drops)
		return a->total_drops < b->total_drops ? -1 : 1;
	if (a->connected_secs != b->connected_secs)
		return a->connected_secs > b->connected_secs ? -1 : 1;
	return a->slot - b->slot;
}

static qboolean Host_NetDropsPrint(qboolean remote, size_t reserve, const char *fmt, ...) FUNC_PRINTF(3,4);
static qboolean Host_NetDropsPrint(qboolean remote, size_t reserve, const char *fmt, ...)
{
	char text[1024];
	va_list ap;

	va_start(ap, fmt);
	q_vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	if (remote)
	{
		size_t needed;

		if (!host_client)
			return false;

		needed = strlen(text) + 2 + reserve; /* svc_print byte plus trailing NUL */
		if ((size_t)host_client->message.cursize + needed > (size_t)host_client->message.maxsize)
			return false;

		MSG_WriteByte(&host_client->message, svc_print);
		MSG_WriteString(&host_client->message, text);
	}
	else
		Con_Printf("%s", text);

	return true;
}

static void Host_NetDropsFormatTime(int seconds, char *buffer, size_t buffer_size)
{
	int minutes;
	int hours;

	if (seconds < 0)
		seconds = 0;

	minutes = seconds / 60;
	seconds -= minutes * 60;
	hours = minutes / 60;
	minutes -= hours * 60;

	q_snprintf(buffer, buffer_size, "%i:%02i:%02i", hours, minutes, seconds);
}

/*
==================
Host_NetDrops_f

Server-side dropped-packet summary, ranked best to worst by map drops per minute.
==================
*/
static void Host_NetDrops_f(void)
{
	netdrops_client_t clients[MAX_SCOREBOARD];
	int count = 0;
	int i;
	qboolean remote = (cmd_source == src_client);

	if (!remote)
	{
		if (!sv.active)
		{
			if (cls.state == ca_connected && !cls.demoplayback)
				cls.netdrops_request_time = realtime;
			Cmd_ForwardToServer();
			return;
		}
	}
	else if (!host_client)
		return;

	for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++)
	{
		client_t *client = &svs.clients[i];
		double map_window_secs;
		double sort_window_secs;

		if (!client->active || !client->netconnection)
			continue;

		clients[count].slot = i;
		clients[count].name = client->name;
		clients[count].ping = Host_ClientPingMS(client);
		clients[count].packetloss = NET_QSocketGetPacketLoss(client->netconnection);
		clients[count].map_drops = NET_QSocketGetUnreliableReceiveMapDrops(client->netconnection);
		clients[count].total_drops = NET_QSocketGetUnreliableReceiveTotalDrops(client->netconnection);
		clients[count].connected_secs = (int)(net_time - NET_QSocketGetTime(client->netconnection));
		map_window_secs = NET_QSocketGetUnreliableReceiveMapDropWindowSecs(client->netconnection);
		clients[count].rate_valid = map_window_secs >= NETDROPS_MIN_WINDOW_SECS;
		clients[count].map_drops_per_min = clients[count].rate_valid
			? (clients[count].map_drops * 60.0) / map_window_secs
			: 0.0;
		sort_window_secs = clients[count].rate_valid ? map_window_secs : NETDROPS_MIN_WINDOW_SECS;
		clients[count].sort_drops_per_min = (clients[count].map_drops * 60.0) / sort_window_secs;
		count++;
	}

	if (!count)
	{
		Host_NetDropsPrint(remote, 0, "netdrops: no connected network clients\n");
		return;
	}

	qsort(clients, count, sizeof(clients[0]), Host_NetDropsCompare);

	if (!Host_NetDropsPrint(remote, 0, "netdrops: best to worst by map drops/min (<%.0fs map window uses %.0fs floor)\n",
		NETDROPS_MIN_WINDOW_SECS, NETDROPS_MIN_WINDOW_SECS))
		return;
	if (!Host_NetDropsPrint(remote, 0, "#   name             ping pl%% mapdrops drops/min totaldrops connected\n"))
		return;
	for (i = 0; i < count; i++)
	{
		char rate[16];
		char connected[16];
		size_t reserve = remote && i + 1 < count ? NETDROPS_REMOTE_TRUNCATE_RESERVE : 0;

		if (clients[i].rate_valid)
			q_snprintf(rate, sizeof(rate), "%.2f", clients[i].map_drops_per_min);
		else
			q_snprintf(rate, sizeof(rate), "<%.0fs", NETDROPS_MIN_WINDOW_SECS);
		Host_NetDropsFormatTime(clients[i].connected_secs, connected, sizeof(connected));

		if (!Host_NetDropsPrint(remote, reserve, "%-3i %-16.16s %4i %3i %8u %9s %10u %s\n",
			i + 1,
			clients[i].name,
			clients[i].ping,
			clients[i].packetloss,
			clients[i].map_drops,
			rate,
			clients[i].total_drops,
			connected))
		{
			Host_NetDropsPrint(remote, 0, "netdrops: output truncated after %i/%i clients\n", i, count);
			break;
		}
	}
}

/*
==================
Host_Pings_f

DarkPlaces/FTE-style scoreboard ping and packet-loss report.
==================
*/
static void Host_Pings_f (void)
{
	int base;

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (!host_client)
		return;

	for (base = 0; base < svs.maxclients; base += PINGS_CHUNK_CLIENTS)
	{
		char line[1024];
		int i;
		int end = q_min(base + PINGS_CHUNK_CLIENTS, svs.maxclients);

		if (base == 0)
			q_strlcpy(line, "pingplreport", sizeof(line));
		else
			q_snprintf(line, sizeof(line), "pingplreport2 %i", base);

		for (i = base; i < end; i++)
		{
			char pair[32];
			client_t *client = &svs.clients[i];
			int ping = Host_ClientPingMS(client);
			int packetloss = (client->spawned && client->netconnection) ? NET_QSocketGetPacketLoss(client->netconnection) : 0;

			q_snprintf(pair, sizeof(pair), " %i %i", ping, packetloss);
			q_strlcat(line, pair, sizeof(line));
		}

		q_strlcat(line, "\n", sizeof(line));
		if (host_client->message.cursize + (int)strlen(line) + 2 < host_client->message.maxsize)
		{
			MSG_WriteByte(&host_client->message, svc_stufftext);
			MSG_WriteString(&host_client->message, line);
		}

		if (!Host_ClientIsQSSM(host_client))
			break;	/* only QSS-M clients know the pingplreport2 continuation chunks */
	}
}

/*
===============================================================================

SERVER TRANSITIONS

===============================================================================
*/

/*
======================
Host_Map_f

handle a
map <servername>
command from the console.  Active clients are kicked off.
======================
*/
static void Host_Map_f (void)
{
	int		i;
	char	name[MAX_QPATH], *p;

	if (Cmd_Argc() < 2)	//no map name given
	{
		if (cls.state == ca_dedicated)
		{
			if (sv.active)
				Con_Printf ("Current map: %s\n", sv.name);
			else
				Con_Printf ("Server not active\n");
		}
		else if (cls.state == ca_connected)
		{
			char   mapPath[MAX_OSPATH];
			int    h;
			qofs_t fsize = -1;

			q_snprintf(mapPath, sizeof(mapPath), "maps/%s.bsp", cl.mapname);
			fsize = COM_OpenFile(mapPath, &h, NULL);
			if (h != -1)
				COM_CloseFile(h);

			if (fsize > 0)
				Con_Printf("Current map: %s ( %s ) - ^m%.1f MB^m\n",
					cl.levelname,
					cl.mapname,
					(float)fsize / (1024.0f * 1024.0f));
			else
				Con_Printf("Current map: %s ( %s )\n", cl.levelname, cl.mapname);
		}
		else
		{
			Con_Printf ("map <levelname>: start a new server\n");
		}
		return;
	}

	if (cmd_source != src_command)
		return;

	cls.demonum = -1;		// stop demo loop in case this fails

	CL_Disconnect ();
	Host_ShutdownServer(false);

	if (key_dest == key_menu)
		M_ToggleMenu(0);	//ask the menu to hide itself so we don't get pooped by our poor tracking of input state on the next line.
	else
		key_dest = key_game;			// remove console or menu
	if (cls.state != ca_dedicated)
		IN_UpdateGrabs();
	SCR_BeginLoadingPlaque ();

	svs.serverflags = 0;			// haven't completed an episode yet
	q_strlcpy (name, Cmd_Argv(1), sizeof(name));
	// remove (any) trailing ".bsp" from mapname -- S.A.
	p = strrchr(name, '.');
	if (p != NULL)
	{
		if (strcmp(p, ".bsp") == 0)
			*p = '\0';
	}

	if (cls.state != ca_dedicated) // woods -- try to download map
	{
		char mapPath[MAX_QPATH];

		q_snprintf(mapPath, sizeof(mapPath), "maps/%s.bsp", name);

		if (!COM_FileExists(mapPath, NULL))
		{
			Con_Printf("\nmap ^m%s^m not found\n\n", name);

			Cmd_ExecuteString(va("download %s.bsp", name), src_command);

			if (!COM_FileExists(mapPath, NULL))
			{
				Con_Printf("\nfailed to download map ^m%s^m\n\n", name);
			}
		}
	}

	PR_SwitchQCVM(&sv.qcvm);
	SV_SpawnServer (name);
	PR_SwitchQCVM(NULL);
	if (!sv.active)
		return;

	if (cls.state != ca_dedicated)
	{
		memset (cls.spawnparms, 0, MAX_MAPSTRING);
		for (i = 2; i < Cmd_Argc(); i++)
		{
			q_strlcat (cls.spawnparms, Cmd_Argv(i), MAX_MAPSTRING);
			q_strlcat (cls.spawnparms, " ", MAX_MAPSTRING);
		}

		Cmd_ExecuteString ("connect local", src_command);
	}
}

/*
======================
Host_Randmap_f

Loads a random map from the "maps" list.
======================
*/
static void Host_Randmap_f (void)
{
	int	i, randlevel, numlevels;
	filelist_item_t	*level;

	if (cmd_source != src_command)
		return;

	for (level = extralevels, numlevels = 0; level; level = level->next)
		numlevels++;

	if (numlevels == 0)
	{
		Con_Printf ("no maps\n");
		return;
	}

	randlevel = (rand() % numlevels);

	for (level = extralevels, i = 0; level; level = level->next, i++)
	{
		if (i == randlevel)
		{
			Con_Printf ("Starting map %s...\n", level->name);
			Cbuf_AddText (va("map %s\n", level->name));
			return;
		}
	}
}

/*
==================
Host_AutoLoad -- woods #autoload (iw)
==================
*/
static qboolean Host_AutoLoad(void)
{
	if (!sv_autoload.value || !sv.lastsave[0] || svs.maxclients != 1 || !svs.clients ||
		!svs.clients[0].active || !svs.clients[0].spawned || !sv_player || sv_player->free ||
		cl.intermission)
		return false;

	if (sv_autoload.value < 2.f)
	{
		if (!SCR_ModalMessage("Load last save? (y/n)", 0.f))
		{
			sv.lastsave[0] = '\0';
			return false;
		}
	}
	else if (sv_autoload.value < 3.f && sv_player->v.health > 0.f)
		return false;

	sv.autoloading = true;
	Con_Printf("Autoloading...\n");
	Cbuf_AddText(va("load \"%s\"\n", sv.lastsave));
	Cbuf_Execute();

	if (sv.autoloading)
	{
		sv.autoloading = false;
		Con_Printf("Autoload failed!\n");
		return false;
	}

	return true;
}

/*
==================
Host_Changelevel_f

Goes to a new map, taking all clients along
==================
*/
static void Host_Changelevel_f (void)
{
	char	level[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("changelevel <levelname> : continue game on a new level\n");
		return;
	}
	if (!sv.active || cls.demoplayback)
	{
		Con_Printf ("Only the server may changelevel\n");
		return;
	}

	/*//johnfitz -- check for client having map before anything else // woods disable this for SV_SpawnServer protection (added) #mapchangeprotect
	q_snprintf (level, sizeof(level), "maps/%s.bsp", Cmd_Argv(1));
	if (!COM_FileExists(level, NULL))
		Host_Error ("cannot find map %s", level);
	//johnfitz*/

	q_strlcpy(level, Cmd_Argv(1), sizeof(level)); // woods #autoload (iw)
	if (!strcmp(sv.name, level) && Host_AutoLoad())
		return;

	key_dest = key_game;	// remove console or menu
	if (cls.state != ca_dedicated)
		IN_UpdateGrabs();	// -- S.A.

	PR_SwitchQCVM(&sv.qcvm);
	SV_SaveSpawnparms ();
	SV_SpawnServer (level);
	PR_SwitchQCVM(NULL);
	// also issue an error if spawn failed -- O.S.
	if (!sv.active)
		Host_Error ("cannot run map %s", level);
}

/*
==================
Host_Restart_f

Restarts the current server for a dead player
==================
*/
static void Host_Restart_f (void)
{
	char	mapname[MAX_QPATH];

	if (cls.demoplayback)
		return;
	if (cmd_source != src_command)
		return;

	if (Host_AutoLoad()) // woods #autoload (iw)
		return;

	if (!sv.active)
	{
		if (*sv.name)
			Cmd_ExecuteString(va("map \"%s\"\n", sv.name), src_command);
		return;
	}
	q_strlcpy (mapname, sv.name, sizeof(mapname));	// mapname gets cleared in spawnserver
	PR_SwitchQCVM(&sv.qcvm);
	SV_SpawnServer (mapname);
	PR_SwitchQCVM(NULL);
	if (!sv.active)
		Host_Error ("cannot restart map %s", mapname);
}

/*
==================
Host_Reconnect_f

This command causes the client to wait for the signon messages again.
This is sent just before a server changes levels

for compatibility with quakeworld et al, we also allow this as a user-command to reconnect to the last server we tried, but we can only reliably do that when we're not already connected
==================
*/
void Host_Reconnect_Con_f (void)
{
	extern char	lastmphost[NET_NAMELEN]; // woods #connectlast
	qboolean Host_GetLastServer(char *name, size_t namesize);
	char fallback[NET_NAMELEN];
	const char *target = NULL;

	CL_Disconnect_f();
	cls.demonum = -1;		// stop demo loop in case this fails
	if (cls.demoplayback)
	{
		CL_StopPlayback ();
		CL_Disconnect ();
	}

	// ignore local: reconnect should target the last network server
	if (*lastmphost && q_strcasecmp(lastmphost, "local") && q_strcasecmp(lastmphost, "localhost"))
		target = lastmphost;
	else if (Host_GetLastServer(fallback, sizeof(fallback)))
		target = fallback;

	if (!target || !CL_BeginConnect(target))
		Con_Printf("reconnect failed\n");
}
static void Host_Reconnect_Sv_f (void)
{
	if (cls.demoplayback)	// cross-map demo playback fix from Baker
		return;

	if ((cl_autodemo.value == 3 || cl_autodemo.value == 4) && cls.demorecording) // woods #autodemo
		Cbuf_AddText("stop\n");

	SCR_BeginLoadingPlaque ();
	cl.protocol_dpdownload = false;
	cls.signon = 0;		// need new connection messages
}

static void Host_Lightstyle_f (void)
{
	CL_UpdateLightstyle(atoi(Cmd_Argv(1)), Cmd_Argv(2));
}

char	lastcattempt[NET_NAMELEN]; // woods verbose connection info
extern char	lastmphost[NET_NAMELEN]; // woods - connected server address // woods #connectlast (Qrack)

/*
===============
Host_GetLastServer
===============
*/
qboolean Host_GetLastServer(char *name, size_t namesize)
{
	if (!name || !namesize)
		return false;

	if (!serverlist)
	{
		Con_Printf("No server connection history.\n");
		return false;
	}

	q_strlcpy(name, serverlist->name, namesize);
	return true;
}

/*
===============
Host_ConnectToLastServer_f // woods #connectlast (Qrack)
===============
*/
void Host_ConnectToLastServer_f (void) // woods #connectlast (Qrack)
{
	char name[NET_NAMELEN];

	if (!Host_GetLastServer(name, sizeof(name)))
		return;

retry:
	if (cls.state == ca_disconnected)
		Cbuf_AddText(va("connect \"%s\"\n", name));
	else
	{
		CL_Disconnect();
		if (cls.state == ca_disconnected)//if a server crash; can create an endless loop error here CLIENTS arent disconnected just in limbo. :(
			goto retry;
	}
}

qboolean Valid_Port(const char* address) // woods #connectfilter
{
	char host[MAX_SERVER_ADDRESS_LEN];
	int port;

	return ParseServerAddress(address, host, sizeof(host), &port);
}

static qboolean Host_ValidWebSocketAddress(const char *address)
{
	const char *authority;
	size_t schemelen;

	schemelen = NET_WebSocketSchemeLength(address, NULL);
	if (!schemelen)
		return false;
	authority = address + schemelen;

	/* The direct ICE WebSocket transport currently connects at the root path. */
	if (!*authority || strpbrk(authority, "/?#"))
		return false;

	return ((Valid_Domain(authority) || Valid_IP(authority)) && Valid_Port(authority));
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
static void Host_Connect_f (void)
{
	char	name[NET_NAMELEN];
	portpingprobe_status_t probe_status;
	qboolean is_local;

	q_strlcpy(name, Cmd_Argv(1), sizeof(name));

	cls.demonum = -1;		// stop demo loop in case this fails
	if (cls.demoplayback)
	{
		CL_StopPlayback ();
		CL_Disconnect ();
	}

	if (!q_strcasecmp(Cmd_Argv(1), "last")) // woods #connectlast (Qrack)
		Host_ConnectToLastServer_f();
	else
	{
		is_local = !q_strcasecmp(name, "local") || !q_strcasecmp(name, "localhost");
		if ((((Valid_Domain(name)) || (Valid_IP(name))) && (Valid_Port(name))) ||
			Host_ValidWebSocketAddress(name) || is_local) // woods #connectfilter -- avoid client lockup if possible
		{
			strcpy(lastcattempt, name); // woods verbose connection info
			if (CL_BeginConnect(name))
			{
				probe_status = NET_PortPingProbe_GetStatus();
				if (probe_status != PORTPINGPROBE_PROBING && probe_status != PORTPINGPROBE_ABORT)
					mpservertime = SDL_GetTicks64(); // woods #servertime
			}
		}
		else
		{
			Con_Printf("\naddress is ^mnot^m a valid ip, domain name, or port\n\n");
			return;
		}
	}
}


/*
===============================================================================

LOAD / SAVE GAME

===============================================================================
*/

#define	SAVEGAME_EXTENDED_HEADER	"// QuakeSpasm extended savegame"

static savedata_t		save_data;
static qboolean			save_pending;
static SDL_Thread		*save_thread;
static SDL_mutex		*save_mutex;
static SDL_cond			*save_finished_condition;
static SDL_cond			*save_pending_condition;
static qboolean			save_report_done;

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current
===============
*/
void Host_SavegameComment (char text[SAVEGAME_COMMENT_LENGTH + 1])
{
	int		i;
	char	kills[20];
	char	*p;

	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
		text[i] = ' ';
	text[SAVEGAME_COMMENT_LENGTH] = '\0';

	i = (int) strlen(cl.levelname);
	if (i > 22) i = 22;
	memcpy (text, cl.levelname, (size_t)i);

// Remove CR/LFs from level name to avoid broken saves, e.g. with autumn_sp map:
// https://celephais.net/board/view_thread.php?id=60452&start=3666
	while ((p = strchr(text, '\n')) != NULL)
		*p = ' ';
	while ((p = strchr(text, '\r')) != NULL)
		*p = ' ';

	sprintf (kills,"kills:%3i/%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
	memcpy (text+22, kills, strlen(kills));

// convert space to _ to make stdio happy
	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
	{
		if (text[i] == ' ')
			text[i] = '_';
	}
}

static void Host_InvalidateSave(const char* relname) // woods #autoload (iw)
{
	if (!strcmp(sv.lastsave, relname))
		sv.lastsave[0] = '\0';
}

static void Host_CheckSaveResult (void)
{
	int abort = SDL_AtomicGet (&save_data.abort);

	if (abort)
	{
		if (sv.lastsave[0])
		{
			sv.lastsave[0] = '\0';
			if (abort < 0)
				Con_Printf ("Save error.\n");
		}
		save_report_done = false;
	}
	else if (save_report_done)
	{
		save_report_done = false;
		Con_Printf ("done.\n");
	}
}

static void Host_AbortSave (void)
{
	SDL_AtomicCAS (&save_data.abort, 0, 1);
	save_report_done = false;
}

void Host_ShutdownSave (void)
{
	if (!save_mutex)
		return;

	SDL_LockMutex (save_mutex);
	while (save_pending)
		SDL_CondWait (save_finished_condition, save_mutex);
	save_pending = true;
	save_data.file = NULL;
	SDL_CondSignal (save_pending_condition);
	SDL_UnlockMutex (save_mutex);

	SDL_WaitThread (save_thread, NULL);
	save_thread = NULL;

	SDL_DestroyCond (save_finished_condition);
	save_finished_condition = NULL;

	SDL_DestroyCond (save_pending_condition);
	save_pending_condition = NULL;

	SDL_DestroyMutex (save_mutex);
	save_mutex = NULL;

	SaveData_Clear (&save_data);
}

void Host_WaitForSaveThread (void)
{
	if (!save_mutex)
		return;

	SDL_LockMutex (save_mutex);
	while (save_pending)
		SDL_CondWait (save_finished_condition, save_mutex);
	SDL_UnlockMutex (save_mutex);

	Host_CheckSaveResult ();
}

qboolean Host_IsSaving (void)
{
	qboolean saving;

	if (!save_mutex)
		return false;

	SDL_LockMutex (save_mutex);
	saving = save_pending;
	SDL_UnlockMutex (save_mutex);

	if (saving)
		return true;

	Host_CheckSaveResult ();

	return false;
}

static void Host_WriteSavegameExtendedData (savedata_t *save)
{
	int	i;

	fputs("/*\n" SAVEGAME_EXTENDED_HEADER "\n", save->file);
	fprintf(save->file, "sv.coop %g\n", save->coop);
	fprintf(save->file, "sv.deathmatch %g\n", save->deathmatch);
	for (i = MAX_LIGHTSTYLES_VANILLA; i < MAX_LIGHTSTYLES; i++)
	{
		if (save->lightstyles[i])
			fprintf (save->file, "sv.lightstyles %i \"%s\"\n", i, save->lightstyles[i]);
	}
	for (i = 1; i < MAX_MODELS; i++)
	{
		if (save->model_precache[i])
			fprintf (save->file, "sv.model_precache %i \"%s\"\n", i, save->model_precache[i]);
	}
	for (i = 1; i < MAX_SOUNDS; i++)
	{
		if (save->sound_precache[i])
			fprintf (save->file, "sv.sound_precache %i \"%s\"\n", i, save->sound_precache[i]);
	}
	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (save->particle_precache[i])
			fprintf (save->file, "sv.particle_precache %i \"%s\"\n", i, save->particle_precache[i]);
	}

	fprintf (save->file, "sv.serverflags %i\n", save->serverflags);
	for (i = NUM_BASIC_SPAWN_PARMS ; i < NUM_TOTAL_SPAWN_PARMS ; i++)
	{
		if (save->spawn_parms[i])
			fprintf (save->file, "spawnparm %i \"%f\"\n", i+1, save->spawn_parms[i]);
	}

	fprintf(save->file, "*/\n");
}

static int Host_BackgroundSave (void *param)
{
	savedata_t	*save = (savedata_t *) param;

	while (true)
	{
		edict_t		*ed;
		int			i;
		qboolean	abort = false;

		SDL_LockMutex (save_mutex);
		while (!save_pending)
			SDL_CondWait (save_pending_condition, save_mutex);
		SDL_UnlockMutex (save_mutex);

		if (!save->file)
			break;

		PR_SwitchQCVM (&sv.qcvm);
		SaveData_WriteHeader (save);
		if (SDL_AtomicGet (&save->abort))
			abort = true;
		for (i = 0, ed = save->edicts; !abort && i < save->num_edicts; i++, ed = NEXT_EDICT (ed))
		{
			ED_WriteSave (save, ed);
			if (SDL_AtomicGet (&save->abort))
				abort = true;
		}
		if (!abort)
		{
			fprintf (save->file, "// %d edicts\n", save->num_edicts);
			Host_WriteSavegameExtendedData (save);
			if (fflush (save->file))
			{
				SDL_AtomicCAS (&save->abort, 0, -1);
				abort = true;
			}
		}
		PR_SwitchQCVM (NULL);

		if (fclose (save->file) && !abort)
		{
			SDL_AtomicCAS (&save->abort, 0, -1);
			abort = true;
		}
		save->file = NULL;
		if (abort)
			Sys_remove (save->path);

		SDL_LockMutex (save_mutex);
		save_pending = false;
		SDL_CondSignal (save_finished_condition);
		SDL_UnlockMutex (save_mutex);
	}

	return 0;
}

static void Host_InitSaveThread (void)
{
	save_mutex = SDL_CreateMutex ();
	save_finished_condition = SDL_CreateCond ();
	save_pending_condition = SDL_CreateCond ();
	if (!save_mutex || !save_finished_condition || !save_pending_condition)
		Sys_Error ("Host_InitSaveThread: %s", SDL_GetError ());
	SaveData_Init (&save_data);
	save_thread = SDL_CreateThread (Host_BackgroundSave, "SaveThread", &save_data);
	if (!save_thread)
		Sys_Error ("Host_InitSaveThread: %s", SDL_GetError ());
}

static qboolean Host_ParseSavegameModeValue (const char *line, const char *key, float *value)
{
	const char	*text;
	char		*end;
	double		parsed;
	float		mode;
	size_t		keylen = strlen(key);

	if (strncmp(line, key, keylen) || line[keylen] != ' ')
		return false;

	text = line + keylen + 1;
	while (*text == ' ' || *text == '\t')
		text++;
	if (!*text || *text == '\r' || *text == '\n')
		return false;

	errno = 0;
	parsed = strtod(text, &end);
	if (errno || end == text || !isfinite(parsed) ||
		parsed < (double)INT_MIN || parsed >= (double)INT_MAX)
		return false;

	mode = (float)parsed;
	if (!isfinite(mode) || mode < (float)INT_MIN || mode >= (float)INT_MAX)
		return false;

	while (*end == ' ' || *end == '\t' || *end == '\r')
		end++;
	if (*end != '\n' && *end != '\0')
		return false;

	*value = mode;
	return true;
}

static void Host_LoadgameModeCvars (const char *data)
{
	const char	*ext = NULL;
	const char	*next = data;
	size_t		headerlen = strlen(SAVEGAME_EXTENDED_HEADER);
	float		saved_coop;
	float		saved_deathmatch;
	qboolean	have_saved_coop = false;
	qboolean	have_saved_deathmatch = false;

	while ((next = strstr(next, SAVEGAME_EXTENDED_HEADER)) != NULL)
	{
		const char *after = next + headerlen;
		qboolean comment_header =
			(next >= data + 3 && !strncmp(next - 3, "/*\n", 3)) ||
			(next >= data + 4 && !strncmp(next - 4, "/*\r\n", 4));

		if (comment_header)
		{
			if (*after == '\r')
				after++;
			if (*after == '\n')
				ext = after + 1;
		}
		next += headerlen;
	}

	while (ext && *ext && strncmp(ext, "*/", 2))
	{
		if (Host_ParseSavegameModeValue(ext, "sv.coop", &saved_coop))
			have_saved_coop = true;
		else if (Host_ParseSavegameModeValue(ext, "sv.deathmatch", &saved_deathmatch))
			have_saved_deathmatch = true;

		ext = strchr(ext, '\n');
		if (ext)
			ext++;
	}

	if (have_saved_deathmatch)
		Cvar_SetValueQuick(&deathmatch, saved_deathmatch);
	if (have_saved_coop)
		Cvar_SetValueQuick(&coop, saved_coop);
}

/*
===============
Host_Savegame_f
===============
*/
static void Host_Savegame_f (void)
{
	
	char	relname[MAX_OSPATH]; // woods #autoload (iw)
	char	name[MAX_OSPATH];
	char	dir[MAX_OSPATH];
	const char	*skipnotify;
	FILE	*f;
	int	i;

	if (cmd_source != src_command)
		return;

	if (!sv.active)
	{
		Con_Printf ("Not playing a local game.\n");
		return;
	}

	if (sv.nomonsters) // woods #nomonsters (ironwail)
	{
		Con_Printf("Can't save when using \"nomonsters\".\n");
		return;
	}

	if (cl.intermission)
	{
		Con_Printf ("Can't save in intermission.\n");
		return;
	}

	if (svs.maxclients != 1)
	{
		Con_Printf ("Can't save multiplayer games.\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Con_Printf ("save <savename> : save a game\n");
		return;
	}

	if (strstr(Cmd_Argv(1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active && (svs.clients[i].edict->v.health <= 0) )
		{
			Con_Printf ("Can't savegame with a dead player\n");
			return;
		}
	}


	q_strlcpy(relname, Cmd_Argv(1), sizeof(relname)); // woods #autoload (iw)
	COM_AddExtension(relname, ".sav", sizeof(relname));

	q_snprintf(dir, sizeof(dir), "%s/saves", com_gamedir); // woods - Create saves directory if it doesn't exist
	Sys_mkdir(dir);
	if (!q_strncasecmp (relname, "autosave/", 9) || !q_strncasecmp (relname, "autosave\\", 9))
	{
		q_snprintf(dir, sizeof(dir), "%s/saves/autosave", com_gamedir);
		Sys_mkdir(dir);
	}

	q_snprintf(name, sizeof(name), "%s/saves/%s", com_gamedir, relname); // woods - save to saves subdirectory
	skipnotify = (Cmd_Argc () < 3 || atof (Cmd_Argv (2))) ? "" : "[skipnotify]";
	Con_SafePrintf("%sSaving game to ", skipnotify);
	Con_LinkPrintf(name, "%s%s", skipnotify, relname);
	Con_SafePrintf("%s...\n", skipnotify);

	if (!strcmp (relname, sv.lastsave) && Host_IsSaving ())
	{
		Host_AbortSave ();
		SDL_LockMutex (save_mutex);
		while (save_pending)
			SDL_CondWait (save_finished_condition, save_mutex);
		SDL_UnlockMutex (save_mutex);
	}

	Host_WaitForSaveThread ();

	PR_SwitchQCVM(&sv.qcvm);
	SaveData_Fill (&save_data);
	PR_SwitchQCVM(NULL);

	f = fopen (name, "w");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	SDL_LockMutex (save_mutex);
	q_strlcpy (save_data.path, name, sizeof (save_data.path));
	save_data.file = f;
	SDL_AtomicSet (&save_data.abort, 0);
	save_report_done = !skipnotify[0];
	save_pending = true;
	SDL_CondSignal (save_pending_condition);
	SDL_UnlockMutex (save_mutex);

	SCR_ShowSaving ();
	q_strlcpy(sv.lastsave, relname, sizeof(sv.lastsave));
}

/*
===============
Host_Loadgame_f
===============
*/
static void Host_Loadgame_f (void)
{
	static char	*start;
	
	char	name[MAX_OSPATH];
	char	relname[MAX_OSPATH]; // woods #autoload (iw)
	char	mapname[MAX_QPATH];
	char	saved_gamedir[MAX_QPATH];
	char	current_gamedir[MAX_OSPATH];
	float	time, tfloat;
	const char	*data;
	const char	*game;
	int	i;
	edict_t	*ent;
	int	entnum;
	int	version;
	float	spawn_parms[NUM_TOTAL_SPAWN_PARMS];

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("load <savename> : load a game\n");
		return;
	}

	if (strstr(Cmd_Argv(1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	if (nomonsters.value) // woods #nomonsters (ironwail)
	{
		Con_Warning("\"%s\" disabled automatically.\n", nomonsters.name);
		Cvar_SetValueQuick(&nomonsters, 0.f);
	}

	cls.demonum = -1;		// stop demo loop in case this fails

	q_strlcpy(relname, Cmd_Argv(1), sizeof(relname)); // woods #autoload (iw)
	COM_AddExtension(relname, ".sav", sizeof(relname));
	Host_WaitForSaveThread ();

// we can't call SCR_BeginLoadingPlaque, because too much stack space has
// been used.  The menu calls it before stuffing loadgame command
//	SCR_BeginLoadingPlaque ();

	// First try loading from saves directory
	q_snprintf(name, sizeof(name), "%s/saves/%s", com_gamedir, relname); // woods #autoload (iw)
	start = (char*)COM_LoadMallocFile_TextMode_OSPath(name, NULL);

	if (start == NULL) // If not found, try loading from game directory, legacy
	{
		q_snprintf(name, sizeof(name), "%s/%s", com_gamedir, relname); // woods #autoload (iw)
		start = (char*) COM_LoadMallocFile_TextMode_OSPath(name, NULL);
	}

	// avoid leaking if the previous Host_Loadgame_f failed with a Host_Error
	if (start == NULL)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		Host_InvalidateSave(relname); // woods #autoload (iw)
		SCR_EndLoadingPlaque ();
		return;
	}
	Con_SafePrintf("Loading game from ");
	Con_LinkPrintf(name, "%s", relname);
	Con_SafePrintf("...\n");

	data = start;
	data = COM_ParseIntNewline (data, &version);
	if (version == SAVEGAME_VERSION_KEX)
	{
		data = COM_ParseStringNewline (data);	// Kex rerelease saves store the gamedir after the version.
		q_strlcpy(saved_gamedir, com_token, sizeof(saved_gamedir));
		if (!COM_GameDirMatches(saved_gamedir))
		{
			game = COM_GetGameNames(false);
			if (!*game)
				game = COM_GetGameNames(true);
			q_strlcpy(current_gamedir, game, sizeof(current_gamedir));

			free (start);
			start = NULL;
			Con_Printf("ERROR: save is for gamedir \"%s\", current gamedir is \"%s\".\n",
				saved_gamedir[0] ? saved_gamedir : "(unknown)", current_gamedir);
			Con_Printf("Use \"game %s\" before loading this save.\n",
				saved_gamedir[0] ? saved_gamedir : GAMENAME);
			SCR_EndLoadingPlaque ();
			return;
		}
		Con_SafePrintf("Loading remaster save.\n");
	}
	else if (version != SAVEGAME_VERSION)
	{
		free (start);
		start = NULL;
		if (sv.autoloading) // woods #autoload (iw)
			Con_Printf("ERROR: Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);
		else
			Host_Error("Savegame is version %i, not %i", version, SAVEGAME_VERSION);
		Host_InvalidateSave(relname);
		SCR_EndLoadingPlaque ();
		return;
	}
	data = COM_ParseStringNewline (data);
	for (i = 0; i < NUM_BASIC_SPAWN_PARMS; i++)
		data = COM_ParseFloatNewline (data, &spawn_parms[i]);
	for (; i < NUM_TOTAL_SPAWN_PARMS; i++)
		spawn_parms[i] = 0;
// this silliness is so we can load 1.06 save files, which have float skill values
	data = COM_ParseFloatNewline(data, &tfloat);
	current_skill = (int)(tfloat + 0.1);
	Cvar_SetValue ("skill", (float)current_skill);

	data = COM_ParseStringNewline (data);
	q_strlcpy (mapname, com_token, sizeof(mapname));
	data = COM_ParseFloatNewline (data, &time);

// Note: calling CL_Disconnect instead of CL_Disconnect_f to avoid stopping the music
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);

	// These cvars affect entity spawning, so restore them before rebuilding the map.
	Host_LoadgameModeCvars(data);

	PR_SwitchQCVM(&sv.qcvm);
	SV_SpawnServer (mapname);

	if (!sv.active)
	{
		PR_SwitchQCVM(NULL);
		free (start);
		start = NULL;
		SCR_EndLoadingPlaque ();
		Con_Printf ("Couldn't load map\n");
		return;
	}
	sv.paused = true;		// pause until all clients connect
	sv.loadgame = true;

// load the light styles
	for (i = 0; i < MAX_LIGHTSTYLES_VANILLA; i++)
	{
		data = COM_ParseStringNewline (data);
		sv.lightstyles[i] = (const char *)Hunk_Strdup (com_token, "lightstyles");
	}
	for (; i < MAX_LIGHTSTYLES; i++)
		sv.lightstyles[i] = NULL;

// load the edicts out of the savegame file
	entnum = -1;		// -1 is the globals
	while (*data)
	{
		while (*data == ' ' || *data == '\r' || *data == '\n')
			data++;
		if (data[0] == '/' && data[1] == '*' && (data[2] == '\r' || data[2] == '\n'))
		{	//looks like an extended saved game
			char *end;
			const char *ext;
			ext = data+2;
			while ((end = (char *)strchr(ext, '\n')))
			{
				*end = 0;
				ext = COM_Parse(ext);
				if (!strcmp(com_token, "sv.lightstyles"))
				{
					int idx;
					ext = COM_Parse(ext);
					idx = atoi(com_token);
					ext = COM_Parse(ext);
					if (idx >= 0 && idx < MAX_LIGHTSTYLES)
					{
						if (*com_token)
							sv.lightstyles[idx] = (const char *)Hunk_Strdup (com_token, "lightstyles");
						else
							sv.lightstyles[idx] = NULL;
					}
				}
				else if (!strcmp(com_token, "sv.model_precache"))
				{
					int idx;
					ext = COM_Parse(ext);
					idx = atoi(com_token);
					ext = COM_Parse(ext);
					if (idx >= 1 && idx < MAX_MODELS)
					{
						sv.model_precache[idx] = (const char *)Hunk_Strdup (com_token, "model_precache");
						sv.models[idx] = Mod_ForName (sv.model_precache[idx], idx==1);
						//if (idx == 1)
						//	sv.worldmodel = sv.models[idx];
					}
				}
				else if (!strcmp(com_token, "sv.sound_precache"))
				{
					int idx;
					ext = COM_Parse(ext);
					idx = atoi(com_token);
					ext = COM_Parse(ext);
					if (idx >= 1 && idx < MAX_SOUNDS)
						sv.sound_precache[idx] = (const char *)Hunk_Strdup (com_token, "sound_precache");
				}
				else if (!strcmp(com_token, "sv.particle_precache"))
				{
					int idx;
					ext = COM_Parse(ext);
					idx = atoi(com_token);
					ext = COM_Parse(ext);
					if (idx >= 1 && idx < MAX_PARTICLETYPES)
						sv.particle_precache[idx] = (const char *)Hunk_Strdup (com_token, "particle_precache");
				}
				else if (!strcmp(com_token, "sv.serverflags") || !strcmp(com_token, "svs.serverflags"))
				{
					int fl;
					ext = COM_Parse(ext);
					fl = atoi(com_token);
					svs.serverflags = fl;
				}
				else if (!strcmp(com_token, "spawnparm"))
				{
					int idx;
					ext = COM_Parse(ext);
					idx = atoi(com_token);
					ext = COM_Parse(ext);
					if (idx >= 1 && idx <= NUM_TOTAL_SPAWN_PARMS)
						spawn_parms[idx-1] = atof(com_token);
				}
				*end = '\n';
				ext = end+1;
			}
		}

		data = COM_Parse(data);
		if (!com_token[0])
			break;		// end of file
		if (strcmp(com_token,"{"))
		{
			Host_Error ("First token isn't a brace");
		}

		if (entnum == -1)
		{	// parse the global vars
			data = ED_ParseGlobals (data, version == SAVEGAME_VERSION_KEX);
		}
		else
		{	// parse an edict
			ent = EDICT_NUM(entnum);
			if (entnum < qcvm->num_edicts) {
				SV_UnlinkEdict(ent);
				ent->free = false;
				memset (&ent->v, 0, qcvm->progs->entityfields * 4);
			}
			else {
				memset (ent, 0, qcvm->edict_size);
				ent->baseline = nullentitystate;
			}
			data = ED_ParseEdict (data, ent);

		// link it into the bsp tree
			if (!ent->free)
				SV_LinkEdict (ent, false);
		}

		entnum++;
	}

	// Free edicts allocated during map loading but no longer used after restoring saved game state
	for (i = entnum; i < qcvm->num_edicts; i++)
		ED_Free(EDICT_NUM(i));

	qcvm->num_edicts = entnum;
	qcvm->time = time;
	sv.autosave.time = time;

	free (start);
	start = NULL;

	for (i = 0; i < NUM_TOTAL_SPAWN_PARMS; i++)
		svs.clients->spawn_parms[i] = spawn_parms[i];

	PR_SwitchQCVM(NULL);

	q_strlcpy(sv.lastsave, relname, sizeof(sv.lastsave)); // woods #autoload (iw)

	if (cls.state != ca_dedicated)
	{
		CL_EstablishConnection ("local");
	}

	if (cls.state != ca_dedicated)
		IN_UpdateGrabs(); // QSS-M adaptation of upstream quickload input refresh
}

//============================================================================

/*
======================
Host_Name_f
======================
*/
static void Host_Name_f (void)
{
	char	newName[32];
	int a, b, c;	// JPG 1.05 - ip address logging  // woods for #iplog
	qboolean truncated = false;

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\n\"name\" is \"%s\"\n\n", cl_name.string);

		char final_string[MAXCMDLINE];
		q_snprintf(final_string, sizeof(final_string), "name \"%s\"", cl_name.string);

		if (edit_line < 0 || edit_line >= CMDLINES) // Ensure edit_line is within bounds
		{
			Con_Printf("\nedit line index out of bounds.\n\n");
		return;
	}

		key_lines[edit_line][0] = ']'; // Prompt character
		key_lines[edit_line][1] = '\0'; // Null terminate

		q_snprintf(key_lines[edit_line] + 1, MAXCMDLINE - 1, "%s", final_string);

		key_linepos = (int)strlen(key_lines[edit_line]); // Set key_linepos to the end of the line

		// Make sure the console is open for editing
		if (key_dest != key_console)
			key_dest = key_console;

		return;
	}

	if (Cmd_Argc () == 2)
	{
		if (strlen(Cmd_Argv(1)) > 15)
			truncated = true;
		q_strlcpy(newName, Cmd_Argv(1), sizeof(newName));
	}
	else
	{
		if (strlen(Cmd_Args()) > 15)
			truncated = true;
		q_strlcpy(newName, Cmd_Args(), sizeof(newName));
	}

	newName[15] = 0;	// client_t structure actually says name[32].

	// JPG 3.02 - remove bad characters // woods for #iplog
	for (a = 0; newName[a]; a++)
	{
		if (newName[a] == 10)
			newName[a] = ' ';
		else if (newName[a] == 13)
			newName[a] += 128;
	}

	if (cmd_source != src_client)
	{
		if (truncated && Q_strcmp(cl_name.string, newName) == 0)
		{
			Con_Printf("\n\"name\" remains \"%s\" (truncated to 15 characters)\n\n", newName);
			return;
		}
		else if (Q_strcmp(cl_name.string, newName) == 0)
		{
			return;
		}

		// Check if this is the first time setting the name (default is "player")
		const char* default_name = "player";
		qboolean is_first_time = (Q_strcmp(cl_name.string, default_name) == 0 && Q_strcmp(newName, default_name) != 0);

		// woods #namehistory -- if the name we're changing from was never
		// recorded (e.g. set via a config/Cvar_Set that bypassed this path),
		// back-fill it before logging the new name so the history stays honest.
		// Skip the "player" default; it's the uninitialized state, not a chosen name.
		if (!is_first_time && cl_name.string[0] &&
			Q_strcmp(cl_name.string, default_name) != 0 &&
			!NameHistory_Find(cl_name.string, NULL))
			NameHistory_Add (cl_name.string);

		Cvar_Set ("name", newName);
		NameHistory_Add (newName); // woods #namehistory

		// Only print the message if it's not the first time setting the name
		if (!is_first_time)
		{
		Con_Printf("\n\"name\" changed to \"%s\"", newName);
		if (truncated)
			Con_Printf(" (truncated to 15 characters)");
		Con_Printf("\n\n");
	}
	}
	else
		SV_UpdateInfo((host_client-svs.clients)+1, "name", newName);

	if (cmd_source == src_client && host_client) // woods #dupnames
		SV_CheckDuplicateNames(host_client);

	// JPG 1.05 - log the IP address woods for #iplog  (log the IP address)
	if (cls.state == ca_connected && !cls.demoplayback)
		if (sscanf(NET_QSocketGetMaskedAddressStringForDisplay(net_activeSockets), "%d.%d.%d", &a, &b, &c) == 3)
			IPLog_Add((a << 16) | (b << 8) | c, newName);
}

/*
===============
Host_Name_Backup_f // woods #smartafk backup the name externally to a text file for possible crash event
===============
*/
void Host_Name_Backup_f(void)
{
	FILE* f;

	char	name[MAX_OSPATH];
	char str[24];

	q_snprintf(name, sizeof(name), "%s/id1/backups", com_basedir); //  create backups folder if not there
	Sys_mkdir(name);

	sprintf(str, "name");

	// dedicated servers initialize the host but don't parse and set the
	// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{
		f = fopen(va("%s/id1/backups/%s.txt", com_basedir, str), "w");

		if (!f)
		{
			Con_Printf("Couldn't write backup config.cfg.\n");
			return;
		}

		fprintf(f, "%s", afk_name);
	
		fclose(f);
	}
}

/*
===============
Host_Name_Load_Backup_f // woods #smartafk load that backup name if AFK in name at startup, and clear it
===============
*/
void Host_Name_Load_Backup_f(void)
{
	char buffer[30];

	FILE* f;

		f = fopen(va("%s/id1/backups/name.txt", com_basedir), "r");

		if (f == NULL) // lets not load backup
		{
			//Con_Printf("no AFK backup to restore from"); //no file means it was deleted normally
			return;
		}

		while (fgets(buffer, sizeof(buffer), f) != NULL)
		{
			Cvar_Set("name", buffer);
		}

		fclose(f);
}

static void Host_Say(qboolean teamonly)
{
#define DED_CHAT_COLOR_ON  "\x1d"
#define DED_CHAT_COLOR_OFF "\x1e"
	int		j;
	client_t	*client;
	client_t	*save;
	const char	*p;
	char		text[MAXCMDLINE], *p2;
	qboolean	quoted;
	qboolean	fromServer = false;
	int		sender_slot;

	if (cmd_source == src_command)
	{
		if (cls.state != ca_dedicated)
		{
			Cmd_ForwardToServer ();
			return;
		}
		fromServer = true;
		teamonly = false;
	}

	if (Cmd_Argc () < 2)
		return;

	save = host_client;
	sender_slot = fromServer ? -1 : (int)(save - svs.clients);

	p = Cmd_Args();
// remove quotes if present
	quoted = false;
	if (*p == '\"')
	{
		p++;
		quoted = true;
	}

	if (!fromServer)
	{
		const char *s = p;
		const char *modname_arg = NULL;

		while (q_isspace((unsigned char)*s))
			s++;

		if (*s == '/' || *s == '!')
		{
			s++;
			if (!q_strncasecmp(s, "modvote", 7) && (s[7] == '\0' || s[7] == '\"' || q_isspace((unsigned char)s[7])))
				modname_arg = s + 7;
		}

		if (modname_arg)
		{
			while (q_isspace((unsigned char)*modname_arg))
				modname_arg++;
			Host_Modvote_Core(save, modname_arg, true);
			return;
		}
	}
// turn on color set 1
	if (!fromServer)
	{
		if (teamplay.value && teamonly) // JPG - added () for mm2
			q_snprintf(text, sizeof(text), "\001(%s): %s", save->name, p);
		else
			q_snprintf(text, sizeof(text), "\001%s: %s", save->name, p);
	}
	else
	{
		if (sv_adminnick.string[0] != '\0') // woods (darkpaces) #adminnick
			q_snprintf(text, sizeof(text), "\001%s: %s", sv_adminnick.string, p);
		else
			q_snprintf(text, sizeof(text), "\001<%s> %s", hostname.string, p);
	}

// check length & truncate if necessary
	j = (int) strlen(text);
	if (j >= (int) sizeof(text) - 1)
	{
		text[sizeof(text) - 2] = '\n';
		text[sizeof(text) - 1] = '\0';
	}
	else
	{
		p2 = text + j;
		while ((const char *)p2 > (const char *)text &&
			(p2[-1] == '\r' || p2[-1] == '\n' || (p2[-1] == '\"' && quoted)) )
		{
			if (p2[-1] == '\"' && quoted)
				quoted = false;
			p2[-1] = '\0';
			p2--;
		}
		p2[0] = '\n';
		p2[1] = '\0';
	}

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client || !client->active || !client->spawned)
			continue;
		if (teamplay.value && teamonly && client->edict->v.team != save->edict->v.team)
			continue;
		if (Host_ServerChatIgnored(client, sender_slot))
			continue;
		host_client = client;
		SV_ClientPrintf("%s", text);
	}
	host_client = save;

	if (cls.state == ca_dedicated)
	{
		char dedtext[MAXCMDLINE + 8];
		const char *src = &text[1];
		const char *msg = strstr (src, ": ");
		const char *trail = src + strlen(src);
		size_t prefix_len, msg_len, suffix_len;

		if (msg)
			msg += 2;
		else if (src[0] == '<')
		{
			msg = strstr (src, "> ");
			if (msg)
				msg += 2;
		}

		if (!msg)
		{
			Sys_Printf ("%s", src);
			return;
		}

		if (trail > msg && trail[-1] == '\n')
			trail--;

		prefix_len = (size_t)(msg - src);
		msg_len = (size_t)(trail - msg);
		suffix_len = strlen (trail);

		if (prefix_len + strlen(DED_CHAT_COLOR_ON) + msg_len +
			strlen(DED_CHAT_COLOR_OFF) + suffix_len + 1 > sizeof(dedtext))
		{
			Sys_Printf ("%s", src);
			return;
		}

		memcpy (dedtext, src, prefix_len);
		memcpy (dedtext + prefix_len, DED_CHAT_COLOR_ON, strlen(DED_CHAT_COLOR_ON));
		memcpy (dedtext + prefix_len + strlen(DED_CHAT_COLOR_ON), msg, msg_len);
		memcpy (dedtext + prefix_len + strlen(DED_CHAT_COLOR_ON) + msg_len,
			DED_CHAT_COLOR_OFF, strlen(DED_CHAT_COLOR_OFF));
		memcpy (dedtext + prefix_len + strlen(DED_CHAT_COLOR_ON) + msg_len +
			strlen(DED_CHAT_COLOR_OFF), trail, suffix_len + 1);
		Sys_Printf ("%s", dedtext);
	}
}

static void Host_Say_f(void)
{
	Host_Say(false);
}

static void Host_Say_f2(void) // woods chat shortcuts
{
	const char* p;
	p = Cmd_Args();
	char text[MAXCMDLINE];
	sprintf(text, "say %s", p);
	Cmd_ExecuteString(text, src_command);
}

static void Host_Say_Team_f(void)
{
	Host_Say(true);
}

static void Host_Say_Team_f2(void) // woods chat shortcuts
{
	const char* p;
	p = Cmd_Args();
	char text[MAXCMDLINE];
	sprintf(text, "say_team %s", p);
	Cmd_ExecuteString(text, src_command);
}

static Uint32 lastLikeTime = 0; // stores the last time nothing was available to #like

static void Host_Like_f (void) // woods #like
{
	if (cl.maxclients <= 1 || cls.demoplayback) // mp or coop only
		return;
	
	Uint32 currentTime = SDL_GetTicks(); // get the current time in milliseconds

	if (currentTime - lastLikeTime < 1000) // 1 second has passed, avoid spamming
		return;

	char text[MAXCMDLINE];
	const char *verb = Key_IsShortcutModifierDown() ? "loves" : "likes";
	qboolean saved_ctrlpressed;

	if (strstr(cl.lastchat, ": ^mlikes^m") || strstr(cl.lastchat, ": ^mloves^m")) // no intinite likes
		return;

	if (cl.lastchat[0] == '\0')
	{
		Con_Printf("\nnothing to like\n");
		lastLikeTime = currentTime;
		return;
	}

	// Check if we're liking a team chat message (contains parentheses)
	qboolean is_team_message = (strchr(cl.lastchat, '(') != NULL && strchr(cl.lastchat, ')') != NULL);

	if (is_team_message) {
		q_snprintf(text, sizeof(text), "say_team %s %s", verb, cl.lastchat + 1);
	}
	else {
		q_snprintf(text, sizeof(text), "say %s %s", verb, cl.lastchat + 1);
	}

	// The platform shortcut modifier selects "loves" here; don't let the generic say modifier flip the channel.
	saved_ctrlpressed = ctrlpressed;
	ctrlpressed = false;
	Cmd_ExecuteString(text, src_command);
	ctrlpressed = saved_ctrlpressed;
}

static void Host_Tell_f(void) // modified by woods to accept wildcards, status #s like proquake identify #tell+
{
	int		i, j;
	client_t	*client;
	client_t	*save;
	const char	*p;
	char		text[MAXCMDLINE], *p2;
	qboolean	quoted;
	char name[MAX_SCOREBOARDNAME];
	qboolean ambiguous = false;

	if (sv.active)
	{
		i = Host_FindServerPlayerSlot(Cmd_Argv(1), name, sizeof(name), &ambiguous);
		if (i < 0)
		{
			Con_Printf("%s player\n", ambiguous ? "No unique match for that" : "No such");
			return;
		}
	}
	else
	{
		i = Host_FindLocalPlayerSlot(Cmd_Argv(1), name, sizeof(name), &ambiguous);
		if (i < 0)
		{
			Con_Printf("%s player\n", ambiguous ? "No unique match for that" : "No such");
			return;
		}
		S_LocalSound("misc/talk.wav"); // woods #tell+
	}

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (Cmd_Argc () < 3)
		return;

	p = Cmd_Args();
	p = strremove((char*)p, (char*)Cmd_Argv(1)); // the msg only -- use strremove to get rid of name
	
// remove quotes if present
	quoted = false;
	if (*p == '\"')
	{
		p++;
		quoted = true;
	}

	q_snprintf (text, sizeof(text), "dm [%s]:%s", host_client->name,p);

// check length & truncate if necessary
	j = (int) strlen(text);
	if (j >= (int) sizeof(text) - 1)
	{
		text[sizeof(text) - 2] = '\n';
		text[sizeof(text) - 1] = '\0';
	}
	else
	{
		p2 = text + j;
		while ((const char *)p2 > (const char *)text &&
			(p2[-1] == '\r' || p2[-1] == '\n' || (p2[-1] == '\"' && quoted)) )
		{
			if (p2[-1] == '\"' && quoted)
				quoted = false;
			p2[-1] = '\0';
			p2--;
		}
		p2[0] = '\n';
		p2[1] = '\0';
	}

	save = host_client;
	qboolean recipient_found = false; // woods #tell+
	qboolean recipient_blocked = false;
	int sender_slot = (int)(save - svs.clients);
	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active || !client->spawned)
			continue;
		if (q_strcasecmp(client->name, name))
			continue;
		if (Host_ServerChatIgnored(client, sender_slot))
		{
			recipient_blocked = true;
			break;
		}
		host_client = client;
		SV_ClientPrintf("%s", text);
		recipient_found = true; // woods #tell+
		break;
	}
	host_client = save;

	if (recipient_found || recipient_blocked) // woods #tell+
		SV_ClientPrintf("\nmessage successfully sent to %s\n\n", name); // send confirmation to sender
	else
		SV_ClientPrintf("\nno such player named %s\n\n", name); // inform sender that recipient wasn't found
}

/*
==================
Host_Color_f
==================
*/
static void Host_Color_f(void)
{
	const char *top, *bottom;
	char xt[4];
	char xb[4];
	char combined[14];
	int t = rand() % 13 + 1; // woods for random colors
	int b = rand() % 13 + 1; // woods for random colors

	if (Cmd_Argc() == 1)
	{
		Con_Printf("\n");
		Con_Printf ("\"%s\" is \"%s %s\" (top bottom)\n", Cmd_Argv(0), CL_PLColours_ToString(CL_PLColours_Parse(cl_topcolor.string)), CL_PLColours_ToString(CL_PLColours_Parse(cl_bottomcolor.string)));
		Con_Printf("\n");
		Con_Printf ("traditional quake colors\n");
		Con_Printf("\n");
		Con_Printf("0 - white         7 - peach\n");
		Con_Printf("1 - brown         8 - purple\n");
		Con_Printf("2 - light blue    9 - magenta\n");
		Con_Printf("3 - green        10 - tan\n");
		Con_Printf("4 - red          11 - green\n");
		Con_Printf("5 - orange       12 - yellow\n");
		Con_Printf("6 - gold         13 - blue\n");
		Con_Printf("\n");
		Con_Printf("hex rgb values (google: rgb color picker)\n");
		Con_Printf("\n");
		Con_Printf("0x66ff00 - bright green\n");
		Con_Printf("0xff00cd - bright magenta\n");
		Con_Printf("0xffff00 - bright yellow\n");
		Con_Printf("\n");
		return;
	}

	if (Cmd_Argc() == 2)
		top = bottom = Cmd_Argv(1);
	else
	{
		top = Cmd_Argv(1);
		bottom = Cmd_Argv(2);
	}

	if (Cmd_Argc() == 2) // just x
		if ((!strcmp(Cmd_Argv(1), "x")) || (!strcmp(Cmd_Argv(1), "y")) || (!strcmp(Cmd_Argv(1), "n"))) // woods for random colors
		{
			sprintf(xt, "%i", t);
			top = xt;
			sprintf(xb, "%i", b);
			bottom = xt;
		}

	if (Cmd_Argc() == 3)
		{ 
			if ((!strcmp(Cmd_Argv(1), "x")) || (!strcmp(Cmd_Argv(1), "y")) || (!strcmp(Cmd_Argv(1), "n"))) // woods for random colors
			{
				sprintf(xt, "%i", t);
				top = xt;
				bottom = Cmd_Argv(2);
			}
	
			if ((!strcmp(Cmd_Argv(2), "x")) || (!strcmp(Cmd_Argv(2), "y")) || (!strcmp(Cmd_Argv(2), "n"))) // woods for random colors
			{
				top = Cmd_Argv(1);
				sprintf(xb, "%i", b);
				bottom = xb;
			}

			if (((!strcmp(Cmd_Argv(1), "x")) || (!strcmp(Cmd_Argv(1), "y")) || (!strcmp(Cmd_Argv(1), "n")))
				&& ((!strcmp(Cmd_Argv(2), "x")) || (!strcmp(Cmd_Argv(2), "y")) || (!strcmp(Cmd_Argv(2), "n")))) // woods for random colors
			{
				sprintf(xt, "%i", t);
				top = xt;
				sprintf(xb, "%i", b);
				bottom = xb;
			}
	}

	if (cmd_source != src_client)
	{
		Cvar_Set ("topcolor", top);
		Cvar_Set ("bottomcolor", bottom);

		if (((!strcmp(Cmd_Argv(1), "x")) || (!strcmp(Cmd_Argv(1), "y")) || (!strcmp(Cmd_Argv(1), "n")))
			|| ((!strcmp(Cmd_Argv(2), "x")) || (!strcmp(Cmd_Argv(2), "y")) || (!strcmp(Cmd_Argv(2), "n"))))
		{
			if (cls.state == ca_connected)
			{
				sprintf(combined, "color %s %s", top, bottom);
				Cmd_ExecuteString(combined, src_command);
			}
		}
		else
			if (cls.state == ca_connected)
				Cmd_ForwardToServer ();
		return;
	}

	SV_UpdateInfo((host_client - svs.clients)+1, "topcolor", top);
	SV_UpdateInfo((host_client - svs.clients)+1, "bottomcolor", bottom);
}

/*
==================
Host_Kill_f
==================
*/
static void Host_Kill_f (void)
{
	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (sv_player->v.health <= 0)
	{
		SV_ClientPrintf ("Can't suicide -- already dead!\n");
		return;
	}

	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG(sv_player);
	PR_ExecuteProgram (pr_global_struct->ClientKill);
}

/*
==================
Host_Pause_f
==================
*/
static void Host_Pause_f (void)
{
//ericw -- demo pause support (inspired by MarkV)
	if (cls.demoplayback)
	{
		cls.demopaused = !cls.demopaused;
		cl.paused = cls.demopaused;
		return;
	}

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}
	if (!pausable.value)
		SV_ClientPrintf ("Pause not allowed.\n");
	else
	{
		sv.paused ^= 1;

		if (sv.paused)
		{
			SV_BroadcastPrintf ("%s paused the game\n", PR_GetString(sv_player->v.netname));
		}
		else
		{
			SV_BroadcastPrintf ("%s unpaused the game\n",PR_GetString(sv_player->v.netname));
		}

	// send notification to all clients
		MSG_WriteByte (&sv.reliable_datagram, svc_setpause);
		MSG_WriteByte (&sv.reliable_datagram, sv.paused);
	}
}

//===========================================================================

/*
==================
Host_PreSpawn_f
==================
*/
static void Host_PreSpawn_f (void)
{
	if (cmd_source != src_client)
	{
		Con_Printf ("prespawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("prespawn not valid -- already spawned\n");
		return;
	}

	//will start splurging out prespawn data
	host_client->sendsignon = 2;
	host_client->signonidx = 0;
}

/*
==================
Host_Spawn_f
==================
*/
static void Host_Spawn_f (void)
{
	int		i;
	client_t	*client;
	edict_t	*ent;

	if (cmd_source != src_client)
	{
		Con_Printf ("spawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("Spawn not valid -- already spawned\n");
		return;
	}

	host_client->knowntoqc = true;
	host_client->lastmovetime = qcvm->time;
// run the entrance script
	if (sv.loadgame)
	{	// loaded games are fully inited already
		// if this is the last client to be connected, unpause
		sv.paused = false;
	}
	else
	{
		// set up the edict
		ent = host_client->edict;

		memset (&ent->v, 0, qcvm->progs->entityfields * 4);
		ent->v.colormap = NUM_FOR_EDICT(ent);
		ent->v.team = (host_client->colors & 15) + 1;
		ent->v.netname = PR_SetEngineString(host_client->name);

		// copy spawn parms out of the client_t
		for (i=0 ; i< NUM_BASIC_SPAWN_PARMS ; i++)
			(&pr_global_struct->parm1)[i] = host_client->spawn_parms[i];
		if (pr_checkextension.value)
		{	//extended spawn parms
			for ( ; i< NUM_TOTAL_SPAWN_PARMS ; i++)
			{
				ddef_t *g = ED_FindGlobal(va("parm%i", i+1));
				if (g)
					qcvm->globals[g->ofs] = host_client->spawn_parms[i];
			}
		}
		// call the spawn function
		pr_global_struct->time = qcvm->time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		PR_ExecuteProgram (pr_global_struct->ClientConnect);

		if ((Sys_DoubleTime() - NET_QSocketGetTime(host_client->netconnection)) <= qcvm->time)
			Sys_Printf ("%s entered the game\n", host_client->name);

		PR_ExecuteProgram (pr_global_struct->PutClientInServer);
	}

// send all current names, colors, and frag counts
	SZ_Clear (&host_client->message);

// send time of update
	MSG_WriteByte (&host_client->message, svc_time);
	MSG_WriteFloat (&host_client->message, qcvm->time);
	if (host_client->protocol_pext2 & PEXT2_PREDINFO)
		MSG_WriteShort(&host_client->message, (host_client->lastmovemessage&0xffff));

	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
	//	if (!client->knowntoqc)
	//		continue;
		if (host_client->protocol_pext2 & PEXT2_PREDINFO)
		{
			MSG_WriteByte (&host_client->message, svc_stufftext);
			MSG_WriteString (&host_client->message, va("//fui %i \"%s\"\n", i, client->userinfo));
		}
		
		{
			MSG_WriteByte (&host_client->message, svc_updatename);
			MSG_WriteByte (&host_client->message, i);
			MSG_WriteString (&host_client->message, client->name);
			MSG_WriteByte (&host_client->message, svc_updatecolors);
			MSG_WriteByte (&host_client->message, i);
			MSG_WriteByte (&host_client->message, client->colors);
		}

		MSG_WriteByte (&host_client->message, svc_updatefrags);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteShort (&host_client->message, client->old_frags);
	}

// send all current light styles
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		//CL_ClearState should have cleared all lightstyles, so don't send irrelevant ones
		if (sv.lightstyles[i])
		{
			if (i > 0xff)
			{
				MSG_WriteByte (&host_client->message, svc_stufftext);
				MSG_WriteString (&host_client->message, va("//ls %i \"%s\"\n", i, sv.lightstyles[i]));
			}
			else
			{
				MSG_WriteByte (&host_client->message, svc_lightstyle);
				MSG_WriteByte (&host_client->message, i);
				MSG_WriteString (&host_client->message, sv.lightstyles[i]);
			}
		}
	}

//
// send some stats
//
	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_TOTALSECRETS);
	MSG_WriteLong (&host_client->message, pr_global_struct->total_secrets);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_TOTALMONSTERS);
	MSG_WriteLong (&host_client->message, pr_global_struct->total_monsters);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_SECRETS);
	MSG_WriteLong (&host_client->message, pr_global_struct->found_secrets);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_MONSTERS);
	MSG_WriteLong (&host_client->message, pr_global_struct->killed_monsters);

//
// send a fixangle
// Never send a roll angle, because savegames can catch the server
// in a state where it is expecting the client to correct the angle
// and it won't happen if the game was just loaded, so you wind up
// with a permanent head tilt
	ent = EDICT_NUM( 1 + (host_client - svs.clients) );
	MSG_WriteByte (&host_client->message, svc_setangle);
	for (i = 0; i < 2; i++)
		if (sv.loadgame)
			MSG_WriteAngle (&host_client->message, ent->v.v_angle[i], sv.protocolflags );
		else
			MSG_WriteAngle (&host_client->message, ent->v.angles[i], sv.protocolflags );
	MSG_WriteAngle (&host_client->message, 0, sv.protocolflags );

	if (!(host_client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		SV_WriteClientdataToMessage (host_client, &host_client->message);

	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 3);
	host_client->sendsignon = PRESPAWN_FLUSH; // woods - switch to enum
}

/*
==================
Host_Begin_f
==================
*/
static void Host_Begin_f (void)
{
	if (cmd_source != src_client)
	{
		Con_Printf ("begin is not valid from the console\n");
		return;
	}

	host_client->spawned = true;
	Host_Modvote_PrintJoinMotd(host_client);
}

//===========================================================================

/*
==================
Host_Kick_f

Kicks a user off of the server
==================
*/
static void Host_Kick_f (void)
{
	const char	*who;
	const char	*message = NULL;
	client_t	*save;
	int		i;
	qboolean	byNumber = false;

	if (cmd_source != src_client)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
	}
	else if (pr_global_struct->deathmatch)
		return;

	save = host_client;

	if (Cmd_Argc() > 2 && Q_strcmp(Cmd_Argv(1), "#") == 0)
	{
		i = Q_atof(Cmd_Argv(2)) - 1;
		if (i < 0 || i >= svs.maxclients)
			return;
		if (!svs.clients[i].active)
			return;
		host_client = &svs.clients[i];
		byNumber = true;
	}
	else
	{
		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (!host_client->active)
				continue;
			if (q_strcasecmp(host_client->name, Cmd_Argv(1)) == 0)
				break;
		}
	}

	if (i < svs.maxclients)
	{
		if (cmd_source != src_client)
			if (cls.state == ca_dedicated)
				who = "Console";
			else
				who = cl_name.string;
		else
			who = save->name;

		// can't kick yourself!
		if (host_client == save)
			return;

		if (Cmd_Argc() > 2)
		{
			message = COM_Parse(Cmd_Args());
			if (byNumber)
			{
				message++;			// skip the #
				while (*message == ' ')		// skip white space
					message++;
				message += strlen(Cmd_Argv(2));	// skip the number
			}
			while (*message && *message == ' ')
				message++;
		}
		if (message)
			SV_ClientPrintf ("Kicked by %s: %s\n", who, message);
		else
			SV_ClientPrintf ("Kicked by %s\n", who);
		SV_DropClient (false);
	}

	host_client = save;
}

/*
===============================================================================

DEBUGGING TOOLS

===============================================================================
*/

static void Give_ConfirmPrint(const char* fmt, ...) // woods #give+
{
	va_list ap;
	char    buf[256];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (cmd_source == src_client)
		SV_ClientPrintf("%s\n", buf);
	else
		Con_Printf("%s\n", buf);
}

/*
==================
Host_Give_f -- woods #give+
==================
*/
typedef enum give_ammo_e
{
	GIVE_AMMO_SHELLS,
	GIVE_AMMO_NAILS,
	GIVE_AMMO_ROCKETS,
	GIVE_AMMO_CELLS,
	GIVE_AMMO_LAVA_NAILS,
	GIVE_AMMO_MULTI_ROCKETS,
	GIVE_AMMO_PLASMA_CELLS
} give_ammo_t;

static qboolean Give_IsIntegerArg (const char *s)
{
	if (!s || !*s)
		return false;

	if (*s == '-' || *s == '+')
		s++;

	if (!isdigit((unsigned char)*s))
		return false;

	while (*++s)
	{
		if (!isdigit((unsigned char)*s))
			return false;
	}

	return true;
}

static qboolean Give_IsIntegerPrefix (const char *s)
{
	if (!s)
		return false;

	if (*s == '-' || *s == '+')
		s++;

	while (*s)
	{
		if (!isdigit((unsigned char)*s))
			return false;
		s++;
	}

	return true;
}

static qboolean Give_IsDigitWeaponArg (const char *item)
{
	return item && item[1] == '\0' && item[0] >= '0' && item[0] <= '9';
}

static qboolean Give_IsArmorColor (const char *s)
{
	return s && (!q_strcasecmp(s, "red") ||
		!q_strcasecmp(s, "yellow") ||
		!q_strcasecmp(s, "green"));
}

static qboolean Give_IsArmorItem (const char *item)
{
	return item && ((!q_strcasecmp(item, "armor")) ||
		(item[0] == 'a' && item[1] == '\0'));
}

static qboolean Give_ItemAcceptsAmount (const char *item)
{
	if (!item)
		return false;

	if (item[1] == '\0')
	{
		switch (item[0])
		{
		case 's':
		case 'n':
		case 'l':
		case 'r':
		case 'm':
		case 'h':
		case 'c':
		case 'p':
		case 'a':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			return true;
		}
	}

	return !q_strcasecmp(item, "shells") ||
		!q_strcasecmp(item, "nails") ||
		!q_strcasecmp(item, "rockets") ||
		!q_strcasecmp(item, "cells") ||
		!q_strcasecmp(item, "lavanails") ||
		!q_strcasecmp(item, "multirockets") ||
		!q_strcasecmp(item, "plasmacells") ||
		!q_strcasecmp(item, "health") ||
		!q_strcasecmp(item, "shotgun") ||
		!q_strcasecmp(item, "sg") ||
		!q_strcasecmp(item, "supershotgun") ||
		!q_strcasecmp(item, "ssg") ||
		!q_strcasecmp(item, "nailgun") ||
		!q_strcasecmp(item, "ng") ||
		!q_strcasecmp(item, "supernailgun") ||
		!q_strcasecmp(item, "sng") ||
		!q_strcasecmp(item, "grenadelauncher") ||
		!q_strcasecmp(item, "gl") ||
		!q_strcasecmp(item, "rocketlauncher") ||
		!q_strcasecmp(item, "rl") ||
		!q_strcasecmp(item, "lightninggun") ||
		!q_strcasecmp(item, "lg") ||
		!q_strcasecmp(item, "6a") ||
		!q_strcasecmp(item, "proximitygun") ||
		!q_strcasecmp(item, "lasercannon") ||
		!q_strcasecmp(item, "mjolnir") ||
		!q_strcasecmp(item, "lavanailgun") ||
		!q_strcasecmp(item, "lavagun") ||
		!q_strcasecmp(item, "multigrenade") ||
		!q_strcasecmp(item, "multigren") ||
		!q_strcasecmp(item, "multirocket") ||
		!q_strcasecmp(item, "multirock") ||
		!q_strcasecmp(item, "plasmagun") ||
		!q_strcasecmp(item, "plasma") ||
		!q_strcasecmp(item, "weapons");
}

static qboolean Give_ShouldConsumeAmount (const char *item, const char *next)
{
	if (!Give_ItemAcceptsAmount(item) || !Give_IsIntegerArg(next))
		return false;

	if (Give_IsDigitWeaponArg(item) && Give_IsDigitWeaponArg(next))
		return false;

	return true;
}

static void Give_AddItem (unsigned int item)
{
	sv_player->v.items = (unsigned)sv_player->v.items | item;
}

static unsigned int Give_SigilFlagsFromItems (unsigned int sigils)
{
	unsigned int flags = 0;

	if (sigils & IT_SIGIL1)
		flags |= 1u;
	if (sigils & IT_SIGIL2)
		flags |= 2u;
	if (sigils & IT_SIGIL3)
		flags |= 4u;
	if (sigils & IT_SIGIL4)
		flags |= 8u;

	return flags;
}

static void Give_AddSigils (unsigned int sigils)
{
	unsigned int flags = (unsigned int)pr_global_struct->serverflags;

	flags |= Give_SigilFlagsFromItems(sigils);

	pr_global_struct->serverflags = flags;
	svs.serverflags = flags;
}

static void Give_NormalizeVanillaSigilItemBits (void)
{
	const unsigned int sigil_mask = (unsigned)(IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4);
	unsigned int items, sigils;

	if (rogue)
		return;

	items = (unsigned)sv_player->v.items;
	sigils = items & sigil_mask;
	if (!sigils)
		return;

	/* Vanilla sigils are canonical in serverflags.  Older give code could
	   leave them in the float-backed item mask, where high bits can drop
	   lower weapon bits on later item grants. */
	Give_AddSigils(sigils);
	sv_player->v.items = items & ~sigil_mask;
}

static void Give_SetAmmo (give_ammo_t ammo, int amt)
{
	eval_t *val;

	amt = q_max(amt, 0);

	/* Rogue mirrors shells, but splits nails/rockets/cells by current weapon. */
	switch (ammo)
	{
	case GIVE_AMMO_SHELLS:
		if (rogue)
		{
			if ((val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_shells1"))))
				val->_float = amt;
		}
		sv_player->v.ammo_shells = amt;
		break;

	case GIVE_AMMO_NAILS:
		if (rogue)
		{
			if ((val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_nails1"))))
			{
				val->_float = amt;
				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_nails = amt;
			}
			break;
		}
		sv_player->v.ammo_nails = amt;
		break;

	case GIVE_AMMO_ROCKETS:
		if (rogue)
		{
			if ((val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_rockets1"))))
			{
				val->_float = amt;
				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_rockets = amt;
			}
			break;
		}
		sv_player->v.ammo_rockets = amt;
		break;

	case GIVE_AMMO_CELLS:
		if (rogue)
		{
			if ((val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_cells1"))))
			{
				val->_float = amt;
				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_cells = amt;
			}
			break;
		}
		sv_player->v.ammo_cells = amt;
		break;

	case GIVE_AMMO_LAVA_NAILS:
		if ((val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_lava_nails"))))
			val->_float = amt;
		if (sv_player->v.weapon > IT_LIGHTNING)
			sv_player->v.ammo_nails = amt;
		break;

	case GIVE_AMMO_MULTI_ROCKETS:
		if ((val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_multi_rockets"))))
			val->_float = amt;
		if (sv_player->v.weapon > IT_LIGHTNING)
			sv_player->v.ammo_rockets = amt;
		break;

	case GIVE_AMMO_PLASMA_CELLS:
		if ((val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_plasma"))))
			val->_float = amt;
		if (sv_player->v.weapon > IT_LIGHTNING)
			sv_player->v.ammo_cells = amt;
		break;
	}
}

static void Give_SetArmor (int amt, qboolean has_amount, const char *color)
{
	if (color && !has_amount)
		amt = 0;

	amt = q_max(amt, 0);

	if (color && !q_strcasecmp(color, "red"))
	{
		sv_player->v.armortype = 0.8f;
		amt = q_max(amt, 200);
	}
	else if (color && !q_strcasecmp(color, "yellow"))
	{
		sv_player->v.armortype = 0.6f;
		amt = q_max(amt, 150);
	}
	else if (color && !q_strcasecmp(color, "green"))
	{
		sv_player->v.armortype = 0.3f;
		amt = q_max(amt, 100);
	}
	else
	{
		if (amt > 150)
			sv_player->v.armortype = 0.8f;
		else if (amt > 100)
			sv_player->v.armortype = 0.6f;
		else
			sv_player->v.armortype = 0.3f;
	}

	sv_player->v.armorvalue = amt;
	sv_player->v.items = (unsigned)sv_player->v.items & ~(unsigned)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3);
	if (sv_player->v.armortype == 0.8f)
		Give_AddItem(IT_ARMOR3);
	else if (sv_player->v.armortype == 0.6f)
		Give_AddItem(IT_ARMOR2);
	else
		Give_AddItem(IT_ARMOR1);

	SV_StartSound(sv_player, NULL, 0, "items/armor1.wav", 255, 1.0f);
}

static void Give_SetBaseWeapons (int ammo)
{
	Give_AddItem(IT_AXE | IT_SHOTGUN | IT_SUPER_SHOTGUN | IT_NAILGUN | IT_SUPER_NAILGUN |
		IT_GRENADE_LAUNCHER | IT_ROCKET_LAUNCHER | IT_LIGHTNING);
	sv_player->v.weapon = IT_SHOTGUN;
	sv_player->v.weaponframe = 0;
	Give_SetAmmo(GIVE_AMMO_SHELLS, ammo);
	Give_SetAmmo(GIVE_AMMO_NAILS, ammo);
	Give_SetAmmo(GIVE_AMMO_ROCKETS, ammo);
	Give_SetAmmo(GIVE_AMMO_CELLS, ammo);
}

static void Give_SetEverything (void)
{
	if (host_client)
		host_client->give_infinite_ammo = false;

	Give_SetBaseWeapons(999);
	Give_SetAmmo(GIVE_AMMO_SHELLS, 100);
	Give_SetAmmo(GIVE_AMMO_NAILS, 200);
	Give_SetAmmo(GIVE_AMMO_ROCKETS, 100);
	Give_SetAmmo(GIVE_AMMO_CELLS, 200);
	sv_player->v.health = sv_player->v.max_health = 250;
	Give_SetArmor(200, true, "red");
	Give_AddSigils(IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4);
}

static void Give_SetInfiniteAmmoValues (void)
{
	Give_SetAmmo(GIVE_AMMO_SHELLS, 999);
	Give_SetAmmo(GIVE_AMMO_NAILS, 999);
	Give_SetAmmo(GIVE_AMMO_ROCKETS, 999);
	Give_SetAmmo(GIVE_AMMO_CELLS, 999);
	Give_SetAmmo(GIVE_AMMO_LAVA_NAILS, 999);
	Give_SetAmmo(GIVE_AMMO_MULTI_ROCKETS, 999);
	Give_SetAmmo(GIVE_AMMO_PLASMA_CELLS, 999);
	sv_player->v.currentammo = 999;
}

void Host_GiveInfiniteAmmoRefill (client_t *client, edict_t *ent)
{
	edict_t *saved_player;

	if (!client || !client->give_infinite_ammo || !ent || ent->free)
		return;

	saved_player = sv_player;
	sv_player = ent;
	Give_SetInfiniteAmmoValues();
	sv_player = saved_player;
}

static void Give_SetInfinite (void)
{
	if (host_client)
		host_client->give_infinite_ammo = true;

	Give_SetBaseWeapons(999);
	Give_SetInfiniteAmmoValues();
	sv_player->v.health = sv_player->v.max_health = 999;
	Give_SetArmor(999, true, "red");
	Give_AddSigils(IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4);
}

static void Give_SyncCurrentAmmo (void)
{
	if (Host_RunEntityFunction("W_SetCurrentAmmo", sv_player, NULL))
		return;

	switch ((int)sv_player->v.weapon)
	{
	case IT_SHOTGUN:
	case IT_SUPER_SHOTGUN:
		sv_player->v.currentammo = sv_player->v.ammo_shells;
		break;
	case IT_NAILGUN:
	case IT_SUPER_NAILGUN:
	case RIT_LAVA_SUPER_NAILGUN:
		sv_player->v.currentammo = sv_player->v.ammo_nails;
		break;
	case IT_GRENADE_LAUNCHER:
	case IT_ROCKET_LAUNCHER:
	case RIT_MULTI_GRENADE:
	case RIT_MULTI_ROCKET:
		sv_player->v.currentammo = sv_player->v.ammo_rockets;
		break;
	case IT_LIGHTNING:
	case HIT_LASER_CANNON:
	case HIT_MJOLNIR:
		sv_player->v.currentammo = sv_player->v.ammo_cells;
		break;
	case RIT_LAVA_NAILGUN:
		if (rogue)
			sv_player->v.currentammo = sv_player->v.ammo_nails;
		break;
	case RIT_PLASMA_GUN:
		if (rogue)
			sv_player->v.currentammo = sv_player->v.ammo_cells;
		if (hipnotic)
			sv_player->v.currentammo = sv_player->v.ammo_rockets;
		break;
	}
}

static qboolean Give_GrantItem (const char *item, int amt, qboolean has_amount, const char *armor_color)
{
	const char *msg = NULL;

	if (!item || !*item)
		return false;

	if (!q_strcasecmp(item, "6a"))
	{
		if (!hipnotic)
		{
			SV_ClientPrintf("proximity gun (ignored - not Hipnotic)\n");
			return true;
		}
		Give_AddItem(HIT_PROXIMITY_GUN);
		Give_SetAmmo(GIVE_AMMO_ROCKETS, amt);
		msg = va("proximity gun + %d rockets", amt);
		goto DONE;
	}

	if (item[1] == '\0')
	{
		switch (item[0])
		{
		case '0':
			if (!hipnotic)
			{
				SV_ClientPrintf("weapon 0 (ignored - not Hipnotic)\n");
				return true;
			}
			Give_AddItem(HIT_MJOLNIR);
			Give_SetAmmo(GIVE_AMMO_CELLS, amt);
			msg = va("mjolnir + %d cells", amt);
			goto DONE;
		case '1':
			Give_AddItem(IT_AXE);
			msg = "axe";
			goto DONE;
		case '2':
			Give_AddItem(IT_SHOTGUN);
			Give_SetAmmo(GIVE_AMMO_SHELLS, amt);
			msg = va("shotgun + %d shells", amt);
			goto DONE;
		case '3':
			Give_AddItem(IT_SUPER_SHOTGUN);
			Give_SetAmmo(GIVE_AMMO_SHELLS, amt);
			msg = va("super shotgun + %d shells", amt);
			goto DONE;
		case '4':
			Give_AddItem(IT_NAILGUN);
			Give_SetAmmo(GIVE_AMMO_NAILS, amt);
			msg = va("nailgun + %d nails", amt);
			goto DONE;
		case '5':
			Give_AddItem(IT_SUPER_NAILGUN);
			Give_SetAmmo(GIVE_AMMO_NAILS, amt);
			msg = va("super nailgun + %d nails", amt);
			goto DONE;
		case '6':
			Give_AddItem(IT_GRENADE_LAUNCHER);
			Give_SetAmmo(GIVE_AMMO_ROCKETS, amt);
			msg = va("grenade launcher + %d rockets", amt);
			goto DONE;
		case '7':
			Give_AddItem(IT_ROCKET_LAUNCHER);
			Give_SetAmmo(GIVE_AMMO_ROCKETS, amt);
			msg = va("rocket launcher + %d rockets", amt);
			goto DONE;
		case '8':
			Give_AddItem(IT_LIGHTNING);
			Give_SetAmmo(GIVE_AMMO_CELLS, amt);
			msg = va("lightning gun + %d cells", amt);
			goto DONE;
		case '9':
			if (hipnotic)
			{
				Give_AddItem(HIT_LASER_CANNON);
				Give_SetAmmo(GIVE_AMMO_CELLS, amt);
				msg = va("laser cannon + %d cells", amt);
			}
			else
			{
				Give_AddItem(IT_SUPER_LIGHTNING);
				Give_SetAmmo(GIVE_AMMO_CELLS, amt);
				msg = va("weapon 9 + %d cells", amt);
			}
			goto DONE;
		case 's':
			Give_SetAmmo(GIVE_AMMO_SHELLS, amt);
			msg = va("%d shells", amt);
			goto DONE;
		case 'n':
			Give_SetAmmo(GIVE_AMMO_NAILS, amt);
			msg = va("%d nails", amt);
			goto DONE;
		case 'l':
			if (!rogue)
			{
				SV_ClientPrintf("lava nails (ignored - not Rogue)\n");
				return true;
			}
			Give_SetAmmo(GIVE_AMMO_LAVA_NAILS, amt);
			msg = va("%d lava nails", amt);
			goto DONE;
		case 'r':
			Give_SetAmmo(GIVE_AMMO_ROCKETS, amt);
			msg = va("%d rockets", amt);
			goto DONE;
		case 'm':
			if (!rogue)
			{
				SV_ClientPrintf("multi rockets (ignored - not Rogue)\n");
				return true;
			}
			Give_SetAmmo(GIVE_AMMO_MULTI_ROCKETS, amt);
			msg = va("%d multi-rockets", amt);
			goto DONE;
		case 'h':
			sv_player->v.health = amt;
			SV_StartSound(sv_player, NULL, 0, "items/r_item1.wav", 255, 1.0f);
			msg = va("%d health", amt);
			goto DONE;
		case 'c':
			Give_SetAmmo(GIVE_AMMO_CELLS, amt);
			msg = va("%d cells", amt);
			goto DONE;
		case 'p':
			if (!rogue)
			{
				SV_ClientPrintf("plasma cells (ignored - not Rogue)\n");
				return true;
			}
			Give_SetAmmo(GIVE_AMMO_PLASMA_CELLS, amt);
			msg = va("%d plasma cells", amt);
			goto DONE;
		case 'a':
			Give_SetArmor(amt, has_amount, armor_color);
			msg = va("%d armour", (int)sv_player->v.armorvalue);
			goto DONE;
		}
	}

	/*----- Ammo --------------------------------------------------*/
	if (!q_strcasecmp(item, "shells")) { Give_SetAmmo(GIVE_AMMO_SHELLS, amt); msg = va("%d shells", amt); }
	else if (!q_strcasecmp(item, "nails")) { Give_SetAmmo(GIVE_AMMO_NAILS, amt); msg = va("%d nails", amt); }
	else if (!q_strcasecmp(item, "rockets")) { Give_SetAmmo(GIVE_AMMO_ROCKETS, amt); msg = va("%d rockets", amt); }
	else if (!q_strcasecmp(item, "cells")) { Give_SetAmmo(GIVE_AMMO_CELLS, amt); msg = va("%d cells", amt); }

	/*----- Rogue Ammo --------------------------------------------*/
	else if (!q_strcasecmp(item, "lavanails"))
	{
		if (!rogue)
		{
			SV_ClientPrintf("lava nails (ignored - not Rogue)\n");
			return true;
		}
		Give_SetAmmo(GIVE_AMMO_LAVA_NAILS, amt);
		msg = va("%d lava nails", amt);
	}
	else if (!q_strcasecmp(item, "multirockets"))
	{
		if (!rogue)
		{
			SV_ClientPrintf("multi rockets (ignored - not Rogue)\n");
			return true;
		}
		Give_SetAmmo(GIVE_AMMO_MULTI_ROCKETS, amt);
		msg = va("%d multi-rockets", amt);
	}
	else if (!q_strcasecmp(item, "plasmacells"))
	{
		if (!rogue)
		{
			SV_ClientPrintf("plasma cells (ignored - not Rogue)\n");
			return true;
		}
		Give_SetAmmo(GIVE_AMMO_PLASMA_CELLS, amt);
		msg = va("%d plasma cells", amt);
	}

	/*----- Health -----------------------------------------------*/
	else if (!q_strcasecmp(item, "health"))
	{
		sv_player->v.health = sv_player->v.max_health = amt;
		SV_StartSound(sv_player, NULL, 0, "items/r_item1.wav", 255, 1.0f);
		msg = va("%d health", amt);
	}

	/*----- Armour (colour or numeric) ---------------------------*/
	else if (!q_strcasecmp(item, "armor"))
	{
		Give_SetArmor(amt, has_amount, armor_color);
		msg = va("%d armour", (int)sv_player->v.armorvalue);
	}

	/*----- Power-ups --------------------------------------------*/
	else if (!q_strcasecmp(item, "quad"))
	{
		eval_t *val;
		int ofs;

		Give_AddItem(IT_QUAD);
		ofs = ED_FindFieldOffset("super_damage_finished");
		if (ofs >= 0 && (val = GetEdictFieldValue(sv_player, ofs)))
			val->_float = qcvm->time + 30.0f;
		SV_StartSound(sv_player, NULL, 0, "items/damage.wav", 255, 1.0f);
		host_client->powerup_warn_flags |= PWARN_GIVE;
		msg = "Quad Damage";
	}
	else if (!q_strcasecmp(item, "pent") || !q_strcasecmp(item, "666"))
	{
		eval_t *val;
		int ofs;

		Give_AddItem(IT_INVULNERABILITY);
		ofs = ED_FindFieldOffset("invincible_finished");
		if (ofs >= 0 && (val = GetEdictFieldValue(sv_player, ofs)))
			val->_float = qcvm->time + 30.0f;
		SV_StartSound(sv_player, NULL, 0, "items/protect.wav", 255, 1.0f);
		host_client->powerup_warn_flags |= PWARN_GIVE;
		msg = "Pent";
	}
	else if (!q_strcasecmp(item, "ring") || !q_strcasecmp(item, "eyes"))
	{
		eval_t *val;
		int ofs;

		Give_AddItem(IT_INVISIBILITY);
		ofs = ED_FindFieldOffset("invisible_finished");
		if (ofs >= 0 && (val = GetEdictFieldValue(sv_player, ofs)))
			val->_float = qcvm->time + 30.0f;
		SV_StartSound(sv_player, NULL, 0, "items/inv1.wav", 255, 1.0f);
		host_client->powerup_warn_flags |= PWARN_GIVE;
		msg = "Ring";
	}
	else if (!q_strcasecmp(item, "suit") || !q_strcasecmp(item, "biosuit"))
	{
		eval_t *val;
		int ofs;

		Give_AddItem(IT_SUIT);
		ofs = ED_FindFieldOffset("radsuit_finished");
		if (ofs >= 0 && (val = GetEdictFieldValue(sv_player, ofs)))
			val->_float = qcvm->time + 30.0f;
		SV_StartSound(sv_player, NULL, 0, "items/suit.wav", 255, 1.0f);
		host_client->powerup_warn_flags |= PWARN_GIVE;
		msg = "Biosuit";
	}

	/*----- Keys --------------------------------------------------*/
	else if (!q_strcasecmp(item, "keys"))
	{
		Give_AddItem(IT_KEY1 | IT_KEY2);
		msg = "both keys";
	}
	else if (!q_strcasecmp(item, "key1") || !q_strcasecmp(item, "silverkey") || !q_strcasecmp(item, "blueflag"))
	{
		Give_AddItem(IT_KEY1);
		msg = "silver key";
	}
	else if (!q_strcasecmp(item, "key2") || !q_strcasecmp(item, "goldkey") || !q_strcasecmp(item, "redflag"))
	{
		Give_AddItem(IT_KEY2);
		msg = "gold key";
	}

	/*----- Sigils -----------------------------------------------*/
	else if (!q_strcasecmp(item, "sigils") || !q_strcasecmp(item, "runes"))
	{
		Give_AddSigils(IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4);
		msg = "all sigils";
	}
	else if (!q_strcasecmp(item, "sigil1") || !q_strcasecmp(item, "rune1"))
	{
		Give_AddSigils(IT_SIGIL1);
		msg = "sigil 1";
	}
	else if (!q_strcasecmp(item, "sigil2") || !q_strcasecmp(item, "rune2"))
	{
		Give_AddSigils(IT_SIGIL2);
		msg = "sigil 2";
	}
	else if (!q_strcasecmp(item, "sigil3") || !q_strcasecmp(item, "rune3"))
	{
		Give_AddSigils(IT_SIGIL3);
		msg = "sigil 3";
	}
	else if (!q_strcasecmp(item, "sigil4") || !q_strcasecmp(item, "rune4"))
	{
		Give_AddSigils(IT_SIGIL4);
		msg = "sigil 4";
	}

	/*----- Individual Weapons ----------------------------------*/
	else if (!q_strcasecmp(item, "axe"))
	{
		Give_AddItem(IT_AXE);
		msg = "axe";
	}
	else if (!q_strcasecmp(item, "shotgun") || !q_strcasecmp(item, "sg"))
	{
		Give_AddItem(IT_SHOTGUN);
		Give_SetAmmo(GIVE_AMMO_SHELLS, amt);
		msg = va("shotgun + %d shells", amt);
	}
	else if (!q_strcasecmp(item, "supershotgun") || !q_strcasecmp(item, "ssg"))
	{
		Give_AddItem(IT_SUPER_SHOTGUN);
		Give_SetAmmo(GIVE_AMMO_SHELLS, amt);
		msg = va("super shotgun + %d shells", amt);
	}
	else if (!q_strcasecmp(item, "nailgun") || !q_strcasecmp(item, "ng"))
	{
		Give_AddItem(IT_NAILGUN);
		Give_SetAmmo(GIVE_AMMO_NAILS, amt);
		msg = va("nailgun + %d nails", amt);
	}
	else if (!q_strcasecmp(item, "supernailgun") || !q_strcasecmp(item, "sng"))
	{
		Give_AddItem(IT_SUPER_NAILGUN);
		Give_SetAmmo(GIVE_AMMO_NAILS, amt);
		msg = va("super nailgun + %d nails", amt);
	}
	else if (!q_strcasecmp(item, "grenadelauncher") || !q_strcasecmp(item, "gl"))
	{
		Give_AddItem(IT_GRENADE_LAUNCHER);
		Give_SetAmmo(GIVE_AMMO_ROCKETS, amt);
		msg = va("grenade launcher + %d rockets", amt);
	}
	else if (!q_strcasecmp(item, "rocketlauncher") || !q_strcasecmp(item, "rl"))
	{
		Give_AddItem(IT_ROCKET_LAUNCHER);
		Give_SetAmmo(GIVE_AMMO_ROCKETS, amt);
		msg = va("rocket launcher + %d rockets", amt);
	}
	else if (!q_strcasecmp(item, "lightninggun") || !q_strcasecmp(item, "lg"))
	{
		Give_AddItem(IT_LIGHTNING);
		Give_SetAmmo(GIVE_AMMO_CELLS, amt);
		msg = va("lightning gun + %d cells", amt);
	}

	/*----- Hipnotic Weapons -----------------------------------*/
	else if (!q_strcasecmp(item, "proximitygun"))
	{
		if (!hipnotic)
		{
			SV_ClientPrintf("proximity gun (ignored - not Hipnotic)\n");
			return true;
		}
		Give_AddItem(HIT_PROXIMITY_GUN);
		Give_SetAmmo(GIVE_AMMO_ROCKETS, amt);
		msg = va("proximity gun + %d rockets", amt);
	}
	else if (!q_strcasecmp(item, "lasercannon"))
	{
		if (!hipnotic)
		{
			SV_ClientPrintf("laser cannon (ignored - not Hipnotic)\n");
			return true;
		}
		Give_AddItem(HIT_LASER_CANNON);
		Give_SetAmmo(GIVE_AMMO_CELLS, amt);
		msg = va("laser cannon + %d cells", amt);
	}
	else if (!q_strcasecmp(item, "mjolnir"))
	{
		if (!hipnotic)
		{
			SV_ClientPrintf("mjolnir (ignored - not Hipnotic)\n");
			return true;
		}
		Give_AddItem(HIT_MJOLNIR);
		Give_SetAmmo(GIVE_AMMO_CELLS, amt);
		msg = va("mjolnir + %d cells", amt);
	}

	/*----- Rogue Weapons --------------------------------------*/
	else if (!q_strcasecmp(item, "lavanailgun") || !q_strcasecmp(item, "lavagun"))
	{
		if (!rogue)
		{
			SV_ClientPrintf("lava nailgun (ignored - not Rogue)\n");
			return true;
		}
		Give_AddItem(RIT_LAVA_NAILGUN);
		Give_SetAmmo(GIVE_AMMO_LAVA_NAILS, amt);
		msg = va("lava nailgun + %d lava nails", amt);
	}
	else if (!q_strcasecmp(item, "multigrenade") || !q_strcasecmp(item, "multigren"))
	{
		if (!rogue)
		{
			SV_ClientPrintf("multi grenade launcher (ignored - not Rogue)\n");
			return true;
		}
		Give_AddItem(RIT_MULTI_GRENADE);
		Give_SetAmmo(GIVE_AMMO_MULTI_ROCKETS, amt);
		msg = va("multi grenade launcher + %d multi-rockets", amt);
	}
	else if (!q_strcasecmp(item, "multirocket") || !q_strcasecmp(item, "multirock"))
	{
		if (!rogue)
		{
			SV_ClientPrintf("multi rocket launcher (ignored - not Rogue)\n");
			return true;
		}
		Give_AddItem(RIT_MULTI_ROCKET);
		Give_SetAmmo(GIVE_AMMO_MULTI_ROCKETS, amt);
		msg = va("multi rocket launcher + %d multi-rockets", amt);
	}
	else if (!q_strcasecmp(item, "plasmagun") || !q_strcasecmp(item, "plasma"))
	{
		if (!rogue)
		{
			SV_ClientPrintf("plasma gun (ignored - not Rogue)\n");
			return true;
		}
		Give_AddItem(RIT_PLASMA_GUN);
		Give_SetAmmo(GIVE_AMMO_PLASMA_CELLS, amt);
		msg = va("plasma gun + %d plasma cells", amt);
	}

	/*----- Weapons set / Macro ----------------------------------*/
	else if (!q_strcasecmp(item, "weapons"))
	{
		Give_SetBaseWeapons(amt);
		msg = "all weapons";
	}
	else if (!q_strcasecmp(item, "all"))
	{
		Give_SetEverything();
		msg = "EVERYTHING";
	}
	else if (!q_strcasecmp(item, "infinite"))
	{
		Give_SetInfinite();
		msg = "INFINITE";
	}
	else
		return false;

DONE:
	if (msg)
		Give_ConfirmPrint("Gave %s", msg);

	return true;
}

static void Host_Give_f (void)
{
	int i;
	qboolean matched = false;

	if (cmd_source != src_client) { Cmd_ForwardToServer(); return; }
	if (pr_global_struct->deathmatch)                /* no cheats in DM           */
		return;

	if (Cmd_Argc() < 2)
	{
		SV_ClientPrintf("usage: give <item> [amount] [item] [amount] ...\n");
		return;
	}

	Give_NormalizeVanillaSigilItemBits();

	for (i = 1; i < Cmd_Argc(); i++)
	{
		const char *item = Cmd_Argv(i);
		const char *armor_color = NULL;
		qboolean has_amount = false;
		int amt = 999;

		if (Give_IsArmorItem(item))
		{
			int look;
			for (look = i + 1; look < Cmd_Argc(); look++)
			{
				const char *next = Cmd_Argv(look);

				if (!armor_color && Give_IsArmorColor(next))
				{
					armor_color = next;
					continue;
				}
				if (!has_amount && Give_IsIntegerArg(next))
				{
					amt = q_max(atoi(next), 0);
					has_amount = true;
					continue;
				}
				break;
			}
			i = look - 1;
		}
		else if (i + 1 < Cmd_Argc() && Give_ShouldConsumeAmount(item, Cmd_Argv(i + 1)))
		{
			amt = q_max(atoi(Cmd_Argv(++i)), 0);
			has_amount = true;
		}

		if (Give_GrantItem(item, amt, has_amount, armor_color))
			matched = true;
		else
			SV_ClientPrintf("Unknown give item '%s'\n", item);
	}

	if (matched)
		Give_SyncCurrentAmmo();
}

qboolean CompleteGive (const char* partial, void* unused) // woods #give+
{
	const int argc = Cmd_Argc();
	const char *prev = (argc >= 3) ? Cmd_Argv(argc - 2) : NULL;
	const char *prevprev = (argc >= 4) ? Cmd_Argv(argc - 3) : NULL;
	const char *prevprevprev = (argc >= 5) ? Cmd_Argv(argc - 4) : NULL;
	const qboolean prev_is_value =
		(prev && prevprev && Give_ShouldConsumeAmount(prevprev, prev)) ||
		(prev && prevprev && Give_IsArmorItem(prevprev) && Give_IsIntegerArg(prev)) ||
		(prev && prevprev && prevprevprev &&
			Give_IsIntegerArg(prev) && Give_IsArmorColor(prevprev) && Give_IsArmorItem(prevprevprev));

	/* ------------------------------------------------------------------ *
	 *  Complete optional value tokens after any give item.               *
	 * ------------------------------------------------------------------ */
	if (!prev_is_value && prev && Give_IsArmorItem(prev))
	{
		static const char* colours[] = { "red", "yellow", "green" };
		static const char* values[] = { "100", "150", "200", "999" };

		for (size_t i = 0; i < Q_COUNTOF(colours); ++i)
			if (!q_strncasecmp(partial, colours[i], strlen(partial)))
				Con_AddToTabList(colours[i], partial, NULL, NULL);

		for (size_t i = 0; i < Q_COUNTOF(values); ++i)
			if (!q_strncasecmp(partial, values[i], strlen(partial)))
				Con_AddToTabList(values[i], partial, NULL, NULL);

		return true;
	}

	if (!prev_is_value && prev && prevprev && Give_IsArmorColor(prev) && Give_IsArmorItem(prevprev) &&
		(!partial[0] || isdigit((unsigned char)partial[0])))
	{
		static const char* values[] = { "100", "150", "200", "999" };
		for (size_t i = 0; i < Q_COUNTOF(values); ++i)
			if (!q_strncasecmp(partial, values[i], strlen(partial)))
				Con_AddToTabList(values[i], partial, NULL, NULL);

		return true;
	}

	if (!prev_is_value && prev && (!q_strcasecmp(prev, "health") || (prev[0] == 'h' && prev[1] == '\0')))
	{
		static const char* hp[] = { "100", "150", "200", "250", "999" };
		for (size_t i = 0; i < Q_COUNTOF(hp); ++i)
			if (!q_strncasecmp(partial, hp[i], strlen(partial)))
				Con_AddToTabList(hp[i], partial, NULL, NULL);

		return true;
	}

	if (!prev_is_value && prev && Give_ItemAcceptsAmount(prev) && !Give_IsArmorItem(prev) && Give_IsIntegerPrefix(partial))
	{
		if (!q_strncasecmp(partial, "999", strlen(partial)))
			Con_AddToTabList("999", partial, NULL, NULL);

		return true;
	}

	/* ------------------------------------------------------------------ *
	 *  Stage 1 - completing an item token.                               *
	 * ------------------------------------------------------------------ */
	if (argc >= 2)
	{
		static const char* items[] = {
			/* ammo ---------------------------------------------------- */
			"shells", "nails", "rockets", "cells",
			/* rogue ammo ---------------------------------------------- */
			"lavanails", "multirockets", "plasmacells",
			/* health -------------------------------------------------- */
			"health",
			/* armour (colour added in stage 2) ------------------------ */
			"armor",
			/* power‑ups ---------------------------------------------- */
			"quad", "pent", "ring", "suit", "eyes", "666", "biosuit",
			/* keys (aliases allowed) ---------------------------------- */
			"keys", "key1", "key2", "silverkey", "goldkey", "blueflag", "redflag",
			/* runes / sigils ----------------------------------------- */
			"sigils", "sigil1", "sigil2", "sigil3", "sigil4", "rune1", "rune2", "rune3", "rune4",
			/* weapons & macros --------------------------------------- */
			"weapons", "all", "infinite",
			/* individual weapons ------------------------------------- */
			"axe", "shotgun", "sg", "supershotgun", "ssg", "nailgun", "ng", "supernailgun", "sng",
			"grenadelauncher", "gl", "rocketlauncher", "rl", "lightninggun", "lg",
			/* hipnotic weapons --------------------------------------- */
			"proximitygun", "lasercannon", "mjolnir",
			/* rogue weapons ------------------------------------------ */
			"lavanailgun", "lavagun", "multigrenade", "multigren", "multirocket", "multirock", "plasmagun", "plasma",
			/* legacy shorthands -------------------------------------- */
			"s", "n", "r", "c", "h", "l", "m", "p",
			/* legacy digits 0‑9 (weapon numbers & Hipnotic specials) --*/
			"0","1","2","3","4","5","6","7","8","9","6a"
		};

		for (size_t i = 0; i < Q_COUNTOF(items); ++i)
			if (!q_strncasecmp(partial, items[i], strlen(partial)))
				Con_AddToTabList(items[i], partial, NULL, NULL);

		return true;       /* handled */
	}

	return false;   /* no suggestions for other argument counts */
}

qboolean CompleteMapSize (const char *partial, void *unused)
{
	if (Cmd_Argc() == 2)
	{
		Con_AddToTabList("-l", partial, NULL, NULL);
		Con_AddToTabList("list", partial, NULL, NULL);
		Con_AddToTabList("-clearall", partial, NULL, NULL);

		{
			filelist_item_t *level;

			for (level = extralevels; level; level = level->next)
				Con_AddToTabList(level->name, partial, NULL, NULL);
		}

		return true;
	}

	if (Cmd_Argc() >= 3 && Host_MapSize_IsListArg(Cmd_Argv(1)))
	{
		filelist_item_t *level;

		for (level = extralevels; level; level = level->next)
			Con_AddToTabList(level->name, partial, NULL, NULL);

		return true;
	}

	if (Cmd_Argc() >= 2)
	{
		filelist_item_t *level;

		for (level = extralevels; level; level = level->next)
			Con_AddToTabList(level->name, partial, NULL, NULL);

		return true;
	}

	return false;
}

static edict_t	*FindViewthing (void)
{
	int		i;
	edict_t	*e = NULL;

	PR_SwitchQCVM(&sv.qcvm);
	i = qcvm->num_edicts;

	if (i == qcvm->num_edicts)
	{
		for (i=0 ; i<qcvm->num_edicts ; i++)
		{
			e = EDICT_NUM(i);
			if ( !strcmp (PR_GetString(e->v.classname), "viewthing") )
				break;
		}
	}

	if (i == qcvm->num_edicts)
	{
		for (i=0 ; i<qcvm->num_edicts ; i++)
		{
			e = EDICT_NUM(i);
			if ( !strcmp (PR_GetString(e->v.classname), "info_player_start") )
				break;
		}
	}

	if (i == qcvm->num_edicts)
	{
		e = NULL;
		Con_Printf ("No viewthing on map\n");
	}

	PR_SwitchQCVM(NULL);
	return e;
}

/*
==================
Host_Viewmodel_f
==================
*/
static void Host_Viewmodel_f (void)
{
	edict_t	*e;
	qmodel_t	*m;

	e = FindViewthing ();
	if (!e)
		return;

	if (!*Cmd_Argv(1))
		m = NULL;
	else
	{
		m = Mod_ForName (Cmd_Argv(1), false);
		if (!m)
		{
			Con_Printf ("Can't load %s\n", Cmd_Argv(1));
			return;
		}
	}

	PR_SwitchQCVM(&sv.qcvm);
	e->v.modelindex = m?SV_Precache_Model(m->name):0;
	e->v.model = PR_SetEngineString(sv.model_precache[(int)e->v.modelindex]);
	e->v.frame = 0;
	PR_SwitchQCVM(NULL);
}

/*
==================
Host_Viewframe_f
==================
*/
static void Host_Viewframe_f (void)
{
	edict_t	*e;
	int		f;
	qmodel_t	*m;

	e = FindViewthing ();
	if (!e)
		return;
	m = cl.model_precache[(int)e->v.modelindex];
	if (m)
	{
		f = atoi(Cmd_Argv(1));
		if (f < 0)
			f = 0;
		if (f >= m->numframes)
			f = m->numframes - 1;

		e->v.frame = f;
	}
}

static void PrintFrameName (qmodel_t *m, int frame)
{
	aliashdr_t 			*hdr;
	maliasframedesc_t	*pframedesc;

	hdr = (aliashdr_t *)Mod_Extradata (m);
	if (!hdr || m->type != mod_alias)
		return;
	pframedesc = &hdr->frames[frame];

	Con_Printf ("frame %i: %s\n", frame, pframedesc->name);
}

/*
==================
Host_Viewnext_f
==================
*/
static void Host_Viewnext_f (void)
{
	edict_t	*e;
	qmodel_t	*m;

	e = FindViewthing ();
	if (!e)
		return;
	m = cl.model_precache[(int)e->v.modelindex];
	if (m)
	{
		e->v.frame = e->v.frame + 1;
		if (e->v.frame >= m->numframes)
			e->v.frame = m->numframes - 1;

		PrintFrameName (m, e->v.frame);
	}
}

/*
==================
Host_Viewprev_f
==================
*/
static void Host_Viewprev_f (void)
{
	edict_t	*e;
	qmodel_t	*m;

	e = FindViewthing ();
	if (!e)
		return;

	m = cl.model_precache[(int)e->v.modelindex];
	if (m)
	{
		e->v.frame = e->v.frame - 1;
		if (e->v.frame < 0)
			e->v.frame = 0;

		PrintFrameName (m, e->v.frame);
	}
}

/*
===============================================================================

DEMO LOOP CONTROL

===============================================================================
*/

/*
==================
Host_Startdemos_f
==================
*/
static void Host_Startdemos_f (void)
{
	int		i, c;

	if (cls.state == ca_dedicated)
		return;

	c = Cmd_Argc() - 1;
	if (c > MAX_DEMOS)
	{
		Con_Printf ("Max %i demos in demoloop\n", MAX_DEMOS);
		c = MAX_DEMOS;
	}
	//Con_Printf ("%i demo(s) in loop\n", c); // woods don't print this

	for (i = 1; i < c + 1; i++)
		q_strlcpy (cls.demos[i-1], Cmd_Argv(i), sizeof(cls.demos[0]));

	if (!sv.active && cls.demonum != -1 && !cls.demoplayback)
	{
		cls.demonum = 0;
		if (!cl_demoreel.value)
		{  /* QuakeSpasm customization: */
			/* go straight to menu, no CL_NextDemo */
			cls.demonum = -1;
			Cbuf_InsertText("togglemenu 1\n");
			return;
		}
		CL_NextDemo ();
	}
	else
	{
		cls.demonum = -1;
	}
}

/*
==================
Host_Demos_f

Return to looping demos
==================
*/
static void Host_Demos_f (void)
{
	if (cls.state == ca_dedicated)
		return;
	if (cls.demonum == -1)
		cls.demonum = 1;
	CL_Disconnect_f ();
	CL_NextDemo ();
}

/*
==================
Host_Stopdemo_f

Return to looping demos
==================
*/
static void Host_Stopdemo_f (void)
{
	if (cls.state == ca_dedicated)
		return;
	if (!cls.demoplayback)
		return;
	CL_StopPlayback ();
	CL_Disconnect ();
}

/*
==================
Host_Resetdemos
Clear looping demo list (called on game change)
==================
*/
void Host_Resetdemos (void)
{
	memset (cls.demos, 0, sizeof (cls.demos));
	cls.demonum = 0;
}

/*
==========================================================
PROQUAKE FUNCTIONS (JPG 1.05)  -- added for #iplog woods
==========================================================
*/

// used to translate to non-fun characters for identify <name>
char unfun[129] =
"................[]olzeasbt89...."
"........[]......olzeasbt89..[.]."
"aabcdefghijklmnopqrstuvwxyz[.].."
".abcdefghijklmnopqrstuvwxyz[.]..";

// try to find s1 inside of s2
int unfun_match(const char* s1, char* s2) // woods add const
{
	int i;
	for (; *s2; s2++)
	{
		for (i = 0; s1[i] && s2[i]; i++)	// woods -- guard s2[i] too, else a match past s2's NUL reads out of bounds
		{
			if (unfun[s1[i] & 127] != unfun[s2[i] & 127])
				break;
		}
		if (!s1[i])
			return true;
	}
	return false;
}

static void Send_Identify_Deferred (void *param) // woods -- #identify+ -- runs on the main thread via Host_DeferCall
{
	char* lastconnected = (char*)param;

	unsigned char* ch; // woods dequake
	for (ch = (unsigned char*)lastconnected; *ch; ch++)
		*ch = dequake[*ch];

	Con_Printf("\n");
	Cbuf_AddText(va("identify %s\n\n", lastconnected));
	SDL_free(lastconnected); // free the SDL_strdup copy made when scheduling
}

/* JPG 1.05
==================
Host_Identify_f

Print all known names for the specified player's ip address
==================
*/

void Host_Identify_f(void)
{
	int i;
	int a, b, c;
	char name[16];

	if (!iplog_size)
	{
		Con_Printf("IP logging not available\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: identify or <player number or name>\n\n");
		
		if (lastconnected[0] != '\0') // woods -- #identify+
		{ 
			Cbuf_AddText("status\n\n");
			Con_Printf("identifying the ^mlast connected^m player\n\n");
			char* lastconnected_copy =  SDL_strdup(lastconnected); // copy of lastconnected, freed by the deferred call
			if (!lastconnected_copy)
				Con_Printf("identify deferred command unavailable\n");
			else if (!Host_DeferCall(0.75, Send_Identify_Deferred, lastconnected_copy))
			{
				SDL_free(lastconnected_copy); // defer table full -- free now so we don't leak
				Con_Printf("identify deferred command unavailable\n");
			}
		}
		else
			Con_Printf("cannot identify ^mlast connected^m player (not found)\n\n");
		return;
	}
	if (sscanf(Cmd_Argv(1), "%d.%d.%d", &a, &b, &c) == 3)
	{
		Con_Printf("known aliases for %d.%d.%d:\n", a, b, c);
		IPLog_Identify((a << 16) | (b << 8) | c);
		return;
	}

	// woods -- only treat the argument as a player slot number when it is
	// entirely numeric; a name that merely starts with a digit (e.g.
	// "1-1? u bitch") would otherwise be parsed by Q_atoi as a slot number
	// and identify the wrong player. Names fall through to a name match.
	const char *arg = Cmd_Argv(1);
	const char *ch;
	qboolean is_slotnum = (arg[0] != '\0' && Cmd_Argc() == 2);
	for (ch = arg; *ch; ch++)
	{
		if (*ch < '0' || *ch > '9')
		{
			is_slotnum = false;
			break;
		}
	}

	if (is_slotnum)
		i = Q_atoi(arg) - 1;
	else
	{
		const char *namearg = Cmd_Args(); // full name, including any spaces
		if (sv.active)
		{
			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].active && unfun_match(namearg, svs.clients[i].name))
					break;
			}
		}
		else
		{
			for (i = 0; i < cl.maxclients; i++)
			{
				if (unfun_match(namearg, cl.scores[i].name))
					break;
			}
		}
	}
	if (sv.active)
	{
		if (i < 0 || i >= svs.maxclients || !svs.clients[i].active)
		{
			Con_Printf("No such player\n");
			return;
		}
		if (sscanf(NET_QSocketGetMaskedAddressStringForDisplay(svs.clients[i].netconnection), "%d.%d.%d", &a, &b, &c) != 3)
		{
			Con_Printf("Could not determine IP information for %s\n", svs.clients[i].name);
			return;
		}
		strncpy(name, svs.clients[i].name, 15);
		name[15] = 0;
		Con_Printf("known aliases for %s:\n", name);
		IPLog_Identify((a << 16) | (b << 8) | c);
	}
	else
	{
		if (i < 0 || i >= cl.maxclients || !cl.scores[i].name[0])
		{
			Con_Printf("No such player\n");
			return;
		}
		if (!cl.scores[i].addr)
		{
			Con_Printf("No IP information for %.15s\nUse 'status'\n", cl.scores[i].name);
			return;
		}
		strncpy(name, cl.scores[i].name, 15);
		name[15] = 0;
		Con_Printf("known aliases for %s:\n", name);
		IPLog_Identify(cl.scores[i].addr);
	}
}

// woods JPG - proquake #sayrandom
int num_rand[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int next_rand[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char msg_rand[10][128][128];
unsigned char msg_order[10][128];
char cmd_rand[10][10] =
{
	"say_rand0",
	"say_rand1",
	"say_rand2",
	"say_rand3",
	"say_rand4",
	"say_rand5",
	"say_rand6",
	"say_rand7",
	"say_rand8",
	"say_rand9"
};

void Host_Say_Rand_f(void) // woods JPG - proquake #sayrandom
{
	int i, j, k, t;

	if (sscanf(Cmd_Argv(0), "say_rand%d", &k))
	{
		if (num_rand[k] && cls.state == ca_connected)
		{
			if (!next_rand[k])
			{
				for (i = 0; i < num_rand[k]; i++)
					msg_order[k][i] = i;
				for (i = 0; i < num_rand[k] - 1; i++)
				{
					j = (rand() % (num_rand[k] - i)) + i;
					t = msg_order[k][j];
					msg_order[k][j] = msg_order[k][i];
					msg_order[k][i] = t;
				}
			}

			MSG_WriteByte(&cls.message, clc_stringcmd);

			if (ctrlpressed)
				SZ_Print(&cls.message, "say_team ");
			else
				SZ_Print(&cls.message, "say ");
			SZ_Print(&cls.message, msg_rand[k][msg_order[k][next_rand[k]]]);
			if (++next_rand[k] == num_rand[k])
				next_rand[k] = 0;
		}
	}
}


//=============================================================================
//download stuff

static void Host_CloseDownload(client_t *client)
{
	if (client->download.file)
		fclose(client->download.file);
	memset(&client->download, 0, sizeof(client->download));
}

static void Host_SendChunkedDownloadStart(client_t *client, int size_or_error, const char *name)
{
	char safe_name[MAX_QPATH];

	/* QSS-M currently caps game-server downloads at 50 MB, so it sends plain
	 * 32-bit sizes. The client parser still accepts FTE's 64-bit marker for
	 * interoperability with other servers. */
	q_strlcpy(safe_name, name ? name : "", sizeof(safe_name));
	MSG_WriteByte(&client->message, svc_download);
	MSG_WriteLong(&client->message, -1);
	MSG_WriteLong(&client->message, size_or_error);
	MSG_WriteString(&client->message, safe_name);
	if (!client->spawned)
		client->sendsignon = PRESPAWN_FLUSH;
}

static void Host_FailChunkedDownload(client_t *client)
{
	char name[MAX_QPATH];

	q_strlcpy(name, client->download.name, sizeof(name));
	Host_CloseDownload(client);
	Host_SendChunkedDownloadStart(client, DLERR_UNKNOWN, name);
}

static void Host_Download_f(void)
{
	const char *fname = Cmd_Argv(1);
	int fsize;
	if (cmd_source == src_command)
	{
		//FIXME: add some sort of queuing thing
//		if (cls.state == ca_connected)
//			Cmd_ForwardToServer ();

		CL_ManualDownload_f (fname); // woods #manualdownload
		return;
	}
	else if (cmd_source == src_client)
	{
		int refusal_error = 0;

		if (host_client->download.file)
		{	//abort the current download if the previous didn't terminate properly.
			SV_ClientPrintf("cancelling previous download\n");
			MSG_WriteByte (&host_client->message, svc_stufftext);
			MSG_WriteString (&host_client->message, "\nstopdownload\n");
			Host_CloseDownload(host_client);
		}

		host_client->download.size = 0;
		host_client->download.chunked = false;
		host_client->download.started = false;
		host_client->download.sendpos = 0;
		host_client->download.ackpos = 0;
		host_client->download.chunkqueue_head = 0;
		host_client->download.chunkqueue_count = 0;
		
		fsize = -1;
		if (!COM_DownloadNameOkay(fname))
		{
			SV_ClientPrintf("refusing download of %s - restricted filename\n", fname);
			refusal_error = DLERR_PERMISSIONS;
		}
		else
		{
			fsize = COM_FOpenFile(fname, &host_client->download.file, NULL);
			if (!host_client->download.file)
			{
				SV_ClientPrintf("server does not have file %s\n", fname);
				refusal_error = DLERR_FILENOTFOUND;
			}
			else if (file_from_pak)
			{
				SV_ClientPrintf("refusing download of %s from inside pak\n", fname);
				fclose(host_client->download.file);
				host_client->download.file = NULL;
				refusal_error = DLERR_PERMISSIONS;
			}
			else if (fsize < 0 || fsize > 50*1024*1024)
			{
				SV_ClientPrintf("refusing download of large file %s\n", fname);
				fclose(host_client->download.file);
				host_client->download.file = NULL;
				refusal_error = DLERR_PERMISSIONS;
			}
		}

		if (host_client->download.file)
		{
			long startpos = ftell(host_client->download.file);

			if (startpos < 0)
			{
				SV_ClientPrintf("refusing download of %s - unable to seek file\n", fname);
				Host_CloseDownload(host_client);
				refusal_error = DLERR_UNKNOWN;
			}
			else
			{
				host_client->download.size = (unsigned int)fsize;
				host_client->download.startpos = (unsigned int)startpos;
				host_client->download.chunked =
					(host_client->protocol_pext1 & PEXT1_CHUNKEDDOWNLOADS) &&
					host_client->limit_unreliable >= DL_CHUNK_PACKET_SIZE;
				Con_Printf("downloading %s to %s\n", fname, host_client->name);
				if (host_client->download.chunked)
					Host_SendChunkedDownloadStart(host_client, (int)host_client->download.size, fname);
				else
				{
					MSG_WriteByte (&host_client->message, svc_stufftext);
					MSG_WriteString (&host_client->message, va("\ncl_downloadbegin %u \"%s\"\n", host_client->download.size, fname));
				}
				q_strlcpy(host_client->download.name, fname, sizeof(host_client->download.name));
				refusal_error = 0;
			}
		}
		if (!host_client->download.file && refusal_error)
		{
			if (refusal_error == DLERR_FILENOTFOUND && strstr(fname, ".loc")) // woods, more info for .loc refusals
				Con_Printf("%s attempted download of %s, server does not have file\n", host_client->name, fname);
			else
				Con_Printf("refusing download of %s to %s\n", fname, host_client->name);

			if (host_client->protocol_pext1 & PEXT1_CHUNKEDDOWNLOADS)
				Host_SendChunkedDownloadStart(host_client, refusal_error, fname);
			else
			{
				MSG_WriteByte (&host_client->message, svc_stufftext);
				MSG_WriteString (&host_client->message, "\nstopdownload\n");
			}
		}
		if (!host_client->download.chunked || !host_client->spawned)
			host_client->sendsignon = PRESPAWN_FLUSH;	//override any keepalive issues. woods - switch to enum
	}
}

static void Host_EnableCSQC_f(void)
{
	size_t e;
	if (cmd_source != src_client)
		return;
	host_client->csqcactive = true;

	//re-flag any ents as needing a resend.
	for (e = 1; e < host_client->numpendingcsqcentities; e++)
		if (host_client->pendingcsqcentities_bits[e] & SENDFLAG_PRESENT)
			host_client->pendingcsqcentities_bits[e] |= SENDFLAG_USABLE;
}
static void Host_DisableCSQC_f(void)
{
	if (cmd_source != src_client)
		return;
	host_client->csqcactive = false;
}

static void Host_StartDownload_f(void)
{
	if (cmd_source != src_client)
		return;
	if (host_client->download.file && !host_client->download.chunked)
		host_client->download.started = true;
	else if (host_client->download.file)
		return;
	else
		SV_ClientPrintf("no download started\n");
}
//just writes download data onto the end of the outgoing unreliable buffer
static void Host_PopDownloadChunk(client_t *client)
{
	client->download.chunkqueue_head =
		(client->download.chunkqueue_head + 1) % countof(client->download.chunkqueue);
	client->download.chunkqueue_count--;
}

qboolean Host_AppendDownloadData(client_t *client, sizebuf_t *buf)
{
	if (buf->cursize > buf->maxsize - DL_LEGACY_HEADER_SIZE)
		return false;	//no space for anything
	if (client->download.file && client->download.chunked)
	{
		byte tbuf[DLBLOCKSIZE];
		size_t got, wanted;
		qboolean sent = false;
		if (!client->download.size)
		{
			Host_CloseDownload(client);
			return false;
		}
		while (client->download.chunkqueue_count)
		{
			unsigned int chunk =
				client->download.chunkqueue[client->download.chunkqueue_head];
			unsigned int maxchunk = (client->download.size - 1) / DLBLOCKSIZE;
			unsigned int offset;

			if (buf->cursize > buf->maxsize - DL_CHUNK_PACKET_SIZE)
				break;

			if (chunk > maxchunk)
			{
				Host_PopDownloadChunk(client);
				continue;
			}
			offset = chunk * DLBLOCKSIZE;

			if (fseek(client->download.file, client->download.startpos + offset, SEEK_SET) != 0)
			{
				Host_FailChunkedDownload(client);
				return sent;
			}

			wanted = client->download.size - offset;
			if (wanted > sizeof(tbuf))
				wanted = sizeof(tbuf);
			got = fread(tbuf, 1, wanted, client->download.file);
			if (got != wanted)
			{
				Host_FailChunkedDownload(client);
				return sent;
			}
			if (got < sizeof(tbuf))
				memset(tbuf + got, 0, sizeof(tbuf) - got);

			Host_PopDownloadChunk(client);
			MSG_WriteByte(buf, svc_download);
			MSG_WriteLong(buf, chunk);
			SZ_Write(buf, tbuf, DLBLOCKSIZE);
			sent = true;
		}
		return sent;
	}
	if (client->download.file && client->download.started)
	{
		byte tbuf[1024];	//keep small enough to fit within DTLS MTU after SCTP+netchan overhead
		unsigned int size = client->download.size - client->download.sendpos;
		//size might be 0 at eof, and that's needed to avoid failure if we drop the last few packets
		if (size > sizeof(tbuf))
			size = sizeof(tbuf);
		if (size > (unsigned int)(buf->maxsize - buf->cursize - DL_LEGACY_HEADER_SIZE))
			size = (unsigned int)(buf->maxsize - buf->cursize - DL_LEGACY_HEADER_SIZE);	//don't overflow

		if (size && fread(tbuf, 1, size, client->download.file) < size)
			client->download.ackpos = client->download.sendpos = client->download.size;	//some kind of error...
		else
		{
			MSG_WriteByte(buf, svcdp_downloaddata);
			MSG_WriteLong(buf, client->download.sendpos);
			MSG_WriteShort(buf, size);
			SZ_Write(buf, tbuf, size);
			client->download.sendpos += size;
			return true;
		}
	}
	return false;
}
//parses incoming acks from the client, so we know which parts of the file the client actually received.
static qboolean Host_DownloadChunkQueued(client_t *client, unsigned int chunk)
{
	unsigned int i;

	for (i = 0; i < client->download.chunkqueue_count; i++)
	{
		unsigned int idx = (client->download.chunkqueue_head + i) % countof(client->download.chunkqueue);
		if (client->download.chunkqueue[idx] == chunk)
			return true;
	}
	return false;
}

static void Host_NextDownload_f(void)
{
	long chunknum;
	unsigned int maxchunk;
	unsigned int chunk, tail;
	char *end;

	if (cmd_source != src_client)
		return;
	if (!host_client->download.file || !host_client->download.chunked)
		return;
	if (Cmd_Argc() < 2)
		return;

	errno = 0;
	chunknum = strtol(Cmd_Argv(1), &end, 0);
	if (errno || end == Cmd_Argv(1) || *end)
		return;
	if (chunknum < 0)
	{
		Host_CloseDownload(host_client);
		return;
	}
	if (!host_client->download.size)
		return;
	maxchunk = (host_client->download.size - 1) / DLBLOCKSIZE;
	if ((unsigned long)chunknum > maxchunk)
		return;

	/* Keep one chunk per nextdl to avoid request amplification; the client can
	 * pipeline separate nextdl commands. */
	chunk = (unsigned int)chunknum;
	if (host_client->download.chunkqueue_count >= countof(host_client->download.chunkqueue))
		return;
	if (Host_DownloadChunkQueued(host_client, chunk))
		return;

	tail = (host_client->download.chunkqueue_head +
		host_client->download.chunkqueue_count) % countof(host_client->download.chunkqueue);
	host_client->download.chunkqueue[tail] = chunk;
	host_client->download.chunkqueue_count++;
}

//parses incoming acks from the client, so we know which parts of the file the client actually received.
void Host_DownloadAck(client_t *client)
{
	unsigned int start = MSG_ReadLong();
	unsigned int size = (unsigned short)MSG_ReadShort();
	unsigned int end;

	if (!client->download.started || !client->download.file)
		return;

	if (client->download.sendpos > client->download.size)
		client->download.sendpos = client->download.size;
	if (start > client->download.sendpos)
		return;
	if (size > client->download.sendpos - start)
		return;

	end = start + size;

	if (client->download.ackpos < start)
	{
		client->download.sendpos = client->download.ackpos;//there was a gap, rewind to the known gap
		if (fseek(client->download.file, client->download.startpos + client->download.sendpos, SEEK_SET) != 0)
		{
			Host_CloseDownload(client);
			return;
		}
	}
	else if (client->download.ackpos < end)
		client->download.ackpos = end;	//no loss yet.
	//else FIXME: build a log of parts known to be acked to avoid resending them later, skip past them in acks

	if (client->download.ackpos == client->download.size)
	{
		unsigned int hash = 0;
		byte *data;
		client->download.started = false;

		data = malloc(client->download.size);
		if (data)
		{
			if (fseek(client->download.file, client->download.startpos, SEEK_SET) != 0)
			{
				free(data);
				Host_CloseDownload(client);
				return;
			}
			size_t read_size = fread(data, 1, client->download.size, client->download.file); // woods
			if (read_size != client->download.size)
			{
				free(data);
				Host_CloseDownload(client);
				return;
			}
			hash = CRC_Block(data, client->download.size);
			free(data);
		}
		fclose(client->download.file);
		client->download.file = NULL;

		MSG_WriteByte (&client->message, svc_stufftext);
		MSG_WriteString (&client->message, va("cl_downloadfinished %u %u \"%s\"\n", client->download.size, hash, client->download.name));
		*client->download.name = 0;
		client->sendsignon = PRESPAWN_FLUSH;	//override any keepalive issues. woods - switch to enum
	}
}

static void Info_ClientPrint_Callback(void *ctx, const char *key, const char *val)
{
	SV_ClientPrintf("%20s: %s\n", key, val);
}
void Host_Serverinfo_f(void)
{	//serverinfo command
	if (cmd_source == src_client)
	{
		Info_Enumerate(svs.serverinfo, Info_ClientPrint_Callback, NULL);
		return;
	}
	if (Cmd_Argc() != 3)
	{
		Con_Printf("Serverinfo:\n");
		if (cls.state >= ca_connected && cmd_source != src_client)
			Info_Print(cl.serverinfo);
		else
			Info_Print(svs.serverinfo);
	}
	else if (cmd_source == src_command)
	{
		const char *key = Cmd_Argv(1);
		const char *val = Cmd_Argv(2);
		if (*key == '*')
		{
			Con_Printf("Refusing to set key \"%s\"\n", key);
			return;
		}
		SV_UpdateInfo(0, key, val);
	}
	else
		Con_Printf("Serverinfo may not be changed here\n");
}

void Host_Setinfo_f(void)
{
	const char *key = Cmd_Argv(1);
	const char *val = Cmd_Argv(2);

	if (cmd_source == src_client)
	{	//clc_stringcmd version
		if (Cmd_Argc() != 3)
		{
			SV_ClientPrintf("Your Serverside User Info:\n");
			Info_Enumerate(host_client->userinfo, Info_ClientPrint_Callback, NULL);
		}
		else
		{
			if (*key == '*') // woods
			{
				if (!strcmp(key, "*ver") && !host_client->spawned) // allow *ver only during initial connection
				{
					Con_DPrintf("allowing *ver set from %s (not yet spawned)\n", host_client->name);
				}
				else
				{
					if (!strcmp(key, "*ver"))
						SV_ClientPrintf("\nrejecting *ver set from %s (already spawned)\n\n", host_client->name);
					else
						SV_ClientPrintf("\nrejecting *%s set from %s (restricted key)\n\n", key + 1, host_client->name);
					return;
				}
			}

			SV_UpdateInfo((host_client - svs.clients)+1, key, val);
		}
	}
	else
	{	//console version
		if (Cmd_Argc() != 3)
		{
			Con_Printf("User Info:\n");
			Info_Print(cls.userinfo);
		}
		else
		{
			cvar_t *var = Cvar_FindVar(key);
			if (var && var->flags & CVAR_USERINFO)
				Cvar_Set(key, val);
			else
			{
				Info_SetKey(cls.userinfo, sizeof(cls.userinfo), key, val);
				if (cls.state == ca_connected)
					Cmd_ForwardToServer();
			}
		}
	}
}
void Host_User_f(void)
{
	/*if (sv.active)
	{
		int i;
		if (Cmd_Argc() == 2)
		{
			i = atoi(Cmd_Argv(1));

			if (i >= cl.maxclients)
				return;	//not a valid slot.

			Con_Printf("User %i (%s):\n", i, svs.clients[i].name);
			Info_Print(svs.clients[i].userinfo);
		}
		else
		{
			for (i = 0; i < svs.maxclients; i++)
			{
				if (*svs.clients[i].name)
				{
					Con_Printf("User %i (%s):\n", i, svs.clients[i].name);
					Info_Print(svs.clients[i].userinfo);
				}
			}
		}
	}
	else*/ if (cls.state == ca_connected)
	{
		int i;
		if (Cmd_Argc() == 2)
		{
			i = atoi(Cmd_Argv(1));

			if (i < 0 || i >= cl.maxclients)
				return;	//not a valid slot.

			Con_Printf("User %i (%s):\n", i, cl.scores[i].name);
			Info_Print(cl.scores[i].userinfo);
		}
		else
		{
			for (i = 0; i < cl.maxclients; i++)
			{
				if (*cl.scores[i].name)
				{
					Con_Printf("User %i (%s):\n", i, cl.scores[i].name);
					Info_Print(cl.scores[i].userinfo);
				}
			}
		}
	}
}

//=============================================================================

/*
==================
Host_InitCommands
==================
*/
void Host_InitCommands (void)
{
#define Cmd_AddCommand_ClientCommandQC(cmd,fnc) Cmd_AddCommand2(cmd,fnc,src_client,true)

	Host_InitSaveThread ();

	Cmd_AddCommand ("maps", Host_Maps_f); //johnfitz
	Cmd_AddCommand ("mods", Host_Mods_f); //johnfitz
	Cmd_AddCommand ("games", Host_Mods_f); // as an alias to "mods" -- S.A. / QuakeSpasm
	Cmd_AddCommand ("mapname", Host_Mapname_f); //johnfitz
	Cmd_AddCommand ("mapsize", Host_MapSize_f);
	Cmd_AddCommand ("randmap", Host_Randmap_f); //ericw

	Cmd_AddCommand_ClientCommand ("serverinfo", Host_Serverinfo_f); //spike
	Cmd_AddCommand_ClientCommand ("setinfo", Host_Setinfo_f); //spike
	Cmd_AddCommand ("user", Host_User_f); //spike

	Cmd_AddCommand_ClientCommand ("status", Host_Status_f);
	Cmd_AddCommand ("quit", Host_Quit_f);
	Cmd_AddCommand_ClientCommandQC ("god", Host_God_f);
	Cmd_AddCommand_ClientCommandQC ("notarget", Host_Notarget_f);
	Cmd_AddCommand_ClientCommandQC ("fly", Host_Fly_f);
	Cmd_AddCommand ("map", Host_Map_f);
	Cmd_AddCommand ("restart", Host_Restart_f);
	Cmd_AddCommand ("changelevel", Host_Changelevel_f);
	Cmd_AddCommand ("connect", Host_Connect_f);
	Cmd_AddCommand_Console ("reconnect", Host_Reconnect_Con_f);
	Cmd_AddCommand_ServerCommand ("reconnect", Host_Reconnect_Sv_f);
	Cmd_AddCommand_ServerCommand ("ls", Host_Lightstyle_f);
	Cmd_AddCommand_ClientCommand ("name", Host_Name_f);
	Cmd_AddCommand_ClientCommand ("namebk", Host_Name_Load_Backup_f); // woods #smartafk
	Cmd_AddCommand_ClientCommandQC ("noclip", Host_Noclip_f);
	Cmd_AddCommand_ClientCommandQC ("setpos", Host_SetPos_f); //QuakeSpasm

	Cmd_AddCommand_ClientCommandQC ("say", Host_Say_f);
	Cmd_AddCommand_ClientCommandQC ("s", Host_Say_f2); // woods chat shortcuts
	Cmd_AddCommand_ClientCommandQC ("say_team", Host_Say_Team_f);
	Cmd_AddCommand_ClientCommandQC ("st", Host_Say_Team_f2); // woods chat shortcuts
	Cmd_AddCommand_ClientCommandQC ("like", Host_Like_f); // woods #like
	Cmd_AddCommand_ClientCommandQC ("tell", Host_Tell_f);
	Cmd_AddCommand_ClientCommand ("ignore", Host_Ignore_f);
	Cmd_AddCommand_ClientCommand ("unignore", Host_Unignore_f);
	Cmd_AddCommand_ClientCommandQC ("color", Host_Color_f);
	Cmd_AddCommand_ClientCommandQC ("kill", Host_Kill_f);
	Cmd_AddCommand_ClientCommandQC ("pause", Host_Pause_f);
	Cmd_AddCommand_ClientCommand ("spawn", Host_Spawn_f);
	Cmd_AddCommand_ClientCommand ("begin", Host_Begin_f);
	Cmd_AddCommand_ClientCommand ("prespawn", Host_PreSpawn_f);
	Cmd_AddCommand_ClientCommandQC ("kick", Host_Kick_f);
	Cmd_AddCommand_ClientCommand ("ping", Host_Ping_f);
	Cmd_AddCommand_ClientCommand ("pings", Host_Pings_f);
	Cmd_AddCommand_ClientCommand ("netdrops", Host_NetDrops_f);
	Cmd_AddCommand ("load", Host_Loadgame_f);
	Cmd_AddCommand ("save", Host_Savegame_f);
	Cmd_AddCommand_ClientCommandQC ("give", Host_Give_f);
	Cmd_AddCommand_ClientCommandQC ("massacre", Host_Massacre_f);
	Cmd_AddCommand_ClientCommandQC ("goto", Host_Goto_f); // woods #goto
	Cmd_AddCommand_ClientCommandQC ("resurrect", Host_Resurrect_f); // woods #resurrect
	Cmd_AddCommand_ClientCommand ("download", Host_Download_f);
	Cmd_AddCommand_ClientCommand ("sv_startdownload", Host_StartDownload_f);
	Cmd_AddCommand_ClientCommand ("nextdl", Host_NextDownload_f);
	Cmd_AddCommand_ClientCommand ("enablecsqc", Host_EnableCSQC_f);
	Cmd_AddCommand_ClientCommand ("disablecsqc", Host_DisableCSQC_f);
	Cmd_AddCommand_ClientCommand ("modvote", Host_Modvote_f);

	Cmd_AddCommand ("startdemos", Host_Startdemos_f);
	Cmd_AddCommand ("demos", Host_Demos_f);
	Cmd_AddCommand ("stopdemo", Host_Stopdemo_f);

	Cmd_AddCommand ("viewmodel", Host_Viewmodel_f);
	Cmd_AddCommand ("viewframe", Host_Viewframe_f);
	Cmd_AddCommand ("viewnext", Host_Viewnext_f);
	Cmd_AddCommand ("viewprev", Host_Viewprev_f);

	Cmd_AddCommand("identify", Host_Identify_f);	// JPG 1.05 - player IP logging // woods #iplog
	Cmd_AddCommand("ipnames", IPLog_PrintNames);	// woods - print all logged names in columns
	Cmd_AddCommand("namehistory", Host_NameHistory_f);	// woods #namehistory
	{
		cmd_function_t *namehistory_cmd = Cmd_FindCommand("namehistory");
		if (namehistory_cmd)
			namehistory_cmd->completion = Host_NameHistory_Completion_f;
	}
	Cmd_AddCommand("ipdump", IPLog_Dump);			// JPG 1.05 - player IP logging // woods #iplog
	Cmd_AddCommand("ipmerge", IPLog_Import);		// JPG 3.00 - import an IP data file // woods #iplog

	// woods JPG - proquake #sayrandom
	int i;
	FILE* f;
	for (i = 0; i < 10; i++)
	{
		f = fopen(va("%s/msgrand%d.txt", com_gamedir, i), "r");
		if (f)
		{
			Cmd_AddCommand(cmd_rand[i], Host_Say_Rand_f);
			num_rand[i] = 0;
			// woods #sayrandom -- msg_rand/msg_order only hold 128 entries; stop before overrunning them
			while (num_rand[i] < 128 && fgets(msg_rand[i][num_rand[i]], 128, f))
			{
				char* ch = strchr(msg_rand[i][num_rand[i]], '\n');
				if (ch)
					*ch = 0;
				if (msg_rand[i][num_rand[i]][0])
					num_rand[i]++;
			}
			if (num_rand[i] == 128)
			{
				char extra[128];
				if (fgets(extra, sizeof(extra), f))
					Con_Warning("msgrand%d.txt exceeds 128 messages; extra ignored\n", i);
			}
			fclose(f);
		}
	}

}
