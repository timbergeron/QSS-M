/* Headless tests of the production module, real SDL worker and libcurl.
 * Only unrelated engine presentation/registry/network-buffer APIs are stubbed. */
#include "../../Quake/cl_translate.c"
#include <assert.h>
#include "q_ctype.h"
#include "translation-list-types.inc"

client_static_t cls;
client_state_t cl;
quakeparms_t *host_parms;
char com_basedir[MAX_OSPATH];
void Sys_mkdir (const char *path) { assert(SDL_CreateDirectory(path)); }
qboolean ctrlpressed;
cvar_t cl_contentfilter = {"cl_contentfilter", "0"};
char dequake[256];
static scoreboard_t scores[18];
static byte sent[4096];
static char output[65536], suggestions[2048];
static cmd_function_t command;
static cmd_function_t *cmd_functions;
static int argc_stub = 1;
static const char *argv_stub[8] = {"translate"};
static SDL_ThreadID main_thread;
static qboolean ignored;
int q_strcasecmp (const char *a, const char *b) { return SDL_strcasecmp(a, b); }
int q_strncasecmp (const char *a, const char *b, size_t n) { return SDL_strncasecmp(a, b, n); }
char *q_strcasestr (const char *a, const char *b) { return (char *)SDL_strcasestr(a, b); }
size_t q_strlcpy (char *a, const char *b, size_t n) { return SDL_strlcpy(a, b, n); }
size_t q_strlcat (char *a, const char *b, size_t n) { return SDL_strlcat(a, b, n); }
int q_snprintf (char *out, size_t size, const char *format, ...)
{
	int result;
	va_list args;
	va_start(args, format); result = vsnprintf(out, size, format, args); va_end(args);
	return result;
}

void Con_Printf (const char *format, ...)
{
	va_list args;
	size_t used = strlen(output);
	assert(SDL_GetCurrentThreadID() == main_thread);
	va_start(args, format); vsnprintf(output + used, sizeof(output) - used, format, args); va_end(args);
}

cmd_function_t *Cmd_AddCommand2 (const char *name, xcommand_t fn, cmd_source_t source, qboolean qc)
{
	command.name = name; command.function = fn; command.srctype = source; command.qcinterceptable = qc;
	command.completion = NULL; cmd_functions = &command;
	/* Startup registrations succeed but return NULL in Cmd_AddCommand2. */
	return NULL;
}
int Cmd_Argc (void) { return argc_stub; }
const char *Cmd_Argv (int index) { return index < argc_stub ? argv_stub[index] : ""; }
void Con_AddToTabList (const char *name, const char *partial, const char *type, const char *param)
{
	(void)type; (void)param;
	if (SDL_strcasestr(name, partial)) { SDL_strlcat(suggestions, name, sizeof(suggestions)); SDL_strlcat(suggestions, "\n", sizeof(suggestions)); }
}
#include "translation-completion.inc"
qboolean CL_ShouldIgnoreChatPrint (const char *text)
{
	return ignored && strstr(text, "Rasta") != NULL;
}
qboolean WordFilter_Check (const char *text, char *dest, size_t size)
{
	const char *bad = strstr(text, "badword");
	SDL_strlcpy(dest, text, size);
	if (!bad) return false;
	memset(dest + (bad - text), '*', 7);
	return true;
}
void MSG_WriteByte (sizebuf_t *buffer, int value)
{
	assert(buffer->cursize < buffer->maxsize);
	buffer->data[buffer->cursize++] = (byte)value;
}
void MSG_WriteString (sizebuf_t *buffer, const char *value)
{
	size_t n = strlen(value) + 1;
	assert((size_t)buffer->cursize + n <= (size_t)buffer->maxsize);
	memcpy(buffer->data + buffer->cursize, value, n);
	buffer->cursize += (int)n;
}

/* Production menu input/draw logic; unrelated renderer/editor APIs are stubbed.
 * Bounds assertions also exercise the fixed 320x200 menu layout. */
enum m_state_e m_state;
keydest_t key_dest;
qboolean m_entersound;
double realtime;
double host_frametime = 1.0 / 60.0;
qboolean keydown[MAX_KEYS], mapshint;
int m_mousex, m_mousey;
static int console_cursor;
#define CONSOLE_TRANSLATE 7
static qboolean textfield_mouse_dragging;
static menu_textfield_t *textfield_drag_field;
static char drawn[65536];
static char tooltip[160];
static const char *checked_player_name;
static int formatted_name_draws;
static int white_everyone_descriptions;
static int dimmed_add, dimmed_compose, dimmed_placeholder, dimmed_everyone;
void M_Menu_Console_f (void) { m_state = m_console; }
void S_LocalSound (const char *name) { (void)name; }
void M_Print (int x, int y, const char *text)
{
	assert(x >= 0 && y >= 0 && y < 200 && x + (int)strlen(text) * 8 <= 320);
	if (y == 172)
	{
		assert(x == (320 - (int)strlen(text) * 8) / 2);
		SDL_strlcpy(tooltip, text, sizeof(tooltip));
	}
	SDL_strlcat(drawn, text, sizeof(drawn)); SDL_strlcat(drawn, "\n", sizeof(drawn));
}
void M_PrintWhite (int x, int y, const char *text)
{
	if (!strcmp(text, " (translate all players)")) { assert(x == 104); white_everyone_descriptions++; }
	M_Print(x, y, text);
}
plcolour_t CL_PLColours_Parse (const char *text)
{
	plcolour_t color = {0};
	assert(!strcmp(text, "0xffffff"));
	return color;
}
void M_PrintRGBA (int x, int y, const char *text, plcolour_t color, float alpha, qboolean mask)
{
	(void)color;
	assert(alpha == .375f);
	if (!mask && !strcmp(text, " (translate all players)")) assert(x == 104);
	else if (!mask) { assert(y == 88 && !strcmp(text, "Type your message")); dimmed_placeholder++; }
	else if (!strcmp(text, "@everyone")) dimmed_everyone++;
	else if (!strcmp(text, "Add Translated Player(s)") || !strcmp(text, "a:add")) dimmed_add++;
	else if (!strcmp(text, "Send Translated Message")) dimmed_compose++;
	M_Print(x, y, text);
}
qpic_t *Draw_CachePic (const char *path)
{
	static qpic_t picture = {.width = 160, .height = 24};
	assert(!strcmp(path, "gfx/p_option.lmp"));
	return &picture;
}
void M_DrawPic (int x, int y, qpic_t *picture)
{ assert(x >= 0 && x + picture->width <= 320 && y >= 0 && y + picture->height <= 200); }
void M_DrawCharacter (int x, int y, int value) { (void)value; assert(x >= 0 && x < 320 && y >= 0 && y < 200); }
void M_DrawCheckbox (int x, int y, int value) { M_DrawCharacter(x, y, value); }
void M_DrawCheckboxBox (int x, int y, int value) { M_DrawCharacter(x, y, value); }
void M_DrawTextBox (int x, int y, int width, int lines)
{ assert(x >= 0 && x + (width + 2) * 8 <= 320 && y >= 0 && y + (lines + 2) * 8 <= 200); }
void Draw_String (int x, int y, const char *text) { M_PrintWhite(x, y, text); }
void M_DrawQuakeBar (int x, int y, int cols)
{ assert(x == 8 && y == 16 && cols == 38); }
void M_DrawEllipsisBar (int x, int y, int cols)
{ assert(x == 16 && (y == 24 || y == 160) && cols == 36); }
void M_PrintScroll (int x, int y, int width, const char *text, double time, qboolean mask)
{
	char visible[35];
	(void)time; (void)mask;
	assert((x == 16 && width == 272) || (x == 32 && width == 256));
	if (!strcmp(text, "Remove All")) assert(x == 32);
	if (checked_player_name && !strcmp(text, checked_player_name))
	{
		assert(!mask); /* Scoreboard glyph colors must not be flipped uniformly. */
		assert(x == 32 && width == 256);
		formatted_name_draws++;
	}
	SDL_strlcpy(visible, text, sizeof(visible)); M_Print(x, y, visible);
}
void M_ForceMousemove (void) {}
qboolean Key_IsShortcutModifierDown (void) { return ctrlpressed; }
#include "translation-list.inc"
void M_TextField_Init (menu_textfield_t *field, char *buffer, int maximum, qboolean digits)
{ field->text = buffer; field->max_len = maximum; field->cursor = (int)strlen(buffer); field->sel_start = -1; field->digits_only = digits; }
void M_TextField_ClearSelection (menu_textfield_t *field) { field->sel_start = -1; }
qboolean M_TextField_Key (menu_textfield_t *field, int key)
{
	if (key == K_LEFTARROW) { field->cursor = q_max(0, field->cursor - 1); return true; }
	if (key == K_RIGHTARROW) { field->cursor = q_min((int)strlen(field->text), field->cursor + 1); return true; }
	return false;
}
qboolean M_TextField_Char (menu_textfield_t *field, int key)
{
	int length = (int)strlen(field->text);
	if (key < 32 || key > 126 || length >= field->max_len) return false;
	memmove(field->text + field->cursor + 1, field->text + field->cursor, (size_t)(length - field->cursor + 1));
	field->text[field->cursor++] = (char)key;
	return true;
}
void M_TextField_CheckMouseRelease (void) {}
void M_TextField_MouseClick (menu_textfield_t *field, int x, int origin)
{ field->cursor = CLAMP(0, (x - origin) / 8, (int)strlen(field->text)); }
void M_TextField_MouseDrag (int x) { (void)x; }
void M_TextField_DrawHighlight (menu_textfield_t *field, int x, int y)
{
	assert(strlen(field->text) <= 32);
	assert(field->sel_start < 0 || field->sel_start <= (int)strlen(field->text));
	M_DrawCharacter(x,y,0);
}
void M_TextField_DrawCursor (menu_textfield_t *field, int x, int y) { M_DrawCharacter(x + field->cursor * 8,y,0); }
#include "translation-menu.inc"

static void world (void)
{
	int i;
	for (i = 0; i < 256; i++) dequake[i] = (i & 127) >= 32 ? (char)(i & 127) : '#';
	for (i = 0; i < 10; i++) dequake[18+i] = (char)('0'+i);
	dequake[16] = '['; dequake[17] = ']';
	cls.state = ca_connected; cls.signon = SIGNONS; cl.scores = scores; cl.maxclients = 8;
	cl.realviewentity = cl.viewentity = 1;
	cls.message.data = sent; cls.message.maxsize = sizeof(sent);
	SDL_strlcpy(scores[0].name, "Me", sizeof(scores[0].name));
	SDL_strlcpy(scores[1].name, "Rasta", sizeof(scores[1].name));
	SDL_strlcpy(scores[2].name, "Other Player", sizeof(scores[2].name));
}

static void observe (const char *text)
{
	CL_Translate_Observe(text, text);
}

static int occurrences (const char *text, const char *needle)
{
	int count = 0;
	while ((text = strstr(text, needle)) != NULL) { count++; text += strlen(needle); }
	return count;
}

static void settle (void)
{
	Uint64 until = SDL_GetTicks() + 3000;
	int pending;
	qboolean busy;
	do
	{
		CL_Translate_Frame();
		if (!tr_mutex) return;
		SDL_LockMutex(tr_mutex); pending = tr_pending_count + tr_completed_count; busy = tr_busy; SDL_UnlockMutex(tr_mutex);
		if (!pending && !busy) return;
		SDL_Delay(5);
	} while (SDL_GetTicks() < until);
	assert(!"worker did not settle within three seconds");
}

