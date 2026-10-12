/* Optional chat translation. Uploads require local authorization, including
 * name rules explicitly saved through the menu. */
#ifndef QUAKE_CL_TRANSLATE_H
#define QUAKE_CL_TRANSLATE_H

void CL_Translate_Init (void);
/* Call only at interactive console submission, before Cbuf loses provenance.
 * Consumes a complete translate line; never expands aliases or other commands. */
qboolean CL_Translate_Console (const char *line);
void CL_Translate_Observe (const char *original, const char *visible);
void CL_Translate_Frame (void);
void CL_Translate_Reset (void);
void CL_Translate_MapChanged (void);
const char *CL_Translate_CompletionStart (const char *line);
void CL_Translate_IgnoreChanged (void);
void CL_Translate_PlayerChanged (int slot, const char *name);
void CL_Translate_Shutdown (void);
#define CL_TRANSLATE_MAX_RULES 128
#define CL_TRANSLATE_MAX_SELECTIONS (MAX_SCOREBOARD + CL_TRANSLATE_MAX_RULES)

typedef struct
{
	int slot, language; /* slot: -1 = saved name; language: -1 = off */
	char name[MAX_SCOREBOARDNAME]; /* raw identity, checked again on activation */
	char display[MAX_SCOREBOARDNAME * 2 + 1];
	qboolean selectable, remembered;
	const char *reason;
} cl_translate_player_t;

typedef enum
{
	TRANSLATE_SEND_IDLE, TRANSLATE_SEND_PENDING, TRANSLATE_SEND_SENT, TRANSLATE_SEND_FAILED
} cl_translate_send_state_t;

/* Queries never upload. Menu mutators are not commands and must only be called
 * from an actual local menu input event, never draw/hover/config callbacks. */
const char *CL_Translate_UnavailableReason (void);
int CL_Translate_MenuPlayers (cl_translate_player_t *players, int capacity);
/* Active watches plus saved names, including while disconnected. */
int CL_Translate_MenuSelections (cl_translate_player_t *players, int capacity);
int CL_Translate_ReadLanguage (void);
int CL_Translate_WriteLanguage (void);
int CL_Translate_LanguageCount (void);
const char *CL_Translate_LanguageName (int language);
const char *CL_Translate_LanguageCode (int language);
const char *CL_Translate_MenuNotice (void);
void CL_Translate_MenuStatus (char *out, size_t size);
cl_translate_send_state_t CL_Translate_SendState (unsigned int *id, char *out, size_t size);
/* Preference for newly added players; never changes existing watches. */
void CL_Translate_MenuReadLanguage (int language);
void CL_Translate_MenuWriteLanguage (int language);
qboolean CL_Translate_MenuWatch (int slot, const char *expected_name, int language);
qboolean CL_Translate_MenuWatchSaved (int slot, const char *expected_name, int language);
/* Explicit local choice to remember/forget an active watch by readable name. */
qboolean CL_Translate_MenuRemember (int slot, const char *expected_name, qboolean remember);
qboolean CL_Translate_MenuEditPlayer (int slot, const char *expected_name, int language, qboolean remember);
qboolean CL_Translate_MenuRemovePlayer (int slot, const char *expected_name);
/* Add currently eligible unselected players, preserving existing targets. */
qboolean CL_Translate_MenuWatchEveryone (int language);
qboolean CL_Translate_MenuWatchEveryoneRemembered (int language);
void CL_Translate_MenuStopAll (void);
qboolean CL_Translate_MenuSend (int language, const char *text);

#endif
