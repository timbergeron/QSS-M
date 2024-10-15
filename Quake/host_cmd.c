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
#include "arch_def.h" // woods for #iplog
#include "net_sys.h" // woods for #iplog
#include "net_defs.h" // woods for #iplog
#include <time.h> // woods #demolistsort
#include <sys/stat.h> // woods #demolistsort
#include "bgmusic.h" // woods #musiclist
#include "json.h" // woods #mapdescriptions
#ifndef _WIN32
#include <dirent.h>
#endif

extern cvar_t	pausable;
extern cvar_t	nomonsters; // woods #nomonsters (ironwail)

// 0 = no, 1 = ask, 2 = when dead, 3 = always
cvar_t sv_autoload = {"sv_autoload", "2", CVAR_ARCHIVE}; // woods #autoload (iw)

int	current_skill;

double		mpservertime;	// woods #servertime
extern		char afk_name[16]; // woods #smartafk

cvar_t sv_adminnick = {"sv_adminnick", "server admin", CVAR_ARCHIVE}; // woods (darkpaces) #adminnick
extern char lastconnected[3]; // woods -- #identify+
extern qboolean ctrlpressed; // woods #saymodifier
extern qboolean Valid_IP(const char* ip_str); // woods #icmp
extern qboolean Valid_Domain(const char* domain_str); // woods #icmp

void CL_ManualDownload_f (const char* filename); // woods #manualdownload