static void unit_tests (void)
{
	tr_reply_t reply;
	char text[2049];
	size_t body;
	/* Help is read-only and available offline. */
	cls.state = ca_disconnected; output[0] = 0;
	assert(CL_Translate_Console("translate help"));
	assert(strstr(output, "translate @Rasta") && strstr(output, "translate spanish @Rasta"));
	assert(strstr(output, "translate off @Rasta") && strstr(output, "Ctrl+Enter"));
	assert(!tr_thread && !tr_outgoing);
	argc_stub = 2; argv_stub[1] = "help"; output[0] = 0; command.function();
	assert(strstr(output, "translate @Rasta") && !tr_thread);
	argc_stub = 1; cls.state = ca_connected; output[0] = 0;
	assert(TR_Reply("{\"version\":1,\"id\":4294967295,\"status\":\"translated\",\"text\":\"Hola\\u00e1\"}", &reply));
	assert(reply.id == UINT32_MAX && reply.fields == 15);
	const char *invalid[] = {
		"{\"id\":1,\"id\":2}", "{\"id\":1,\"\\u0069d\":2}", "{\"id\":true}",
		"{\"id\":4294967296}", "{\"id\":1e2}", "{\"id\":01}", "{\"id\":-1}",
		"{\"text\":\"a\\u0000b\"}", "{\"text\":\"\\ud800\"}", "{\"text\":\"\\udc00\"}",
		"{\"text\":\"\\q\"}", "{\"text\":\"\xff\"}", "{\"id\":1,}", "{\"id\":1} {}", "[]"
	};
	for (size_t i = 0; i < countof(invalid); i++) assert(!TR_Reply(invalid[i], &reply));
	/* Truncated network JSON must fail safely at every byte boundary, including
	 * escaped Unicode and surrogate pairs; exact allocation lets ASan catch reads. */
	const char *whole = "{\"version\":1,\"id\":42,\"status\":\"translated\",\"text\":\"Hola\\u00e1\\ud83d\\ude00\"}";
	for (size_t i = 0; i < strlen(whole); i++)
	{
		char *partial = malloc(i + 1); assert(partial);
		memcpy(partial, whole, i); partial[i] = 0;
		assert(!TR_Reply(partial, &reply)); free(partial);
	}
	assert(TR_DisplayText(text, sizeof(text), "¿Quién? — café\n^1hola"));
	assert(strstr(text, "cafe") && strstr(text, "^^1hola") && !strchr(text, '\n'));
	assert(!TR_DisplayText(text, sizeof(text), "???"));
	assert(!TR_DisplayText(text, 4, "too long"));
	assert(!TR_DisplayText(text, sizeof(text), "\xc0\x80"));
	tr_completion_t oversized;
	memset(&oversized, 0, sizeof(oversized));
	char too_big[TR_RESPONSE + 1]; memset(too_big, 'x', sizeof(too_big));
	assert(!TR_Write(too_big, 1, sizeof(too_big), &oversized));
	assert(!TR_Write("a\0b", 1, 3, &oversized));
	assert(TR_Readable(text, sizeof(text), "^1hola ^xF00dm2", 15));
	assert(!strcmp(text, "hola dm2"));
	assert(!TR_Readable(text, sizeof(text), "^[secret^]", 10));
	assert(TR_Readable(text, sizeof(text), "\222\223\224", 3) && !strcmp(text, "012"));
	assert(TR_Classify("\001Rasta: hola", 12, &body) == 1 && body == 8);
	assert(TR_Classify("Rasta: hola", 11, &body) < 0);
	assert(TR_Classify("\001(Rasta): hola", 14, &body) < 0);
	SDL_strlcpy(scores[3].name, "Rasta", sizeof(scores[3].name));
	assert(TR_Classify("\001Rasta: hola", 12, &body) < 0);
	scores[3].name[0] = 0;
	SDL_strlcpy(scores[3].name, "(Rasta)", sizeof(scores[3].name));
	assert(TR_Classify("\001(Rasta): hola", 14, &body) < 0);
	scores[3].name[0] = 0;
	assert(TR_Trivial("gg") && TR_Trivial("dm2") && !TR_Trivial("dale") && !TR_Trivial("RA en 10"));
	assert(TR_FindPlayer("rAs ") == 1 && TR_FindPlayer("\"Other Player\" ") == 2);
	assert(!strcmp(CL_Translate_CompletionStart("translate @"), "@"));
	assert(!strcmp(CL_Translate_CompletionStart(" \ttranslate\t@Other Pl"), "@Other Pl"));
	assert(!strcmp(CL_Translate_CompletionStart("translate english @Other Pl"), "@Other Pl"));
	assert(!strcmp(CL_Translate_CompletionStart("translate off @Other Pl"), "@Other Pl"));
	assert(!strcmp(CL_Translate_CompletionStart("translate save @Other Pl"), "@Other Pl"));
	assert(!strcmp(CL_Translate_CompletionStart("translate spanish save @Other Pl"), "@Other Pl"));
	assert(!CL_Translate_CompletionStart("translate spanish my message"));
	assert(!CL_Translate_Console("echo x; translate english @Rasta"));
	output[0] = 0; CL_Translate_Console("translate @Raster");
	assert(!strcmp(output, "Player \"Raster\" isn't here. Press Tab to choose.\n"));
	output[0] = 0; CL_Translate_Console("translate english @\"Raster\" \t");
	assert(!strcmp(output, "Player \"Raster\" isn't here. Press Tab to choose.\n"));
	SDL_strlcpy(scores[3].name, "Raster", sizeof(scores[3].name));
	assert(TR_FindPlayer("Ra") == TR_PLAYER_AMBIGUOUS && TR_FindPlayer("Rasta") == 1);
	output[0] = 0; CL_Translate_Console("translate @Ra");
	assert(!strcmp(output, "Several players match \"Ra\". Press Tab to choose.\n"));
	output[0] = 0; CL_Translate_Console("translate spanish @Ra");
	assert(!strcmp(output, "Several players match \"Ra\". Press Tab to choose.\n"));
	output[0] = 0; CL_Translate_Console("translate off @Ra");
	assert(!strcmp(output, "Several players match \"Ra\". Press Tab to choose.\n"));
	assert(!tr_outgoing && !tr_watch[1] && !tr_watch[3] && !tr_thread);
	scores[3].name[0] = 0;
	output[0] = 0; CL_Translate_Console("translate @^^1Raster");
	assert(strstr(output, "\"^^1Raster\"") && !tr_outgoing); /* literal color syntax */
	assert(CL_Translate_Console("translate english @Unknown"));
	assert(CL_Translate_Console("translate @Unknown"));
	assert(CL_Translate_Console("translate @"));
	assert(!tr_outgoing && !tr_watch[1]);
	/* The shorthand always selects English, including with a Spanish menu
	 * choice. Names are not parsed as language tokens or outgoing text. */
	CL_Translate_MenuReadLanguage(1);
	CL_Translate_Console(" \ttranslate\t@Ra");
	assert(tr_watch[1] == 1 && tr_read_language == 1 && !tr_thread);
	assert(!strcmp(CL_Translate_MenuNotice(), "Listening for new chat."));
	CL_Translate_Console("translate spanish @Rasta"); assert(tr_watch[1] == 2);
	CL_Translate_Console("translate @Rasta"); assert(tr_watch[1] == 1);
	CL_Translate_Console("translate @\"Other Player\""); assert(tr_watch[2] == 1);
	SDL_strlcpy(scores[3].name, "PlayerNameWithTwentyFiveLetters", sizeof(scores[3].name));
	observe("\001PlayerNameWithTwentyFiveLetters: gg\n");
	CL_Translate_Console("translate @PlayerNameWithTwentyFiveLetters"); assert(tr_watch[3] == 1);
	assert(!strcmp(CL_Translate_MenuNotice(), "Listening for new chat.") && !tr_thread);
	CL_Translate_MenuStopAll(); scores[3].name[0] = 0;
	SDL_strlcpy(scores[3].name, "Rasta", sizeof(scores[3].name));
	assert(TR_FindPlayer("Rasta") == TR_PLAYER_AMBIGUOUS);
	output[0] = 0; CL_Translate_Console("translate @Rasta");
	assert(!strcmp(output, "Several players match \"Rasta\". Press Tab to choose.\n"));
	assert(!tr_watch[1] && !tr_watch[3] && !tr_outgoing);
	scores[3].name[0] = 0;
	CL_Translate_MenuReadLanguage(0);
	/* Every menu choice is accepted by name/code and offered by Tab. */
	assert(CL_Translate_LanguageCount() == 22);
	for (int i = 0; i < CL_Translate_LanguageCount(); i++)
	{
		char name[24], label[4], command_line[64];
		assert(TR_Language(CL_Translate_LanguageName(i)) == i);
		assert(TR_Language(CL_Translate_LanguageCode(i)) == i);
		TR_LanguageLabel(label, i);
		assert(strlen(label) == strlen(CL_Translate_LanguageCode(i)));
		q_strlcpy(name, CL_Translate_LanguageName(i), sizeof(name));
		for (int j = 0; name[j]; j++) name[j] = (char)tolower((unsigned char)name[j]);
		argc_stub = 2; argv_stub[1] = name; suggestions[0] = 0;
		command.completion(name); assert(strstr(suggestions, name));
		q_snprintf(command_line, sizeof(command_line), "translate %s @Rasta", CL_Translate_LanguageCode(i));
		CL_Translate_Console(command_line); assert(tr_watch[1] == i + 1 && !tr_thread);
	}
	CL_Translate_MenuStopAll();
	output[0] = 0; CL_Translate_Console("translate klingon Hello");
	assert(strstr(output, "Unknown language. Press Tab") && !tr_outgoing);
	argc_stub = 3; argv_stub[1] = "english"; argv_stub[2] = "@Rasta";
	command.function(); /* queued alias/server/script path is deliberately inert */
	assert(!tr_watch[1] && !tr_thread);
	suggestions[0] = 0;
	command.completion("@Other Pl");
	assert(strstr(suggestions, "@Other Player"));
	argc_stub = 2; argv_stub[1] = "@"; suggestions[0] = 0;
	command.function(); assert(!tr_watch[1] && !tr_thread);
	command.completion("@"); assert(!strcmp(suggestions, "@everyone\n@Rasta\n@Other Player\n") && !tr_thread);
	ignored = true; suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@everyone\n@Other Player\n"));
	argc_stub = 3; argv_stub[1] = "english"; argv_stub[2] = "@"; suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@everyone\n@Other Player\n"));
	argv_stub[1] = "off"; suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@everyone\n@Other Player\n"));
	ignored = false; SDL_strlcpy(scores[3].name, "Rasta", sizeof(scores[3].name)); suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@everyone\n@Other Player\n")); /* duplicate names excluded */
	SDL_strlcpy(scores[3].name, "(Rasta)", sizeof(scores[3].name)); suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@everyone\n@Rasta\n@Other Player\n")); /* team/public collision excluded */
	SDL_strlcpy(scores[3].name, "^[unreadable^]", sizeof(scores[3].name)); suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@everyone\n@Rasta\n@Other Player\n"));
	scores[3].name[0] = 0;
	assert(!tr_thread && !tr_watch[1] && !tr_outgoing);
	argc_stub = 3; argv_stub[1] = "@Other"; argv_stub[2] = "Pl"; suggestions[0] = 0;
	command.completion("@Other Pl"); assert(!strcmp(suggestions, "@Other Player\n"));
	argc_stub = 2; argv_stub[1] = ""; suggestions[0] = 0;
	command.completion(""); assert(strstr(suggestions, "english") && strstr(suggestions, "spanish") && strstr(suggestions, "off") && strstr(suggestions, "help") && !strstr(suggestions, "@"));
	argc_stub = 3; argv_stub[1] = "spanish"; argv_stub[2] = "message"; suggestions[0] = 0;
	command.completion("Other"); assert(!suggestions[0]);
	/* Keep the @ marker and full name literal, including colored input. */
	argc_stub = 2; argv_stub[1] = "@"; suggestions[0] = 0;
	SDL_strlcpy(scores[3].name, "Colored", sizeof(scores[3].name));
	scores[3].name[0] |= 0x80;
	command.completion("@\303ol"); assert(!strcmp(suggestions, "@Colored\n"));
	memset(scores[3].name, 'N', sizeof(scores[3].name) - 1);
	scores[3].name[sizeof(scores[3].name) - 2] = (char)('7' | 0x80);
	scores[3].name[sizeof(scores[3].name) - 1] = 0;
	char full_name[MAX_SCOREBOARDNAME + 2], expected_name[MAX_SCOREBOARDNAME + 3];
	q_snprintf(full_name, sizeof(full_name), "@%s", scores[3].name);
	full_name[strlen(full_name) - 1] = '7';
	q_snprintf(expected_name, sizeof(expected_name), "%s\n", full_name);
	suggestions[0] = 0; command.completion(full_name);
	assert(!strcmp(suggestions, expected_name));
	assert(TR_FindPlayer(full_name + 1) == 3);
	scores[3].name[0] = 0;
	argc_stub = 1;
	CL_Translate_Reset();
	/* A full ring must give an earlier catch-up line its own preceding context,
	 * rather than a context window based on the newest line in the ring. */
	observe("\001Rasta: line one\n\001Rasta: line two\n\001Rasta: line three\n");
	observe("\001Rasta: line four\n\001Rasta: line five\n\001Rasta: line six\n");
	tr_job_t contextual;
	memset(&contextual, 0, sizeof(contextual)); contextual.slot = 1;
	SDL_strlcpy(contextual.original, tr_history[1][3].text, sizeof(contextual.original));
	assert(TR_Request(&contextual, &tr_history[1][3]));
	assert(strstr(contextual.request, "line one") && strstr(contextual.request, "line two") && strstr(contextual.request, "line three"));
	assert(occurrences(contextual.request, "line four") == 1 && !strstr(contextual.request, "line five") && !strstr(contextual.request, "line six"));
	CL_Translate_Reset(); output[0] = 0;
}

