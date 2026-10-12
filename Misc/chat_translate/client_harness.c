/* Headless tests of the production module, real SDL worker and libcurl.
 * Only unrelated engine presentation/registry/network-buffer APIs are stubbed. */
#include "../../Quake/cl_translate.c"
#include <assert.h>

client_static_t cls;
client_state_t cl;
cvar_t cl_contentfilter = {"cl_contentfilter", "0"};
char dequake[256];
static scoreboard_t scores[8];
static byte sent[4096];
static char output[65536], suggestions[2048];
static cmd_function_t command;
static int argc_stub = 1;
static const char *argv_stub[8] = {"translate"};
static SDL_ThreadID main_thread;
static qboolean ignored;
static cvar_t *registered_preferences[2];
static char preference_values[2][32];
static int registered_count;

void Cvar_RegisterVariable (cvar_t *var)
{
	for (int i = 0; i < registered_count; i++) if (registered_preferences[i] == var) return;
	assert(registered_count < 2);
	registered_preferences[registered_count++] = var;
}
void Cvar_SetCallback (cvar_t *var, cvarcallback_t callback) { var->callback = callback; }
void Cvar_Set (const char *name, const char *value)
{
	for (int i = 0; i < registered_count; i++) if (!strcmp(registered_preferences[i]->name, name))
	{
		SDL_strlcpy(preference_values[i], value, sizeof(preference_values[i]));
		registered_preferences[i]->string = preference_values[i];
		if (registered_preferences[i]->callback) registered_preferences[i]->callback(registered_preferences[i]);
		return;
	}
	assert(!"unknown preference");
}

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
	return &command;
}
int Cmd_Argc (void) { return argc_stub; }
const char *Cmd_Argv (int index) { return index < argc_stub ? argv_stub[index] : ""; }
void Con_AddToTabList (const char *name, const char *partial, const char *type, const char *param)
{
	(void)type; (void)param;
	if (SDL_strcasestr(name, partial)) { SDL_strlcat(suggestions, name, sizeof(suggestions)); SDL_strlcat(suggestions, "\n", sizeof(suggestions)); }
}
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
int m_mousex, m_mousey;
static int console_cursor;
#define CONSOLE_TRANSLATE 7
static qboolean textfield_mouse_dragging;
static menu_textfield_t *textfield_drag_field;
static char drawn[65536];
void M_Menu_Console_f (void) { m_state = m_console; }
void S_LocalSound (const char *name) { (void)name; }
void M_Print (int x, int y, const char *text)
{
	assert(x >= 0 && y >= 0 && y < 200 && x + (int)strlen(text) * 8 <= 320);
	SDL_strlcat(drawn, text, sizeof(drawn)); SDL_strlcat(drawn, "\n", sizeof(drawn));
}
void M_PrintWhite (int x, int y, const char *text) { M_Print(x, y, text); }
void M_DrawCharacter (int x, int y, int value) { (void)value; assert(x >= 0 && x < 320 && y >= 0 && y < 200); }
void M_DrawCheckbox (int x, int y, int value) { M_DrawCharacter(x, y, value); }
void M_DrawTextBox (int x, int y, int width, int lines)
{ assert(x >= 0 && x + (width + 2) * 8 <= 320 && y >= 0 && y + (lines + 2) * 8 <= 200); }
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
	/* Help is read-only, available offline, and does not consume the disclosure. */
	cls.state = ca_disconnected; output[0] = 0;
	assert(CL_Translate_Console("translate help"));
	assert(strstr(output, "OpenRouter and its model host") && strstr(output, "up to 3 recent messages"));
	assert(strstr(output, "Outgoing requests contain only the text you enter"));
	assert(!tr_disclosed && !tr_thread && !tr_outgoing);
	argc_stub = 2; argv_stub[1] = "help"; output[0] = 0; command.function();
	assert(strstr(output, "translate @Rasta") && !tr_disclosed && !tr_thread);
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
	/* The shorthand always selects English, including with a saved Spanish
	 * preference. Names are not parsed as language tokens or outgoing text. */
	Cvar_Set("cl_translate_read_language", "es");
	CL_Translate_Console(" \ttranslate\t@Ra");
	assert(tr_watch[1] == 1 && tr_read_language == 1 && !strcmp(cl_translate_read_language.string, "es") && !tr_thread);
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
	Cvar_Set("cl_translate_read_language", "en");
	argc_stub = 3; argv_stub[1] = "english"; argv_stub[2] = "@Rasta";
	command.function(); /* queued alias/server/script path is deliberately inert */
	assert(!tr_watch[1] && !tr_thread);
	suggestions[0] = 0;
	command.completion("@Other Pl");
	assert(strstr(suggestions, "@Other Player"));
	argc_stub = 2; argv_stub[1] = "@"; suggestions[0] = 0;
	command.function(); assert(!tr_watch[1] && !tr_thread);
	command.completion("@"); assert(!strcmp(suggestions, "@Rasta\n@Other Player\n") && !tr_thread);
	ignored = true; suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@Other Player\n"));
	argc_stub = 3; argv_stub[1] = "english"; argv_stub[2] = "@"; suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@Other Player\n"));
	argv_stub[1] = "off"; suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@Other Player\n"));
	ignored = false; SDL_strlcpy(scores[3].name, "Rasta", sizeof(scores[3].name)); suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@Other Player\n")); /* duplicate names excluded */
	SDL_strlcpy(scores[3].name, "(Rasta)", sizeof(scores[3].name)); suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@Rasta\n@Other Player\n")); /* team/public collision excluded */
	SDL_strlcpy(scores[3].name, "^[unreadable^]", sizeof(scores[3].name)); suggestions[0] = 0;
	command.completion("@"); assert(!strcmp(suggestions, "@Rasta\n@Other Player\n"));
	scores[3].name[0] = 0;
	assert(!tr_thread && !tr_watch[1] && !tr_outgoing);
	argc_stub = 3; argv_stub[1] = "@Other"; argv_stub[2] = "Pl"; suggestions[0] = 0;
	command.completion("@Other Pl"); assert(!strcmp(suggestions, "@Other Player\n"));
	argc_stub = 2; argv_stub[1] = ""; suggestions[0] = 0;
	command.completion(""); assert(strstr(suggestions, "english") && strstr(suggestions, "spanish") && strstr(suggestions, "off") && strstr(suggestions, "help") && !strstr(suggestions, "@"));
	argc_stub = 3; argv_stub[1] = "spanish"; argv_stub[2] = "message"; suggestions[0] = 0;
	command.completion("Other"); assert(!suggestions[0]);
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
	assert(occurrences(output, "Translation sends selected chat") == 1);
	assert(strstr(output, "recent context") && strstr(output, "messages you ask to send") && strstr(output, "its model host"));
	identity = tr_id; generation = SDL_GetAtomicInt(&tr_generation);
	acknowledgment_size = strlen(output);
	CL_Translate_Console("translate @Ra"); CL_Translate_Console("translate english @Rasta");
	assert(tr_watch[1] == 1 && tr_id == identity && SDL_GetAtomicInt(&tr_generation) == generation);
	assert(strlen(output) == acknowledgment_size); /* repeat commands are quiet */
	settle();
	assert(strstr(output, "[EN earlier] Rasta: Hello"));
	assert(strstr(output, "[EN earlier] Rasta: Shall we change maps?"));
	assert(strstr(output, "[EN earlier] Rasta: Sure"));
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
	assert(strstr(output, "[EN] Rasta: Hello") && !strstr(output, "earlier"));
	/* English stays original-only; the sender prefix is never model-generated. */
	output[0] = 0; observe("\001Rasta: nice shot\n"); settle();
	assert(!strstr(output, "[EN]"));
	identity = tr_id; observe("\001Rasta: gg\n"); settle(); assert(tr_id == identity);
	observe("\001Me: hola\n"); settle(); assert(tr_id == identity);

	CL_Translate_Console("translate spanish Anyone up for 2v2 on dm2?");
	assert(!output[0] && tr_outgoing); /* progress is in the menu, no repeated disclosure */
	settle();
	assert(cls.message.cursize > 1 && sent[0] == clc_stringcmd);
	assert(!strcmp((char *)sent + 1, "say Alguien para 2v2 en dm2?"));
	assert(!strcmp(output, "[Sent ES] Public chat sent.\n"));
	identity = tr_id;
	/* Normal print handling displays the server echo; observation adds no copy
	 * and never re-translates our own message. */
	Con_Printf("%s", "\001Me: Alguien para 2v2 en dm2?\n");
	observe("\001Me: Alguien para 2v2 en dm2?\n"); settle();
	assert(occurrences(output, "Alguien para 2v2 en dm2?") == 1 && tr_id == identity);
	assert(occurrences(output, "[Sent ES]") == 1);
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
	assert(tr_disclosed && !strstr(output, "Translation sends selected chat"));

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
	assert(strstr(output, "[EN earlier] Replacement: Hello") && strstr(output, "[EN earlier] Replacement: Sure"));
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
	output[0] = 0; assert(!tr_disclosed);
	CL_Translate_Console("translate spanish delay");
	assert(occurrences(output, "Translation sends selected chat") == 1);
	/* Outgoing-first use shares the same disclosure with a subsequent watch. */
	CL_Translate_Console("translate @Replacement");
	assert(occurrences(output, "Translation sends selected chat") == 1 && tr_outgoing);
	SDL_Delay(50);
	Uint64 before = SDL_GetTicks(); CL_Translate_Shutdown();
	assert(SDL_GetTicks() - before < 1000 && !cls.message.cursize);
}

