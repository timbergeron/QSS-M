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
// cmd.c -- Quake script command processing module

#include "quakedef.h"

cvar_t	cl_nopext = {"cl_nopext","0",CVAR_NONE};	//Spike -- prevent autodetection of protocol extensions, so that servers fall back to only their base protocol (without needing to reconfigure the server. Requires reconnect.
cvar_t	cmd_warncmd = {"cl_warncmd","1",CVAR_NONE};	//Spike -- prevent autodetection of protocol extensions, so that servers fall back to only their base protocol (without needing to reconfigure the server. Requires reconnect.
cvar_t	cl_aliasoverlap = {"cl_aliasoverlap","1",CVAR_NONE}; //Spike -- Rename new aliases if they would override cvar names.

void Cmd_ForwardToServer (void);

#define	MAX_ALIAS_NAME	32

#define CMDLINE_LENGTH 256 //johnfitz -- mirrored in common.c

typedef struct cmdalias_s
{
	struct cmdalias_s	*next;
	char	name[MAX_ALIAS_NAME];
	char	*value;
} cmdalias_t;

cmdalias_t	*cmd_alias;

qboolean	cmd_wait;

extern qboolean ctrlpressed; // woods #saymodifier

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
void Cmd_Wait_f (void)
{
	cmd_wait = true;
}

/*
===============
Cmd_CfgMarker_f -- woods - Skip apropos text for unknown commands executed from config (ironwail)
===============
*/
static qboolean in_cfg_exec = false;
static void Cmd_CfgMarker_f(void)
{
	in_cfg_exec = false;
}

/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

sizebuf_t	cmd_text;