static void everyone_tests (void)
{
	unsigned int identity;
	int generation;
	CL_Translate_Reset(); ignored = true;
	SDL_strlcpy(scores[3].name, "Duplicate", sizeof(scores[3].name));
	SDL_strlcpy(scores[4].name, "Duplicate", sizeof(scores[4].name));
	SDL_strlcpy(scores[5].name, "^[unreadable^]", sizeof(scores[5].name));
	SDL_strlcpy(scores[6].name, "(Other Player)", sizeof(scores[6].name));
	SDL_strlcpy(scores[7].name, "everyone", sizeof(scores[7].name));
	output[0] = 0; CL_Translate_Console("translate @EVERYONE \t");
	assert(tr_watch[2] == 1 && tr_watch[7] == 1 && !tr_watch[0] && !tr_watch[1]);
	for (int i = 3; i <= 6; i++) assert(!tr_watch[i]);
	assert(occurrences(output, "Translating") == 1 && strstr(output, "2 players' public chat to English"));
	assert(occurrences(output, "Stop:") == 1 && !tr_thread && !tr_outgoing);
	identity = tr_id; generation = SDL_GetAtomicInt(&tr_generation); output[0] = 0;
	CL_Translate_Console("translate english @everyone");
	assert(!output[0] && tr_id == identity && SDL_GetAtomicInt(&tr_generation) == generation);
	CL_Translate_Console("translate portuguese @everyone");
	assert(tr_watch[2] == 3 && tr_watch[7] == 3);
	assert(SDL_GetAtomicInt(&tr_generation) == generation + 1);
	/* Quoting distinguishes a real player named everyone from the keyword. */
	CL_Translate_Console("translate spanish @\"everyone\"");
	assert(tr_watch[7] == 2 && tr_watch[2] == 3);
	argc_stub = 2; argv_stub[1] = "@"; suggestions[0] = 0;
	command.completion("@");
	assert(occurrences(suggestions, "@everyone\n") == 1 && strstr(suggestions, "@\"everyone\""));
	SDL_strlcpy(scores[3].name, "Newcomer", sizeof(scores[3].name));
	assert(!tr_watch[3]); /* No automatic opt-in for later arrivals. */
	CL_Translate_Console("translate off @everyone");
	for (int i = 0; i < cl.maxclients; i++) assert(!tr_watch[i]);
	CL_Translate_Reset();
	scores[2].name[0] = scores[7].name[0] = 0;
	SDL_strlcpy(scores[3].name, "Duplicate", sizeof(scores[3].name));
	SDL_strlcpy(scores[6].name, "(Rasta)", sizeof(scores[6].name));
	output[0] = 0; CL_Translate_Console("translate @everyone");
	assert(strstr(output, "No eligible players") && !tr_thread && !tr_outgoing);
	suggestions[0] = 0; command.completion("@"); assert(!suggestions[0]);
	cls.state = ca_disconnected; output[0] = 0; CL_Translate_Console("translate @everyone");
	assert(strstr(output, "not connected") && tr_id == identity);
	memset(scores, 0, sizeof(scores)); world(); ignored = false; argc_stub = 1;
	CL_Translate_Reset(); output[0] = 0;
}

static void integration_tests (void)
{
	uint32_t identity;
	int generation;
	size_t acknowledgment_size;
	/* History is local before opt-in. Unknown/private/team/demo/fragmented lines
	 * are neither uploaded nor retained. A several-line print is reclassified. */
	observe("\001Rasta: hola\n");
	observe("\001Other Player: private context must stay local\n");
	observe("\001Rasta: cambiamos mapa?\n\001Rasta: dale\n");
	assert(tr_history_count[1] == 3 && !tr_thread);
	observe("dm [Rasta]: secret\n"); observe("server: admin\n"); observe("\001(Rasta): team\n");
	observe("\001Rasta: incomplete");
	cls.demoplayback = true; observe("\001Rasta: demo\n"); cls.demoplayback = false;
	assert(tr_history_count[1] == 3);
	assert(CL_Translate_Console("translate @Ra"));
	assert(strstr(output, "Translating Rasta's public chat to English.\nStop: translate off @Rasta\n"));
	assert(strstr(output, "Checking 3 recent messages.\n") && !strcmp(CL_Translate_MenuNotice(), "Checking 3 recent messages."));
	assert(!strstr(output, "Translation sends selected chat"));
	identity = tr_id; generation = SDL_GetAtomicInt(&tr_generation);
	acknowledgment_size = strlen(output);
	CL_Translate_Console("translate @Ra"); CL_Translate_Console("translate english @Rasta");
	assert(tr_watch[1] == 1 && tr_id == identity && SDL_GetAtomicInt(&tr_generation) == generation);
	assert(strlen(output) == acknowledgment_size); /* repeat commands are quiet */
	settle();
	assert(strstr(output, "^m[EN earlier]^m Rasta: Hello"));
	assert(strstr(output, "^m[EN earlier]^m Rasta: Shall we change maps?"));
	assert(strstr(output, "^m[EN earlier]^m Rasta: Sure"));
	assert(tr_translated == 3 && !cls.message.cursize);
	identity = tr_id;
	CL_Translate_Console("translate @Rasta"); settle(); assert(tr_id == identity && tr_watch[1] == 1);
	CL_Translate_Console("translate english @Rasta"); settle(); assert(tr_id == identity && tr_watch[1] == 1);
	CL_Translate_Console("translate off @Rasta");
	output[0] = 0; CL_Translate_Console("translate english @Rasta"); settle(); assert(tr_id == identity);
	assert(strstr(output, "Listening for new chat.\n") && !strstr(output, "Checking"));
	assert(!strstr(output, "Translation sends selected chat"));

	output[0] = 0;
	observe("\001Rasta: hola\n"); settle();
	assert(strstr(output, "^m[EN]^m Rasta: Hello") && !strstr(output, "earlier"));
	/* English stays original-only; the sender prefix is never model-generated. */
	output[0] = 0; observe("\001Rasta: nice shot\n"); settle();
	assert(!strstr(output, "[EN]"));
	identity = tr_id; observe("\001Rasta: gg\n"); settle(); assert(tr_id == identity);
	observe("\001Me: hola\n"); settle(); assert(tr_id == identity);

	CL_Translate_Console("translate spanish Anyone up for 2v2 on dm2?");
	assert(!output[0] && tr_outgoing); /* progress is in the menu */
	settle();
	assert(cls.message.cursize > 1 && sent[0] == clc_stringcmd);
	assert(!strcmp((char *)sent + 1, "say Alguien para 2v2 en dm2?"));
	assert(!output[0] && tr_send_state == TRANSLATE_SEND_SENT);
	assert(!strcmp(tr_send_notice, "Sent publicly in Spanish."));
	identity = tr_id;
	/* Normal print handling displays the server echo; observation adds no copy
	 * and never re-translates our own message. */
	Con_Printf("%s", "\001Me: Alguien para 2v2 en dm2?\n");
	observe("\001Me: Alguien para 2v2 en dm2?\n"); settle();
	assert(occurrences(output, "Alguien para 2v2 en dm2?") == 1 && tr_id == identity);
	assert(!strstr(output, "[Sent"));
	cls.message.cursize = 0;
	/* Capture the team modifier at submission, never when HTTP finishes. */
	ctrlpressed = true; output[0] = 0;
	CL_Translate_Console("translate spanish Hello");
	assert(strstr(tr_send_notice, "for team"));
	ctrlpressed = false; settle();
	assert(!strcmp((char *)sent + 1, "say_team Hola") && !output[0]);
	assert(!strcmp(tr_send_notice, "Sent to team in Spanish."));
	cls.message.cursize = 0;
	CL_Translate_Console("translate english nice shot"); ctrlpressed = true; settle();
	assert(!strcmp((char *)sent + 1, "say nice shot"));
	cls.message.cursize = 0; output[0] = 0;
	CL_Translate_Console("translate spanish unlisted input"); ctrlpressed = false; settle();
	assert(!cls.message.cursize && strstr(output, "Your message was not sent"));
	/* The menu's explicit public composer stays public with Ctrl held. */
	ctrlpressed = true; assert(CL_Translate_MenuSend(1, "Hello"));
	ctrlpressed = false; settle();
	assert(!strcmp((char *)sent + 1, "say Hola"));
	cls.message.cursize = 0;
	CL_Translate_Console("translate spanish Send %h; quit"); settle();
	assert(!strcmp((char *)sent + 1, "say Enviar %h; quit"));
	cls.message.cursize = 0;
	CL_Translate_Console("translate spanish unlisted input"); settle();
	assert(!cls.message.cursize && strstr(output, "Your message was not sent"));
	SDL_strlcpy(scores[3].name, "Second Listener", sizeof(scores[3].name));
	CL_Translate_PlayerChanged(3, scores[3].name);
	CL_Translate_Console("translate spanish Hello");
	CL_Translate_Console("translate english @Second Listener"); settle();
	assert(tr_watch[1] == 1 && tr_watch[3] == 1);
	assert(!strcmp((char *)sent + 1, "say Hola")); cls.message.cursize = 0;

	/* Wrong id, duplicate keys and an overlong translation never become chat. */
	for (int i = 0; i < 2; i++)
	{
		tr_completion_t done;
		memset(&done, 0, sizeof(done));
		done.job.id = 900; done.job.slot = -1; done.job.outgoing = true;
		done.job.generation = SDL_GetAtomicInt(&tr_generation); done.job.deadline = SDL_GetTicks() + 1000;
		done.http = 200; done.result = CURLE_OK;
		SDL_strlcpy(done.response, i ? "{\"version\":1,\"id\":900,\"status\":\"unchanged\",\"id\":900}" :
			"{\"version\":1,\"id\":901,\"status\":\"translated\",\"text\":\"bad send\"}", sizeof(done.response));
		TR_Complete(&done); assert(!cls.message.cursize);
	}
	tr_failures = 0;

	/* Changing ignores/filter clears old private context and pending work. */
	ignored = true; CL_Translate_IgnoreChanged();
	identity = tr_id; observe("\001Rasta: hola\n"); settle();
	assert(tr_id == identity && tr_history_count[1] == 0);
	ignored = false; CL_Translate_IgnoreChanged();
	cl_contentfilter.value = 2; CL_Translate_Frame();
	char filtered[TR_TEXT + 1]; assert(TR_FilteredBody(filtered, "badword hola", 12));
	assert(!strcmp(filtered, "******* hola"));
	cl_contentfilter.value = 0; CL_Translate_Frame();

	/* Catch-up is time-bounded, and a map clears history while retaining watch. */
	output[0] = 0;
	CL_Translate_Console("translate off");
	observe("\001Rasta: hola\n");
	tr_backoff_until = SDL_GetTicks() + 10000; identity = tr_id; output[0] = 0;
	CL_Translate_Console("translate @Rasta");
	assert(tr_id == identity && strstr(output, "Recent messages could not be queued.\n"));
	assert(!strstr(output, "Checking") && !strstr(output, "Listening for new chat"));
	tr_backoff_until = 0; CL_Translate_Console("translate off @Rasta"); output[0] = 0;
	tr_history[1][0].time = SDL_GetTicks() - TR_RECENT_AGE - 1;
	identity = tr_id; CL_Translate_Console("translate english @Rasta"); settle(); assert(tr_id == identity);
	assert(strstr(output, "Listening for new chat.\n") && !strstr(output, "Checking"));
	CL_Translate_MapChanged(); assert(tr_watch[1] == 1 && !tr_history_count[1]);
	assert(!strstr(output, "Translation sends selected chat"));

	/* A burst keeps bounded fresh work, and cannot discard a typed reply. */
	Uint64 arrival = SDL_GetTicks(); observe("\001Rasta: delay\n");
	assert(SDL_GetTicks() - arrival < 100);
	SDL_Delay(50);
	for (int i = 0; i < 12; i++) observe("\001Rasta: hola\n");
	SDL_LockMutex(tr_mutex); assert(tr_pending_count <= TR_QUEUE); SDL_UnlockMutex(tr_mutex);
	assert(tr_dropped > 0);
	CL_Translate_Console("translate spanish Hello");
	settle();
	assert(!strcmp((char *)sent + 1, "say Hola")); cls.message.cursize = 0;

	/* A stalled frame cannot lose an explicitly typed send completion. */
	SDL_LockMutex(tr_mutex);
	memset(tr_completed, 0, sizeof(tr_completed));
	tr_completed_count = TR_COMPLETIONS;
	SDL_UnlockMutex(tr_mutex);
	CL_Translate_Console("translate spanish Hello"); SDL_Delay(150); settle();
	assert(!strcmp((char *)sent + 1, "say Hola")); cls.message.cursize = 0;
	SDL_LockMutex(tr_mutex); assert(tr_completion_drops > 0); SDL_UnlockMutex(tr_mutex);

	/* A slot rename cancels an active result instead of attributing it to the
	 * replacement. Off/disconnect cancel outgoing sends too. */
	output[0] = 0; observe("\001Rasta: delay\n"); SDL_Delay(50);
	SDL_strlcpy(scores[1].name, "Replacement", sizeof(scores[1].name));
	CL_Translate_PlayerChanged(1, scores[1].name); settle();
	assert(!tr_watch[1] && !strstr(output, "Delayed"));
	CL_Translate_Console("translate spanish delay"); SDL_Delay(50);
	CL_Translate_Console("translate off"); settle(); assert(!cls.message.cursize);
	CL_Translate_Console("translate spanish delay"); SDL_Delay(50);
	CL_Translate_Reset(); settle(); assert(!cls.message.cursize);

	/* Catch-up counts real checks, skips trivial/handled lines, and uses the
	 * same menu feedback. Checking does not promise a translation for every line. */
	observe("\001Replacement: hola\n\001Replacement: gg\n\001Replacement: dale\n");
	output[0] = 0; CL_Translate_Console("translate @Replacement");
	assert(strstr(output, "Checking 2 recent messages.\n") && !strcmp(CL_Translate_MenuNotice(), "Checking 2 recent messages."));
	identity = tr_id; settle();
	assert(strstr(output, "^m[EN earlier]^m Replacement: Hello") && strstr(output, "^m[EN earlier]^m Replacement: Sure"));
	CL_Translate_Console("translate off @Replacement"); observe("\001Replacement: hola\n");
	output[0] = 0; CL_Translate_Console("translate @Replacement");
	assert(strstr(output, "Checking 1 recent message.\n") && tr_id == identity + 1);
	settle(); CL_Translate_Console("translate off @Replacement");
	output[0] = 0; identity = tr_id; CL_Translate_Console("translate @Replacement"); settle();
	assert(strstr(output, "Listening for new chat.\n") && tr_id == identity && !strstr(output, "earlier]"));
	/* A full queue of live chat leaves room for one catch-up check. Replacing
	 * earlier catch-up jobs in this same batch must not inflate the announced count. */
	CL_Translate_MenuStopAll(); CL_Translate_MapChanged();
	CL_Translate_Console("translate @Replacement"); observe("\001Replacement: delay\n"); SDL_Delay(50);
	for (int i = 0; i < TR_QUEUE - 1; i++) observe("\001Replacement: hola\n");
	observe("\001Second Listener: hola\n\001Second Listener: dale\n\001Second Listener: cambiamos mapa?\n");
	output[0] = 0; CL_Translate_Console("translate @Second Listener");
	assert(strstr(output, "Checking 1 recent message.\n") && !strstr(output, "Checking 3"));
	CL_Translate_MenuStopAll(); settle();
	/* New targets travel through the real worker/contract and keep their labels
	 * and handled bits, including a three-character code. */
	CL_Translate_MapChanged(); output[0] = 0;
	CL_Translate_Console("translate german @Replacement");
	observe("\001Replacement: language label test\n"); settle();
	assert(strstr(output, "^m[DE]^m Replacement: Mock de translation"));
	assert(tr_history[1][0].handled & (1u << TR_Language("de")));
	output[0] = 0; CL_Translate_Console("translate filipino @Replacement"); settle();
	assert(strstr(output, "^m[FIL earlier]^m Replacement: Mock fil translation"));
	identity = tr_id; CL_Translate_Console("translate german @Replacement"); settle();
	assert(tr_id == identity); /* Already handled in this target. */
	output[0] = 0; CL_Translate_Console("translate french language label test"); settle();
	assert(!output[0] && tr_send_state == TRANSLATE_SEND_SENT);
	assert(!strcmp(tr_send_notice, "Sent publicly in French."));
	assert(!strcmp((char *)sent + 1, "say Mock fr translation"));
	cls.message.cursize = 0; CL_Translate_MenuStopAll();
	/* Retarget a whole roster once, preserving catch-up for every player. */
	CL_Translate_MapChanged();
	observe("\001Replacement: language label test\n\001Other Player: language label test\n\001Second Listener: language label test\n");
	CL_Translate_Console("translate english @everyone");
	generation = SDL_GetAtomicInt(&tr_generation); output[0] = 0;
	CL_Translate_Console("translate portuguese @everyone");
	assert(SDL_GetAtomicInt(&tr_generation) == generation + 1);
	settle();
	assert(strstr(output, "^m[PT earlier]^m Replacement:") && strstr(output, "^m[PT earlier]^m Other Player:"));
	assert(strstr(output, "^m[PT earlier]^m Second Listener:") && !strstr(output, "[EN"));
	identity = tr_id; acknowledgment_size = strlen(output);
	CL_Translate_Console("translate portuguese @everyone"); settle();
	assert(tr_id == identity && strlen(output) == acknowledgment_size);
	SDL_strlcpy(scores[4].name, "Later Arrival", sizeof(scores[4].name));
	observe("\001Later Arrival: language label test\n");
	assert(!tr_watch[4] && tr_id == identity);
	scores[4].name[0] = 0; CL_Translate_PlayerChanged(4, "");
	CL_Translate_MenuStopAll();
	/* Keeping IDs across reconnects still permits safe recovery at uint32 wrap. */
	tr_job_t wrapping;
	memset(&wrapping, 0, sizeof(wrapping)); wrapping.slot = -1;
	tr_id = UINT32_MAX; generation = SDL_GetAtomicInt(&tr_generation);
	assert(!TR_Enqueue(&wrapping, NULL, NULL));
	assert(tr_id == 0 && SDL_GetAtomicInt(&tr_generation) != generation && !tr_outgoing);

	/* Daily quota pauses future uploads; reset/reconnect does not bypass it. */
	tr_completion_t limited;
	memset(&limited, 0, sizeof(limited));
	limited.job.id = 1000; limited.job.slot = -1; limited.job.outgoing = true;
	limited.job.generation = SDL_GetAtomicInt(&tr_generation); limited.job.deadline = SDL_GetTicks() + 1000;
	limited.http = 503; limited.result = CURLE_OK; limited.retry_after = 60;
	SDL_strlcpy(limited.response, "{\"error\":\"upstream_rate_limit\"}", sizeof(limited.response));
	output[0] = 0; TR_Complete(&limited);
	assert(tr_backoff_until > SDL_GetTicks() + 59000 && strstr(output, "translation provider is rate limited"));
	CL_Translate_Reset(); tr_backoff_until = 0;
	limited.job.generation = SDL_GetAtomicInt(&tr_generation);
	limited.http = 429; limited.result = CURLE_OK; limited.retry_after = 86400;
	SDL_strlcpy(limited.response, "{\"error\":\"daily_limit\"}", sizeof(limited.response));
	TR_Complete(&limited); assert(tr_backoff_until > SDL_GetTicks() + 86000000);
	CL_Translate_Reset(); identity = tr_id;
	CL_Translate_Console("translate spanish Hello");
	assert(tr_id == identity && !tr_outgoing && !cls.message.cursize);
	tr_backoff_until = 0; /* isolate the following shutdown test */
	output[0] = 0;
	CL_Translate_Console("translate spanish delay");
	assert(!output[0]);
	/* Outgoing-first use and subsequent watches have no automatic disclosure. */
	CL_Translate_Console("translate @Replacement");
	assert(!strstr(output, "Translation sends selected chat") && tr_outgoing);
	SDL_Delay(50);
	Uint64 before = SDL_GetTicks(); CL_Translate_Shutdown();
	assert(SDL_GetTicks() - before < 1000 && !cls.message.cursize);
}