static void menu_tests (void)
{
	cl_translate_player_t players[MAX_SCOREBOARD];
	unsigned int identity;
	char notice[160];
	memset(scores, 0, sizeof(scores)); world(); ignored = false; cls.demoplayback = false;
	cls.message.cursize = 0; cl_contentfilter.value = 0;
	CL_Translate_Init(); CL_Translate_Reset(); tr_backoff_until = 0;
	Cvar_Set("cl_translate_read_language", "en"); Cvar_Set("cl_translate_write_language", "es");
	M_Menu_Translate_f(); M_Translate_Draw();
	assert(!tr_thread && !tr_watch[1] && !tr_watch[2]);
	assert(translatemenu.count == 2 && strstr(drawn, "Chat Translation"));
	assert(CL_Translate_MenuPlayers(players, MAX_SCOREBOARD) == 2 && players[0].slot == 1);
	M_Translate_Mousemove(100,72); M_Translate_Key(K_RIGHTARROW); M_Translate_Draw();
	assert(!tr_thread && !tr_watch[1]); /* hover/arrows on a player never opt in */
	Cvar_Set("cl_translate_read_language", "es"); Cvar_Set("cl_translate_write_language", "en");
	assert(!tr_thread && !tr_watch[1] && CL_Translate_ReadLanguage() == 1);
	Cvar_Set("cl_translate_read_language", "unsupported");
	assert(CL_Translate_ReadLanguage() == 1 && !strcmp(cl_translate_read_language.string, "es"));
	Cvar_Set("cl_translate_read_language", "en");
	M_Translate_Key(K_ENTER); assert(tr_watch[1] == 1 && !tr_thread);
	M_Translate_Draw(); assert(translatemenu.players[0].language == 0);
	CL_Translate_Console("translate spanish @Other Player"); M_Translate_Draw();
	assert(CL_Translate_MixedLanguages());
	translatemenu.cursor = 0; M_Translate_Key(K_RIGHTARROW); M_Translate_Draw();
	assert(tr_watch[1] == 2 && tr_watch[2] == 2 && !CL_Translate_MixedLanguages());
	CL_Translate_Console("translate off @Rasta"); M_Translate_Draw();
	assert(translatemenu.players[0].language == -1);
	CL_Translate_Console("translate @Rasta");
	translatemenu.cursor = 1; translatemenu.notice_until = 0; drawn[0] = 0; M_Translate_Draw();
	assert(translatemenu.players[0].language == 0 && CL_Translate_ReadLanguage() == 1);
	assert(strstr(drawn, "Console: translate @Rasta"));
	translatemenu.cursor = 2; drawn[0] = 0; M_Translate_Draw();
	assert(strstr(drawn, "Console: translate es @Other Player"));
	CL_Translate_Console("translate off @Rasta"); M_Translate_Draw();
	ignored = true; M_Translate_Draw(); translatemenu.cursor = 1; M_Translate_Key(K_ENTER);
	assert(!tr_watch[1] && !tr_thread); ignored = false;
	M_Translate_Draw(); translatemenu.cursor = 1;
	SDL_strlcpy(scores[1].name, "Replacement", sizeof(scores[1].name));
	M_Translate_Key(K_ENTER); assert(!tr_watch[1]); /* stale cached identity refused */
	SDL_strlcpy(scores[1].name, "Rasta", sizeof(scores[1].name)); M_Translate_Draw();
	SDL_strlcpy(scores[3].name, "Rasta", sizeof(scores[3].name)); M_Translate_Draw();
	assert(!translatemenu.players[0].selectable); scores[3].name[0] = 0;
	SDL_strlcpy(scores[3].name, "^[unreadable^]", sizeof(scores[3].name)); M_Translate_Draw();
	assert(!translatemenu.players[2].selectable && !strcmp(translatemenu.players[2].reason, "name"));
	scores[3].name[0] = 0;
	for (int i = 3; i < 8; i++) SDL_snprintf(scores[i].name, sizeof(scores[i].name), "Player %d", i);
	M_Translate_Draw(); translatemenu.cursor = translatemenu.count; M_Translate_Draw();
	assert(translatemenu.scroll > 0 && M_Translate_RowY(translatemenu.cursor) == 112);
	for (int i = 3; i < 8; i++) scores[i].name[0] = 0;
	CL_Translate_MenuStopAll(); M_Translate_Draw();
	Cvar_Set("cl_translate_write_language", "es");
	translatemenu.cursor = translatemenu.count + 3; M_Translate_Key(K_ENTER);
	assert(M_Translate_TextEntry());
	for (const char *p = "Hello"; *p; p++) M_Translate_Char(*p);
	M_Translate_Draw(); identity = tr_id;
	assert(!tr_thread && !strcmp(translatemenu.draft, "Hello"));
	M_Translate_Key(K_ENTER); assert(translatemenu.awaiting_send && tr_id == identity + 1);
	M_Translate_Key(K_ENTER); assert(tr_id == identity + 1); /* no double send */
	/* A reconnect while this menu is hidden must not let a different console
	 * send reuse its request ID and clear this canceled menu draft. */
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
	CL_Translate_MenuWatch(1, scores[1].name, 0);
	CL_Translate_MenuWriteLanguage(1); assert(tr_watch[1] == 1);
	CL_Translate_Reset(); cls.state = ca_disconnected; M_Menu_Translate_f(); M_Translate_Draw();
	assert(!translatemenu.count && strstr(drawn, "Join a multiplayer game first"));
	M_Translate_Leave();
	CL_Translate_Shutdown();
}

int main (void)
{
	assert(SDL_Init(0)); main_thread = SDL_GetCurrentThreadID();
	assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
	world(); CL_Translate_Init(); unit_tests(); integration_tests(); menu_tests();
	curl_global_cleanup(); SDL_Quit();
	puts("Translation client and menu: opt-in, identity, navigation, drafts, send and lifecycle checks passed.");
	return 0;
}