/*
============
Cbuf_Init
============
*/
void Cbuf_Init (void)
{
	SZ_Alloc (&cmd_text, 1<<18);		// space for commands and script files. spike -- was 8192, but modern configs can be _HUGE_, at least if they contain lots of comments/docs for things.
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText (const char *text)
{
	int		l;

	l = Q_strlen (text);

	if (cmd_text.cursize + l >= cmd_text.maxsize)
	{
		Con_Printf ("Cbuf_AddText: overflow\n");
		return;
	}

	SZ_Write (&cmd_text, text, l);
}
void Cbuf_AddTextLen (const char *text, int l)
{
	if (cmd_text.cursize + l >= cmd_text.maxsize)
	{
		Con_Printf ("Cbuf_AddText: overflow\n");
		return;
	}

	SZ_Write (&cmd_text, text, l);
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
FIXME: actually change the command buffer to do less copying
============
*/
void Cbuf_InsertText (const char *text)
{
	char	*temp;
	int		templen;

// copy off any commands still remaining in the exec buffer
	templen = cmd_text.cursize;
	if (templen)
	{
		temp = (char *) Z_Malloc (templen);
		Q_memcpy (temp, cmd_text.data, templen);
		SZ_Clear (&cmd_text);
	}
	else
		temp = NULL;	// shut up compiler

// add the entire text of the file
	Cbuf_AddText (text);
	SZ_Write (&cmd_text, "\n", 1);
// add the copied off data
	if (templen)
	{
		SZ_Write (&cmd_text, temp, templen);
		Z_Free (temp);
	}
}

//Spike: for renderer/server isolation
void Cbuf_Waited(void)
{
	cmd_wait = false;
}

/*
============
Cbuf_Execute

Spike: reworked 'wait' for renderer/server rate independance
============
*/
void Cbuf_Execute (void)
{
	int		i;
	char	*text;
	char	line[1024];
	int		quotes, comment;

	while (cmd_text.cursize && !cmd_wait)
	{
// find a \n or ; line break
		text = (char *)cmd_text.data;

		quotes = 0;
		comment = 0;
		for (i=0 ; i< cmd_text.cursize ; i++)
		{
			if (text[i] == '"')
				quotes++;
			if (text[i] == '/' && text[i+1] == '/')
				comment=true;
			if ( !(quotes&1) && !comment &&  text[i] == ';')
				break;	// don't break if inside a quoted string
			if (text[i] == '\n')
				break;
		}

		if (i > (int)sizeof(line) - 1)
		{
			memcpy (line, text, sizeof(line) - 1);
			line[sizeof(line) - 1] = 0;
		}
		else
		{
			memcpy (line, text, i);
			line[i] = 0;
		}

// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec, alias) can insert data at the
// beginning of the text buffer

		if (i == cmd_text.cursize)
			cmd_text.cursize = 0;
		else
		{
			i++;
			cmd_text.cursize -= i;
			memmove (text, text + i, cmd_text.cursize);
		}

// execute the command line
		Cmd_ExecuteString (line, src_command);
	}
}

/*
==============================================================================

						SCRIPT COMMANDS

==============================================================================
*/

/*
===============
Cmd_StuffCmds_f -- johnfitz -- rewritten to read the "cmdline" cvar, for use with dynamic mod loading

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void Cmd_StuffCmds_f (void)
{
	extern cvar_t cmdline;
	char	cmds[CMDLINE_LENGTH];
	int		i, j, plus;

	plus = false;	// On Unix, argv[0] is command name

	for (i = 0, j = 0; cmdline.string[i]; i++)
	{
		if (cmdline.string[i] == '+')
		{
			plus = true;
			if (j > 0)
			{
				cmds[j-1] = ';';
				cmds[j++] = ' ';
			}
		}
		else if (cmdline.string[i] == '-' &&
			(i==0 || cmdline.string[i-1] == ' ')) //johnfitz -- allow hypenated map names with +map
				plus = false;
		else if (plus)
			cmds[j++] = cmdline.string[i];
	}
	cmds[j] = 0;

	Host_RunCvarMigrations ();
	Cbuf_InsertText (cmds);
}

/* id1/pak0.pak from 2021 re-release doesn't have a default.cfg
 * embedding Quakespasm's customized default.cfg for that...  */
#include "default_cfg.h"

/*
===============
Cmd_Exec_f
===============
*/
void Cmd_Exec_f (void)
{
	const char	*f, *config_f;
	char		config_path[MAX_QPATH];
	qboolean	config_only;
	int		mark;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("exec <filename> : execute a script file\n");
		return;
	}

	mark = Hunk_LowMark ();
	config_only = COM_ConfigFilePrefersConfigsDir(Cmd_Argv(1));
	f = config_only ? NULL : (const char *)COM_LoadHunkFile (Cmd_Argv(1), NULL);
	config_f = NULL;
	if (COM_ConfigFileUsesConfigsDir(Cmd_Argv(1)))
	{
		q_snprintf(config_path, sizeof(config_path), "configs/%s", Cmd_Argv(1));
		config_f = (const char *)COM_LoadHunkFile (config_path, NULL);
	}
	if (!f && !strcmp(Cmd_Argv(1), "default.cfg")) {
		f = default_cfg;	/* see above.. */
	}
	if (!f && !config_f)
	{
		if (cmd_warncmd.value)
			Con_Printf ("couldn't exec %s\n",Cmd_Argv(1));
		return;
	}
	if (cmd_warncmd.value)
	{
		if (f)
			Con_Printf ("execing %s\n",Cmd_Argv(1));
		if (config_f)
			Con_Printf ("execing %s\n",config_path);
	}

	if (!in_cfg_exec) // woods - Skip apropos text for unknown commands executed from config (ironwail)
	{
		in_cfg_exec = true;
		// Note: this will be executed *after* the config
		Cbuf_InsertText("__cfgmarker");
	}

	Cbuf_InsertText ("\n");	//just in case there was no trailing \n.
	if (config_f)
		Cbuf_InsertText (config_f);
	if (f)
		Cbuf_InsertText (f);
	if (f != default_cfg || config_f) {
		Hunk_FreeToLowMark (mark);
	}
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void Cmd_Echo_f (void)
{
	int		i;

	for (i=1 ; i<Cmd_Argc() ; i++)
		Con_Printf ("%s ",Cmd_Argv(i));
	Con_Printf ("\n");
}

/*
===============
Cmd_Alias_f -- johnfitz -- rewritten

Creates a new command that executes a command string (possibly ; seperated)
===============
*/
void Cmd_Alias_f (void)
{
	cmdalias_t	*a;
	char		cmd[1024];
	int			i, c, count = 0; // woods #search
	const char	*s;
	qboolean found = false; // woods #search

	switch (Cmd_Argc())
	{
	case 1: //list all aliases
		for (a = cmd_alias, i = 0; a; a=a->next, i++)
			Con_SafePrintf ("   %s: %s", a->name, a->value);
		if (i)
			Con_SafePrintf ("%i alias command(s)\n", i);
		else
			Con_SafePrintf ("no alias commands found\n");
		break;
	case 2: //output current alias string
		for (a = cmd_alias ; a ; a=a->next)
			if (!strcmp(Cmd_Argv(1), a->name))
			{
				Con_Printf("   %s: %s", a->name, a->value);
				found = true;
				break;
			}

		if (!found) // woods -- if exact alias not found, search for substring in all aliases names and values
		{
			char buf[MAX_OSPATH];
			char buf2[MAX_OSPATH];
			const char* search_arg = Cmd_Argv(1);

			for (a = cmd_alias; a; a = a->next)
			{
				const char* nameFound = q_strcasestr(a->name, search_arg);
				const char* valueFound = q_strcasestr(a->value, search_arg);

				if (nameFound || valueFound)
				{
					if (nameFound)
						COM_TintSubstring(a->name, search_arg, buf, sizeof(buf));

					if (valueFound)
						COM_TintSubstring(a->value, search_arg, buf2, sizeof(buf2));

					Con_Printf("   %s: %s", nameFound ? buf : a->name, valueFound ? buf2 : a->value);
					count++;
				}
			}
			if (count > 0)
				Con_Printf("\nexact alias not found. however, %d instance(s) of the search string were found\n", count);
			else
				Con_Printf("\nno alias or instances of the search string found\n");
		}
	
	break;
	default: //set alias string
		s = Cmd_Argv(1);
		if (strlen(s) >= MAX_ALIAS_NAME)
		{
			Con_Printf ("Alias name is too long\n");
			return;
		}

		// check for overlap with a command
		// commands always take precedence over aliases (so mods can't clobber 'quit' etc), 
		// so creating an alias with one of these names is stupid. always try to rename them.
		char final_name[MAX_ALIAS_NAME];
		q_strlcpy(final_name, s, sizeof(final_name));
		if (Cmd_Exists(s) || (!cl_aliasoverlap.value && Cvar_FindVar(s)))
		{ //aliases take precedence over cvars (while cvars can be set via 'set'), so user's choice.
			// Append "_a" to conflicting names
			if (q_strlcat(final_name, "_a", sizeof(final_name)) >= sizeof(final_name))
			{
				Con_Printf("Alias name would be too long after renaming\n");
				return;
			}
			Con_Printf("alias ^m%s^m: renamed to ^m%s^m due to %s conflict\n",
				s, final_name, Cmd_Exists(s) ? "command" : "cvar");
		}

		// if the alias already exists, reuse it
		for (a = cmd_alias ; a ; a=a->next)
		{
			if (!strcmp(final_name, a->name))
			{
				Z_Free (a->value);
				break;
			}
		}

		if (!a)
		{
			a = (cmdalias_t *) Z_Malloc (sizeof(cmdalias_t));
			a->next = cmd_alias;
			cmd_alias = a;
		}
		strcpy (a->name, final_name);

		// copy the rest of the command line
		cmd[0] = 0;		// start out with a null string
		c = Cmd_Argc();
		for (i = 2; i < c; i++)
		{
			q_strlcat (cmd, Cmd_Argv(i), sizeof(cmd));
			if (i != c - 1)
				q_strlcat (cmd, " ", sizeof(cmd));
		}
		if (q_strlcat(cmd, "\n", sizeof(cmd)) >= sizeof(cmd))
		{
			Con_Printf("alias value too long!\n");
			cmd[0] = '\n';	// nullify the string
			cmd[1] = 0;
		}

		a->value = Z_Strdup (cmd);
		break;
	}
}

/*
==============
Alias_Edit_f -- woods #aliasedit

Allows the user to edit an existing alias by placing the alias command
into the console's edit line for modification.
==============
*/
void Alias_Edit_f (void)
{
	int argc = Cmd_Argc();

	if (argc < 2)
	{
		Con_Printf("\nusage:\n");
		Con_Printf("  %s <alias>\n\n", Cmd_Argv(0));
		return;
	}

	const char* alias_name = Cmd_Argv(1);

	cmdalias_t* current_alias = cmd_alias;
	while (current_alias != NULL)
	{
		if (strcmp(alias_name, current_alias->name) == 0)
		{
			break; // Alias found
		}
		current_alias = current_alias->next;
	}

	if (current_alias == NULL)
	{
		Con_Printf("\nno alias named \"%s\" found\n\n", alias_name);
		return;
	}

	char alias_value_cleaned[MAXCMDLINE]; // Prepare a buffer to store the cleaned alias value

	q_snprintf(alias_value_cleaned, sizeof(alias_value_cleaned), "%s", current_alias->value);

	size_t alias_len = strlen(alias_value_cleaned); // Remove the trailing newline character if present
	if (alias_len > 0 && alias_value_cleaned[alias_len - 1] == '\n')
		alias_value_cleaned[alias_len - 1] = '\0';

	char final_command[MAXCMDLINE]; // Construct the alias command string without the newline
	q_snprintf(final_command, sizeof(final_command), "alias \"%s\" \"%s\"", alias_name, alias_value_cleaned);

	if (edit_line < 0 || (size_t)edit_line >= CMDLINES) // Validate the edit_line index

	{
		Con_Printf("edit line index (%d) is out of bounds. valid range is 0 to %d.\n", edit_line, CMDLINES - 1);
		return;
	}

	// Initialize the edit line with the prompt character ']'
	key_lines[edit_line][0] = ']';
	key_lines[edit_line][1] = '\0';

	q_snprintf(key_lines[edit_line] + 1, sizeof(key_lines[edit_line]) - 1, "%s", final_command);

	key_linepos = strlen(key_lines[edit_line]); // Update the cursor position to the end of the line
}

/*
===============
Cmd_Unalias_f -- johnfitz
===============
*/
void Cmd_Unalias_f (void)
{
	cmdalias_t	*a, *prev;

	switch (Cmd_Argc())
	{
	default:
	case 1:
		Con_Printf("unalias <name> : delete alias\n");
		break;
	case 2:
		prev = NULL;
		for (a = cmd_alias; a; a = a->next)
		{
			if (!strcmp(Cmd_Argv(1), a->name))
			{
				if (prev)
					prev->next = a->next;
				else
					cmd_alias  = a->next;

				Z_Free (a->value);
				Z_Free (a);
				return;
			}
			prev = a;
		}
		Con_Printf ("No alias named %s\n", Cmd_Argv(1));
		break;
	}
}

qboolean Cmd_AliasExists (const char *aliasname)
{
	cmdalias_t *a;
	for (a=cmd_alias ; a ; a=a->next)
	{
		if (!q_strcasecmp (aliasname, a->name))
			return true;
	}
	return false;
}

/*
===============
Cmd_Unaliasall_f -- johnfitz
===============
*/
void Cmd_Unaliasall_f (void)
{
	cmdalias_t	*blah;

	while (cmd_alias)
	{
		blah = cmd_alias->next;
		Z_Free(cmd_alias->value);
		Z_Free(cmd_alias);
		cmd_alias = blah;
	}
}

/*
===============
Alias_WriteAliases - woods #serveralias

Writes all non-server aliases to a file as:
  alias <name> "<value>"

1. Outputs "unaliasall."
2. Categorizes '+' (key press), '-' (key release), and other aliases.
3. Filters out server aliases, sorts and pairs '+'/'-' aliases, then writes all aliases without trailing newlines.

===============
*/

#define MAX_ALIASES 1024
#define MAX_ALIAS_VALUE 1024

static qboolean Alias_NameCollides(const char* name)
{
	return (Cmd_Exists(name) || Cvar_FindVar(name));
}

qboolean IsServerAlias(const char* name, server_alias_t* server_aliases) 
{
	server_alias_t* sa;
	for (sa = server_aliases; sa; sa = sa->next) {
		if (strcmp(name, sa->name) == 0) {
			return true;
		}
	}
	return false;
}

void RemoveTrailingNewline(char* str)
{
	size_t len = strlen(str);
	while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) 
	{
		str[len - 1] = '\0';
		len--;
	}
}

int CompareAliases(const void* a, const void* b) 
{
	cmdalias_t* alias_a = *(cmdalias_t**)a;
	cmdalias_t* alias_b = *(cmdalias_t**)b;
	return q_strcasecmp(alias_a->name, alias_b->name);
}

void Alias_WriteAliases(FILE* f) 
{
	cmdalias_t* alias;
	cmdalias_t* plus_aliases[MAX_ALIASES];
	cmdalias_t* minus_aliases[MAX_ALIASES];
	cmdalias_t* other_aliases[MAX_ALIASES];
	int plus_count = 0, minus_count = 0, other_count = 0;
	int i, j;
	extern cmdalias_t* cmd_alias;
	extern server_alias_t* server_aliases;

	if (!f) {
		fprintf(stderr, "Alias_WriteAliases: Invalid file pointer\n");
		return;
	}

	// Print unaliasall at the top
	fprintf(f, "unaliasall\n");

	// Gather and categorize the aliases
	for (alias = cmd_alias; alias; alias = alias->next) {
		if (alias->value[0] != '\0') {
			if (IsServerAlias(alias->name, server_aliases)) {
				continue; // Skip server aliases
			}
			if (cfg_save_aliases.value == 1 && Alias_NameCollides(alias->name))
				continue;
			if (alias->name[0] == '+') {
				if (plus_count < MAX_ALIASES) {
					plus_aliases[plus_count++] = alias;
				}
			}
			else if (alias->name[0] == '-') {
				if (minus_count < MAX_ALIASES) {
					minus_aliases[minus_count++] = alias;
				}
			}
			else {
				if (other_count < MAX_ALIASES) {
					other_aliases[other_count++] = alias;
				}
			}
		}
	}

	// Sort each group of aliases alphabetically
	qsort(plus_aliases, plus_count, sizeof(cmdalias_t*), CompareAliases);
	qsort(minus_aliases, minus_count, sizeof(cmdalias_t*), CompareAliases);
	qsort(other_aliases, other_count, sizeof(cmdalias_t*), CompareAliases);

	// Process '+' aliases and their corresponding '-' aliases
	for (i = 0; i < plus_count; i++) {
		cmdalias_t* plus_alias = plus_aliases[i];
		qboolean found_partner = false;

		for (j = 0; j < minus_count; j++) {
			cmdalias_t* minus_alias = minus_aliases[j];

			if (q_strcasecmp(plus_alias->name + 1, minus_alias->name + 1) == 0) {
				// Remove trailing newlines from copies
				char plus_value[MAX_ALIAS_VALUE];
				char minus_value[MAX_ALIAS_VALUE];

				Q_strncpy(plus_value, plus_alias->value, MAX_ALIAS_VALUE);
				RemoveTrailingNewline(plus_value);

				Q_strncpy(minus_value, minus_alias->value, MAX_ALIAS_VALUE);
				RemoveTrailingNewline(minus_value);

				fprintf(f, "alias \"%s\" \"%s\"\n", plus_alias->name, plus_value);
				fprintf(f, "alias \"%s\" \"%s\"\n", minus_alias->name, minus_value);

				found_partner = true;
				break;
			}
		}

		if (!found_partner) {
			// Print unpaired '+' alias
			char plus_value[MAX_ALIAS_VALUE];
			Q_strncpy(plus_value, plus_alias->value, MAX_ALIAS_VALUE);
			RemoveTrailingNewline(plus_value);

			fprintf(f, "alias \"%s\" \"%s\"\n", plus_alias->name, plus_value);
		}
	}

	// Print unpaired '-' aliases
	for (i = 0; i < minus_count; i++) {
		cmdalias_t* minus_alias = minus_aliases[i];
		qboolean found_partner = false;

		for (j = 0; j < plus_count; j++) {
			cmdalias_t* plus_alias = plus_aliases[j];

			if (q_strcasecmp(minus_alias->name + 1, plus_alias->name + 1) == 0) {
				found_partner = true;
				break;
			}
		}

		if (!found_partner) {
			// Print unpaired '-' alias
			char minus_value[MAX_ALIAS_VALUE];
			Q_strncpy(minus_value, minus_alias->value, MAX_ALIAS_VALUE);
			RemoveTrailingNewline(minus_value);

			fprintf(f, "alias \"%s\" \"%s\"\n", minus_alias->name, minus_value);
		}
	}

	// Print other aliases
	for (i = 0; i < other_count; i++) {
		alias = other_aliases[i];
		char value_copy[MAX_ALIAS_VALUE];
		Q_strncpy(value_copy, alias->value, MAX_ALIAS_VALUE);
		RemoveTrailingNewline(value_copy);

		fprintf(f, "alias \"%s\" \"%s\"\n", alias->name, value_copy);
	}
}

/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

#define	MAX_ARGS		80

static	int			cmd_argc;
static	char		*cmd_argv[MAX_ARGS];
static	char		cmd_null_string[] = "";
static	const char	*cmd_args = NULL;

cmd_source_t	cmd_source;

//johnfitz -- better tab completion
//static	cmd_function_t	*cmd_functions;		// possible commands to execute
cmd_function_t	*cmd_functions;		// possible commands to execute
//johnfitz

/*
===============
Load_Ghost_ID_f // woods #smartafk load that backup name if AFK in name at startup, and clear it
===============
*/
void Load_Ghost_ID_f (void)
{
	char buffer[5];

	FILE* f;

	f = fopen(va("%s/id1/backups/ghost.txt", com_basedir), "r");

	if (f == NULL) // lets not load backup
	{
		//Con_Printf("no AFK backup to restore from"); //no file means it was deleted normally
		return;
	}

	while (fgets(buffer, sizeof(buffer), f) != NULL)
	{
		Con_Printf("your last ghostcode was ^m%.3s\n", buffer);
	}

	fclose(f);
}

/*
============
Cmd_IsReservedName -- woods #iwtabcomplete

Returns true if name starts with 2 underscores
============
*/
qboolean Cmd_IsReservedName (const char* name)
{
	return name[0] == '_' && name[1] == '_';
}

/*
============
Cmd_List_f -- johnfitz
============
*/
void Cmd_List_f (void)
{
	cmd_function_t	*cmd;
	const char	*partial;
	int		len, count;

	if (Cmd_Argc() > 1)
	{
		partial = Cmd_Argv (1);
		len = Q_strlen(partial);
	}
	else
	{
		partial = NULL;
		len = 0;
	}

	count=0;
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (cmd->srctype == src_server)
			continue;
		if (Cmd_IsReservedName(cmd->name))
			continue;
		if (partial && Q_strncmp(partial, cmd->name, len))
			continue;
		Con_SafePrintf("   %s\n", cmd->name);
		count++;
	}

	Con_SafePrintf ("%i commands", count);
	if (partial)
	{
		Con_SafePrintf (" beginning with \"%s\"", partial);
	}
	Con_SafePrintf ("\n");
}