static void menu_tests (void)
{
	unsigned int identity;
	char notice[160];
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cls.demoplayback = false;
	cls.message.cursize = 0; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset(); tr_backoff_until = 0;
	CL_Translate_MenuReadLanguage(0); CL_Translate_MenuWriteLanguage(1);
	output[0] = drawn[0] = 0; dimmed_add = 0;
	M_Menu_Translate_f(); M_Translate_Draw();
	assert(!translatemenu.adding && !translatemenu.count && strstr(drawn, "Chat Translation"));
	assert(strstr(drawn, "Add Translated Player(s)") && strstr(drawn, "Send Translated Message"));
	assert(strstr(drawn, "Choose whose chat to translate") && !strstr(drawn, "Live Translation"));
	assert(!strstr(drawn, "Translate to"));
	assert(!output[0] && !tr_thread && !tr_outgoing);
	assert(translatemenu.can_add && !dimmed_add);
	M_Translate_Key(K_RIGHTARROW); assert(!translatemenu.adding); /* arrows do not open actions */
	ctrlpressed = true; M_Translate_Key('a'); ctrlpressed = false;
	drawn[0] = 0; white_everyone_descriptions = 0; M_Translate_Draw();
	assert(translatemenu.adding && translatemenu.count == 2 && translatemenu.players[0].slot == 1);
	assert(!strstr(drawn, "Back") && strstr(drawn, "@everyone") && strstr(drawn, "(translate all players)"));
	assert(strstr(drawn, "Other Player") < strstr(drawn, "@everyone") && white_everyone_descriptions == 1);
	M_Translate_Key(K_UPARROW); assert(translatemenu.list.cursor == 1 && !translatemenu.remember);
	identity = tr_id;
	m_mousex = 100; m_mousey = M_Translate_RowY(M_Translate_EveryoneRow() - 1); M_Translate_Key(K_MOUSE1);
	assert(translatemenu.list.cursor == 1 && tr_id == identity && !translatemenu.remember);
	M_Translate_Key(K_UPARROW); assert(translatemenu.list.cursor == 0);
	M_Translate_Key(K_RIGHTARROW); assert(CL_Translate_ReadLanguage() == 1 && !tr_watch[1]);
	M_Translate_Key(K_RIGHTARROW); assert(CL_Translate_ReadLanguage() == 2 && !tr_watch[1]);
	M_Translate_Key(K_LEFTARROW); M_Translate_Key(K_LEFTARROW);
	M_Translate_Key(K_LEFTARROW); assert(CL_Translate_ReadLanguage() == CL_Translate_LanguageCount() - 1);
	M_Translate_Key(K_RIGHTARROW); M_Translate_Key(K_DOWNARROW); M_Translate_Key(K_DOWNARROW);
	assert(translatemenu.list.cursor == TRANSLATE_CHOOSE_PLAYERS_ROW);
	M_Translate_Mousemove(100, M_Translate_RowY(TRANSLATE_CHOOSE_PLAYERS_ROW)); M_Translate_Key(K_RIGHTARROW);
	assert(!tr_watch[1] && !tr_thread); /* browsing never enables a watch */
	M_Translate_Key(K_ENTER); drawn[0] = 0; M_Translate_Draw();
	assert(!translatemenu.adding && translatemenu.count == 1 && tr_watch[1] == 1);
	assert(strstr(drawn, "Rasta") && strstr(drawn, "English") && strstr(drawn, "remove"));
	assert(!strcmp(tooltip, "Language: English")); /* Routine notices never cover a player's language. */
	CL_Translate_Console("translate spanish @Other Player"); drawn[0] = 0; M_Translate_Draw();
	assert(translatemenu.count == 2 && tr_watch[1] == 1 && tr_watch[2] == 2);
	/* Actions precede the players; the blank separator is never interactive. */
	assert(strstr(drawn, "Send Translated Message") < strstr(drawn, "Rasta"));
	identity = tr_id;
	m_mousex = 100; m_mousey = M_Translate_RowY(TRANSLATE_PLAYERS_ROW - 1);
	M_Translate_Key(K_MOUSE1);
	assert(M_Translate_PlayerIndex() == 0 && tr_id == identity && tr_watch[1] == 1);
	m_mousey = M_Translate_RowY(TRANSLATE_PLAYERS_ROW + translatemenu.count);
	M_Translate_Key(K_MOUSE1);
	assert(M_Translate_PlayerIndex() == 0 && tr_id == identity && tr_watch[1] == 1 && tr_watch[2] == 2);
	M_Translate_Mousemove(100, M_Translate_RowY(TRANSLATE_PLAYERS_ROW + 1)); drawn[0] = 0; M_Translate_Draw();
	assert(!strcmp(tooltip, "Language: Spanish") && !strstr(drawn, "\nLanguage\n"));
	ctrlpressed = true; M_Translate_Key('e'); ctrlpressed = false; M_Translate_Draw();
	assert(translatemenu.player_edit && tr_watch[2] == 2 && strstr(drawn, "Accept Changes"));
	M_Translate_Key(K_UPARROW); M_Translate_Key(K_UPARROW); M_Translate_Key(K_RIGHTARROW);
	assert(tr_watch[2] == 2); /* Card edits remain drafts until accepted. */
	M_Translate_Key(K_DOWNARROW); M_Translate_Key(K_DOWNARROW); M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(tr_watch[1] == 1 && tr_watch[2] == 3 && !strcmp(tooltip, "Language: Portuguese"));
	M_Translate_Key(K_LEFTARROW); assert(tr_watch[2] == 2);
	translatemenu.list.cursor = TRANSLATE_PLAYERS_ROW; M_Translate_Key(K_RIGHTARROW); M_Translate_Draw();
	assert(tr_watch[1] == 2 && tr_watch[2] == 2);
	M_Translate_Key(K_RIGHTARROW); M_Translate_Draw();
	assert(tr_watch[1] == 3 && tr_watch[2] == 2 && strstr(drawn, "Portuguese"));
	M_Translate_Key(K_LEFTARROW);
	M_Translate_Key(K_LEFTARROW); assert(tr_watch[1] == 1);
	assert(tr_watch[2] == 2); /* Another player's target stays independent. */
	CL_Translate_MenuReadLanguage(2); assert(CL_Translate_ReadLanguage() == 2 && tr_watch[1] == 1 && tr_watch[2] == 2);
	CL_Translate_MenuReadLanguage(CL_Translate_LanguageCount()); assert(CL_Translate_ReadLanguage() == 2);
	CL_Translate_MenuReadLanguage(0);
	/* Everyone adds remaining players with the chooser's language and keeps
	 * previously selected targets; both keyboard and mouse activation work. */
	SDL_strlcpy(scores[3].name, "Third Player", sizeof(scores[3].name));
	SDL_strlcpy(scores[4].name, "Fourth Player", sizeof(scores[4].name));
	observe("\001Third Player: gg\n\001Fourth Player: gg\n");
	CL_Translate_MenuReadLanguage(2); M_Translate_Draw();
	translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(translatemenu.adding && translatemenu.count == 2);
	translatemenu.list.cursor = M_Translate_EveryoneRow(); M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(!translatemenu.adding && translatemenu.count == 4 && tr_watch[3] == 3 && tr_watch[4] == 3);
	assert(tr_watch[1] == 1 && tr_watch[2] == 2 && !tr_thread);
	CL_Translate_MenuWatch(4, scores[4].name, -1);
	CL_Translate_MenuReadLanguage(1); M_Translate_Draw();
	translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER); M_Translate_Draw();
	m_mousex = 100; m_mousey = M_Translate_RowY(M_Translate_EveryoneRow()); M_Translate_Key(K_MOUSE1); M_Translate_Draw();
	assert(!translatemenu.adding && tr_watch[4] == 2 && tr_watch[3] == 3 && tr_watch[1] == 1 && tr_watch[2] == 2);
	CL_Translate_MenuWatch(3, scores[3].name, -1); CL_Translate_MenuWatch(4, scores[4].name, -1);
	scores[3].name[0] = scores[4].name[0] = 0;
	CL_Translate_MenuReadLanguage(0); M_Translate_Draw();

	/* Console changes keep selection attached to the same player. */
	translatemenu.list.cursor = TRANSLATE_PLAYERS_ROW + 1;
	CL_Translate_Console("translate off @Rasta"); M_Translate_Draw();
	assert(translatemenu.count == 1 && translatemenu.list.cursor == TRANSLATE_PLAYERS_ROW && translatemenu.players[0].slot == 2);
	M_Translate_Key(K_DEL); M_Translate_Draw();
	assert(!tr_watch[2] && !translatemenu.count && !tr_thread);
	CL_Translate_Console("translate @Rasta");
	CL_Translate_Console("translate spanish @Other Player"); M_Translate_Draw();
	M_Translate_Mousemove(100, M_Translate_RowY(TRANSLATE_PLAYERS_ROW));
	m_mousex = 200; m_mousey = 192; M_Translate_Key(K_MOUSE1); M_Translate_Draw();
	assert(!tr_watch[1] && tr_watch[2] == 2 && translatemenu.count == 1);
	CL_Translate_Console("translate @Rasta"); M_Translate_Draw(); translatemenu.list.cursor = TRANSLATE_PLAYERS_ROW;
	CL_Translate_Console("translate off @Rasta"); M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(!tr_watch[1] && tr_watch[2] == 2); /* a cached language control never enables */

	/* The chooser excludes self, selected, ignored, duplicate and unreadable names. */
	translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(translatemenu.adding && translatemenu.count == 1 && translatemenu.players[0].slot == 1);
	ignored = true; dimmed_everyone = 0; M_Translate_Draw();
	assert(!translatemenu.count && !tr_watch[1] && dimmed_everyone == 1);
	M_Translate_Key(K_ESCAPE); assert(!translatemenu.adding && m_state == m_translate);
	dimmed_add = 0; M_Translate_Draw(); assert(!translatemenu.can_add && dimmed_add == 2);
	ignored = false; M_Translate_Draw(); assert(translatemenu.can_add);
	translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER); M_Translate_Draw();
	SDL_strlcpy(scores[1].name, "Replacement", sizeof(scores[1].name));
	M_Translate_Key(K_ENTER); assert(translatemenu.adding && !tr_watch[1]); /* stale identity refused */
	SDL_strlcpy(scores[1].name, "Rasta", sizeof(scores[1].name));
	SDL_strlcpy(scores[3].name, "Rasta", sizeof(scores[3].name)); M_Translate_Draw();
	assert(!translatemenu.count);
	SDL_strlcpy(scores[3].name, "^[unreadable^]", sizeof(scores[3].name)); M_Translate_Draw();
	assert(translatemenu.count == 1 && translatemenu.players[0].slot == 1);
	translatemenu.list.cursor = TRANSLATE_CHOOSE_PLAYERS_ROW;
	scores[3].name[0] = 0;
	/* An already-added cached choice retains its console-selected target. */
	CL_Translate_Console("translate english @Other Player");
	CL_Translate_Console("translate spanish @Rasta"); identity = tr_id;
	M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(!translatemenu.adding && tr_watch[1] == 2 && tr_watch[2] == 1 && tr_id == identity);
	/* Add Translated Player(s) dims and skips input when all eligible players are selected. */
	dimmed_add = 0; M_Translate_Draw();
	assert(!translatemenu.can_add && dimmed_add == 2);
	translatemenu.list.cursor = TRANSLATE_MESSAGE_ROW;
	M_Translate_Mousemove(100, M_Translate_RowY(TRANSLATE_ADD_ROW));
	assert(translatemenu.list.cursor == TRANSLATE_MESSAGE_ROW);
	m_mousex = 100; m_mousey = M_Translate_RowY(TRANSLATE_ADD_ROW);
	M_Translate_Key(K_MOUSE1); assert(!translatemenu.adding && tr_id == identity);
	M_Translate_Key(K_DOWNARROW); assert(translatemenu.list.cursor == TRANSLATE_PLAYERS_ROW);
	M_Translate_Key(K_UPARROW); assert(translatemenu.list.cursor == TRANSLATE_MESSAGE_ROW);
	translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER);
	assert(!translatemenu.adding && tr_id == identity);
	/* A player becoming ineligible while the chooser is open leaves it safe. */
	CL_Translate_Console("translate off @Rasta"); M_Translate_Draw(); assert(translatemenu.can_add);
	translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER); M_Translate_Draw();
	CL_Translate_Console("translate spanish @Rasta"); M_Translate_Draw();
	assert(translatemenu.adding && !translatemenu.count);
	identity = tr_id;
	m_mousey = 176; M_Translate_Key(K_MOUSE1); assert(translatemenu.adding);
	M_Translate_Key(K_UPARROW); M_Translate_Key(K_DOWNARROW); M_Translate_Key(K_TAB);
	M_Translate_Key(K_MWHEELUP); M_Translate_Key(K_MWHEELDOWN); M_Translate_Key(K_ENTER);
	assert(translatemenu.adding && translatemenu.list.cursor < TRANSLATE_CHOOSE_PLAYERS_ROW && tr_id == identity);
	CL_Translate_MenuReadLanguage(0);
	M_Translate_Key(K_ESCAPE); assert(!translatemenu.adding);

	/* Both lists scroll on a full server without overlapping the action rows. */
	cl.maxclients = (int)countof(scores);
	for (int i = 3; i < cl.maxclients; i++) SDL_snprintf(scores[i].name, sizeof(scores[i].name), "Player %d", i);
	dimmed_add = 0; M_Translate_Draw(); assert(translatemenu.can_add && !dimmed_add);
	translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(translatemenu.adding && translatemenu.count == 15);
	translatemenu.list.cursor = translatemenu.count + TRANSLATE_CHOOSE_PLAYERS_ROW - 1; M_Translate_Draw();
	assert(translatemenu.list.scroll > 0 && M_Translate_RowY(translatemenu.list.cursor) == 152);
	M_Translate_Key(K_ENTER); M_Translate_Draw(); assert(!translatemenu.adding && tr_watch[17]);
	for (int i = 3; i < cl.maxclients; i++) CL_Translate_MenuWatch(i, scores[i].name, i % 2);
	M_Translate_Draw(); translatemenu.list.cursor = TRANSLATE_PLAYERS_ROW + translatemenu.count - 1; M_Translate_Draw();
	assert(translatemenu.count == 17 && translatemenu.list.scroll > 0 && M_Translate_RowY(translatemenu.list.cursor) == 152);
	/* Bookmark navigation and scrollbar dragging remain read-only. */
	identity = tr_id; M_Translate_Key(K_HOME); M_Translate_Draw();
	assert(translatemenu.list.cursor == TRANSLATE_MESSAGE_ROW && translatemenu.list.scroll == 0);
	M_Translate_Key(K_PGDN); assert(translatemenu.list.cursor == 15);
	M_Translate_Key(K_END); M_Translate_Draw();
	assert(translatemenu.list.cursor == translatemenu.list.numitems - 1 && translatemenu.list.scroll == 6);
	M_Translate_Key(K_UPARROW); assert(M_Translate_PlayerIndex() == translatemenu.count - 1);
	M_Translate_Key(K_DOWNARROW); assert(translatemenu.list.cursor == translatemenu.list.numitems - 1);
	M_Translate_Key(K_HOME); M_Translate_Key('p'); M_Translate_Draw();
	assert(!strcmp(translatemenu.players[M_Translate_PlayerIndex()].display, "Player 3"));
	M_Translate_Key(K_MWHEELDOWN); M_Translate_Draw();
	assert(translatemenu.list.cursor >= translatemenu.list.scroll && translatemenu.list.cursor < translatemenu.list.scroll + 16);
	keydown[K_MOUSE1] = true; m_mousex = 296; m_mousey = 144; M_Translate_Key(K_MOUSE1);
	M_Translate_Draw(); assert(translatemenu.scrollbar_grab && translatemenu.list.scroll == 6);
	M_Translate_Mousemove(296, 32); assert(translatemenu.list.scroll == 0);
	M_Translate_Key(K_ESCAPE); assert(!translatemenu.scrollbar_grab && m_state == m_translate);
	keydown[K_MOUSE1] = false; assert(tr_id == identity);
	ctrlpressed = true; M_Translate_Key('c'); ctrlpressed = false; M_Translate_Draw();
	assert(!translatemenu.count && !tr_watch[1] && !tr_watch[17]);
	for (int i = 3; i < (int)countof(scores); i++) scores[i].name[0] = 0;
	cl.maxclients = 8;

	/* The composer owns its outgoing language and retains failed/canceled drafts. */
	CL_Translate_MenuWriteLanguage(1);
	translatemenu.list.cursor = 0; ctrlpressed = true; M_Translate_Key('t'); ctrlpressed = false;
	assert(M_Translate_TextEntry());
	drawn[0] = 0; dimmed_placeholder = 0; M_Translate_Draw();
	assert(!strstr(drawn, "Back") && dimmed_placeholder == 1);
	M_Translate_Key(K_DOWNARROW); assert(translatemenu.compose_cursor == 2);
	M_Translate_Key(K_DOWNARROW); assert(translatemenu.compose_cursor == 0);
	M_Translate_Key(K_RIGHTARROW); assert(CL_Translate_WriteLanguage() == 2);
	M_Translate_Key(K_RIGHTARROW); assert(CL_Translate_WriteLanguage() == 3);
	M_Translate_Key(K_LEFTARROW); M_Translate_Key(K_LEFTARROW); assert(CL_Translate_WriteLanguage() == 1);
	M_Translate_Key(K_UPARROW); assert(translatemenu.compose_cursor == 2);
	M_Translate_Key(K_TAB); M_Translate_Key(K_TAB); assert(M_Translate_TextEntry());
	identity = tr_id; m_mousey = 168; M_Translate_Key(K_MOUSE1);
	assert(translatemenu.compose && M_Translate_TextEntry() && tr_id == identity);
	for (const char *p = "Hello"; *p; p++) M_Translate_Char(*p);
	M_Translate_Draw(); identity = tr_id;
	assert(!tr_thread && !strcmp(translatemenu.draft, "Hello") && dimmed_placeholder == 1);
	M_Translate_Key(K_ENTER); assert(translatemenu.awaiting_send && tr_id == identity + 1);
	M_Translate_Key(K_ENTER); assert(tr_id == identity + 1); /* no double send */
	CL_Translate_Reset();
	CL_Translate_Console("translate spanish Anyone up for 2v2 on dm2?"); settle(); M_Translate_Draw();
	assert(!strcmp(translatemenu.draft, "Hello") && !translatemenu.awaiting_send);
	assert(!strcmp((char *)sent + 1, "say Alguien para 2v2 en dm2?")); cls.message.cursize = 0;
	translatemenu.compose_cursor = 1; translatemenu.editing = true; M_Translate_Key(K_ENTER);
	settle(); M_Translate_Draw();
	assert(!strcmp((char *)sent + 1, "say Hola") && !translatemenu.draft[0]);
	assert(CL_Translate_SendState(NULL, notice, sizeof(notice)) == TRANSLATE_SEND_SENT);
	cls.message.cursize = 0;
	for (const char *p = "unlisted input"; *p; p++) M_Translate_Char(*p);
	M_Translate_Key(K_ENTER); settle(); M_Translate_Draw();
	assert(!cls.message.cursize && !strcmp(translatemenu.draft, "unlisted input"));
	assert(CL_Translate_SendState(NULL, notice, sizeof(notice)) == TRANSLATE_SEND_FAILED);
	identity = tr_id;
	memset(translatemenu.draft, 'x', 512); translatemenu.draft[512] = 0;
	M_TextField_Init(&translatemenu.field, translatemenu.draft, 512, false);
	translatemenu.field.sel_start = 0; M_Translate_Draw();
	assert(tr_id == identity && M_Translate_FieldViewStart() > 0);
	SDL_strlcpy(translatemenu.draft, "delay", sizeof(translatemenu.draft));
	M_TextField_Init(&translatemenu.field, translatemenu.draft, 512, false);
	M_Translate_Key(K_ENTER); SDL_Delay(50); CL_Translate_IgnoreChanged(); settle(); M_Translate_Draw();
	assert(!cls.message.cursize && !strcmp(translatemenu.draft, "delay") && strstr(translatemenu.notice, "Not sent"));
	M_Translate_Key(K_ESCAPE); assert(!translatemenu.compose);
	M_Translate_Key(K_ESCAPE); assert(m_state == m_console && console_cursor == CONSOLE_TRANSLATE);
	CL_Translate_MenuReadLanguage(1); CL_Translate_Reset();
	assert(CL_Translate_ReadLanguage() == 1 && CL_Translate_WriteLanguage() == 1);
	/* Both actions dim and reject keyboard/mouse input outside multiplayer.
	 * No action is highlighted when every row is unavailable. */
	identity = tr_id;
	for (int i = 0; i < 4; i++)
	{
		cls.state = i == 0 ? ca_disconnected : ca_connected;
		cl.maxclients = i == 1 ? 1 : 8;
		cls.demoplayback = i == 2;
		cls.signon = i == 3 ? SIGNONS - 1 : SIGNONS;
		drawn[0] = 0; dimmed_add = dimmed_compose = 0;
		M_Menu_Translate_f(); M_Translate_Draw();
		assert(!translatemenu.can_add && !translatemenu.can_compose && dimmed_add == 2 && dimmed_compose == 1);
		assert(translatemenu.list.cursor == -1 && strstr(drawn, "a:add"));
		m_mousey = 144; M_Translate_Key(K_MOUSE1);
		M_Translate_Mousemove(100, 160); assert(translatemenu.list.cursor == -1);
		m_mousey = 160; M_Translate_Key(K_MOUSE1);
		translatemenu.list.cursor = TRANSLATE_MESSAGE_ROW; M_Translate_Key(K_ENTER);
		M_Translate_Key(K_UPARROW); M_Translate_Key(K_DOWNARROW); M_Translate_Key(K_TAB);
		CL_Translate_Console("translate spanish Hello");
		assert(!translatemenu.adding && !translatemenu.compose && translatemenu.list.cursor == -1);
		assert(tr_id == identity && !tr_outgoing);
	}
	/* Multiplayer restores the composer; a disconnect between draw and Enter
	 * cannot open a stale enabled action. Existing drafts stay intact. */
	cls.state = ca_connected; cls.demoplayback = false; cls.signon = SIGNONS;
	dimmed_add = dimmed_compose = 0; M_Translate_Draw();
	assert(translatemenu.can_compose && !dimmed_compose);
	translatemenu.list.cursor = TRANSLATE_MESSAGE_ROW; cls.state = ca_disconnected;
	M_Translate_Key(K_ENTER); assert(!translatemenu.compose);
	cls.state = ca_connected; M_Translate_Draw();
	m_mousex = 160; m_mousey = M_Translate_RowY(TRANSLATE_MESSAGE_ROW); M_Translate_Key(K_MOUSE1);
	assert(translatemenu.compose && !strcmp(translatemenu.draft, "delay"));
	M_Translate_Key(K_ESCAPE); assert(!translatemenu.compose);
	M_Translate_Key(K_ESCAPE); assert(m_state == m_console);
	M_Translate_Leave(); CL_Translate_Shutdown();
}