/*
==================
Host_Quit_f
==================
*/
void Host_Quit_f (void)
{
	
	if (key_dest == key_console && cls.state != ca_dedicated && !cls.menu_qcvm.progs && cl.matchinp) // woods #matchquit
		M_Menu_Quit_f ();
	
	if (key_dest != key_console && cls.state != ca_dedicated && !cls.menu_qcvm.progs)
	{
		M_Menu_Quit_f ();
		return;
	}
	CL_Disconnect ();
	Host_ShutdownServer(false);

	if (!cl_afk.value) // if I disable it, lets delete it
		remove(va("%s/id1/backups/name.txt", com_basedir));

	Sys_Quit ();
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
		if (!Q_strcmp (name, item->name))
			return;
	}

	item = (filelist_item_t *) Z_Malloc(sizeof(filelist_item_t));
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

	fprintf(file, "[\n"); // Start the JSON array

	filelist_item_t* level;
	qboolean first = true;
	for (level = extralevels; level; level = level->next)
	{
		if (!first) // Add a comma between entries (but not before the first entry)
			fprintf(file, ",\n");
		first = false;

		// Escape the level name and description dynamically
		char* escaped_name = JSON_EscapeString(level->name);
		char* escaped_description = JSON_EscapeString(level->data);

		if (!escaped_name || !escaped_description) {
			Con_DPrintf("Failed to escape JSON string\n");
			free(escaped_name);
			free(escaped_description);
			fclose(file);
			return;
		}

		fprintf(file, "  {\n");
		fprintf(file, "    \"name\": \"%s\",\n", escaped_name);
		fprintf(file, "    \"description\": \"%s\"\n", escaped_description);
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

		if (!name || !description) continue;

		filelist_item_t* item = malloc(sizeof(filelist_item_t));
		if (!item) {
			Con_DPrintf("Memory allocation failed\n");
			FreeLevelList(*extralevels_from_json);
			JSON_Free(json);
			return;
		}

		Q_strncpy(item->name, name, MAX_QPATH - 1);
		item->name[MAX_QPATH - 1] = '\0';

		Q_strncpy(item->data, description, 49);
		item->data[49] = '\0';

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

void ExtraMaps_ParseDescriptions (void)
{
	filelist_item_t* level;
	filelist_item_t* extralevels_from_json = NULL;

	LoadMapDescriptionsFromJSON(&extralevels_from_json);

	for (level = extralevels; level; level = level->next)
		UpdateMaxWordLength(level->name);

	for (level = extralevels; level; level = level->next) // for each level, check if we have a cached description
	{
		filelist_item_t* json_level = FindLevelInList(extralevels_from_json, level->name);
		if (json_level)
		{
			// Use cached description
			strncpy(level->data, json_level->data, sizeof(level->data) - 1);
			level->data[sizeof(level->data) - 1] = '\0';
		}
		else
		{
			// Load description from .bsp file
			char mapdesc[MAXDESC];
			Mod_LoadMapDescription(mapdesc, sizeof(mapdesc), level->name);
			Q_strncpy(level->data, mapdesc, sizeof(level->data) - 1);
			Con_DPrintf("cached new map description %s\n", level->name);
			level->data[sizeof(level->data) - 1] = '\0';

			// Add new description to the JSON list
			filelist_item_t* new_json_level = malloc(sizeof(filelist_item_t));
			Q_strncpy(new_json_level->name, level->name, MAX_QPATH - 1);
			new_json_level->name[MAX_QPATH - 1] = '\0';
			Q_strncpy(new_json_level->data, level->data, sizeof(new_json_level->data) - 1);
			new_json_level->data[sizeof(new_json_level->data) - 1] = '\0';
			new_json_level->next = extralevels_from_json;
			extralevels_from_json = new_json_level;
		}
	}

	SaveMapDescriptionsToJSON(extralevels_from_json);

	FreeLevelList(extralevels_from_json);
	extralevels_from_json = NULL;

	descriptionsParsed = true;
}

void FileList_Add_MapDesc (const char* levelName) // for a map download
{
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
		snprintf(combined, sizeof(combined), "%-*s %.50s", word_length + num_spaces, level->name, level->data);

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
void Modlist_Init (void)
{
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
	DWORD		attribs;
	char		dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf (dir_string, sizeof(dir_string), "%s/*", com_basedir);
	fhnd = FindFirstFile(dir_string, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
			continue;
		q_snprintf (mod_string, sizeof(mod_string), "%s/%s", com_basedir, fdat.cFileName);
		attribs = GetFileAttributes (mod_string);
		if (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
			/* don't bother testing for pak files / progs.dat */
			Modlist_Add(fdat.cFileName);
		}
	} while (FindNextFile(fhnd, &fdat));

	FindClose(fhnd);
}
#else
void Modlist_Init (void)
{
	DIR		*dir_p, *mod_dir_p;
	struct dirent	*dir_t;
	char		dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

	q_snprintf (dir_string, sizeof(dir_string), "%s/", com_basedir);
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
#endif

//==============================================================================
// woods -- server list management #serverlist
//==============================================================================

filelist_item_t* serverlist;

static void ServerList_Clear(void)
{
	FileList_Clear(&serverlist);
}

void ServerList_Rebuild(void)
{
	ServerList_Clear();
	ServerList_Init();
}

void ServerList_Init(void)
{
	int i;
	char	name[MAX_OSPATH];

	q_snprintf(name, sizeof(name), "%s/id1", com_basedir); //  make an id1 folder if it doesnt exist already #smartafk
	Sys_mkdir(name);

	q_snprintf(name, sizeof(name), "%s/id1/backups", com_basedir); //  create backups folder if not there
	Sys_mkdir(name);

	FILE* file = fopen(va("%s/id1/backups/%s", com_basedir, SERVERLIST), "r");
	
	if (file == NULL) {
		return;
	}

	char buffer[256];
	while (fgets(buffer, sizeof(buffer), file) != NULL) {

		for (i = 0;; i++) {
			if (buffer[i] == '\n') {
				buffer[i] = '\0';
				break;
			}
		}

		FileList_Add(buffer, NULL, &serverlist); // woods #demolistsort add arg
	}
	fclose(file);
}

//==============================================================================
// woods -- bookmarks list management #bookmarksmenu
//==============================================================================

filelist_item_t* bookmarkslist;

static void BookmarksList_Clear(void)
{
	FileList_Clear(&bookmarkslist);
}

void BoomarksList_Rebuild(void)
{
	BookmarksList_Clear();
	BookmarksList_Init();
}

void BookmarksList_Init(void)
{
	char	name[MAX_OSPATH];

	q_snprintf(name, sizeof(name), "%s/id1", com_basedir); //  make an id1 folder if it doesnt exist already #smartafk
	Sys_mkdir(name);

	q_snprintf(name, sizeof(name), "%s/id1/backups", com_basedir); //  create backups folder if not there
	Sys_mkdir(name);

	FILE* file = fopen(va("%s/id1/backups/%s", com_basedir, BOOKMARKSLIST), "r");

	if (file == NULL) {
		return;
	}

	char buffer[256];
	while (fgets(buffer, sizeof(buffer), file) != NULL) {
		// Remove newline character
		buffer[strcspn(buffer, "\n")] = '\0';

		char* extra_info = NULL;
		char* token = strtok(buffer, ","); // Split the string at the comma

		if (token != NULL) {
			extra_info = strtok(NULL, ""); // Get the remainder of the string after the comma
		}

		FileList_Add(token, extra_info, &bookmarkslist); // Pass the split parts to FileList_Add
	}
	fclose(file);
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
	char		cfgname[32];
	char		cfgnamedir[32];
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
#else
	DIR		*dir_p;
	struct dirent	*dir_t;
	struct stat file_stat;
	struct tm* tm;
#endif
	char		filestring[MAX_OSPATH];
	char		demname[50];
	char		ignorepakdir[32];
	char		dateStr[80]; // To store the date string
	searchpath_t	*search;
	pack_t		*pak;
	int		i;

	// we don't want to list the demos in id1 pakfiles,
	// because these are not "add-on" demos
	q_snprintf (ignorepakdir, sizeof(ignorepakdir), "/%s/", GAMENAME);
	
	for (search = com_searchpaths; search; search = search->next)
	{
		if (!search->pack) //directory
		{
#ifdef _WIN32
			q_snprintf (filestring, sizeof(filestring), "%s/demos/*.dem", search->filename); // woods #demosfolder
			fhnd = FindFirstFile(filestring, &fdat);
			if (fhnd == INVALID_HANDLE_VALUE)
				continue;
			do
			{
				// Convert the last-write time to local time
				FileTimeToSystemTime(&fdat.ftLastWriteTime, &stUTC);
				SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

				// Build a string showing the date
				sprintf(dateStr, "%04d-%02d-%02d %02d:%02d:%02d",
					stLocal.wYear, stLocal.wMonth, stLocal.wDay,
					stLocal.wHour, stLocal.wMinute, stLocal.wSecond);

				COM_StripExtension(fdat.cFileName, demname, sizeof(demname));
				FileList_Add (demname, dateStr, &demolist);
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
#else
			q_snprintf (filestring, sizeof(filestring), "%s/demos/", search->filename); // woods #demosfolder
			dir_p = opendir(filestring);
			if (dir_p == NULL)
				continue;
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "dem") != 0)
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

				if (stat(fullpath, &file_stat) == 0)
				{
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
				}
				else
				{
					strncpy(dateStr, "Unknown Date", sizeof(dateStr) - 1);
					dateStr[sizeof(dateStr) - 1] = '\0'; // Ensure null termination
		}

				COM_StripExtension(dir_t->d_name, demname, sizeof(demname));
				FileList_Add (demname, dateStr, &demolist);
			}
			closedir(dir_p);
#endif
		}
		else //pakfile
		{
			if (!strstr(search->pack->filename, ignorepakdir))
			{ //don't list standard id demos
				for (i = 0, pak = search->pack; i < pak->numfiles; i++)
				{
					if (!strcmp(COM_FileGetExtension(pak->files[i].name), "dem"))
					{
						COM_StripExtension(pak->files[i].name, demname, sizeof(demname));
						FileList_Add (demname, "Unknown Date", &demolist);
					}
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
			for (i = 0, pak = search->pack; i < pak->numfiles; i++)
			{
				if (SkyhasValidExtension (pak->files[i].name)) {
					char skyname[32];
					COM_StripExtension (pak->files[i].name, skyname, sizeof(skyname));
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
	int			a, b, c; // Baker 3.60 - a,b,c added for IP masking// woods

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
		
		if (cmd_source != src_command && sscanf(client->netconnection ? NET_QSocketGetTrueAddressString(client->netconnection) : "botclient", "%d.%d.%d", &a, &b, &c) == 3)
			print_fn("   %d.%d.%d.xxx\n", a, b, c); // Baker 3.60 - a,b,c added for IP masking // woods
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

/*
====================
Host_SetPos_f

adapted from fteqw, originally by Alex Shadowalker
====================
*/
static void Host_SetPos_f(void)
{
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

	if (Cmd_Argc() == 2 && !q_strcasecmp(Cmd_Argv(1), "last")) // woods #setlast
	{
		if (!has_last_viewpos)
		{
			SV_ClientPrintf("\nno previous viewpos available\n\n");
			return;
		}

		VectorCopy(last_viewpos, sv_player->v.origin);
		VectorCopy(last_viewangles, sv_player->v.angles);
		sv_player->v.fixangle = 1;

		VectorClear(sv_player->v.velocity);

		SV_LinkEdict(sv_player, false);
		return;
	}

	if (Cmd_Argc() != 7 && Cmd_Argc() != 4)
	{
		SV_ClientPrintf("usage:\n");
		SV_ClientPrintf("   setpos <x> <y> <z>\n");
		SV_ClientPrintf("   setpos <x> <y> <z> <pitch> <yaw> <roll>\n");
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

	if (sv_player->v.movetype != MOVETYPE_NOCLIP)
	{
		noclip_anglehack = true;
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		SV_ClientPrintf ("noclip ON\n");
	}

	//make sure they're not going to whizz away from it
	sv_player->v.velocity[0] = 0;
	sv_player->v.velocity[1] = 0;
	sv_player->v.velocity[2] = 0;
	
	sv_player->v.origin[0] = atof(Cmd_Argv(1));
	sv_player->v.origin[1] = atof(Cmd_Argv(2));
	sv_player->v.origin[2] = atof(Cmd_Argv(3));
	
	if (Cmd_Argc() == 7)
	{
		sv_player->v.angles[0] = atof(Cmd_Argv(4));
		sv_player->v.angles[1] = atof(Cmd_Argv(5));
		sv_player->v.angles[2] = atof(Cmd_Argv(6));
		sv_player->v.fixangle = 1;
	}
	
	SV_LinkEdict (sv_player, false);
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
ICMP_Ping_Host -- woods #icmp
==================
*/

#ifdef _WIN32
int ICMP_Ping_Host(const char* host)
{
	char command[256];
	char buffer[128];
	snprintf(command, sizeof(command), "ping -n 1 -w 150 %s", host);

	HANDLE hRead, hWrite;
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

	if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
		Con_DPrintf("Failed to create pipe\n");
		return -1;
	}

	STARTUPINFO si = { sizeof(STARTUPINFO) };
	PROCESS_INFORMATION pi;
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;
	si.wShowWindow = SW_HIDE;  // Prevents a window from popping up

	if (!CreateProcess(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
		Con_DPrintf("Failed to execute command: %s\n", command);
		CloseHandle(hWrite);
		CloseHandle(hRead);
		return -1;
	}

	CloseHandle(hWrite);

	// Read the output
	DWORD bytesRead;
	BOOL success;
	float rtt = -1;
	while (success = ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL), success&& bytesRead > 0) {
		buffer[bytesRead] = '\0';
		char* rtt_start = strstr(buffer, "time=");
		if (rtt_start != NULL) {
			sscanf(rtt_start, "time=%f", &rtt);
			break;
		}
	}

	CloseHandle(hRead);
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	if (rtt >= 0) {
		Con_DPrintf("RTT calculated: %.2f ms\n", rtt);
		return (int)rtt;
	}
	else {
		Con_DPrintf("Failed to retrieve RTT\n");
		return -1;
	}
}
#else  //  linux/macOS
int ICMP_Ping_Host(const char* host)
{
	char command[256];
	char buffer[128];

	snprintf(command, sizeof(command), "ping -c 1 -W 200 %s", host);
	FILE* fp = popen(command, "r");
	if (fp == NULL) {
		perror("popen failed");
		return -1;
	}

	Con_DPrintf("Executing command: %s\n", command);

	float rtt = -1;

	while (fgets(buffer, sizeof(buffer), fp) != NULL) // Read the output line by line to find the RTT
	{ 
		char* rtt_start = strstr(buffer, "time=");
		if (rtt_start != NULL) {
			rtt_start += 5; // Move past "time=" to the number
			rtt = strtof(rtt_start, NULL);
			Con_DPrintf("RTT calculated: %.2f ms\n", rtt);
			break;
		}
	}

	int status = pclose(fp);
	if (status == 0 && rtt != -1) {
		Con_DPrintf("Host %s is reachable with RTT %.2f ms.\n", host, rtt);
		return (int)rtt;
	}
	else {
		Con_DPrintf("Host %s is not reachable or RTT calculation failed.\n", host);
		return -1;
	}
}
#endif

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

		const char* host_no_port = COM_StripPort(n);

		if (Valid_IP(host_no_port) || Valid_Domain(host_no_port))
		{
			int rtt = ICMP_Ping_Host(host_no_port);
			if (rtt >= 0)
			{
				Con_Printf("%i ms\n", rtt);
			}
			else
			{
				Con_Printf("ping failed, host may not accept ICMP pings or is non-responsive\n");
			}
			free((void*)host_no_port);
			return;
		}
		else
		{
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
			Con_Printf ("Current map: %s ( %s )\n", cl.levelname, cl.mapname);
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
	p = strstr(name, ".bsp");
	if (p && p[4] == '\0')
		*p = '\0';
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
	if (!sv_autoload.value || !sv.lastsave[0] || svs.maxclients != 1 || cl.intermission)
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
	CL_Disconnect_f();
	cls.demonum = -1;		// stop demo loop in case this fails
	if (cls.demoplayback)
	{
		CL_StopPlayback ();
		CL_Disconnect ();
	}
	CL_EstablishConnection (NULL);
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
Log_Last_Server_f // woods #connectlast (Qrack)
===============
*/
void Log_Last_Server_f(void)
{
	FILE* f;

	char server[MAX_OSPATH];

	q_snprintf(server, sizeof(server), "%s/id1/backups", com_basedir); //  create backups folder if not there
	Sys_mkdir(server);

	if (!q_strcasecmp(lastmphost, "local"))
		return;

	f = fopen(va("%s/id1/backups/%s.txt", com_basedir, "lastserver"), "w");

	if (!f)
	{
		Con_Printf("Couldn't write backup last server\n");
		return;
	}

	fprintf(f, "%s", lastmphost);

	fclose(f);
}

/*
===============
Host_ConnectToLastServer_f // woods #connectlast (Qrack)
===============
*/
void Host_ConnectToLastServer_f (void) // woods #connectlast (Qrack)
{
	FILE* f;
	char name[NET_NAMELEN];

	f = fopen(va("%s/id1/backups/%s.txt", com_basedir, "lastserver"), "r+");

	if (f == NULL)
	{
		Con_Printf("No server connection history.\n");
		return;
	}

	if (fgets(name, NET_NAMELEN, f) == NULL)
	{
		Con_Printf("Error reading from file.\n");
		fclose(f);
		return;
	}
		
	retry:
		if (cls.state == ca_disconnected)
			Cbuf_AddText(va("connect %s\n", name));
		else
		{
			CL_Disconnect();
			if (cls.state == ca_disconnected)//if a server crash; can create an endless loop error here CLIENTS	arent disconnected just in limbo. :(
				goto retry;
		}

	fclose(f);
}

qboolean Valid_IP(const char* ip_str) // woods #connectfilter
{
	int count = 0;
	for (int i = 0; i < strlen(ip_str); i++)
		if (ip_str[i] == '.')
			count++;

	if (count != 3) // three periods
		return false;

	int num1, num2, num3, num4;
	if (sscanf(ip_str, "%d.%d.%d.%d", &num1, &num2, &num3, &num4) != 4) // four numbers
		return false;

	if (num1 < 0 || num1 > 255 || num2 < 0 || num2 > 255 || num3 < 0 || num3 > 255 || num4 < 0 || num4 > 255)  // 0-255 numbers
		return false;

	return true;
}


qboolean Valid_Domain(const char* domain_str) // woods #connectfilter
{
	int count = 0;
	for (int i = 0; i < strlen(domain_str); i++)  // count the periods
	{
		if (domain_str[i] == '.') {
			count++;
		}
	}

	if (count < 1 || count > 4)  // adjust for international domains
		return false;

	return true;
}

qboolean Valid_Port(char* address) // woods #connectfilter
{
	char* port_start = strchr(address, ':');
	if (port_start != NULL) 
	{
		int port_len = strlen(port_start + 1);
		if (port_len == 5)
		{
			for (int i = 0; i < port_len; i++) 
			{
				if (!isdigit(*(port_start + i + 1))) 
					return false;
			}
			return true;
		}
		else 
			return false;
	}
	return true;
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
static void Host_Connect_f (void)
{
	char	name[MAX_QPATH];
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
		if ((((Valid_Domain(name)) || (Valid_IP(name))) && (Valid_Port(name))) || !q_strcasecmp(name, "local") || !q_strcasecmp(name, "localhost")) // woods #connectfilter -- avoid client lockup if possible
		{
			strcpy(lastcattempt, name); // woods verbose connection info
			CL_EstablishConnection(name);
			Host_Reconnect_Sv_f();

			mpservertime = SDL_GetTicks(); // woods #servertime
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

#define	SAVEGAME_VERSION	5

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current
===============
*/
static void Host_SavegameComment (char *text)
{
	int		i;
	char	kills[20];
	char	*p1, *p2;

	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
		text[i] = ' ';

// Remove CR/LFs from level name to avoid broken saves, e.g. with autumn_sp map:
// https://celephais.net/board/view_thread.php?id=60452&start=3666
	p1 = strchr(cl.levelname, '\n');
	p2 = strchr(cl.levelname, '\r');
	if (p1 != NULL) *p1 = 0;
	if (p2 != NULL) *p2 = 0;

	i = (int) strlen(cl.levelname);
	if (i > 22) i = 22;
	memcpy (text, cl.levelname, (size_t)i);
	sprintf (kills,"kills:%3i/%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
	memcpy (text+22, kills, strlen(kills));
// convert space to _ to make stdio happy
	for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
	{
		if (text[i] == ' ')
			text[i] = '_';
	}
	if (p1 != NULL) *p1 = '\n';
	if (p2 != NULL) *p2 = '\r';
	text[SAVEGAME_COMMENT_LENGTH] = '\0';
}

static void Host_InvalidateSave(const char* relname) // woods #autoload (iw)
{
	if (!strcmp(sv.lastsave, relname))
		sv.lastsave[0] = '\0';
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
	FILE	*f;
	int	i;
	char	comment[SAVEGAME_COMMENT_LENGTH+1];

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

	if (Cmd_Argc() != 2)
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
	Con_Printf("Saving game to ^m%s^m...\n", relname);

	q_snprintf(name, sizeof(name), "%s/%s", com_gamedir, relname);


	f = fopen (name, "w");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	PR_SwitchQCVM(&sv.qcvm);

	fprintf (f, "%i\n", SAVEGAME_VERSION);
	Host_SavegameComment (comment);
	fprintf (f, "%s\n", comment);
	for (i = 0; i < NUM_BASIC_SPAWN_PARMS; i++)
		fprintf (f, "%f\n", svs.clients->spawn_parms[i]);
	fprintf (f, "%d\n", current_skill);
	fprintf (f, "%s\n", sv.name);
	fprintf (f, "%f\n", qcvm->time);

// write the light styles
	for (i = 0; i < MAX_LIGHTSTYLES_VANILLA; i++)
	{
		if (sv.lightstyles[i])
			fprintf (f, "%s\n", sv.lightstyles[i]);
		else
			fprintf (f,"m\n");
	}

	ED_WriteGlobals (f);
	for (i = 0; i < qcvm->num_edicts; i++)
	{
		ED_Write (f, EDICT_NUM(i));
		fflush (f);
	}

	//add extra info (lightstyles, precaches, etc) in a way that's supposed to be compatible with DP.
	//sidenote - this provides extended lightstyles and support for late precaches
	//it does NOT protect against spawnfunc precache changes - we would need to include makestatics here too (and optionally baselines, or just recalculate those).
	fprintf(f, "/*\n");
	fprintf(f, "// QuakeSpasm extended savegame\n");
	for (i = MAX_LIGHTSTYLES_VANILLA; i < MAX_LIGHTSTYLES; i++)
	{
		if (sv.lightstyles[i])
			fprintf (f, "sv.lightstyles %i \"%s\"\n", i, sv.lightstyles[i]);
	}
	for (i = 1; i < MAX_MODELS; i++)
	{
		if (sv.model_precache[i])
			fprintf (f, "sv.model_precache %i \"%s\"\n", i, sv.model_precache[i]);
	}
	for (i = 1; i < MAX_SOUNDS; i++)
	{
		if (sv.sound_precache[i])
			fprintf (f, "sv.sound_precache %i \"%s\"\n", i, sv.sound_precache[i]);
	}
	for (i = 1; i < MAX_PARTICLETYPES; i++)
	{
		if (sv.particle_precache[i])
			fprintf (f, "sv.particle_precache %i \"%s\"\n", i, sv.particle_precache[i]);
	}

	fprintf (f, "sv.serverflags %i\n", svs.serverflags);
	for (i = NUM_BASIC_SPAWN_PARMS ; i < NUM_TOTAL_SPAWN_PARMS ; i++)
	{
		if (svs.clients->spawn_parms[i])
			fprintf (f, "spawnparm %i \"%f\"\n", i+1, svs.clients->spawn_parms[i]);
	}

	fprintf(f, "*/\n");


	fclose (f);
	Con_Printf ("done.\n");
	PR_SwitchQCVM(NULL);

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
	float	time, tfloat;
	const char	*data;
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
	Con_Printf("Loading game from ^m%s^m...\n", relname);

// we can't call SCR_BeginLoadingPlaque, because too much stack space has
// been used.  The menu calls it before stuffing loadgame command
//	SCR_BeginLoadingPlaque ();

	q_snprintf(name, sizeof(name), "%s/%s", com_gamedir, relname); // woods #autoload (iw)

// avoid leaking if the previous Host_Loadgame_f failed with a Host_Error
	if (start != NULL)
		free (start);
	
	start = (char *) COM_LoadMallocFile_TextMode_OSPath(name, NULL);
	if (start == NULL)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		Host_InvalidateSave(relname); // woods #autoload (iw)
		return;
	}

	data = start;
	data = COM_ParseIntNewline (data, &version);
	if (version != SAVEGAME_VERSION)
	{
		free (start);
		start = NULL;
		if (sv.autoloading) // woods #autoload (iw)
			Con_Printf("ERROR: Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);
		else
			Host_Error("Savegame is version %i, not %i", version, SAVEGAME_VERSION);
		Host_InvalidateSave(relname);
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

	CL_Disconnect_f ();

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
			while ((end = strchr(ext, '\n')))
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
					if (idx >= 1 && idx < MAX_MODELS)
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
			data = ED_ParseGlobals (data);
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

	qcvm->num_edicts = entnum;
	qcvm->time = time;

	free (start);
	start = NULL;

	for (i = 0; i < NUM_TOTAL_SPAWN_PARMS; i++)
		svs.clients->spawn_parms[i] = spawn_parms[i];

	PR_SwitchQCVM(NULL);

	q_strlcpy(sv.lastsave, relname, sizeof(sv.lastsave)); // woods #autoload (iw)

	if (cls.state != ca_dedicated)
	{
		CL_EstablishConnection ("local");
		Host_Reconnect_Sv_f ();
	}
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

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\"name\" is \"%s\"\n", cl_name.string);
		return;
	}
	if (Cmd_Argc () == 2)
		q_strlcpy(newName, Cmd_Argv(1), sizeof(newName));
	else
		q_strlcpy(newName, Cmd_Args(), sizeof(newName));
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
		if (Q_strcmp(cl_name.string, newName) == 0)
			return;
		Cvar_Set ("name", newName);
	}
	else
		SV_UpdateInfo((host_client-svs.clients)+1, "name", newName);

	// JPG 1.05 - log the IP address woods for #iplog  (log the IP address)
	if (cls.state == ca_connected && !cls.demoplayback)
		if (sscanf(net_activeSockets->maskedaddress, "%d.%d.%d", &a, &b, &c) == 3)
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
	int		j;
	client_t	*client;
	client_t	*save;
	const char	*p;
	char		text[MAXCMDLINE], *p2;
	qboolean	quoted;
	qboolean	fromServer = false;

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

	p = Cmd_Args();
// remove quotes if present
	quoted = false;
	if (*p == '\"')
	{
		p++;
		quoted = true;
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
		host_client = client;
		SV_ClientPrintf("%s", text);
	}
	host_client = save;

	if (cls.state == ca_dedicated)
		Sys_Printf("%s", &text[1]);
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

	if (strstr(cl.lastchat, ": likes")) // no intinite likes
		return;

	if (cl.lastchat[0] == '\0')
	{
		Con_Printf("\nnothing to like\n\n");
		lastLikeTime = currentTime;
		return;
	}

	q_snprintf(text, sizeof(text), "say likes %s", cl.lastchat + 1);
	Cbuf_AddText(text);
}

static void Host_Tell_f(void) // modified by woods to accept wildcards, status #s like proquake identify #tell+
{
	int		i, j;
	client_t	*client;
	client_t	*save;
	const char	*p;
	char		text[MAXCMDLINE], *p2;
	qboolean	quoted;
	char name[16];

	int unfun_match(const char* s1, char* s2);

	q_strlcpy(name, Cmd_Argv(1), sizeof(name)); // set name to the name of player telling
	i = Q_atoi(Cmd_Argv(1)) - 1; // set i to the NUMBER of player telling

	if (i == -1)
	{
		if (sv.active)
		{
			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].active && unfun_match(Cmd_Argv(1), svs.clients[i].name))
					break;
			}
		}
		else
		{
			for (i = 0; i < cl.maxclients; i++)
			{
				if (unfun_match(Cmd_Argv(1), cl.scores[i].name))
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
		strncpy(name, svs.clients[i].name, 15);
		name[15] = 0;
	}
	else
	{
		if (i < 0 || i >= cl.maxclients || !cl.scores[i].name[0])
		{
 			Con_Printf("No such player\n");
			return;
		}
		else
		{
			strncpy(name, cl.scores[i].name, 15); // copy scoreboard number player to name
			name[15] = 0;
			S_LocalSound("misc/talk.wav"); // woods #tell+
		}
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
	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active || !client->spawned)
			continue;
		if (q_strcasecmp(client->name, name))
			continue;
		host_client = client;
		SV_ClientPrintf("%s", text);
		recipient_found = true; // woods #tell+
		break;
	}
	host_client = save;

	if (recipient_found) // woods #tell+
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
		MSG_WriteAngle (&host_client->message, ent->v.angles[i], sv.protocolflags );
	MSG_WriteAngle (&host_client->message, 0, sv.protocolflags );

	if (!(host_client->protocol_pext2 & PEXT2_REPLACEMENTDELTAS))
		SV_WriteClientdataToMessage (host_client, &host_client->message);

	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 3);
	host_client->sendsignon = true;
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

/*
==================
Host_Give_f
==================
*/
static void Host_Give_f (void)
{
	const char	*t;
	int	v;
	eval_t	*val;

	if (cmd_source != src_client)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch)
		return;

	t = Cmd_Argv(1);
	v = atoi (Cmd_Argv(2));

	switch (t[0])
	{
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
		// MED 01/04/97 added hipnotic give stuff
		if (hipnotic)
		{
			if (t[0] == '6')
			{
				if (t[1] == 'a')
					sv_player->v.items = (int)sv_player->v.items | HIT_PROXIMITY_GUN;
				else
					sv_player->v.items = (int)sv_player->v.items | IT_GRENADE_LAUNCHER;
			}
			else if (t[0] == '9')
				sv_player->v.items = (int)sv_player->v.items | HIT_LASER_CANNON;
			else if (t[0] == '0')
				sv_player->v.items = (int)sv_player->v.items | HIT_MJOLNIR;
			else if (t[0] >= '2')
				sv_player->v.items = (int)sv_player->v.items | (IT_SHOTGUN << (t[0] - '2'));
		}
		else
		{
			if (t[0] >= '2')
				sv_player->v.items = (int)sv_player->v.items | (IT_SHOTGUN << (t[0] - '2'));
		}
		break;

	case 's':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_shells1"));
			if (val)
				val->_float = v;
		}
		sv_player->v.ammo_shells = v;
		break;

	case 'n':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_nails1"));
			if (val)
			{
				val->_float = v;
				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_nails = v;
			}
		}
		else
		{
			sv_player->v.ammo_nails = v;
		}
		break;

	case 'l':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_lava_nails"));
			if (val)
			{
				val->_float = v;
				if (sv_player->v.weapon > IT_LIGHTNING)
					sv_player->v.ammo_nails = v;
			}
		}
		break;

	case 'r':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_rockets1"));
			if (val)
			{
				val->_float = v;
				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_rockets = v;
			}
		}
		else
		{
			sv_player->v.ammo_rockets = v;
		}
		break;

	case 'm':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_multi_rockets"));
			if (val)
			{
				val->_float = v;
				if (sv_player->v.weapon > IT_LIGHTNING)
					sv_player->v.ammo_rockets = v;
			}
		}
		break;

	case 'h':
		sv_player->v.health = v;
		break;

	case 'c':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_cells1"));
			if (val)
			{
				val->_float = v;
				if (sv_player->v.weapon <= IT_LIGHTNING)
					sv_player->v.ammo_cells = v;
			}
		}
		else
		{
			sv_player->v.ammo_cells = v;
		}
		break;

	case 'p':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, ED_FindFieldOffset("ammo_plasma"));
			if (val)
			{
				val->_float = v;
				if (sv_player->v.weapon > IT_LIGHTNING)
					sv_player->v.ammo_cells = v;
			}
		}
		break;

	//johnfitz -- give armour
	case 'a':
		if (v > 150)
		{
			sv_player->v.armortype = 0.8;
			sv_player->v.armorvalue = v;
			sv_player->v.items = sv_player->v.items -
					((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
					IT_ARMOR3;
		}
		else if (v > 100)
		{
			sv_player->v.armortype = 0.6;
			sv_player->v.armorvalue = v;
			sv_player->v.items = sv_player->v.items -
					((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
					IT_ARMOR2;
		}
		else if (v >= 0)
		{
			sv_player->v.armortype = 0.3;
			sv_player->v.armorvalue = v;
			sv_player->v.items = sv_player->v.items -
					((int)(sv_player->v.items) & (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
					IT_ARMOR1;
		}
		break;
		//johnfitz
	}

	//johnfitz -- update currentammo to match new ammo (so statusbar updates correctly)
	switch ((int)(sv_player->v.weapon))
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
	case RIT_LAVA_NAILGUN: //same as IT_AXE
		if (rogue)
			sv_player->v.currentammo = sv_player->v.ammo_nails;
		break;
	case RIT_PLASMA_GUN: //same as HIT_PROXIMITY_GUN
		if (rogue)
			sv_player->v.currentammo = sv_player->v.ammo_cells;
		if (hipnotic)
			sv_player->v.currentammo = sv_player->v.ammo_rockets;
		break;
	}
	//johnfitz
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
		for (i = 0; s1[i]; i++)
		{
			if (unfun[s1[i] & 127] != unfun[s2[i] & 127])
				break;
		}
		if (!s1[i])
			return true;
	}
	return false;
}

unsigned int Send_Identify_Command (unsigned int interval, void* param) // woods -- #identify+
{
	char* lastconnected = (char*)param;

	unsigned char* ch; // woods dequake
	for (ch = (unsigned char*)lastconnected; *ch; ch++)
		*ch = dequake[*ch];

	Con_Printf("\n");
	Cbuf_AddText(va("identify %s\n\n", lastconnected));
	return 0;
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
			char* lastconnected_copy =  SDL_strdup(lastconnected); // copy of lastconnected to pass to the callback function
			SDL_AddTimer(750, Send_Identify_Command, lastconnected_copy);
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

	i = Q_atoi(Cmd_Argv(1)) - 1;
	if (i == -1)
	{
		if (sv.active)
		{
			for (i = 0; i < svs.maxclients; i++)
			{
				if (svs.clients[i].active && unfun_match(Cmd_Argv(1), svs.clients[i].name))
					break;
			}
		}
		else
		{
			for (i = 0; i < cl.maxclients; i++)
			{
				if (unfun_match(Cmd_Argv(1), cl.scores[i].name))
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
		if (sscanf(svs.clients[i].netconnection->maskedaddress, "%d.%d.%d", &a, &b, &c) != 3)
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
		if (host_client->download.file)
		{	//abort the current download if the previous didn't terminate properly.
			SV_ClientPrintf("cancelling previous download\n");
			MSG_WriteByte (&host_client->message, svc_stufftext);
			MSG_WriteString (&host_client->message, "\nstopdownload\n");
			fclose(host_client->download.file);
			host_client->download.file = NULL;
		}

		host_client->download.size = 0;
		host_client->download.started = false;
		host_client->download.sendpos = 0;
		host_client->download.ackpos = 0;
		
		fsize = -1;
		if (!COM_DownloadNameOkay(fname))
			SV_ClientPrintf("refusing download of %s - restricted filename\n", fname);
		else
		{
			fsize = COM_FOpenFile(fname, &host_client->download.file, NULL);
			if (!host_client->download.file)
				SV_ClientPrintf("server does not have file %s\n", fname);
			else if (file_from_pak)
			{
				SV_ClientPrintf("refusing download of %s from inside pak\n", fname);
				fclose(host_client->download.file);
				host_client->download.file = NULL;
			}
			else if (fsize < 0 || fsize > 50*1024*1024)
			{
				SV_ClientPrintf("refusing download of large file %s\n", fname);
				fclose(host_client->download.file);
				host_client->download.file = NULL;
			}
		}

		host_client->download.size = (unsigned int)fsize;
		if (host_client->download.file)
		{
			host_client->download.startpos = ftell(host_client->download.file);
			Con_Printf("downloading %s to %s\n", fname, host_client->name);
			MSG_WriteByte (&host_client->message, svc_stufftext);
			MSG_WriteString (&host_client->message, va("\ncl_downloadbegin %u \"%s\"\n", host_client->download.size, fname));
			q_strlcpy(host_client->download.name, fname, sizeof(host_client->download.name));
		}
		else if (!COM_FileExists(fname, NULL) && strstr(fname, ".loc")) // woods, more info for .loc refusals
		{
			Con_Printf("%s attempted download of %s, server does not have file\n", host_client->name, fname);
			MSG_WriteByte(&host_client->message, svc_stufftext);
			MSG_WriteString(&host_client->message, "\nstopdownload\n");
		}
		else
		{
			Con_Printf("refusing download of %s to %s\n", fname, host_client->name);
			MSG_WriteByte (&host_client->message, svc_stufftext);
			MSG_WriteString (&host_client->message, "\nstopdownload\n");
		}
		host_client->sendsignon = true;	//override any keepalive issues.
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
	if (host_client->download.file)
		host_client->download.started = true;
	else
		SV_ClientPrintf("no download started\n");
}
//just writes download data onto the end of the outgoing unreliable buffer
void Host_AppendDownloadData(client_t *client, sizebuf_t *buf)
{
	if (buf->cursize+7 > buf->maxsize)
		return;	//no space for anything
	if (client->download.file && client->download.started)
	{
		byte tbuf[1400];	//don't be too aggressive, ethernet mtu is about 1450
		unsigned int size = client->download.size - client->download.sendpos;
		//size might be 0 at eof, and that's needed to avoid failure if we drop the last few packets
		if (size > sizeof(tbuf))
			size = sizeof(tbuf);
		if ((int)size > buf->maxsize-(buf->cursize+7))
			size = (int)(buf->maxsize-(buf->cursize+7));	//don't overflow

		if (size && fread(tbuf, 1, size, host_client->download.file) < size)
			client->download.ackpos = client->download.sendpos = client->download.size;	//some kind of error...
		else
		{
			MSG_WriteByte(buf, svcdp_downloaddata);
			MSG_WriteLong(buf, client->download.sendpos);
			MSG_WriteShort(buf, size);
			SZ_Write(buf, tbuf, size);
			client->download.sendpos += size;
		}
	}
}
//parses incoming acks from the client, so we know which parts of the file the client actually received.
void Host_DownloadAck(client_t *client)
{
	unsigned int start = MSG_ReadLong();
	unsigned int size = (unsigned short)MSG_ReadShort();

	if (!client->download.started || !client->download.file)
		return;

	if (client->download.ackpos < start)
	{
		client->download.sendpos = client->download.ackpos;//there was a gap, rewind to the known gap
		fseek(client->download.file, host_client->download.startpos+client->download.sendpos, SEEK_SET);
	}
	else if (client->download.ackpos < start+size)
		client->download.ackpos = start+size;	//no loss yet.
	//else FIXME: build a log of parts known to be acked to avoid resending them later, skip past them in acks

	if (client->download.ackpos == client->download.size)
	{
		unsigned int hash = 0;
		byte *data;
		client->download.started = false;

		data = malloc(client->download.size);
		if (data)
		{
			fseek(client->download.file, host_client->download.startpos, SEEK_SET);
			fread(data, 1, host_client->download.size, client->download.file);
			hash = CRC_Block(data, host_client->download.size);
			free(data);
		}
		fclose(client->download.file);
		client->download.file = NULL;

		MSG_WriteByte (&host_client->message, svc_stufftext);
		MSG_WriteString (&host_client->message, va("cl_downloadfinished %u %u \"%s\"\n", client->download.size, hash, client->download.name));
		*client->download.name = 0;
		host_client->sendsignon = true;	//override any keepalive issues.
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
				if (*key == '*') // woods #*ver
					return;	//users may not change * keys (beyond initial connection anyway).
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

			if (i >= cl.maxclients)
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

	Cmd_AddCommand ("maps", Host_Maps_f); //johnfitz
	Cmd_AddCommand ("mods", Host_Mods_f); //johnfitz
	Cmd_AddCommand ("games", Host_Mods_f); // as an alias to "mods" -- S.A. / QuakeSpasm
	Cmd_AddCommand ("mapname", Host_Mapname_f); //johnfitz
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
	Cmd_AddCommand_ClientCommandQC ("color", Host_Color_f);
	Cmd_AddCommand_ClientCommandQC ("kill", Host_Kill_f);
	Cmd_AddCommand_ClientCommandQC ("pause", Host_Pause_f);
	Cmd_AddCommand_ClientCommand ("spawn", Host_Spawn_f);
	Cmd_AddCommand_ClientCommand ("begin", Host_Begin_f);
	Cmd_AddCommand_ClientCommand ("prespawn", Host_PreSpawn_f);
	Cmd_AddCommand_ClientCommandQC ("kick", Host_Kick_f);
	Cmd_AddCommand_ClientCommand ("ping", Host_Ping_f);
	Cmd_AddCommand ("load", Host_Loadgame_f);
	Cmd_AddCommand ("save", Host_Savegame_f);
	Cmd_AddCommand_ClientCommandQC ("give", Host_Give_f);
	Cmd_AddCommand_ClientCommand ("download", Host_Download_f);
	Cmd_AddCommand_ClientCommand ("sv_startdownload", Host_StartDownload_f);
	Cmd_AddCommand_ClientCommand ("enablecsqc", Host_EnableCSQC_f);
	Cmd_AddCommand_ClientCommand ("disablecsqc", Host_DisableCSQC_f);

	Cmd_AddCommand ("startdemos", Host_Startdemos_f);
	Cmd_AddCommand ("demos", Host_Demos_f);
	Cmd_AddCommand ("stopdemo", Host_Stopdemo_f);

	Cmd_AddCommand ("viewmodel", Host_Viewmodel_f);
	Cmd_AddCommand ("viewframe", Host_Viewframe_f);
	Cmd_AddCommand ("viewnext", Host_Viewnext_f);
	Cmd_AddCommand ("viewprev", Host_Viewprev_f);

	Cmd_AddCommand("identify", Host_Identify_f);	// JPG 1.05 - player IP logging // woods #iplog
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
			while (fgets(msg_rand[i][num_rand[i]], 128, f))
			{
				char* ch = strchr(msg_rand[i][num_rand[i]], '\n');
				if (ch)
					*ch = 0;
				if (msg_rand[i][num_rand[i]][0])
					num_rand[i]++;
			}
			fclose(f);
		}
	}

}