/*
============
Cmd_ListAllContaining

scans through each command and cvar names+descriptions for the given substring
we don't support descriptions, so this isn't really all that useful, but even without the sake of consistency it still combines cvars+commands under a single command.
============
*/
static void Cmd_ListAllContaining(const char* substr)
{
	char tmpbuf[256];
	int hits = 0;
	cmd_function_t* cmd;
	cvar_t* var;
	const char* plural;
	const char* plural2; // woods #addaliases


	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (cmd->srctype != src_server && q_strcasestr(cmd->name, substr) && !Cmd_IsReservedName(cmd->name))
		{
			hits++;
			Con_SafePrintf("   %s\n", COM_TintSubstring(cmd->name, substr, tmpbuf, sizeof(tmpbuf)));
		}
	}

	for (var = Cvar_FindVarAfter("", 0) ; var; var=var->next)
	{
		if (q_strcasestr(var->name, substr))
		{
			hits++;
			Con_SafePrintf ("   %s (current value: \"%s\")\n", COM_TintSubstring(var->name, substr, tmpbuf, sizeof(tmpbuf)), var->string);
		}
	}

	plural = (hits == 1) ? "" : "s";
	plural2 = (hits == 1) ? "" : "es"; // woods #addaliases
	if (!hits)
		Con_SafePrintf("no cvars/commands/aliases contain '%s'\n", substr);
	else
		Con_SafePrintf("%d cvar%s/command%s/alias%s containing '%s'\n", hits, plural, plural, plural2, substr); // woods #addaliases
}