static void menu_card_tests (void)
{
	unsigned int identity;
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset(); CL_Translate_MenuReadLanguage(0);
	assert(CL_Translate_MenuWatch(1, scores[1].name, 1));
	M_Menu_Translate_f(); M_Translate_Draw(); identity = tr_id;
	M_Translate_Key(K_ENTER); drawn[0] = 0; M_Translate_Draw();
	assert(translatemenu.player_edit && translatemenu.player_cursor == 2);
	assert(strstr(drawn, "Player") && strstr(drawn, "Rasta") && strstr(drawn, "Spanish") && strstr(drawn, "Accept Changes"));
	assert(!strstr(drawn, "a:add") && tr_id == identity && !tr_thread);
	/* Cancel discards both draft choices and keeps the current selection. */
	M_Translate_Key(K_UPARROW); M_Translate_Key(K_SPACE);
	M_Translate_Key(K_UPARROW); M_Translate_Key(K_RIGHTARROW);
	assert(translatemenu.edit_language == 2 && translatemenu.edit_remember && tr_watch[1] == 2 && !tr_rule_count);
	M_Translate_Key(K_ESCAPE); assert(!translatemenu.player_edit && tr_watch[1] == 2 && !tr_rule_count);
	ctrlpressed = true; M_Translate_Key('e'); ctrlpressed = false;
	assert(translatemenu.player_edit && translatemenu.edit_language == 1 && !translatemenu.edit_remember);
	/* Mouse editing also remains local until Accept Changes. */
	M_Translate_Mousemove(100, 86); assert(translatemenu.player_cursor == 0);
	m_mousex = 100; m_mousey = 86; M_Translate_Key(K_MOUSE1);
	m_mousex = 150; m_mousey = 114; M_Translate_Key(K_MOUSE1);
	M_Translate_Draw(); assert(tr_watch[1] == 2 && !tr_rule_count && tr_id == identity);
	m_mousex = 50; m_mousey = 138; M_Translate_Key(K_MOUSE1); M_Translate_Draw();
	assert(!translatemenu.player_edit && tr_watch[1] == 3 && tr_rule_count == 1);
	assert(!strcmp(tooltip, "Language: Portuguese"));
	/* Clicking the player opens the card; unchecking Remember keeps the watch
	 * active for this game but removes its saved name rule. */
	m_mousex = 40; m_mousey = M_Translate_RowY(TRANSLATE_PLAYERS_ROW); M_Translate_Key(K_MOUSE1);
	assert(translatemenu.player_edit && translatemenu.edit_remember);
	m_mousex = 150; m_mousey = 114; M_Translate_Key(K_MOUSE1);
	m_mousex = 50; m_mousey = 138; M_Translate_Key(K_MOUSE1);
	assert(!translatemenu.player_edit && tr_watch[1] == 3 && !tr_rule_count);
	/* A slot replacement cannot receive an edit intended for its old player. */
	M_Translate_Key(K_ENTER); assert(translatemenu.player_edit);
	translatemenu.player_cursor = 0; M_Translate_Key(K_RIGHTARROW);
	SDL_strlcpy(scores[1].name, "Replacement", sizeof(scores[1].name)); CL_Translate_PlayerChanged(1, scores[1].name);
	translatemenu.player_cursor = 2; M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(translatemenu.player_edit && !tr_watch[1] && !tr_rule_count && tr_id == identity);
	assert(strstr(translatemenu.notice, "selection changed"));
	M_Translate_Key(K_ESCAPE); assert(!translatemenu.player_edit && m_state == m_translate);
	M_Translate_Leave(); CL_Translate_Shutdown();
}

static void menu_name_tests (void)
{
	const char mixed_name[] = {'M', 'i' | 128, 18, 'X', 0};
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset();
	CL_Translate_MenuReadLanguage(0);
	q_strlcpy(scores[3].name, mixed_name, sizeof(scores[3].name));
	checked_player_name = mixed_name; formatted_name_draws = 0;
	M_Menu_Translate_f(); translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER);
	M_Translate_Draw(); assert(formatted_name_draws == 1);
	M_Translate_Key('m'); assert(translatemenu.players[M_Translate_PlayerIndex()].slot == 3);
	M_Translate_Key(K_ENTER); M_Translate_Draw(); assert(formatted_name_draws == 2 && tr_watch[3] == 1);
	assert(!strcmp(translatemenu.players[0].display, "Mi0X")); /* Matching stays readable. */
	CL_Translate_MenuWatch(3, scores[3].name, -1);
	q_strlcpy(scores[3].name, "badword", sizeof(scores[3].name));
	cl_contentfilter.value = 2; checked_player_name = "*******"; formatted_name_draws = 0;
	M_Menu_Translate_f(); translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER);
	M_Translate_Draw(); assert(formatted_name_draws == 1);
	M_Translate_Key('b'); M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(formatted_name_draws == 2 && tr_watch[3] == 1 && !tr_thread);
	checked_player_name = NULL; cl_contentfilter.value = 0;
	M_Translate_Leave(); CL_Translate_Shutdown();
}

static void saved_name_case_tests (void)
{
	FILE *file;
	unsigned int identity;
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset(); identity = tr_id;
	cls.state = ca_disconnected;
	CL_Translate_Console("translate spanish save @rasta"); TR_LoadRules();
	M_Menu_Translate_f(); M_Translate_Draw();
	world(); CL_Translate_Reset(); CL_Translate_Frame();
	assert(tr_watch[1] == 2 && tr_rule_count == 1 && !strcmp(tr_rules[0].name, "Rasta"));
	M_Translate_Draw(); assert(translatemenu.list.cursor == TRANSLATE_PLAYERS_ROW);
	/* Re-saving another capitalization changes the same rule. */
	cls.state = ca_disconnected; CL_Translate_Reset();
	CL_Translate_Console("translate french save @RASTA"); TR_LoadRules();
	assert(tr_rule_count == 1);
	world(); CL_Translate_Reset(); CL_Translate_Frame();
	assert(tr_watch[1] == 4 && !strcmp(tr_rules[0].name, "Rasta"));
	/* Players differing only by capitalization remain ambiguous. */
	q_strlcpy(scores[3].name, "rasta", sizeof(scores[3].name));
	CL_Translate_Reset(); CL_Translate_Frame(); assert(!tr_watch[1] && !tr_watch[3]);
	scores[3].name[0] = 0;
	/* Previously valid case-variant rules merge; the later entry wins. */
	file = fopen(tr_rules_path, "wb"); assert(file);
	fputs("QSSM_TRANSLATE_RULES 1\nen 7261737461\nes 5261737461\n", file); fclose(file);
	TR_LoadRules(); assert(tr_rule_count == 1 && tr_rules[0].language == 1);
	CL_Translate_Reset(); CL_Translate_Frame(); assert(tr_watch[1] == 2);
	/* Merging must not hide an exact duplicate behind another capitalization. */
	cls.state = ca_disconnected; CL_Translate_Reset();
	file = fopen(tr_rules_path, "wb"); assert(file);
	fputs("QSSM_TRANSLATE_RULES 1\nen 7261737461\nes 5261737461\npt 7261737461\n", file); fclose(file);
	TR_LoadRules(); assert(!tr_rule_count);
	assert(tr_id == identity && !tr_thread && !tr_outgoing);
	M_Translate_Leave(); CL_Translate_MenuStopAll(); CL_Translate_Shutdown();
}

static void saved_name_formatting_tests (void)
{
	const char mixed_name[] = {'M', 'i' | 128, 18, 'X', 0};
	FILE *file;
	char path[MAX_OSPATH];
	unsigned int identity;
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset(); identity = tr_id;
	q_strlcpy(scores[3].name, mixed_name, sizeof(scores[3].name));
	assert(CL_Translate_MenuWatchSaved(3, scores[3].name, 0));
	cls.state = ca_disconnected; CL_Translate_Reset();
	checked_player_name = mixed_name; formatted_name_draws = 0;
	M_Menu_Translate_f(); drawn[0] = 0; M_Translate_Draw();
	assert(translatemenu.count == 1 && !strcmp(translatemenu.players[0].name, "Mi0X"));
	assert(formatted_name_draws == 1 && strstr(drawn, mixed_name));
	/* Offline edits keep styling; disk reload preserves the exact glyph bytes. */
	M_Translate_Key(K_RIGHTARROW); M_Translate_Draw();
	CL_Translate_Shutdown(); CL_Translate_Init(); CL_Translate_Reset();
	formatted_name_draws = 0; M_Menu_Translate_f(); M_Translate_Draw();
	assert(formatted_name_draws == 1 && translatemenu.players[0].language == 1);
	/* Plain console re-saves update the language without discarding real styling. */
	CL_Translate_Console("translate french save @mi0x");
	formatted_name_draws = 0; M_Translate_Draw();
	assert(translatemenu.count == 1 && formatted_name_draws == 1 && translatemenu.players[0].language == 3);
	TR_LoadRules(); formatted_name_draws = 0; M_Translate_Draw();
	assert(formatted_name_draws == 1 && translatemenu.players[0].language == 3);
	M_Translate_Key(K_ENTER); drawn[0] = 0; M_Translate_Draw();
	assert(strstr(drawn, mixed_name)); M_Translate_Key(K_ESCAPE);
	/* Offline console names preserve typed markup, including quoted names. */
	CL_Translate_MenuStopAll(); checked_player_name = "^2EzJack"; formatted_name_draws = 0;
	CL_Translate_Console("translate spanish save @\"^2EzJack\"");
	M_Menu_Translate_f(); M_Translate_Draw();
	assert(formatted_name_draws == 1 && !strcmp(translatemenu.players[0].name, "EzJack"));
	q_strlcpy(path, tr_rules_path, sizeof(path));
	q_snprintf(tr_rules_path, sizeof(tr_rules_path), "%s/missing/rules.txt", host_parms->userdir);
	CL_Translate_Console("translate french save @^3EzJack");
	formatted_name_draws = 0; M_Translate_Draw();
	assert(formatted_name_draws == 1 && translatemenu.players[0].language == 1);
	q_strlcpy(tr_rules_path, path, sizeof(tr_rules_path)); TR_LoadRules();
	formatted_name_draws = 0; M_Translate_Draw(); assert(formatted_name_draws == 1);
	/* Native menu drawing treats font byte 9 as a glyph, not a layout tab. */
	CL_Translate_MenuStopAll(); checked_player_name = "Glyph\tName"; formatted_name_draws = 0;
	CL_Translate_Console("translate save @Glyph\tName"); M_Menu_Translate_f(); M_Translate_Draw();
	assert(translatemenu.count == 1 && formatted_name_draws == 1 && !strcmp(translatemenu.players[0].name, "Glyph Name"));
	TR_LoadRules(); formatted_name_draws = 0; M_Translate_Draw(); assert(formatted_name_draws == 1);
	/* Legacy readable keys load literally and gain styling from a live roster. */
	file = fopen(path, "wb"); assert(file);
	fputs("QSSM_TRANSLATE_RULES 1\nen 4d693058\nen 52615e31737461\n", file); fclose(file);
	TR_LoadRules(); checked_player_name = "Ra^1sta"; formatted_name_draws = 0;
	M_Menu_Translate_f(); M_Translate_Draw(); assert(translatemenu.count == 2 && formatted_name_draws == 1);
	world(); CL_Translate_Reset(); CL_Translate_Frame();
	assert(tr_watch[3] == 1); /* Match Mi0X despite its high-bit colors and special digit. */
	cls.state = ca_disconnected; CL_Translate_Reset();
	CL_Translate_Shutdown(); CL_Translate_Init();
	checked_player_name = mixed_name; formatted_name_draws = 0;
	M_Menu_Translate_f(); M_Translate_Draw(); assert(translatemenu.count == 2 && formatted_name_draws == 1);
	/* Mod-specific &c colors remain valid when loading outside that mod. */
	file = fopen(path, "wb"); assert(file);
	fputs("QSSM_TRANSLATE_RULES 2\nen 4d693058 26636630304d693058\n", file); fclose(file);
	cl.modtype = 0; TR_LoadRules(); checked_player_name = "&cf00Mi0X"; formatted_name_draws = 0;
	M_Menu_Translate_f(); M_Translate_Draw(); assert(translatemenu.count == 1 && formatted_name_draws == 1);
	/* Formatted bytes may not change the readable identity or inject invalid markup. */
	file = fopen(path, "wb"); assert(file);
	fputs("QSSM_TRANSLATE_RULES 2\nen 4d693058 4f74686572\n", file); fclose(file);
	TR_LoadRules(); assert(!tr_rule_count);
	file = fopen(path, "wb"); assert(file);
	fputs("QSSM_TRANSLATE_RULES 2\nen 4d693058 5e5b4d693058\n", file); fclose(file);
	TR_LoadRules(); assert(!tr_rule_count);
	assert(tr_id == identity && !tr_thread && !tr_outgoing);
	checked_player_name = NULL; M_Translate_Leave(); CL_Translate_MenuStopAll(); CL_Translate_Shutdown();
}