/*
============
Cmd_Apropos_f
============
*/
void Cmd_Apropos_f(void)
{
	const char* substr = Cmd_Argv(1);
	if (!*substr)
	{
		Con_SafePrintf("%s <substring> : search through commands and cvars for the given substring\n", Cmd_Argv(0));
		return;
	}
	Cmd_ListAllContaining(substr);
}

/*
====================
Cmd_PrintTxt_f -- Prints a text file into the console -- woods from joequake #printtxt
====================
*/
void Cmd_PrintTxt_f(void)
{
	char	name[MAX_FILELENGTH], buf[256] = { 0 };
	FILE* f;

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf("\nprinttxt <txtfile> : prints a text file located in quake folder\n\nexample: printtxt id1/config.cfg or printtxt history.txt\n\n");
		return;
	}

	Q_strncpy(name, Cmd_Argv(1), sizeof(name));
	Q_strncpy(buf, va("%s/%s", com_basedir, name), sizeof(buf));

	if (!(f = fopen(buf, "rt")))
	{
		Con_Printf("ERROR: couldn't open %s\n", name);
		return;
	}

	Con_Printf("\n");
	while (fgets(buf, 256, f))
	{
		Con_Printf("%s", buf);
		memset(buf, 0, sizeof(buf));
	}

	Con_Printf("\n");
	fclose(f);
}

/*
====================
Cmd_History_f -- woods #history
====================
*/
void Cmd_History_f(void)
{
	const char* secondary = NULL;

	History_Shutdown();
	History_Init();

	if (Cmd_Argc() >= 2)
	{
		secondary = Cmd_Argv(1);
	
		if (!strcmp(secondary, "servers") || !strcmp(secondary, "-s") || !strcmp(secondary, "s"))
		{ 
			Cmd_ExecuteString("printtxt id1/backups/servers.txt\n", src_command);
			return;
		}
	
		if (!strcmp(secondary, "console") || !strcmp(secondary, "-c") || !strcmp(secondary, "c"))
		{
			Cmd_ExecuteString("printtxt history.txt\n", src_command);
			return;
		}

		if (!strcmp(secondary, "all") || !strcmp(secondary, "-a") || !strcmp(secondary, "a"))
		{
			Con_Printf("\n^mserver history:\n");
			Cmd_ExecuteString("printtxt id1/backups/servers.txt\n", src_command);
			Con_Printf("^mconsole history:\n");
			Cmd_ExecuteString("printtxt history.txt\n", src_command);
			return;
		}
	}

	Con_Printf("\n");
	Con_Printf("usage: history <option>\n\n");
	Con_Printf("all, -a, a      show all history\n");
	Con_Printf("console, -c, c  console history\n");
	Con_Printf("servers, -s, s  server connection history\n");
	
	Con_Printf("\n");

}

/*
============
Alias_List_f -- woods #aliaslist
============
*/
void Alias_List_f(void)
{
	cmdalias_t* alias;
	const char* partial;
	int		len, count;

	if (Cmd_Argc() > 1)
	{
		partial = Cmd_Argv(1);
		len = Q_strlen(partial);
	}
	else
	{
		partial = NULL;
		len = 0;
	}

	count = 0;
	for (alias = cmd_alias; alias; alias = alias->next)
	{
		if (partial && Q_strncmp(partial, alias->name, len))
		{
			continue;
		}
		Con_SafePrintf("   %s\n", alias->name);
		count++;
	}


	Con_SafePrintf("%i aliases", count);
	if (partial)
	{
		Con_SafePrintf(" beginning with \"%s\"", partial);
	}
	Con_SafePrintf("\n");
}

/*
============
Cmd_If_f -- woods #if
============
*/
void Cmd_If_f(void)
{
	cvar_t* cvar;
	char command[MAXCMDLINE];
	char* cmd, * next_cmd;
	int i;
	float value;
	enum { CHECK_NONZERO, CHECK_EQUAL, CHECK_NOT_EQUAL, CHECK_LESS, CHECK_GREATER, CHECK_LESS_EQUAL, CHECK_GREATER_EQUAL } check_type = CHECK_NONZERO;

	if (Cmd_Argc() < 3)
	{
		Con_Printf("\nif <cvar> [operator <value>] <command(s)>: execute command(s) based on the comparison of cvar to <value>\n\nsupported operators: ==, !=, <, >, <=, >=. if no operator is provided, non-zero cvar will trigger the command(s)\n\n");
		return;
	}

	cvar = Cvar_FindVar(Cmd_Argv(1));
	if (!cvar)
	{
		Con_Printf("if: cvar %s not found\n", Cmd_Argv(1));
		return;
	}

	if (Cmd_Argc() > 3)
	{
		if (strcmp(Cmd_Argv(2), "==") == 0)
			check_type = CHECK_EQUAL;
		else if (strcmp(Cmd_Argv(2), "!=") == 0)
			check_type = CHECK_NOT_EQUAL;
		else if (strcmp(Cmd_Argv(2), "<") == 0)
			check_type = CHECK_LESS;
		else if (strcmp(Cmd_Argv(2), ">") == 0)
			check_type = CHECK_GREATER;
		else if (strcmp(Cmd_Argv(2), "<=") == 0)
			check_type = CHECK_LESS_EQUAL;
		else if (strcmp(Cmd_Argv(2), ">=") == 0)
			check_type = CHECK_GREATER_EQUAL;

		value = atof(Cmd_Argv(3));
	}

	if ((check_type == CHECK_NONZERO && cvar->value) ||
		(check_type == CHECK_EQUAL && cvar->value == value) ||
		(check_type == CHECK_NOT_EQUAL && cvar->value != value) ||
		(check_type == CHECK_LESS && cvar->value < value) ||
		(check_type == CHECK_GREATER && cvar->value > value) ||
		(check_type == CHECK_LESS_EQUAL && cvar->value <= value) ||
		(check_type == CHECK_GREATER_EQUAL && cvar->value >= value))
	{
		memset(command, 0, sizeof(command));
		for (i = (check_type != CHECK_NONZERO) ? 4 : 2; i < Cmd_Argc(); i++)
		{
			q_strlcat(command, Cmd_Argv(i), sizeof(command));
			if (i != Cmd_Argc() - 1)
			{
				q_strlcat(command, " ", sizeof(command));  // preserve spaces between arguments
			}
		}

		cmd = strtok(command, ";");
		while (cmd != NULL)
		{
			next_cmd = strtok(NULL, ";");
			Cbuf_AddText(cmd);
			Cbuf_AddText("\n");  // new line character to signify end of command
			cmd = next_cmd;
			if (next_cmd == NULL) // checking if strtok returned NULL
			{
				break;
			}
		}
	}
}

/*
============
Random_f -- woods #randomcmd
============
*/
void Random_f (void)
{
	const int CMD_NAME_ARG = 1;
	int numArgs = Cmd_Argc();
	int selectedNum;
	int i;
	char sayCommand[MAXCMDLINE];
	char argsString[MAXCMDLINE] = "";
	const char *randomizePrefix = "say randomize: ";
	const char *resultPrefix = "say result: ";
	const size_t argsStringSize = sizeof(sayCommand) - strlen(randomizePrefix) - strlen("\n");

	if (numArgs <= CMD_NAME_ARG + 1) // Command name + at least one option
	{
		Con_Printf ("\nusage: random <option1> <option2> ... <optionN>\n\n");
		return;
	}

	// select a random argument number
	selectedNum = CMD_NAME_ARG + (rand() % (numArgs - CMD_NAME_ARG));
	const char* selectedArg = Cmd_Argv (selectedNum);

	// construct a string with all the arguments
	for (i = CMD_NAME_ARG; i < numArgs; i++)
	{
		const char* arg = Cmd_Argv(i);

		if (q_strlcat(argsString, arg, argsStringSize) >= argsStringSize)
		{
			Con_Printf ("\nToo many arguments for random command.\n\n");
			return;
		}

		if (i < numArgs - 1)
		{
			if (q_strlcat(argsString, ", ", argsStringSize) >= argsStringSize)
			{
				Con_Printf ("\ntoo many arguments for random command.\n\n");
				return;
			}
		}
	}

	if (cls.state == ca_connected)
	{
		q_strlcpy (sayCommand, randomizePrefix, sizeof(sayCommand));
		q_strlcat (sayCommand, argsString, sizeof(sayCommand));
		q_strlcat (sayCommand, "\n", sizeof(sayCommand));
		Cbuf_AddText (sayCommand);

		q_strlcpy (sayCommand, resultPrefix, sizeof(sayCommand));
		q_strlcat (sayCommand, selectedArg, sizeof(sayCommand));
		q_strlcat (sayCommand, "\n", sizeof(sayCommand));
		Cbuf_AddText (sayCommand);
	}
	else
		Con_Printf ("result: ^m%s\n", selectedArg);
}

/*
============
Cmd_Init
============
*/
void Cmd_Init (void)
{
	Cmd_AddCommand ("cmdlist", Cmd_List_f); //johnfitz
	Cmd_AddCommand ("lastid", Load_Ghost_ID_f); // woods for ghostcode memory #ghostcode
	Cmd_AddCommand ("unalias", Cmd_Unalias_f); //johnfitz
	Cmd_AddCommand ("unaliasall", Cmd_Unaliasall_f); //johnfitz

	Cmd_AddCommand ("stuffcmds",Cmd_StuffCmds_f);
	Cmd_AddCommand ("exec",Cmd_Exec_f);
	Cmd_AddCommand ("echo",Cmd_Echo_f);
	Cmd_AddCommand ("alias",Cmd_Alias_f);
	Cmd_AddCommand ("aliasedit", Alias_Edit_f); // woods #aliaslist
	Cmd_AddCommand ("cmd", Cmd_ForwardToServer);
	Cmd_AddCommand ("wait", Cmd_Wait_f);

	Cmd_AddCommand ("apropos", Cmd_Apropos_f);
	Cmd_AddCommand ("find", Cmd_Apropos_f);

	Cmd_AddCommand("__cfgmarker", Cmd_CfgMarker_f); // woods - Skip apropos text for unknown commands executed from config (ironwail)
	Cmd_AddCommand("printtxt", Cmd_PrintTxt_f);
	Cmd_AddCommand("aliaslist", Alias_List_f); // woods #aliaslist
	Cmd_AddCommand("history", Cmd_History_f); // woods #history
	Cmd_AddCommand ("if", Cmd_If_f); // woods #if
	Cmd_AddCommand ("random", Random_f); // woods #randomcmd

	Cvar_RegisterVariable (&cl_nopext);
	Cvar_RegisterVariable (&cmd_warncmd);
	Cvar_RegisterVariable (&cl_aliasoverlap);
}

/*
============
Cmd_Argc
============
*/
int	Cmd_Argc (void)
{
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
const char	*Cmd_Argv (int arg)
{
	if (arg < 0 || arg >= cmd_argc)
		return cmd_null_string;
	return cmd_argv[arg];
}

/*
============
Cmd_Args
============
*/
const char	*Cmd_Args (void)
{
	if (!cmd_args)
		return "";
	return cmd_args;
}

/*
============
Cmd_AddArg  -- woods #iwtabcomplete
============
*/
void Cmd_AddArg (const char* arg)
{
	if (cmd_argc < MAX_ARGS)
	{
		cmd_argv[cmd_argc] = Z_Strdup(arg);
		cmd_argc++;
	}
}

/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
============
*/
void Cmd_TokenizeString (const char *text)
{
	int		i;

// clear the args from the last string
	for (i=0 ; i<cmd_argc ; i++)
		Z_Free (cmd_argv[i]);

	cmd_argc = 0;
	cmd_args = NULL;

	while (1)
	{
// skip whitespace up to a /n
		while (*text && (unsigned char)*text <= ' ' && *text != '\n')
		{
			text++;
		}

		if (*text == '\n')
		{	// a newline seperates commands in the buffer
			text++;
			break;
		}

		if (!*text)
			return;

		if (cmd_argc == 1)
			 cmd_args = text;

		text = COM_Parse (text);
		if (!text)
			return;

		if (cmd_argc < MAX_ARGS)
		{
			cmd_argv[cmd_argc] = Z_Strdup (com_token);
			cmd_argc++;
		}
	}

}

/*
============
Cmd_AddCommand

spike -- added an extra arg for client (also renamed and made a macro)
============
*/
cmd_function_t *Cmd_AddCommand2 (const char *cmd_name, xcommand_t function, cmd_source_t srctype, qboolean qcinterceptable)
{
	cmd_function_t	*cmd;
	cmd_function_t	*cursor,*prev; //johnfitz -- sorted list insert

// fail if the command is a variable name
	if (Cvar_VariableString(cmd_name)[0])
	{
		Con_Printf ("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
		return NULL;
	}

// fail if the command already exists
	for (cmd=cmd_functions ; cmd ; cmd=cmd->next)
	{
		if (!Q_strcmp (cmd_name, cmd->name) && cmd->srctype == srctype)
		{
			if (cmd->function != function && function)
				Con_Printf ("Cmd_AddCommand: %s already defined\n", cmd_name);
			return NULL;
		}
	}

	if (host_initialized)
	{
		cmd = (cmd_function_t *) malloc(sizeof(*cmd) + strlen(cmd_name)+1);
		if (!cmd)
			Sys_Error ("Cmd_AddCommand2: out of memory (%s)", cmd_name);
		cmd->name = strcpy((char*)(cmd + 1), cmd_name);
		cmd->dynamic = true;
	}
	else
	{
		cmd = (cmd_function_t *) Hunk_Alloc (sizeof(*cmd));
		cmd->name = cmd_name;
		cmd->dynamic = false;
	}
	cmd->function = function;
	cmd->srctype = srctype;
	cmd->qcinterceptable = qcinterceptable;

	//johnfitz -- insert each entry in alphabetical order
	if (cmd_functions == NULL || strcmp(cmd->name, cmd_functions->name) < 0) //insert at front
	{
		cmd->next = cmd_functions;
		cmd_functions = cmd;
	}
	else //insert later
	{
		prev = cmd_functions;
		cursor = cmd_functions->next;
		while ((cursor != NULL) && (strcmp(cmd->name, cursor->name) > 0))
		{
			prev = cursor;
			cursor = cursor->next;
		}
		cmd->next = prev->next;
		prev->next = cmd;
	}
	//johnfitz

	if (cmd->dynamic)
		return cmd;
	return NULL;
}
void Cmd_RemoveCommand (cmd_function_t *cmd)
{
	cmd_function_t **link;
	for (link = &cmd_functions; *link; link = &(*link)->next)
	{
		if (*link == cmd)
		{
			*link = cmd->next;
			free(cmd);
			return;
		}
	}
	Sys_Error ("Cmd_RemoveCommand unable to remove command %s",cmd->name);
}

/*
============
Cmd_Find -- woods #iwtabcomplete
============
*/
cmd_function_t* Cmd_FindCommand (const char* cmd_name)
{
	cmd_function_t* cmd;

	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!q_strcasecmp (cmd_name, cmd->name))
		{
			if (cmd->srctype != src_command)	// these commands only exist in certain situations... so pretend they don't exist here.
				continue;
			return cmd;
		}
	}

	return NULL;
}