static void offline_menu_tests (void)
{
	cl_translate_player_t players[CL_TRANSLATE_MAX_SELECTIONS];
	unsigned int identity;
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset();
	assert(CL_Translate_MenuWatch(1, scores[1].name, 1));
	assert(CL_Translate_MenuRemember(1, scores[1].name, true));
	assert(CL_Translate_MenuWatch(2, scores[2].name, 0)); /* Session-only name must disappear. */
	cls.state = ca_disconnected; CL_Translate_Reset(); identity = tr_id;
	assert(!CL_Translate_MenuPlayers(players, CL_TRANSLATE_MAX_SELECTIONS));
	M_Menu_Translate_f(); drawn[0] = 0; M_Translate_Draw();
	assert(translatemenu.count == 1 && translatemenu.players[0].slot == -1 && translatemenu.players[0].remembered);
	assert(strstr(drawn, "Rasta") && !strstr(drawn, "Other Player") && !strstr(drawn, "not connected"));
	assert(!translatemenu.can_add && !translatemenu.can_compose && !strcmp(tooltip, "Language: Spanish"));
	/* Offline cards edit only local rules; no outgoing message or HTTP work. */
	M_Translate_Key(K_ENTER); assert(translatemenu.player_edit && translatemenu.edit_remember);
	translatemenu.player_cursor = 0; M_Translate_Key(K_RIGHTARROW);
	translatemenu.player_cursor = 2; M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(!translatemenu.player_edit && !strcmp(tooltip, "Language: Portuguese"));
	assert(tr_rule_count == 1 && tr_rules[0].language == 2 && tr_id == identity && !tr_thread && !tr_outgoing);
	/* On reconnection the updated rule takes effect and the menu keeps its
	 * selection, without showing a duplicate saved/connected player row. */
	world(); CL_Translate_Reset(); TR_LoadRules(); CL_Translate_Frame(); M_Translate_Draw();
	assert(tr_watch[1] == 3 && translatemenu.count == 1 && M_Translate_PlayerIndex() == 0 && translatemenu.players[0].slot == 1);
	scores[1].name[0] = 0; CL_Translate_PlayerChanged(1, ""); M_Translate_Draw();
	assert(translatemenu.count == 1 && translatemenu.players[0].slot == -1 && M_Translate_PlayerIndex() == 0);
	cls.state = ca_disconnected; CL_Translate_Reset(); M_Translate_Draw();
	M_Translate_Key(K_RIGHTARROW); M_Translate_Draw(); assert(tr_rules[0].language == 3);
	/* Unchecking Remember removes an offline rule. */
	M_Translate_Key(K_ENTER); translatemenu.player_cursor = 1; M_Translate_Key(K_SPACE);
	translatemenu.player_cursor = 2; M_Translate_Key(K_ENTER); drawn[0] = 0; M_Translate_Draw();
	assert(!translatemenu.count && !tr_rule_count && strstr(drawn, "No saved players") && !strstr(drawn, "not connected"));
	/* Canonical saved names must not be parsed as Quake markup a second time. */
	assert(TR_SaveNamedRule("Ra^1sta", 1, NULL));
	M_Menu_Translate_f(); M_Translate_Draw(); assert(!strcmp(translatemenu.players[0].name, "Ra^1sta"));
	M_Translate_Key(K_ENTER); translatemenu.player_cursor = 0; M_Translate_Key(K_RIGHTARROW);
	translatemenu.player_cursor = 2; M_Translate_Key(K_ENTER);
	assert(TR_ExactRuleName("Ra^1sta") == 0 && tr_rules[0].language == 2);
	M_Translate_Draw(); M_Translate_Key(K_DEL); M_Translate_Draw(); assert(!tr_rule_count && !translatemenu.count);
	/* The whole bounded saved-name registry remains reachable by scrolling. */
	for (int i = 0; i < TR_RULES; i++)
	{
		char name[32]; q_snprintf(name, sizeof(name), "Saved player %d", i);
		assert(TR_UpdateNamedRule(name, i % CL_Translate_LanguageCount(), NULL));
	}
	assert(TR_WriteRules());
	M_Menu_Translate_f(); M_Translate_Draw(); assert(translatemenu.count == TR_RULES);
	M_Translate_Key(K_END); M_Translate_Draw();
	assert(translatemenu.list.scroll > 0 && translatemenu.list.cursor == translatemenu.list.numitems - 1);
	M_Translate_Key(K_ENTER); drawn[0] = 0; M_Translate_Draw();
	assert(!tr_rule_count && !translatemenu.count && tr_id == identity && !tr_thread && !tr_outgoing);
	M_Translate_Leave(); CL_Translate_Shutdown();
}

static void offline_console_save_tests (void)
{
	char path[MAX_OSPATH];
	unsigned int identity;
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset(); identity = tr_id;
	cls.state = ca_disconnected; cls.signon = 0; cl.scores = NULL; cl.maxclients = 0;
	cls.message.cursize = 0;
	/* Direct console saving needs no live roster or network request. */
	argc_stub = 4; argv_stub[1] = "spanish"; argv_stub[2] = "save"; argv_stub[3] = "@Tim";
	command.function(); assert(!tr_rule_count); argc_stub = 1;
	output[0] = 0;
	assert(CL_Translate_Console("translate spanish save @Tim"));
	M_Menu_Translate_f(); drawn[0] = 0; M_Translate_Draw();
	assert(translatemenu.count == 1 && !strcmp(translatemenu.players[0].name, "Tim"));
	assert(translatemenu.players[0].slot == -1 && translatemenu.players[0].language == 1 && translatemenu.players[0].remembered);
	assert(strstr(drawn, "Tim") && !strstr(drawn, "No saved players") && strstr(output, "Saved"));
	assert(tr_id == identity && !tr_thread && !tr_outgoing && !cls.message.cursize);
	/* Restart reloads the rule; repeating a name updates rather than duplicates it. */
	CL_Translate_Shutdown(); CL_Translate_Init(); CL_Translate_Reset();
	M_Menu_Translate_f(); M_Translate_Draw();
	assert(translatemenu.count == 1 && translatemenu.players[0].language == 1);
	/* An explicit new save also authorizes rules after Remove All paused them. */
	CL_Translate_MenuStopAll(); CL_Translate_Console("translate spanish save @Tim");
	CL_Translate_Console("translate portuguese save @Tim"); M_Translate_Draw();
	assert(translatemenu.count == 1 && translatemenu.players[0].language == 2);
	CL_Translate_Console("translate save @\"Other Player\"");
	CL_Translate_Console("translate portuguese save @^1Rasta");
	CL_Translate_Console("translate portuguese save @Ra^^1sta");
	M_Translate_Draw();
	assert(translatemenu.count == 4 && TR_ExactRuleName("Other Player") >= 0 && TR_ExactRuleName("Rasta") >= 0 && TR_ExactRuleName("Ra^1sta") >= 0);
	/* Invalid/oversized names and offline @everyone cannot create rules. */
	CL_Translate_Console("translate save @"); CL_Translate_Console("translate save @\"\"");
	CL_Translate_Console("translate save @^1");
	CL_Translate_Console("translate save @PlayerNameThatIsMuchTooLongForTheScoreboard");
	CL_Translate_Console("translate save @everyone");
	CL_Translate_Console("translate @Tim"); CL_Translate_Console("translate spanish hola");
	assert(tr_rule_count == 4 && tr_id == identity && !tr_thread && !tr_outgoing && !cls.message.cursize);
	/* Storage failure preserves existing rules, including the previous language. */
	q_strlcpy(path, tr_rules_path, sizeof(path));
	q_snprintf(tr_rules_path, sizeof(tr_rules_path), "%s/missing/rules.txt", host_parms->userdir);
	output[0] = 0; CL_Translate_Console("translate french save @Tim");
	CL_Translate_Console("translate save @New Player"); M_Translate_Draw();
	assert(strstr(output, "Could not save") && translatemenu.count == 4 && translatemenu.players[0].language == 2);
	q_strlcpy(tr_rules_path, path, sizeof(tr_rules_path)); TR_LoadRules();
	assert(tr_rule_count == 4 && tr_rules[TR_ExactRuleName("Tim")].language == 2);
	/* Saved full names restore eligible watches once a multiplayer roster exists. */
	world(); CL_Translate_Frame(); M_Translate_Draw();
	assert(tr_watch[1] == 3 && tr_watch[2] == 1 && translatemenu.count == 4 && !tr_watch[0]);
	assert(tr_id == identity && !tr_thread && !tr_outgoing && !cls.message.cursize);
	M_Translate_Leave(); CL_Translate_MenuStopAll(); CL_Translate_Shutdown();
}

static void absent_console_save_tests (void)
{
	const char tim[] = {'T' | 128, 'i', 'm', 0};
	char path[MAX_OSPATH];
	unsigned int identity;
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset(); identity = tr_id; cls.message.cursize = 0;
	/* An absent name can be saved while connected without watching anyone else. */
	CL_Translate_Console("translate spanish save @tim");
	M_Menu_Translate_f(); M_Translate_Draw();
	assert(translatemenu.count == 1 && translatemenu.players[0].slot == -1 && translatemenu.players[0].language == 1);
	CL_Translate_Console("translate french save @TIM");
	CL_Translate_Console("translate portuguese save @\"Absent Player\"");
	TR_LoadRules(); M_Translate_Draw();
	assert(translatemenu.count == 2 && tr_rules[TR_ExactRuleName("Tim")].language == 3);
	assert(!tr_watch[0] && !tr_watch[1] && !tr_watch[2] && tr_id == identity && !tr_thread && !tr_outgoing && !cls.message.cursize);
	/* The later roster supplies the actual capitalization and glyphs. */
	q_strlcpy(scores[3].name, tim, sizeof(scores[3].name)); CL_Translate_PlayerChanged(3, scores[3].name);
	CL_Translate_Frame(); M_Translate_Draw();
	assert(tr_watch[3] == 4 && !strcmp(tr_rules[TR_ExactRuleName("Tim")].name, "Tim") && translatemenu.count == 2);
	/* Re-saving that absent name retains its styling, even on a live server. */
	scores[3].name[0] = 0; CL_Translate_PlayerChanged(3, ""); CL_Translate_Frame();
	CL_Translate_Console("translate spanish save @tim"); TR_LoadRules();
	checked_player_name = tim; formatted_name_draws = 0; M_Translate_Draw();
	assert(formatted_name_draws == 1 && !tr_watch[3] && tr_rules[TR_ExactRuleName("Tim")].language == 1);
	q_strlcpy(path, tr_rules_path, sizeof(path));
	q_snprintf(tr_rules_path, sizeof(tr_rules_path), "%s/missing/rules.txt", host_parms->userdir);
	output[0] = 0; CL_Translate_Console("translate german save @tim");
	CL_Translate_Console("translate save @Another Missing Player");
	assert(strstr(output, "Could not save") && tr_rule_count == 2 && tr_rules[TR_ExactRuleName("Tim")].language == 1);
	q_strlcpy(tr_rules_path, path, sizeof(tr_rules_path));
	assert(tr_id == identity && !tr_thread && !tr_outgoing && !cls.message.cursize);
	checked_player_name = NULL; M_Translate_Leave(); CL_Translate_MenuStopAll(); CL_Translate_Shutdown();
}