/*
============
Cmd_Exists -- woods #iwtabcomplete
============
*/
qboolean Cmd_Exists (const char* cmd_name)
{
	return Cmd_FindCommand (cmd_name) != NULL;
}

/*
============
Cmd_Exists2 -- woods #ezsay
============
*/
qboolean	Cmd_Exists2(const char* cmd_name)
{
	cmd_function_t* cmd;

	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!Q_strcmp(cmd_name, cmd->name))
		{
			//if (cmd->srctype != src_command)	//these commands only exist in certain situations... so pretend they don't exist here.
			//	continue;
			return true;
		}
	}

	return false;
}

/*
============
Cmd_CompleteCommand
============
*/
const char *Cmd_CompleteCommand (const char *partial)
{
	cmd_function_t	*cmd;
	int		len;

	len = Q_strlen(partial);

	if (!len)
		return NULL;

// check functions
	for (cmd=cmd_functions ; cmd ; cmd=cmd->next)
		if (!Q_strncmp (partial,cmd->name, len))
			return cmd->name;

	return NULL;
}

qboolean Cmd_IsQuitMistype (const char* input) // woods -- #smartquit
{
	if (!input)
		return false;

	if (Cvar_FindVar(input) || Cmd_Exists2(input) || Cmd_AliasExists(input))
		return false;

	if (q_strncasecmp(input, "qu", 2) != 0) 
		return false; // Not a mistype for "quit"

	const char* correct_cmd = "quit"; // Define the correct command

	int threshold = 2; 	// Define a threshold for mistypes (e.g., distance <= 2)
	int distance = LevenshteinDistance(input, correct_cmd); // Calculate the Levenshtein distance

	if (distance == -1)
		return false;

	return distance > 0 && distance <= threshold; 	// Return true if within the threshold, else false
}

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
qboolean	Cmd_ExecuteString (const char *text, cmd_source_t src)
{
	cmd_function_t	*cmd;
	cmdalias_t		*a;

	cmd_source = src;
	Cmd_TokenizeString (text);

// execute the command line
	if (!Cmd_Argc())
		return true;		// no tokens

	if (Cmd_IsQuitMistype(Cmd_Argv(0))) // // woods -- #smartquit -- check for mistyped "quit" command
	{
		if (SCR_ModalMessage(va("you typed: ^m%s^m\n\n do you want to quit? (^my^m/^mn^m)\n", Cmd_Argv(0)), 0.0f))
			Host_Quit_f();
		return true;
	}

// check functions
	for (cmd=cmd_functions ; cmd ; cmd=cmd->next)
	{
		if (!q_strcasecmp (cmd_argv[0],cmd->name))
		{
			if (src == src_client && cmd->srctype != src_client)
				continue;
			else if (src == src_command && cmd->srctype == src_server)
				continue;	//src_command can execute anything but server commands (which it ignores, allowing for alternative behaviour)
			else if (src == src_server && cmd->srctype != src_server)
				continue;	//src_server may only execute server commands (such commands must be safe to parse within the context of a network message, so no disconnect/connect/playdemo/etc)
			else
			{
				qboolean ret = false;
				qcvm_t *oldvm;
				if (!ret && cmd->function && (!cmd->qcinterceptable || src == src_client))
				{
					cmd->function ();
					ret = true;
				}
				oldvm = ret?NULL:qcvm;
				if (oldvm)
					PR_SwitchQCVM(NULL);
				if (!ret && cl.qcvm.extfuncs.CSQC_ConsoleCommand)
				{
					PR_SwitchQCVM(&cl.qcvm);
					G_INT(OFS_PARM0) = PR_MakeTempString(text);
					PR_ExecuteProgram(cl.qcvm.extfuncs.CSQC_ConsoleCommand);
					ret = G_FLOAT(OFS_RETURN);
					PR_SwitchQCVM(NULL);
				}
				if (!ret && cls.menu_qcvm.extfuncs.m_consolecommand)
				{
					PR_SwitchQCVM(&cls.menu_qcvm);
					G_INT(OFS_PARM0) = PR_MakeTempString(text);
					PR_ExecuteProgram(cls.menu_qcvm.extfuncs.m_consolecommand);
					ret = G_FLOAT(OFS_RETURN);
					PR_SwitchQCVM(NULL);
				}
				if (oldvm)
					PR_SwitchQCVM(oldvm);
				if (!ret && cmd->function)
				{
					cmd->function ();
					ret = true;
				}
				if (!ret)
					Con_Printf ("gamecode not running, cannot \"%s\"\n", Cmd_Argv(0));
			}
			return true;
		}
	}

	if (src == src_client)
	{	//spike -- please don't execute similarly named aliases, nor custom cvars...
		Con_DPrintf("%s tried to %s\n", host_client->name, text);
		return false;
	}
	if (src != src_command)
		return false;

// check alias
	for (a=cmd_alias ; a ; a=a->next)
	{
		if (!q_strcasecmp (cmd_argv[0], a->name))
		{
			Cbuf_InsertText (a->value);
			return true;
		}
	}

// check cvars
	if (!Cvar_Command ())
		if (cmd_warncmd.value || developer.value)
		{
			if (in_cfg_exec) // woods - Skip apropos text for unknown commands executed from config (ironwail)
				Con_Printf("Unknown command \"%s\"\n", Cmd_Argv(0));
			else
				Cmd_ListAllContaining(Cmd_Argv(0));
		}

	return true;
}

static char *Cmd_AppendChatChar (char *dst, char *end, int c)
{
	if (dst < end)
		*dst++ = (char)c;
	return dst;
}

static char *Cmd_AppendChatString (char *dst, char *end, const char *s)
{
	if (!s)
		return dst;
	while (*s && dst < end)
		*dst++ = *s++;
	return dst;
}

static char *Cmd_AppendChatFmt (char *dst, char *end, const char *fmt, ...) FUNC_PRINTF(3,4);
static char *Cmd_AppendChatFmt (char *dst, char *end, const char *fmt, ...)
{
	va_list args;
	int len;
	size_t remaining;

	if (dst >= end)
		return end;

	remaining = (size_t)(end - dst);
	va_start(args, fmt);
	len = q_vsnprintf(dst, remaining + 1, fmt, args);
	va_end(args);

	if (len < 0 || (size_t)len > remaining)
		return end;
	return dst + len;
}

/*
===================
Cmd_ForwardToServer

Sends the entire command line over to the server
===================
*/
void Cmd_ForwardToServer (void)
{
	char *dst, *end, buff[512];		// JPG - used for say/say_team formatting // woods #pqteam
	const char* src; // woods add const
	int minutes, seconds, match_time;	// JPG - used for %t // woods #pqteam
	
	if (cls.state != ca_connected)
	{
		Con_Printf ("Can't \"%s\", not connected\n", Cmd_Argv(0));
		return;
	}

	if (cls.demoplayback)
		return;		// not really connected

	if (cl.modtype == 1)
	{
		for (int i = 0; i < Cmd_Argc() - 1; i++) // woods - hack pending transition to *observer
		{
			if (!q_strcasecmp(Cmd_Argv(i), "setinfo") &&
				!q_strcasecmp(Cmd_Argv(i + 1), "observer"))
			{
				Con_Printf("\nchanging ^mobserver^m key is not allowed\n\n");
				return;
			}
		}
	}

	MSG_WriteByte (&cls.message, clc_stringcmd);

	//----------------------------------------------------------------------
// JPG - handle say separately for formatting--start // woods #pqteam
	if ((!q_strcasecmp(Cmd_Argv(0), "say") || !q_strcasecmp(Cmd_Argv(0), "say_team")) && Cmd_Argc() > 1)
	{
		if (ctrlpressed && !q_strcasecmp(Cmd_Argv(0), "say")) // woods #saymodifier
			SZ_Print(&cls.message, "say_team");
		else if (ctrlpressed && !q_strcasecmp(Cmd_Argv(0), "say_team")) // woods #saymodifier
			SZ_Print(&cls.message, "say");
		else
			SZ_Print(&cls.message, Cmd_Argv(0));
		SZ_Print(&cls.message, " ");

		src = Cmd_Args();
		dst = buff;
		end = buff + sizeof(buff) - 1;
		while (*src && dst < end)
		{
			if (*src == '%')
			{
				switch (*++src)
				{
				case 'h':
					dst = Cmd_AppendChatFmt(dst, end, "%d", cl.stats[STAT_HEALTH]);
					break;

				case 'a':
				{
					char* ch = "[G]:[Y]:[R]";
					int first = 1;
					int item;

					dst = Cmd_AppendChatFmt(dst, end, "%d", cl.stats[STAT_ARMOR]);
					//R00k: added to show armor type					
					if (cl.stats[STAT_ARMOR] > 0)
					{
						for (item = IT_ARMOR1; item <= IT_ARMOR3; item *= 2)
						{
							if (*ch != ':' && (cl.items & item))
							{
								if (!first)
									dst = Cmd_AppendChatChar(dst, end, ',');
								first = 0;
								while (*ch && *ch != ':')
									dst = Cmd_AppendChatChar(dst, end, *ch++);
							}
							for (; *ch && *ch != ':'; ch++)
								;
							if (!*ch)
								break;
							ch++;
						}
					}
				}
				break;

				case 'A':
					if (cl.stats[STAT_HEALTH] > 0)
					{
						switch (cl.stats[STAT_ACTIVEWEAPON])
						{
						case IT_SHOTGUN:
							if (*++src == 't')
							{
								dst = Cmd_AppendChatString(dst, end, "shells");
							}
							else
							{
								if (cl.stats[STAT_SHELLS] < 5)
									dst = Cmd_AppendChatString(dst, end, "I need shells ");
								else
									dst = Cmd_AppendChatFmt(dst, end, "%d shells", cl.stats[STAT_SHELLS]);
							}
							break;

						case IT_SUPER_SHOTGUN:
							if (*++src == 't')
							{
								dst = Cmd_AppendChatString(dst, end, "shells");
							}
							else
							{
								if (cl.stats[STAT_SHELLS] < 5)
									dst = Cmd_AppendChatString(dst, end, "I need shells ");
								else
									dst = Cmd_AppendChatFmt(dst, end, "%d shells", cl.stats[STAT_SHELLS]);
							}
							break;

						case IT_NAILGUN:
							if (*++src == 't')
							{
								dst = Cmd_AppendChatString(dst, end, "nails");
							}
							else
							{
								if (cl.stats[STAT_NAILS] < 5)
									dst = Cmd_AppendChatString(dst, end, "I need nails ");
								else
									dst = Cmd_AppendChatFmt(dst, end, "%d nails", cl.stats[STAT_NAILS]);
							}
							break;

						case IT_SUPER_NAILGUN:
							if (*++src == 't')
							{
								dst = Cmd_AppendChatString(dst, end, "nails");
							}
							else
							{
								if (cl.stats[STAT_NAILS] < 5)
									dst = Cmd_AppendChatString(dst, end, "I need nails ");
								else
									dst = Cmd_AppendChatFmt(dst, end, "%d nails", cl.stats[STAT_NAILS]);
							}
							break;

						case IT_GRENADE_LAUNCHER:
							if (*++src == 't')
							{
								dst = Cmd_AppendChatString(dst, end, "rockets");
							}
							else
							{
								if (cl.stats[STAT_ROCKETS] < 5)
									dst = Cmd_AppendChatString(dst, end, "I need rockets");
								else
									dst = Cmd_AppendChatFmt(dst, end, "%d rockets", cl.stats[STAT_ROCKETS]);
							}
							break;

						case IT_ROCKET_LAUNCHER:
							if (*++src == 't')
							{
								dst = Cmd_AppendChatString(dst, end, "rockets");
							}
							else
							{
								if (cl.stats[STAT_ROCKETS] < 5)
									dst = Cmd_AppendChatString(dst, end, "I need rockets");
								else
									dst = Cmd_AppendChatFmt(dst, end, "%d rockets", cl.stats[STAT_ROCKETS]);
							}
							break;

						case IT_LIGHTNING:
							if (*++src == 't')
							{
								dst = Cmd_AppendChatString(dst, end, "cells");
							}
							else
							{
								if (cl.stats[STAT_CELLS] < 5)
									dst = Cmd_AppendChatString(dst, end, "I need cells");
								else
									dst = Cmd_AppendChatFmt(dst, end, "%d cells", cl.stats[STAT_CELLS]);
							}
							break;

						default:
							if (*++src == 't')
							{
								dst = Cmd_AppendChatString(dst, end, "nothing ");
							}
							break;
						}
					}
					else
						dst = Cmd_AppendChatString(dst, end, "I need RL");
					break;

				case 'r':
					if (cl.stats[STAT_HEALTH] > 0 && (cl.items & IT_ROCKET_LAUNCHER))
					{
						if (cl.stats[STAT_ROCKETS] < 5)
							dst = Cmd_AppendChatString(dst, end, "I need rockets");
						else
							dst = Cmd_AppendChatString(dst, end, "I have RL");
					}
					else
						dst = Cmd_AppendChatString(dst, end, "I need RL");
					break;

				case 'l':
					// guard cl.entities[cl.viewentity] like CL_Viewpos_f: ca_connected can
					// coexist with a NULL/short cl.entities during signon
					if (cl.entities && cl.viewentity >= 1 && cl.viewentity < cl.num_entities)
						dst = Cmd_AppendChatString(dst, end, LOC_GetLocation(cl.entities[cl.viewentity].origin));
					break;

				case 'd':
					dst = Cmd_AppendChatString(dst, end, LOC_GetLocation(cl.death_location));
					break;
				case 'D':
					if (cl.stats[STAT_HEALTH] <= 0)
						dst = Cmd_AppendChatString(dst, end, "died");
					break;

				case 'c':
					dst = Cmd_AppendChatFmt(dst, end, "%d", cl.stats[STAT_CELLS]);
					break;

				case 'x':
					dst = Cmd_AppendChatFmt(dst, end, "%d", cl.stats[STAT_ROCKETS]);
					break;

				case 'R':
					if (cl.items & IT_SIGIL1)
					{
						dst = Cmd_AppendChatString(dst, end, "Resistance");
						break;
					}
					if (cl.items & IT_SIGIL2)
					{
						dst = Cmd_AppendChatString(dst, end, "Strength");
						break;
					}
					if (cl.items & IT_SIGIL3)
					{
						dst = Cmd_AppendChatString(dst, end, "Haste");
						break;
					}
					if (cl.items & IT_SIGIL4)
					{
						dst = Cmd_AppendChatString(dst, end, "Regen");
						break;
					}
					break;

				case 'F':
					if (cl.items & IT_KEY1)
					{
						dst = Cmd_AppendChatString(dst, end, "Blue Flag");
						break;
					}
					if (cl.items & IT_KEY2)
					{
						dst = Cmd_AppendChatString(dst, end, "Red Flag");
						break;
					}
					break;

				case 'p':
					if (cl.stats[STAT_HEALTH] > 0)
					{
						if (cl.items & IT_QUAD)
						{
							dst = Cmd_AppendChatString(dst, end, "our quad");  // woods added our
							if (cl.items & (IT_INVULNERABILITY | IT_INVISIBILITY))
								dst = Cmd_AppendChatChar(dst, end, '+');
						}
						if (cl.items & IT_INVULNERABILITY)
						{
							dst = Cmd_AppendChatString(dst, end, "our pent"); // woods added our
							if (cl.items & IT_INVISIBILITY)
								dst = Cmd_AppendChatChar(dst, end, '+');
						}
						if (cl.items & IT_INVISIBILITY)
							dst = Cmd_AppendChatString(dst, end, "our ring"); // woods added our

						//						if (cl.items & IT_SUIT)
							//						dst = Cmd_AppendChatString(dst, end, "biosuit");//R00k lol "team Biosuit"
					}
					break;

				case 'w':	// JPG 3.00
				{
					int first = 1;
					int item;
					char* ch = "SSG:NG:SNG:GL:RL:LG";
					if (cl.stats[STAT_HEALTH] > 0)
					{
						for (item = IT_SUPER_SHOTGUN; item <= IT_LIGHTNING; item *= 2)
						{
							if (*ch != ':' && (cl.items & item))
							{
								if (!first)
									dst = Cmd_AppendChatChar(dst, end, ',');
								first = 0;
								while (*ch && *ch != ':')
									dst = Cmd_AppendChatChar(dst, end, *ch++);
							}
							for (; *ch && *ch != ':'; ch++)
								;
							if (!*ch)
								break;
							ch++;
						}
					}
					if (first)
						dst = Cmd_AppendChatString(dst, end, "no weapons");
				}
				break;

				case 'z':	// woods added for only reporting rl and lg
				{
					int first = 1;
					int item;
					char* ch = "with RL:LG";  // woods only care about lg and rl
					if (cl.stats[STAT_HEALTH] > 0)
					{
						for (item = IT_ROCKET_LAUNCHER; item <= IT_LIGHTNING; item *= 2) // woods only care about lg and rl
						{
							if (*ch != ':' && (cl.items & item))
							{
								if (!first)
									dst = Cmd_AppendChatChar(dst, end, ' ');  // woods changed divider
								first = 0;
								while (*ch && *ch != ':')
									dst = Cmd_AppendChatChar(dst, end, *ch++);
							}
							for (; *ch && *ch != ':'; ch++)
								;
							if (!*ch)
								break;
							ch++;
						}
					}
				//	if (first)
						//	dst = Cmd_AppendChatString(dst, end, "nothing");  // woods changed to one word, easier to read
				}
				break;
				//R00k added W for weapon in hand based on pq_weapons.string
				case 'W':
					if (cl.stats[STAT_HEALTH] > 0)
					{
						int		item;
						char* ch = "SSG:NG:SNG:GL:RL:LG";

						for (item = IT_SUPER_SHOTGUN; item <= IT_LIGHTNING; item *= 2)
						{
							if (*ch != ':' && (item == cl.stats[STAT_ACTIVEWEAPON]))
							{
								while (*ch && *ch != ':')
									dst = Cmd_AppendChatChar(dst, end, *ch++);
								break;
							}
							for (; *ch && *ch != ':'; ch++)
								;
							if (!*ch)
								break;
							ch++;
						}
					}
					else
						dst = Cmd_AppendChatString(dst, end, "no weapons");
					break;

				case '%':
					dst = Cmd_AppendChatChar(dst, end, '%');
					break;

				case 't':
					if ((cl.minutes || cl.seconds) && cl.seconds < 128)
					{
						if (cl.match_pause_time)
							match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.match_pause_time - cl.last_match_time));
						else
							match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.time - cl.last_match_time));
						minutes = match_time / 60;
						seconds = match_time - 60 * minutes;
					}
					else
					{
						minutes = cl.time / 60;
						seconds = cl.time - 60 * minutes;
						minutes &= 511;
					}
					dst = Cmd_AppendChatFmt(dst, end, "%d:%02d", minutes, seconds);
					break;

				case 'T':
					if ((cl.minutes || cl.seconds) && cl.seconds < 128)
					{
						if (cl.match_pause_time)
							match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.match_pause_time - cl.last_match_time));
						else
							match_time = ceil(60.0 * cl.minutes + cl.seconds - (cl.time - cl.last_match_time));
						minutes = match_time / 60;
						seconds = match_time - 60 * minutes;
					}
					else
					{
						minutes = cl.time / 60;
						seconds = cl.time - 60 * minutes;
						minutes &= 511;
					}
					dst = Cmd_AppendChatFmt(dst, end, ":%02d", seconds);
					break;

				default:
					dst = Cmd_AppendChatChar(dst, end, '%');
					if (*src)
						dst = Cmd_AppendChatChar(dst, end, *src);
					break;
				}
				if (*src)
					src++;
			}
			else
				dst = Cmd_AppendChatChar(dst, end, *src++);
		}
		*dst = 0;

		if (!strcmp(Cmd_Argv(1), "f_version")) // woods make f_version print for legacy clients
		{
			SZ_Print(&cls.message, "q_version\n");
			MSG_WriteByte(&cls.message, clc_stringcmd);
			SZ_Print(&cls.message, Cmd_Argv(0));
			SZ_Print(&cls.message, " ");
			SZ_Print(&cls.message, "f_version\n");
			return;
		}

		if (!strcmp(Cmd_Argv(1), "f_system")) // woods make f_system print for legacy clients
		{
			SZ_Print(&cls.message, "q_sysinfo\n");
			MSG_WriteByte(&cls.message, clc_stringcmd);
			SZ_Print(&cls.message, Cmd_Argv(0));
			SZ_Print(&cls.message, " ");
			SZ_Print(&cls.message, "f_system\n");
			return;
		}

		SZ_Print(&cls.message, buff);
		return;
	}
	// JPG - handle say separately for formatting--end
	//----------------------------------------------------------------------

	if (q_strcasecmp(Cmd_Argv(0), "cmd") != 0)
	{
		SZ_Print (&cls.message, Cmd_Argv(0));
		SZ_Print (&cls.message, " ");
	}
	else
	{
		//hack zone for compat.
		//stuffcmd("cmd foo\n") is a good way to query the client to see if it knows foo because the server is guarenteed a response even if it doesn't understand it, saving a timeout
		if (!strcmp(Cmd_Args(), "protocols"))
		{	//server asked us for a list of protocol numbers that we claim to support. this allows cool servers like fte to autodetect higher limits etc.
			//servers may assume that the client's preferred protocol will be listed first.
			SZ_Print (&cls.message, va("protocols %i %i %i %i %i", PROTOCOL_RMQ, PROTOCOL_FITZQUAKE, PROTOCOL_VERSION_BJP3, PROTOCOL_VERSION_DP7, PROTOCOL_NETQUAKE));
			return;
		}
		if (!strcmp(Cmd_Args(), "pext") && !cl_nopext.value)
		{	//server asked us for a key+value list of the extensions+attributes we support
			SZ_Print (&cls.message, va("pext"
						" %#x %#x"
						" %#x %#x",
						PROTOCOL_FTE_PEXT1, PEXT1_SUPPORTED_CLIENT,
						PROTOCOL_FTE_PEXT2, PEXT2_SUPPORTED_CLIENT));
			return;
		}
	}
	if (Cmd_Argc() > 1)
		SZ_Print (&cls.message, Cmd_Args());
	else
		SZ_Print (&cls.message, "\n");
}


/*
================
Cmd_CheckParm

Returns the position (1 to argc-1) in the command's argument list
where the given parameter apears, or 0 if not present
================
*/

int Cmd_CheckParm (const char *parm)
{
	int i;

	if (!parm)
		Sys_Error ("Cmd_CheckParm: null input\n");

	for (i = 1; i < Cmd_Argc (); i++)
		if ( !q_strcasecmp (parm, Cmd_Argv (i)) )
			return i;

	return 0;
}