static void console_save_tests (void)
{
	unsigned int identity;
	int count;
	char path[MAX_OSPATH];
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset(); identity = tr_id;
	assert(CL_Translate_Console("translate save @Rasta"));
	assert(tr_watch[1] == 1 && tr_rule_count == 1 && tr_rules[0].language == 0 && !tr_thread);
	count = (int)strlen(output);
	CL_Translate_Console("translate save @Rasta"); assert(tr_id == identity && (int)strlen(output) == count);
	CL_Translate_Console("translate spanish save @\"Other Player\"");
	assert(tr_watch[2] == 2 && tr_rule_count == 2 && tr_rules[TR_RuleForName(scores[2].name)].language == 1);
	CL_Translate_Reset(); TR_LoadRules(); CL_Translate_Frame();
	assert(tr_watch[1] == 1 && tr_watch[2] == 2 && !tr_thread);
	/* A queued command/script cannot create persistent upload authorization. */
	CL_Translate_MenuStopAll(); argc_stub = 3; argv_stub[1] = "save"; argv_stub[2] = "@Rasta";
	command.function(); assert(!tr_rule_count && !tr_watch[1] && !tr_thread);
	/* Invalid save syntax must never turn into outgoing chat. */
	CL_Translate_Console("translate save hello"); CL_Translate_Console("translate english save");
	CL_Translate_Console("translate save @Me"); assert(!tr_rule_count && !tr_watch[0] && !tr_outgoing && tr_id == identity);
	ignored = true; CL_Translate_Console("translate save @Rasta"); assert(!tr_rule_count && !tr_watch[1]);
	ignored = false; SDL_strlcpy(scores[3].name, "Rasta", sizeof(scores[3].name));
	CL_Translate_Console("translate save @Ra"); assert(!tr_rule_count && !tr_watch[1]); scores[3].name[0] = 0;
	/* Saving fails before starting the watch when storage is unavailable. */
	q_strlcpy(path, tr_rules_path, sizeof(path));
	q_snprintf(tr_rules_path, sizeof(tr_rules_path), "%s/missing/rules.txt", host_parms->userdir);
	CL_Translate_Console("translate save @Rasta"); assert(!tr_rule_count && !tr_watch[1] && !tr_thread);
	q_strlcpy(tr_rules_path, path, sizeof(tr_rules_path));
	/* Console @everyone saves all current eligible names, including watches
	 * already selected; later arrivals are not a wildcard match. */
	CL_Translate_Console("translate english @Rasta"); CL_Translate_Console("translate portuguese save @everyone");
	assert(tr_rule_count == 2 && tr_watch[1] == 3 && tr_watch[2] == 3);
	SDL_strlcpy(scores[3].name, "Late Arrival", sizeof(scores[3].name));
	CL_Translate_Reset(); TR_LoadRules(); CL_Translate_Frame();
	assert(tr_watch[1] == 3 && tr_watch[2] == 3 && !tr_watch[3] && tr_id == identity && !tr_thread);
	/* Tab offers the save keyword, then the same eligible names and full
	 * multiword matches as ordinary watches, without changing any settings. */
	argc_stub = 2; argv_stub[1] = "sa"; suggestions[0] = 0; command.completion("sa"); assert(strstr(suggestions, "save\n"));
	argc_stub = 3; argv_stub[1] = "spanish"; argv_stub[2] = "sa";
	suggestions[0] = 0; command.completion("sa"); assert(!strcmp(suggestions, "save\n"));
	argc_stub = 3; argv_stub[1] = "save"; argv_stub[2] = "@";
	suggestions[0] = 0; command.completion("@"); assert(strstr(suggestions, "@everyone\n") && strstr(suggestions, "@Rasta\n"));
	argc_stub = 5; argv_stub[1] = "spanish"; argv_stub[2] = "save"; argv_stub[3] = "@Other"; argv_stub[4] = "Pl";
	suggestions[0] = 0; command.completion("@Other Pl"); assert(!strcmp(suggestions, "@Other Player\n"));
	assert(tr_rule_count == 2 && tr_id == identity && !tr_thread);
	argc_stub = 1; CL_Translate_MenuStopAll(); CL_Translate_Shutdown();
}

static void persistence_tests (void)
{
	cl_translate_player_t players[MAX_SCOREBOARD];
	char path[MAX_OSPATH];
	FILE *file;
	unsigned int identity;
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cls.demoplayback = false;
	CL_Translate_Init(); CL_Translate_Reset();
	assert(tr_rule_count == 0 && !tr_thread);
	/* The default remains a session-only selection. */
	assert(CL_Translate_MenuWatch(1, scores[1].name, 1));
	CL_Translate_Reset(); CL_Translate_Frame(); assert(!tr_watch[1] && !tr_rule_count);
	M_Menu_Translate_f(); translatemenu.list.cursor = TRANSLATE_ADD_ROW; M_Translate_Key(K_ENTER);
	drawn[0] = 0; M_Translate_Draw();
	assert(strstr(drawn, "Remember by name: No") && !translatemenu.remember);
	identity = tr_id;
	translatemenu.list.cursor = 1; M_Translate_Key(K_RIGHTARROW); M_Translate_Draw();
	assert(translatemenu.remember && !tr_rule_count && tr_id == identity && !tr_thread);
	CL_Translate_MenuReadLanguage(1);
	translatemenu.list.cursor = TRANSLATE_CHOOSE_PLAYERS_ROW; M_Translate_Key(K_ENTER); M_Translate_Draw();
	assert(!translatemenu.adding && tr_watch[1] == 2 && tr_rule_count == 1);
	assert(CL_Translate_MenuPlayers(players, MAX_SCOREBOARD) == 2 && players[0].remembered);
	q_strlcpy(path, tr_rules_path, sizeof(path));
	file = fopen(path, "rb"); assert(file); fclose(file);
	/* Editing a saved player's language updates disk as well as this game. */
	M_Translate_Key(K_RIGHTARROW); assert(tr_watch[1] == 3 && tr_rules[0].language == 2);
	CL_Translate_Shutdown(); memset(tr_rules, 0, sizeof(tr_rules)); tr_rule_count = 0;
	CL_Translate_Init(); CL_Translate_Reset(); CL_Translate_Frame();
	assert(tr_rule_count == 1 && tr_watch[1] == 3 && !tr_thread && tr_id == identity);
	/* Full names survive capitalization, slot changes and color changes. */
	SDL_strlcpy(scores[1].name, "Raster", sizeof(scores[1].name)); CL_Translate_PlayerChanged(1, scores[1].name);
	CL_Translate_Frame(); assert(!tr_watch[1]);
	SDL_strlcpy(scores[3].name, "^1Rasta", sizeof(scores[3].name)); CL_Translate_PlayerChanged(3, scores[3].name);
	CL_Translate_Frame(); assert(tr_watch[3] == 3 && !tr_watch[1]);
	SDL_strlcpy(scores[3].name, "rasta", sizeof(scores[3].name)); CL_Translate_PlayerChanged(3, scores[3].name);
	CL_Translate_Frame(); assert(tr_watch[3] == 3);
	SDL_strlcpy(scores[1].name, "Rasta", sizeof(scores[1].name));
	SDL_strlcpy(scores[3].name, "Rasta", sizeof(scores[3].name));
	CL_Translate_Reset(); CL_Translate_Frame(); assert(!tr_watch[1] && !tr_watch[3]);
	scores[3].name[0] = 0; ignored = true; CL_Translate_IgnoreChanged(); CL_Translate_Frame(); assert(!tr_watch[1]);
	ignored = false; CL_Translate_IgnoreChanged(); CL_Translate_Frame(); assert(tr_watch[1] == 3);
	CL_Translate_Reset(); cls.demoplayback = true; CL_Translate_Frame(); assert(!tr_watch[1]);
	cls.demoplayback = false; cl.maxclients = 1; CL_Translate_Frame(); assert(!tr_watch[1]);
	cl.maxclients = 8; cls.signon = SIGNONS - 1; CL_Translate_Frame(); assert(!tr_watch[1]);
	cls.signon = SIGNONS; CL_Translate_Frame(); assert(tr_watch[1] == 3);
	/* Remove All forgets saved rules as well as the current selections. */
	CL_Translate_MenuStopAll(); CL_Translate_Frame(); assert(!tr_watch[1] && !tr_rule_count);
	CL_Translate_MapChanged(); CL_Translate_Frame(); assert(!tr_watch[1]);
	CL_Translate_Reset(); CL_Translate_Frame(); assert(!tr_watch[1]);
	assert(CL_Translate_MenuWatch(1, scores[1].name, 2));
	assert(CL_Translate_MenuRemember(1, scores[1].name, true));
	/* Removing a player forgets the rule and cannot reactivate after restart. */
	assert(CL_Translate_MenuWatch(1, scores[1].name, -1));
	CL_Translate_Reset(); TR_LoadRules(); CL_Translate_Frame(); assert(!tr_watch[1] && !tr_rule_count);
	/* Everyone remembers only the newly selected names, not a wildcard or an
	 * already selected player's independently chosen language. */
	assert(CL_Translate_MenuWatch(2, scores[2].name, 3));
	SDL_strlcpy(scores[3].name, "Third Player", sizeof(scores[3].name));
	assert(CL_Translate_MenuWatchEveryoneRemembered(1));
	assert(tr_rule_count == 2 && tr_watch[1] == 2 && tr_watch[3] == 2 && tr_watch[2] == 4);
	SDL_strlcpy(scores[4].name, "Late Arrival", sizeof(scores[4].name));
	CL_Translate_PlayerChanged(4, scores[4].name); CL_Translate_Frame(); assert(!tr_watch[4]);
	CL_Translate_Reset(); TR_LoadRules(); CL_Translate_Frame();
	assert(tr_watch[1] == 2 && tr_watch[3] == 2 && !tr_watch[2] && !tr_watch[4]);
	/* A failed save preserves the old rule and language in memory and on disk. */
	q_snprintf(tr_rules_path, sizeof(tr_rules_path), "%s/missing/rules.txt", host_parms->userdir);
	assert(!CL_Translate_MenuEditPlayer(1, scores[1].name, 4, false)); assert(tr_watch[1] == 2 && tr_rule_count == 2);
	assert(!CL_Translate_MenuWatch(1, scores[1].name, 4)); assert(tr_watch[1] == 2);
	assert(!CL_Translate_MenuWatch(1, scores[1].name, -1)); assert(tr_watch[1] == 2);
	q_strlcpy(tr_rules_path, path, sizeof(tr_rules_path)); TR_LoadRules(); assert(tr_rule_count == 2 && tr_rules[0].language == 1);
	assert(CL_Translate_MenuWatch(1, scores[1].name, -1));
	assert(CL_Translate_MenuWatch(3, scores[3].name, -1)); assert(!tr_rule_count);
	/* Malformed or injected preference data enables nothing; it is never exec'd. */
	file = fopen(path, "wb"); assert(file);
	fputs("QSSM_TRANSLATE_RULES 1\nen 5261737461\nes 00\n", file); fclose(file);
	CL_Translate_Reset(); TR_LoadRules(); CL_Translate_Frame(); assert(!tr_rule_count && !tr_watch[1]);
	file = fopen(path, "wb"); assert(file);
	fputs("translate @everyone\n", file); fclose(file);
	TR_LoadRules(); CL_Translate_Frame(); assert(!tr_rule_count && !tr_watch[1] && tr_id == identity);
	assert(TR_WriteRules());
	M_Translate_Leave(); CL_Translate_Shutdown();
}

int main (void)
{
	quakeparms_t parameters = {0};
	parameters.userdir = getenv("QSSM_TRANSLATE_TEST_DIR");
	assert(parameters.userdir && *parameters.userdir);
	host_parms = &parameters;
	assert(SDL_Init(0)); main_thread = SDL_GetCurrentThreadID();
	assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
	world(); CL_Translate_Init();
	assert(Cmd_FindCommand("translate") && command.completion);
	unit_tests(); everyone_tests(); integration_tests(); menu_tests(); menu_card_tests(); menu_name_tests(); saved_name_case_tests(); saved_name_formatting_tests(); offline_menu_tests(); offline_console_save_tests(); absent_console_save_tests(); console_save_tests(); persistence_tests();
	curl_global_cleanup(); SDL_Quit();
	puts("Translation client and menu: opt-in, identity, navigation, drafts, send and lifecycle checks passed.");
	return 0;
}
