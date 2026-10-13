/*
 * QSS-M optional chat translation (GPL-2.0-or-later).
 * Engine state, text conversion and display belong to the main thread.
 * The worker sees only copied jobs, bounded queues and atomic cancellation.
 * There is deliberately no cvar or executable setter that authorizes uploads.
 */
#include "quakedef.h"
#include "cl_translate.h"
#include "net_curl.h"
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif

extern qboolean WordFilter_Check (const char *text, char *dest, size_t size);
extern qboolean ctrlpressed; /* Existing Ctrl/Command chat modifier. */

#define TR_TEXT 512
#define TR_REQUEST 4096
#define TR_RESPONSE 8192
#define TR_HISTORY 6
#define TR_QUEUE 5
#define TR_COMPLETIONS 8
#define TR_LIFETIME 5000
#define TR_RECENT_AGE 60000
#define TR_CONTEXT_AGE 20000
#define TR_RULES CL_TRANSLATE_MAX_RULES

static const char *tr_endpoint = "https://timbergeron.com/qssm/translate";
static const char *tr_codes[] = {
	"en", "es", "pt", "fr", "de", "it", "nl", "pl", "sv", "no", "da",
	"fi", "cs", "ro", "hu", "tr", "id", "ms", "vi", "fil", "hr", "sk"
};
static const char *tr_languages[] = {
	"English", "Spanish", "Portuguese", "French", "German", "Italian", "Dutch",
	"Polish", "Swedish", "Norwegian", "Danish", "Finnish", "Czech", "Romanian",
	"Hungarian", "Turkish", "Indonesian", "Malay", "Vietnamese", "Filipino",
	"Croatian", "Slovak"
};
/* Each history entry has one handled bit per language. */
typedef char tr_language_table_check[(countof(tr_codes) == countof(tr_languages) && countof(tr_codes) <= 32) ? 1 : -1];
/* Menu language choices last for this process and never start translation. */
static int tr_read_language, tr_write_language = 1;
static char tr_menu_notice[160], tr_send_notice[160];
static cl_translate_send_state_t tr_send_state;
static unsigned int tr_send_id;

static int TR_Language (const char *text)
{
	int i;
	for (i = 0; i < (int)countof(tr_codes); i++)
		if (!q_strcasecmp(text, tr_codes[i]) || !q_strcasecmp(text, tr_languages[i])) return i;
	return -1;
}

static void TR_LanguageLabel (char label[4], int language)
{
	int i;
	q_strlcpy(label, tr_codes[language], 4);
	for (i = 0; label[i]; i++) label[i] = (char)toupper((unsigned char)label[i]);
}

typedef struct
{
	uint32_t serial;
	Uint64 time;
	unsigned int handled;
	char text[TR_TEXT + 1];
} tr_history_t;

typedef struct
{
	uint32_t id, serial;
	int generation, slot, language, priority;
	qboolean earlier, outgoing, team;
	Uint64 deadline;
	char name[MAX_SCOREBOARDNAME];
	char original[TR_TEXT + 1];
	char request[TR_REQUEST + 1];
} tr_job_t;

typedef struct
{
	tr_job_t job;
	CURLcode result;
	long http;
	unsigned int retry_after;
	Uint64 elapsed;
	size_t bytes;
	char response[TR_RESPONSE + 1];
} tr_completion_t;

static tr_history_t tr_history[MAX_SCOREBOARD][TR_HISTORY];
static int tr_history_count[MAX_SCOREBOARD];
static char tr_names[MAX_SCOREBOARD][MAX_SCOREBOARDNAME];
static int tr_watch[MAX_SCOREBOARD]; /* zero = off, language index + 1 otherwise */
typedef struct
{
	char name[MAX_SCOREBOARDNAME + 1]; /* Exact readable name, without colors. */
	int language;
} tr_rule_t;
static tr_rule_t tr_rules[TR_RULES];
static int tr_rule_count;
static char tr_rules_path[MAX_OSPATH];
static qboolean tr_rules_dirty, tr_rules_paused;
static void TR_ApplyRules (void);
static uint32_t tr_serial, tr_id;
static SDL_Mutex *tr_mutex;
static SDL_Condition *tr_condition;
static SDL_Thread *tr_thread;
static SDL_AtomicInt tr_generation, tr_stopping;
static tr_job_t tr_pending[TR_QUEUE];
static tr_completion_t tr_completed[TR_COMPLETIONS];
static int tr_pending_count, tr_completed_count;
static unsigned int tr_completion_drops; /* mutex protected */
static qboolean tr_busy, tr_initialized, tr_outgoing;
static qboolean tr_worker_failed;
static int tr_filter, tr_failures, tr_backoff_step;
static Uint64 tr_backoff_until, tr_last_notice, tr_last_latency;
static unsigned int tr_translated, tr_unchanged, tr_skipped, tr_dropped, tr_errors;

static qboolean TR_Live (void)
{
	return cls.state == ca_connected && !cls.demoplayback && cls.signon == SIGNONS &&
		cl.scores && cl.maxclients > 1 && cl.maxclients <= MAX_SCOREBOARD &&
		!cl.qcvm.extfuncs.CSQC_Parse_Print;
}

static int TR_LocalSlot (void)
{
	return (cl.realviewentity > 0 ? cl.realviewentity : cl.viewentity) - 1;
}

/* Validate before using the engine's permissive UTF-8 fallback converter. */
static qboolean TR_UTF8 (const char *s)
{
	const unsigned char *p = (const unsigned char *)s;
	while (*p)
	{
		unsigned int cp, min;
		int n, i;
		if (*p < 0x80) { p++; continue; }
		if (*p >= 0xc2 && *p <= 0xdf) { cp = *p & 31; n = 1; min = 0x80; }
		else if (*p >= 0xe0 && *p <= 0xef) { cp = *p & 15; n = 2; min = 0x800; }
		else if (*p >= 0xf0 && *p <= 0xf4) { cp = *p & 7; n = 3; min = 0x10000; }
		else return false;
		p++;
		for (i = 0; i < n; i++, p++)
		{
			if ((*p & 0xc0) != 0x80) return false;
			cp = (cp << 6) | (*p & 63);
		}
		if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
	}
	return true;
}

static qboolean TR_Hex (char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Decode known Quake formatting before clearing its high-bit color. Unknown
 * Unicode/link markup is excluded rather than uploaded as a fabricated body. */
static qboolean TR_Readable (char *out, size_t size, const char *text, size_t length)
{
	size_t i, used = 0;
	if (!size) return false;
	out[0] = 0;
	for (i = 0; i < length; i++)
	{
		unsigned char c = (unsigned char)text[i];
		if (c == '^' && i + 1 < length)
		{
			char next = text[i + 1];
			if (next == '[' || next == ']' || next == 'U' || next == '{') return false;
			if (next == '^') i++;
			else if ((next >= '0' && next <= '9') || strchr("hbdsmgr", next)) { i++; continue; }
			else if (next == 'x' && i + 4 < length && TR_Hex(text[i+2]) &&
				TR_Hex(text[i+3]) && TR_Hex(text[i+4])) { i += 4; continue; }
			else if (next == '&' && i + 3 < length &&
				(TR_Hex(text[i+2]) || text[i+2] == '-') &&
				(TR_Hex(text[i+3]) || text[i+3] == '-')) { i += 3; continue; }
		}
		if ((cl.modtype == 4 || cl.modtype == 5) && c == '&' && i + 4 < length &&
			text[i+1] == 'c' && TR_Hex(text[i+2]) && TR_Hex(text[i+3]) && TR_Hex(text[i+4]))
		{ i += 4; continue; }
		if (c == '\r' || c == '\n' || c == '\t') c = ' ';
		else if (c == 0 || c == 0x1b || c == 0x7f) return false;
		else c = (unsigned char)dequake[c & 127];
		if (c < 32 || c > 126) c = ' ';
		if (used + 1 >= size) return false;
		out[used++] = (char)c;
		out[used] = 0;
	}
	while (used && out[used-1] == ' ') used--;
	out[used] = 0;
	return true;
}

/* Output is literal text. Escaping carets prevents color and link interpretation.
 * A short ASCII prefix also keeps Con_Print from recognizing chat/sounding twice. */
static qboolean TR_DisplayText (char *out, size_t size, const char *utf8)
{
	const char *p = utf8;
	size_t used = 0;
	qboolean words = false;
	if (!TR_UTF8(utf8)) return false;
	while (*p)
	{
		char replacement[UTF8_QUAKE_BUFSIZE];
		size_t i, n = UTF8_ToQuakeChar(replacement, &p);
		for (i = 0; i < n; i++)
		{
			unsigned char c = (unsigned char)replacement[i];
			if (c < 32 || c == 127) c = ' ';
			if (c > 127) c = '?';
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) words = true;
			if (used + (c == '^' ? 2 : 1) >= size) return false;
			if (c == '^') out[used++] = '^';
			out[used++] = (char)c;
		}
	}
	while (used && out[used-1] == ' ') used--;
	out[used] = 0;
	return words;
}

static qboolean TR_FilteredBody (char *out, const char *body, size_t length)
{
	char raw[TR_TEXT + 1], filtered[TR_TEXT + 1];
	if (length > TR_TEXT) return false;
	memcpy(raw, body, length);
	raw[length] = 0;
	if (cl_contentfilter.value == 2 && WordFilter_Check(raw, filtered, sizeof(filtered)))
		return TR_Readable(out, TR_TEXT + 1, filtered, strlen(filtered));
	return TR_Readable(out, TR_TEXT + 1, raw, length);
}

static qboolean TR_Name (char *out, size_t size, const char *raw)
{
	char readable[MAX_SCOREBOARDNAME + 1], filtered[MAX_SCOREBOARDNAME + 1];
	size_t i, used = 0;
	if (!size) return false;
	out[0] = 0;
	if (!TR_Readable(readable, sizeof(readable), raw, strlen(raw))) return false;
	if (cl_contentfilter.value == 2 && WordFilter_Check(readable, filtered, sizeof(filtered)))
		q_strlcpy(readable, filtered, sizeof(readable));
	for (i = 0; readable[i]; i++)
	{
		if (used + (readable[i] == '^' ? 2 : 1) >= size) return false;
		if (readable[i] == '^') out[used++] = '^';
		out[used++] = readable[i];
		out[used] = 0;
	}
	return true;
}

static qboolean TR_Ignored (int slot, const char *name)
{
	char prefix[MAX_SCOREBOARDNAME + 8];
	(void)slot;
	q_snprintf(prefix, sizeof(prefix), "\001%s: \n", name);
	return CL_ShouldIgnoreChatPrint(prefix);
}

/* Public and team prefixes can collide (a public player named "(Rasta)").
 * Recognize both before deciding; team is not eligible in this first release. */
static int TR_Classify (const char *line, size_t length, size_t *body)
{
	int slot = -1, matches = 0, teams = 0, i;
	size_t longest = 0;
	if (!length || line[0] != '\001') return -1;
	for (i = 0; i < cl.maxclients; i++)
	{
		const char *name = cl.scores[i].name;
		size_t n = strlen(name);
		if (!n) continue;
		if (length >= n + 3 && !memcmp(line + 1, name, n) &&
			line[n+1] == ':' && line[n+2] == ' ')
		{
			if (n > longest) { longest = n; slot = i; matches = 1; }
			else if (n == longest) matches++;
		}
		if (length >= n + 5 && line[1] == '(' && !memcmp(line + 2, name, n) &&
			line[n+2] == ')' && line[n+3] == ':' && line[n+4] == ' ') teams++;
	}
	if (matches != 1 || teams) return -1;
	*body = longest + 3;
	return slot;
}

static qboolean TR_Trivial (const char *text)
{
	size_t n = strlen(text), i;
	qboolean letters = false;
	if (!*text || !q_strcasecmp(text, "gg") || !q_strcasecmp(text, "gl")) return true;
	if (n == 3 && !q_strncasecmp(text, "dm", 2) && text[2] >= '1' && text[2] <= '6') return true;
	for (i = 0; i < n; i++)
		if (isalnum((unsigned char)text[i])) letters = true;
	return !letters;
}

static void TR_ClearWork (void)
{
	int generation = SDL_GetAtomicInt(&tr_generation);
	if (tr_outgoing)
	{
		tr_send_state = TRANSLATE_SEND_FAILED;
		q_strlcpy(tr_send_notice, "Not sent: translation was canceled.", sizeof(tr_send_notice));
		Con_Printf("Translation canceled. Your message was not sent.\n");
	}
	SDL_SetAtomicInt(&tr_generation, generation == INT_MAX ? 1 : generation + 1);
	if (tr_mutex)
	{
		SDL_LockMutex(tr_mutex);
		tr_pending_count = tr_completed_count = 0;
		SDL_UnlockMutex(tr_mutex);
		if (tr_condition) SDL_SignalCondition(tr_condition);
	}
	tr_outgoing = false;
}

void CL_Translate_Reset (void)
{
	TR_ClearWork();
	tr_rules_dirty = true;
	tr_rules_paused = false;
	memset(tr_watch, 0, sizeof(tr_watch));
	memset(tr_history, 0, sizeof(tr_history));
	memset(tr_history_count, 0, sizeof(tr_history_count));
	memset(tr_names, 0, sizeof(tr_names));
	tr_serial = 0;
	/* Request IDs also correlate menu drafts with their own sends. Keep them
	 * unique across reconnects so a later console send cannot clear an old draft. */
	/* Service backoff survives reconnects: reconnecting must not retry an outage. */
}

void CL_Translate_IgnoreChanged (void)
{
	TR_ClearWork();
	tr_rules_dirty = true;
	memset(tr_history, 0, sizeof(tr_history));
	memset(tr_history_count, 0, sizeof(tr_history_count));
}

void CL_Translate_MapChanged (void)
{
	CL_Translate_IgnoreChanged();
	tr_serial = 0;
	/* Retain watches on this connection. Scoreboard name updates still revoke
	 * a watch if the slot's identity changes while the new map signs on. */
}

void CL_Translate_PlayerChanged (int slot, const char *name)
{
	if (slot < 0 || slot >= MAX_SCOREBOARD) return;
	if (!name) name = "";
	if (!strcmp(tr_names[slot], name)) return;
	tr_rules_dirty = true;
	if (tr_names[slot][0] && tr_watch[slot]) TR_ClearWork();
	tr_watch[slot] = tr_history_count[slot] = 0;
	memset(tr_history[slot], 0, sizeof(tr_history[slot]));
	q_strlcpy(tr_names[slot], name, sizeof(tr_names[slot]));
}

static int TR_ExactRuleName (const char *name)
{
	int i;
	if (!name) return -1;
	for (i = 0; i < tr_rule_count; i++) if (!strcmp(name, tr_rules[i].name)) return i;
	return -1;
}

static int TR_RuleForName (const char *raw)
{
	char name[MAX_SCOREBOARDNAME + 1];
	if (!raw || !TR_Readable(name, sizeof(name), raw, strlen(raw))) return -1;
	return TR_ExactRuleName(name);
}

/* Read a fixed local preferences file directly, never from packs/search paths
 * or executable configs. Hex names cannot turn preferences into commands. */
static void TR_LoadRules (void)
{
	tr_rule_t loaded[TR_RULES];
	char line[MAX_SCOREBOARDNAME * 2 + 32];
	int count = 0, i;
	FILE *file;
	qboolean valid = true;
	tr_rule_count = 0;
	if (!*tr_rules_path || !(file = fopen(tr_rules_path, "rb"))) return;
	if (!fgets(line, sizeof(line), file) || strcmp(line, "QSSM_TRANSLATE_RULES 1\n")) valid = false;
	while (valid && fgets(line, sizeof(line), file))
	{
		char *hex = strchr(line, ' ');
		size_t n = strlen(line), bytes;
		if (!hex || !n || line[n - 1] != '\n' || count == TR_RULES) { valid = false; break; }
		line[n - 1] = 0; *hex++ = 0;
		bytes = strlen(hex);
		loaded[count].language = TR_Language(line);
		if (loaded[count].language < 0 || !bytes || (bytes & 1) || bytes / 2 >= MAX_SCOREBOARDNAME)
		{ valid = false; break; }
		for (i = 0; i < (int)bytes / 2; i++)
		{
			unsigned int c;
			if (!TR_Hex(hex[i*2]) || !TR_Hex(hex[i*2+1]) || sscanf(hex + i*2, "%2x", &c) != 1 || c < 32 || c > 126)
			{ valid = false; break; }
			loaded[count].name[i] = (char)c;
		}
		loaded[count].name[bytes / 2] = 0;
		if (!valid) break;
		for (i = 0; i < count; i++) if (!strcmp(loaded[i].name, loaded[count].name)) valid = false;
		count++;
	}
	if (ferror(file)) valid = false;
	fclose(file);
	if (valid) { memcpy(tr_rules, loaded, (size_t)count * sizeof(*loaded)); tr_rule_count = count; }
	else Con_Printf("Saved translation rules could not be read; none were enabled.\n");
}

static qboolean TR_WriteRules (void)
{
	char temporary[MAX_OSPATH];
	FILE *file;
	int i, j;
	qboolean ok = true;
	if (!*tr_rules_path || (size_t)q_snprintf(temporary, sizeof(temporary), "%s.tmp", tr_rules_path) >= sizeof(temporary)) return false;
	file = fopen(temporary, "wb");
	if (!file) return false;
	if (fprintf(file, "QSSM_TRANSLATE_RULES 1\n") < 0) ok = false;
	for (i = 0; i < tr_rule_count && ok; i++)
	{
		if (fprintf(file, "%s ", tr_codes[tr_rules[i].language]) < 0) ok = false;
		for (j = 0; tr_rules[i].name[j] && ok; j++)
			if (fprintf(file, "%02x", (unsigned char)tr_rules[i].name[j]) < 0) ok = false;
		if (fputc('\n', file) == EOF) ok = false;
	}
	if (fclose(file) != 0) ok = false;
	if (ok)
	{
#ifdef _WIN32
		ok = MoveFileExA(temporary, tr_rules_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
		ok = rename(temporary, tr_rules_path) == 0;
#endif
	}
	if (!ok) remove(temporary);
	return ok;
}

static qboolean TR_UpdateNamedRule (const char *name, int language)
{
	int i = TR_ExactRuleName(name);
	if (language < 0)
	{
		if (i >= 0) memmove(tr_rules + i, tr_rules + i + 1, (size_t)(--tr_rule_count - i) * sizeof(*tr_rules));
		return true;
	}
	if (!name || !*name || strlen(name) >= MAX_SCOREBOARDNAME) return false;
	if (i < 0)
	{
		if (tr_rule_count == TR_RULES) return false;
		i = tr_rule_count++;
	}
	q_strlcpy(tr_rules[i].name, name, sizeof(tr_rules[i].name));
	tr_rules[i].language = language;
	return true;
}

static qboolean TR_UpdateRule (const char *raw, int language)
{
	char name[MAX_SCOREBOARDNAME + 1];
	return raw && TR_Readable(name, sizeof(name), raw, strlen(raw)) && TR_UpdateNamedRule(name, language);
}

static qboolean TR_SaveNamedRule (const char *name, int language)
{
	tr_rule_t previous[TR_RULES];
	int count = tr_rule_count;
	memcpy(previous, tr_rules, sizeof(previous));
	if (TR_UpdateNamedRule(name, language) && TR_WriteRules()) { tr_rules_dirty = true; return true; }
	memcpy(tr_rules, previous, sizeof(previous)); tr_rule_count = count;
	return false;
}

static qboolean TR_SaveRule (const char *raw, int language)
{
	char name[MAX_SCOREBOARDNAME + 1];
	return raw && TR_Readable(name, sizeof(name), raw, strlen(raw)) && TR_SaveNamedRule(name, language);
}

static qboolean TR_Append (char *out, size_t size, const char *text)
{
	return q_strlcat(out, text, size) < size;
}

static qboolean TR_Quote (char *out, size_t size, const char *text)
{
	if (!TR_Append(out, size, "\"")) return false;
	while (*text)
	{
		char piece[7];
		unsigned char c = (unsigned char)*text++;
		if (c == '"' || c == '\\') { piece[0] = '\\'; piece[1] = c; piece[2] = 0; }
		else if (c < 32) q_snprintf(piece, sizeof(piece), "\\u%04x", c);
		else { piece[0] = c; piece[1] = 0; }
		if (!TR_Append(out, size, piece)) return false;
	}
	return TR_Append(out, size, "\"");
}

static qboolean TR_Request (tr_job_t *job, const tr_history_t *current)
{
	tr_history_t *ring = job->slot >= 0 ? tr_history[job->slot] : NULL;
	int count = job->slot >= 0 ? tr_history_count[job->slot] : 0;
	int start, end = 0, i, used;
	char prefix[192];
	/* Catch-up messages can be older than the ring's newest entry. Their context
	 * must be the three preceding lines, never a window anchored at the tail. */
	if (current) while (end < count && ring[end].serial < current->serial) end++;
	/* Retry serialization only, removing oldest context; never retry an API call. */
	for (start = q_max(0, end - 3); start <= end; start++)
	{
		q_snprintf(prefix, sizeof(prefix),
			"{\"version\":1,\"id\":%u,\"target\":\"%s\",\"mode\":\"%s\",\"channel\":\"%s\",\"context\":[",
			(unsigned int)job->id, tr_codes[job->language], job->outgoing ? "outgoing" : "incoming",
			job->team ? "team" : "public");
		q_strlcpy(job->request, prefix, sizeof(job->request));
		used = 0;
		for (i = start; i < end; i++)
		{
			if (!current || ring[i].serial >= current->serial ||
				current->time < ring[i].time || current->time - ring[i].time > TR_CONTEXT_AGE) continue;
			if (used == 3) continue;
			if (used++ && !TR_Append(job->request, sizeof(job->request), ",")) break;
			if (!TR_Append(job->request, sizeof(job->request), "{\"speaker\":\"p1\",\"text\":") ||
				!TR_Quote(job->request, sizeof(job->request), ring[i].text) ||
				!TR_Append(job->request, sizeof(job->request), "}")) break;
		}
		if (i < end) continue;
		if (TR_Append(job->request, sizeof(job->request), "],\"message\":{\"speaker\":\"p1\",\"text\":") &&
			TR_Quote(job->request, sizeof(job->request), job->original) &&
			TR_Append(job->request, sizeof(job->request), "}}")) return true;
	}
	return false;
}

static size_t TR_Write (void *data, size_t size, size_t count, void *opaque)
{
	tr_completion_t *done = opaque;
	size_t bytes;
	if (size && count > SIZE_MAX / size) return 0;
	bytes = size * count;
	if (bytes > TR_RESPONSE - done->bytes || memchr(data, 0, bytes)) return 0;
	memcpy(done->response + done->bytes, data, bytes);
	done->bytes += bytes;
	done->response[done->bytes] = 0;
	return bytes;
}

static size_t TR_Header (char *data, size_t size, size_t count, void *opaque)
{
	tr_completion_t *done = opaque;
	size_t bytes, i;
	unsigned int seconds = 0;
	if (size && count > SIZE_MAX / size) return 0;
	bytes = size * count;
	if (bytes < 13 || q_strncasecmp(data, "Retry-After:", 12)) return bytes;
	i = 12;
	while (i < bytes && (data[i] == ' ' || data[i] == '\t')) i++;
	if (i == bytes || data[i] < '0' || data[i] > '9') return bytes;
	for (; i < bytes && data[i] >= '0' && data[i] <= '9'; i++)
		seconds = q_min(86400u, seconds * 10u + (unsigned int)(data[i] - '0'));
	while (i < bytes && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n')) i++;
	if (i == bytes) done->retry_after = seconds;
	return bytes;
}

static qboolean TR_Cancelled (const tr_job_t *job)
{
	return SDL_GetAtomicInt(&tr_stopping) ||
		SDL_GetAtomicInt(&tr_generation) != job->generation || SDL_GetTicks() >= job->deadline;
}

static int TR_Progress (void *opaque, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
	(void)a; (void)b; (void)c; (void)d;
	return TR_Cancelled((tr_job_t *)opaque);
}

/* A short multi loop allows job cancellation during idle sockets, too. DNS
 * remains backend-dependent; never put this loop on the gameplay thread. */
static CURLcode TR_Perform (CURL *curl, CURLM *multi, const tr_job_t *job)
{
	CURLMcode status;
	CURLcode result = CURLE_FAILED_INIT;
	int running, remaining;
	CURLMsg *message;
	if (!multi) return CURLE_OUT_OF_MEMORY;
	status = curl_multi_add_handle(multi, curl);
	if (status != CURLM_OK) goto finish;
	while (!TR_Cancelled(job))
	{
		status = curl_multi_perform(multi, &running);
		if (status == CURLM_CALL_MULTI_PERFORM) continue;
		if (status != CURLM_OK) break;
		if (!running)
		{
			while ((message = curl_multi_info_read(multi, &remaining)))
				if (message->msg == CURLMSG_DONE) result = message->data.result;
			break;
		}
#if CURL_AT_LEAST_VERSION(7, 66, 0)
		status = curl_multi_poll(multi, NULL, 0, 50, NULL);
#else
		{
			int descriptors = 0;
			Uint64 before = SDL_GetTicks();
			status = curl_multi_wait(multi, NULL, 0, 50, &descriptors);
			if (!descriptors && SDL_GetTicks() - before < 50) SDL_Delay(10);
		}
#endif
		if (status != CURLM_OK) break;
	}
	if (TR_Cancelled(job)) result = CURLE_ABORTED_BY_CALLBACK;
	curl_multi_remove_handle(multi, curl);
finish:
	return result;
}

static int TR_Worker (void *unused)
{
	CURL *curl = curl_easy_init();
	CURLM *multi = curl_multi_init(); /* keep the connection pool across jobs */
	struct curl_slist *headers = NULL;
	(void)unused;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	for (;;)
	{
		tr_completion_t done;
		Uint64 start, remaining;
		memset(&done, 0, sizeof(done));
		SDL_LockMutex(tr_mutex);
		while (!tr_pending_count && !SDL_GetAtomicInt(&tr_stopping)) SDL_WaitCondition(tr_condition, tr_mutex);
		if (SDL_GetAtomicInt(&tr_stopping)) { SDL_UnlockMutex(tr_mutex); break; }
		done.job = tr_pending[0];
		memmove(tr_pending, tr_pending + 1, (size_t)--tr_pending_count * sizeof(tr_pending[0]));
		tr_busy = true;
		SDL_UnlockMutex(tr_mutex);
		start = SDL_GetTicks();
		remaining = start < done.job.deadline ? done.job.deadline - start : 0;
		done.result = CURLE_ABORTED_BY_CALLBACK;
		if (!TR_Cancelled(&done.job) && remaining >= 1000 && curl && multi && headers)
		{
			long timeout = (long)q_min(remaining, (Uint64)4000);
			curl_easy_setopt(curl, CURLOPT_URL, tr_endpoint);
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, done.job.request);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(done.job.request));
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, TR_Write);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &done);
			curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, TR_Header);
			curl_easy_setopt(curl, CURLOPT_HEADERDATA, &done);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, q_min(timeout, 1500L));
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, TR_Progress);
			curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &done.job);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "QSS-M chat translation/1");
#if CURL_AT_LEAST_VERSION(7, 85, 0)
#ifdef QSSM_TRANSLATE_TEST
			curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http");
#else
			curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#endif
#else
#ifdef QSSM_TRANSLATE_TEST
			curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);
#else
			curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
#endif
			done.result = TR_Perform(curl, multi, &done.job);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &done.http);
		}
		else if (!curl || !multi || !headers) done.result = CURLE_FAILED_INIT;
		done.elapsed = SDL_GetTicks() - start;
		SDL_LockMutex(tr_mutex);
		tr_busy = false;
		/* The main thread normally drains every frame. Drop completions when
		 * stalled; never grow the queue or wait for the gameplay thread. */
		if (tr_completed_count == TR_COMPLETIONS)
		{
			int victim;
			for (victim = 0; victim < tr_completed_count; victim++)
				if (!tr_completed[victim].job.outgoing) break;
			if (victim < tr_completed_count)
			{
				memmove(tr_completed + victim, tr_completed + victim + 1,
					(size_t)(--tr_completed_count - victim) * sizeof(tr_completed[0]));
				tr_completion_drops++;
			}
		}
		/* Never lose an outgoing completion: that would leave its pending
		 * indicator stuck and could incorrectly block later typed messages. */
		if (tr_completed_count < TR_COMPLETIONS) tr_completed[tr_completed_count++] = done;
		SDL_UnlockMutex(tr_mutex);
	}
	curl_slist_free_all(headers);
	if (curl) curl_easy_cleanup(curl);
	if (multi) curl_multi_cleanup(multi);
	return 0;
}

static qboolean TR_Start (void)
{
	if (tr_thread) return true;
	if (tr_worker_failed) return false;
	if (!tr_mutex) tr_mutex = SDL_CreateMutex();
	if (!tr_condition) tr_condition = SDL_CreateCondition();
	if (tr_mutex && tr_condition) tr_thread = SDL_CreateThread(TR_Worker, "ChatTranslate", NULL);
	if (tr_thread) return true;
	tr_worker_failed = true;
	Con_Printf("Translation unavailable: worker could not start. Restart to retry.\n");
	return false;
}

static qboolean TR_Enqueue (tr_job_t *job, const tr_history_t *history, uint32_t *evicted_id)
{
	int i, victim;
	if (evicted_id) *evicted_id = 0;
	if (SDL_GetTicks() < tr_backoff_until || !TR_Start()) return false;
	if (tr_id == UINT32_MAX) { CL_Translate_Reset(); tr_id = 0; return false; }
	job->id = ++tr_id;
	job->generation = SDL_GetAtomicInt(&tr_generation);
	if (!TR_Request(job, history)) { tr_skipped++; return false; }
	SDL_LockMutex(tr_mutex);
	if (tr_pending_count == TR_QUEUE)
	{
		victim = -1;
		for (i = 0; i < tr_pending_count; i++)
			if (!tr_pending[i].outgoing && tr_pending[i].priority <= job->priority &&
				(victim < 0 || tr_pending[i].priority < tr_pending[victim].priority)) victim = i;
		if (victim < 0) { SDL_UnlockMutex(tr_mutex); return false; }
		if (evicted_id) *evicted_id = tr_pending[victim].id;
		memmove(tr_pending + victim, tr_pending + victim + 1,
			(size_t)(--tr_pending_count - victim) * sizeof(tr_pending[0]));
		tr_dropped++;
	}
	for (i = 0; i < tr_pending_count && tr_pending[i].priority >= job->priority; i++) {}
	memmove(tr_pending + i + 1, tr_pending + i, (size_t)(tr_pending_count - i) * sizeof(tr_pending[0]));
	tr_pending[i] = *job;
	tr_pending_count++;
	SDL_UnlockMutex(tr_mutex);
	SDL_SignalCondition(tr_condition);
	return true;
}

typedef enum { TR_SUBMIT_SKIPPED, TR_SUBMIT_QUEUED, TR_SUBMIT_DROPPED } tr_submit_result_t;

static tr_submit_result_t TR_Submit (int slot, tr_history_t *history, qboolean earlier, uint32_t *evicted_id)
{
	tr_job_t job;
	int language = tr_watch[slot] - 1;
	if (language < 0 || slot == TR_LocalSlot() || TR_Ignored(slot, tr_names[slot]) ||
		(history->handled & (1u << language)) || TR_Trivial(history->text)) return TR_SUBMIT_SKIPPED;
	memset(&job, 0, sizeof(job));
	job.slot = slot;
	job.language = language;
	job.serial = history->serial;
	job.earlier = earlier;
	job.priority = earlier ? 0 : 1;
	job.deadline = (earlier ? SDL_GetTicks() : history->time) + TR_LIFETIME;
	q_strlcpy(job.name, tr_names[slot], sizeof(job.name));
	q_strlcpy(job.original, history->text, sizeof(job.original));
	if (!TR_Enqueue(&job, history, evicted_id)) { tr_dropped++; return TR_SUBMIT_DROPPED; }
	return TR_SUBMIT_QUEUED;
}

void CL_Translate_Observe (const char *original, const char *visible)
{
	const char *raw = original, *shown = visible;
	if (!tr_initialized || !TR_Live()) return;
	while (*raw && *shown)
	{
		const char *end = strchr(raw, '\n'), *visible_end = strchr(shown, '\n');
		char text[TR_TEXT + 1], line[TR_TEXT + MAX_SCOREBOARDNAME + 8];
		size_t length, body;
		int slot, count;
		tr_history_t *history;
		if (!end || !visible_end) break; /* no fragmented print assembly */
		length = (size_t)(end - raw);
		slot = TR_Classify(raw, length, &body);
		if (slot >= 0 && length < sizeof(line) && length >= body)
		{
			memcpy(line, raw, length); line[length] = 0;
			if (!CL_ShouldIgnoreChatPrint(line) && TR_FilteredBody(text, raw + body, length - body) && *text)
			{
				CL_Translate_PlayerChanged(slot, cl.scores[slot].name);
				if (tr_serial == UINT32_MAX) { CL_Translate_Reset(); return; }
				count = tr_history_count[slot];
				if (count == TR_HISTORY)
				{
					memmove(tr_history[slot], tr_history[slot] + 1, (TR_HISTORY - 1) * sizeof(tr_history[slot][0]));
					count--;
				}
				history = &tr_history[slot][count];
				memset(history, 0, sizeof(*history));
				history->serial = ++tr_serial;
				history->time = SDL_GetTicks();
				q_strlcpy(history->text, text, sizeof(history->text));
				tr_history_count[slot] = count + 1;
				TR_Submit(slot, history, false, NULL);
			}
		}
		raw = end + 1; shown = visible_end + 1;
	}
}

/* A tiny strict parser for this flat response contract. JSON_Parse is useful
 * elsewhere but permits duplicate keys, lossy embedded NULs and primitives
 * that are unsuitable for validating a network authorization/display result. */
static void TR_Space (const char **p)
{
	while (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t') (*p)++;
}

static int TR_HexValue (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static qboolean TR_CodePoint (const char **p, unsigned int *cp)
{
	int i, digit;
	*cp = 0;
	for (i = 0; i < 4; i++)
	{
		digit = TR_HexValue(**p);
		if (digit < 0) return false;
		*cp = (*cp << 4) | (unsigned int)digit;
		(*p)++;
	}
	return true;
}

static qboolean TR_String (const char **p, char *out, size_t size)
{
	size_t used = 0;
	if (*(*p)++ != '"') return false;
	while (**p && **p != '"')
	{
		char piece[5];
		size_t n = 1;
		unsigned char c = (unsigned char)*(*p)++;
		if (c < 32) return false;
		piece[0] = (char)c;
		if (c == '\\')
		{
			c = (unsigned char)*(*p)++;
			if (!c) return false;
			if (c == 'u')
			{
				unsigned int cp, low;
				if (!TR_CodePoint(p, &cp) || !cp) return false;
				if (cp >= 0xd800 && cp <= 0xdbff)
				{
					if ((*p)[0] != '\\' || (*p)[1] != 'u') return false;
					*p += 2;
					if (!TR_CodePoint(p, &low) || low < 0xdc00 || low > 0xdfff) return false;
					cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
				}
				else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
				n = UTF8_WriteCodePoint(piece, 4, cp);
				if (!n) return false;
			}
			else
			{
				switch (c)
				{
				case '"': case '\\': case '/': piece[0] = c; break;
				case 'n': piece[0] = '\n'; break;
				case 'r': piece[0] = '\r'; break;
				case 't': piece[0] = '\t'; break;
				case 'b': piece[0] = '\b'; break;
				case 'f': piece[0] = '\f'; break;
				default: return false;
				}
			}
		}
		if (n >= size - used) return false;
		memcpy(out + used, piece, n); used += n;
	}
	if (**p != '"') return false;
	(*p)++;
	out[used] = 0;
	return TR_UTF8(out);
}

static qboolean TR_Integer (const char **p, uint32_t *out)
{
	uint32_t value = 0;
	if (**p < '0' || **p > '9') return false;
	if (**p == '0' && (*p)[1] >= '0' && (*p)[1] <= '9') return false;
	while (**p >= '0' && **p <= '9')
	{
		unsigned int digit = (unsigned int)(*(*p)++ - '0');
		if (value > (UINT32_MAX - digit) / 10) return false;
		value = value * 10 + digit;
	}
	*out = value;
	return true;
}

typedef struct
{
	uint32_t version, id;
	unsigned int fields;
	char status[24], text[1025], reason[32], error[32];
} tr_reply_t;

static qboolean TR_Reply (const char *p, tr_reply_t *reply)
{
	memset(reply, 0, sizeof(*reply));
	TR_Space(&p);
	if (*p++ != '{') return false;
	for (;;)
	{
		char key[24];
		unsigned int bit;
		TR_Space(&p);
		if (!TR_String(&p, key, sizeof(key))) return false;
		TR_Space(&p);
		if (*p++ != ':') return false;
		TR_Space(&p);
		if (!strcmp(key, "version")) bit = 1;
		else if (!strcmp(key, "id")) bit = 2;
		else if (!strcmp(key, "status")) bit = 4;
		else if (!strcmp(key, "text")) bit = 8;
		else if (!strcmp(key, "reason")) bit = 16;
		else if (!strcmp(key, "error")) bit = 32;
		else return false;
		if (reply->fields & bit) return false;
		reply->fields |= bit;
		if (bit == 1 || bit == 2)
		{
			if (!TR_Integer(&p, bit == 1 ? &reply->version : &reply->id)) return false;
		}
		else
		{
			char *destination = bit == 4 ? reply->status : bit == 8 ? reply->text :
				bit == 16 ? reply->reason : reply->error;
			size_t capacity = bit == 4 ? sizeof(reply->status) : bit == 8 ? sizeof(reply->text) :
				bit == 16 ? sizeof(reply->reason) : sizeof(reply->error);
			if (!TR_String(&p, destination, capacity)) return false;
		}
		TR_Space(&p);
		if (*p == '}') { p++; TR_Space(&p); return !*p; }
		if (*p++ != ',') return false;
	}
}

static void TR_Notice (const char *message, qboolean outgoing)
{
	Uint64 now = SDL_GetTicks();
	if (outgoing)
	{
		tr_send_state = TRANSLATE_SEND_FAILED;
		q_snprintf(tr_send_notice, sizeof(tr_send_notice), "Not sent: %s", message);
	}
	if (outgoing || !tr_last_notice || now - tr_last_notice >= 60000)
	{
		Con_Printf("Translation: %s%s\n", message, outgoing ? " Your message was not sent." : "");
		tr_last_notice = now ? now : 1;
	}
}

static void TR_Failure (tr_completion_t *done, const tr_reply_t *reply)
{
	unsigned int seconds = done->retry_after;
	const char *message = "service unavailable.";
	tr_errors++;
	if (reply && !strcmp(reply->error, "daily_limit")) message = "daily service allowance reached.";
	else if (reply && !strcmp(reply->error, "rate_limit")) message = "request limit for this connection reached.";
	else if (reply && !strcmp(reply->error, "upstream_rate_limit")) message = "translation provider is rate limited.";
	else if (reply && !strcmp(reply->error, "unsupported_version")) message = "client update required.";
	else if (reply && !strcmp(reply->error, "disabled")) message = "service temporarily disabled.";
	if ((done->http == 429 || done->http == 503) && seconds)
	{
		seconds = q_min(seconds, reply && !strcmp(reply->error, "daily_limit") ? 86400u : 300u);
		tr_backoff_until = SDL_GetTicks() + (Uint64)seconds * 1000;
	}
	else if (++tr_failures >= 3)
	{
		seconds = q_min(300u, 30u << q_min(tr_backoff_step, 4));
		tr_backoff_step = q_min(tr_backoff_step + 1, 4);
		tr_backoff_until = SDL_GetTicks() + (Uint64)seconds * 1000;
	}
	if (SDL_GetTicks() < tr_backoff_until && tr_mutex)
	{
		/* An explicitly typed message must get its own failure notice, even
		 * when an earlier incoming failure emptied the remaining queue. */
		if (tr_outgoing && !done->job.outgoing)
		{
			TR_Notice(message, true);
			tr_outgoing = false;
		}
		TR_ClearWork();
	}
	TR_Notice(message, done->job.outgoing);
}

static void TR_MarkHandled (const tr_job_t *job)
{
	int i;
	if (job->slot < 0) return;
	for (i = 0; i < tr_history_count[job->slot]; i++)
		if (tr_history[job->slot][i].serial == job->serial)
			tr_history[job->slot][i].handled |= 1u << job->language;
}

static qboolean TR_Send (const char *command)
{
	size_t needed = strlen(command) + 2; /* opcode plus terminating NUL */
	if (cls.message.cursize < 0 || (size_t)cls.message.cursize + needed > (size_t)cls.message.maxsize)
		return false;
	MSG_WriteByte(&cls.message, clc_stringcmd);
	MSG_WriteString(&cls.message, command);
	return true;
}

static void TR_Complete (tr_completion_t *done)
{
	tr_reply_t reply;
	tr_job_t *job = &done->job;
	char text[2049], name[MAX_SCOREBOARDNAME * 2 + 1], filtered[2049];
	qboolean parsed;
	if (job->generation != SDL_GetAtomicInt(&tr_generation)) return;
	if (job->outgoing) tr_outgoing = false;
	if (!TR_Live()) return;
	if (SDL_GetTicks() >= job->deadline)
	{
		tr_dropped++;
		if (job->outgoing) TR_Notice("request expired.", true);
		return;
	}
	if (done->result == CURLE_ABORTED_BY_CALLBACK)
	{
		tr_dropped++;
		if (job->outgoing) TR_Notice("request expired or was cancelled.", true);
		return;
	}
	if (!job->outgoing && (job->slot < 0 || job->slot >= cl.maxclients ||
		tr_watch[job->slot] != job->language + 1 || strcmp(job->name, cl.scores[job->slot].name) ||
		TR_Ignored(job->slot, job->name))) return;
	tr_last_latency = done->elapsed;
	parsed = TR_Reply(done->response, &reply);
	if (done->result != CURLE_OK || done->http != 200)
	{
		TR_Failure(done, parsed && reply.fields == 32 ? &reply : NULL);
		return;
	}
	if (!parsed || reply.version != 1 || reply.id != job->id || (reply.fields & 7) != 7 ||
		(!strcmp(reply.status, "translated") ? reply.fields != 15 :
		!strcmp(reply.status, "unchanged") ? reply.fields != 7 :
		!strcmp(reply.status, "skipped") ? reply.fields != 23 ||
			(strcmp(reply.reason, "uncertain") && strcmp(reply.reason, "blocked") && strcmp(reply.reason, "invalid_output")) : true))
	{
		TR_Failure(done, NULL);
		return;
	}
	tr_failures = tr_backoff_step = 0;
	TR_MarkHandled(job);
	if (!strcmp(reply.status, "skipped"))
	{
		tr_skipped++;
		if (job->outgoing) TR_Notice("no usable translation.", true);
		return;
	}
	if (!strcmp(reply.status, "unchanged"))
	{
		tr_unchanged++;
		if (!job->outgoing) return;
		q_strlcpy(reply.text, job->original, sizeof(reply.text));
	}
	if (!TR_DisplayText(text, sizeof(text), reply.text))
	{
		tr_skipped++;
		if (job->outgoing) TR_Notice("unreadable translation.", true);
		return;
	}
	if (cl_contentfilter.value == 2 && WordFilter_Check(text, filtered, sizeof(filtered)))
		q_strlcpy(text, filtered, sizeof(text));
	if (job->outgoing)
	{
		char command[MAXCMDLINE];
		const char *say = job->team ? "say_team" : "say";
		size_t limit = (cl.modtype == 1 || cl.modtype == 4) ? MAX_CHAT_SIZE_EX : MAX_CHAT_SIZE;
		/* Send a literal stringcmd, bypassing Cbuf, aliases, QC console hooks,
		 * percent macros and any modifier changes since this job was submitted.
		 * The server receives one say command, never an executable client line. */
		if (strlen(text) >= limit || strlen(text) + strlen(say) + 2 > sizeof(command))
		{ TR_Notice("translated message exceeds the server chat limit.", true); return; }
		q_snprintf(command, sizeof(command), "%s %s", say, text);
		if (!TR_Send(command)) { TR_Notice("chat send buffer is full.", true); return; }
		tr_send_state = TRANSLATE_SEND_SENT;
		q_snprintf(tr_send_notice, sizeof(tr_send_notice), job->team ? "Sent to team in %s." : "Sent publicly in %s.", tr_languages[job->language]);
		/* The server echo displays the message; success feedback stays in the menu. */
	}
	else
	{
		if (!strcmp(reply.status, "unchanged") || !strcmp(text, job->original)) return;
		if (!TR_Name(name, sizeof(name), job->name)) return;
		char label[4];
		TR_LanguageLabel(label, job->language);
		Con_Printf("^m[%s%s]^m %s: %s\n", label, job->earlier ? " earlier" : "", name, text);
	}
	tr_translated++;
}

void CL_Translate_Frame (void)
{
	int i;
	if (!tr_initialized) return;
	if (tr_filter != (int)cl_contentfilter.value)
	{
		CL_Translate_IgnoreChanged();
		tr_filter = (int)cl_contentfilter.value;
	}
	if (cls.demoplayback)
	{
		for (i = 0; i < MAX_SCOREBOARD; i++)
			if (tr_watch[i] || tr_history_count[i]) { CL_Translate_Reset(); break; }
	}
	if (!TR_Live()) tr_rules_dirty = true;
	else if (tr_rules_dirty && !tr_rules_paused) TR_ApplyRules();
	if (!tr_mutex) return;
	for (;;)
	{
		tr_completion_t done;
		SDL_LockMutex(tr_mutex);
		if (!tr_completed_count) { SDL_UnlockMutex(tr_mutex); break; }
		done = tr_completed[0];
		memmove(tr_completed, tr_completed + 1, (size_t)--tr_completed_count * sizeof(tr_completed[0]));
		SDL_UnlockMutex(tr_mutex);
		TR_Complete(&done);
	}
}

static void TR_Help (void)
{
	Con_Printf("^mtranslate @Rasta^m  - listen in English\n\n"
		"^mtranslate save @Rasta^m  - remember this player in English\n\n"
		"^mtranslate spanish save @Rasta^m  - remember this player in Spanish\n\n"
		"^mtranslate @everyone^m  - listen to all current players in English\n\n"
		"^mtranslate save @everyone^m  - remember the current player names\n\n"
		"^mtranslate spanish @Rasta^m  - listen in a selected language\n\n"
		"^mtranslate spanish^m Anyone up for 2v2?  - translate and send chat (^mCtrl+Enter^m for team)\n\n"
		"^mtranslate off @Rasta^m  - stop one player; ^mtranslate off^m  - stop all\n\n"
		"^mtranslate^m  - status; ^mtranslate help^m  - commands\n");
}

static void TR_Status (void)
{
	int i, watched = 0, queued = 0;
	unsigned int completion_drops = 0;
	qboolean busy = false;
	Uint64 now = SDL_GetTicks();
	for (i = 0; i < MAX_SCOREBOARD; i++)
		if (tr_watch[i])
		{
			char name[MAX_SCOREBOARDNAME * 2 + 1];
			if (!TR_Name(name, sizeof(name), tr_names[i])) q_strlcpy(name, "?", sizeof(name));
			Con_Printf("  @%s -> %s\n", name, tr_languages[tr_watch[i]-1]);
			watched++;
		}
	if (tr_mutex)
	{
		SDL_LockMutex(tr_mutex); queued = tr_pending_count; busy = tr_busy;
		completion_drops = tr_completion_drops; SDL_UnlockMutex(tr_mutex);
	}
	Con_Printf("Translation: %d watched, %d waiting, %d active, last latency %u ms.\n",
		watched, queued, busy, (unsigned int)tr_last_latency);
	if (tr_worker_failed) Con_Printf("Translation worker unavailable; restart to retry.\n");
	if (now < tr_backoff_until) Con_Printf("Service paused for %u more seconds.\n", (unsigned int)((tr_backoff_until-now+999)/1000));
	Con_Printf("Results: %u translated, %u unchanged, %u skipped, %u dropped, %u errors.\n",
		tr_translated, tr_unchanged, tr_skipped, tr_dropped + completion_drops, tr_errors);
	Con_Printf("Commands: translate help\n");
}

/* Only the direct console path calls CL_Translate_Console. Normal execution
 * stays registered for discovery and help, but cannot authorize any upload. */
static void TR_Command (void)
{
	if (Cmd_Argc() == 1) TR_Status();
	else if (Cmd_Argc() == 2 && !q_strcasecmp(Cmd_Argv(1), "help")) TR_Help();
	else Con_Printf("Type translate directly in the local console; scripts, aliases and server commands cannot start it.\n");
}

enum { TR_PLAYER_NOT_FOUND = -1, TR_PLAYER_AMBIGUOUS = -2 };

static qboolean TR_PlayerQuery (char *wanted, size_t size, const char *target)
{
	size_t length = strlen(target);
	while (length && (target[length-1] == ' ' || target[length-1] == '\t')) length--;
	if (length >= 2 && target[0] == '"' && target[length-1] == '"') { target++; length -= 2; }
	return length && TR_Readable(wanted, size, target, length) && *wanted;
}

static qboolean TR_Everyone (const char *target)
{
	size_t length = strlen(target);
	while (length && (target[length - 1] == ' ' || target[length - 1] == '\t')) length--;
	/* Reserve only the literal keyword. @\"everyone\" still names one player. */
	return length == 8 && !q_strncasecmp(target, "everyone", length);
}

static int TR_FindPlayer (const char *target)
{
	char wanted[MAX_SCOREBOARDNAME + 1];
	int i, exact = -1, exact_count = 0, partial = -1, partial_count = 0;
	if (!TR_PlayerQuery(wanted, sizeof(wanted), target)) return TR_PLAYER_NOT_FOUND;
	for (i = 0; i < cl.maxclients; i++)
	{
		char name[MAX_SCOREBOARDNAME + 1];
		if (!cl.scores[i].name[0] || !TR_Readable(name, sizeof(name), cl.scores[i].name, strlen(cl.scores[i].name))) continue;
		if (!q_strcasecmp(name, wanted)) { exact = i; exact_count++; }
		if (q_strcasestr(name, wanted)) { partial = i; partial_count++; }
	}
	if (exact_count) return exact_count == 1 ? exact : TR_PLAYER_AMBIGUOUS;
	return partial_count > 1 ? TR_PLAYER_AMBIGUOUS : partial;
}

static void TR_PlayerMatchError (const char *target, int match)
{
	char query[MAXCMDLINE + 1], display[MAXCMDLINE * 2 + 1];
	size_t i, used = 0;
	if (!*target) { Con_Printf("Type @ and press Tab to choose a player.\n"); return; }
	if (!TR_PlayerQuery(query, sizeof(query), target))
	{ Con_Printf("Enter a readable player name. Press Tab to choose.\n"); return; }
	/* Treat the typed name as literal console text, including color/link carets. */
	for (i = 0; query[i]; i++)
	{
		if (query[i] == '^') display[used++] = '^';
		display[used++] = query[i];
	}
	display[used] = 0;
	if (match == TR_PLAYER_AMBIGUOUS)
		Con_Printf("Several players match \"%s\". Press Tab to choose.\n", display);
	else Con_Printf("Player \"%s\" isn't here. Press Tab to choose.\n", display);
}

const char *CL_Translate_UnavailableReason (void)
{
	if (cls.demoplayback) return "Translation is unavailable during demos.";
	if (cls.state != ca_connected || cls.signon != SIGNONS || cl.maxclients <= 1) return "not connected";
	return TR_Live() ? NULL : "Public chat translation is unsupported here.";
}

const char *CL_Translate_LanguageName (int language)
{
	return language >= 0 && language < (int)countof(tr_languages) ? tr_languages[language] : "English";
}

int CL_Translate_LanguageCount (void) { return (int)countof(tr_codes); }

const char *CL_Translate_LanguageCode (int language)
{
	return language >= 0 && language < (int)countof(tr_codes) ? tr_codes[language] : "en";
}

static void TR_MenuNotice (const char *message)
{
	q_strlcpy(tr_menu_notice, message, sizeof(tr_menu_notice));
	Con_Printf("%s\n", message);
}

const char *CL_Translate_MenuNotice (void) { return tr_menu_notice; }

int CL_Translate_MenuPlayers (cl_translate_player_t *players, int capacity)
{
	int i, count = 0;
	if (!TR_Live() || !players || capacity <= 0) return 0;
	for (i = 0; i < cl.maxclients && count < capacity; i++)
	{
		cl_translate_player_t *player;
		char readable[MAX_SCOREBOARDNAME + 1] = {0};
		if (!cl.scores[i].name[0] || i == TR_LocalSlot()) continue;
		player = &players[count++];
		memset(player, 0, sizeof(*player));
		player->slot = i;
		q_strlcpy(player->name, cl.scores[i].name, sizeof(player->name));
		player->language = !strcmp(tr_names[i], player->name) ? tr_watch[i] - 1 : -1;
		player->remembered = TR_RuleForName(player->name) >= 0;
		player->reason = "";
		if (!TR_Name(player->display, sizeof(player->display), player->name) ||
			!TR_Readable(readable, sizeof(readable), player->name, strlen(player->name)))
		{
			q_strlcpy(player->display, "Unreadable player name", sizeof(player->display));
			player->reason = "name";
		}
		else if (TR_Ignored(i, player->name)) player->reason = "ignored";
		else if (TR_FindPlayer(readable) != i) player->reason = "ambiguous";
		else
		{
			char line[MAX_SCOREBOARDNAME + 8];
			size_t body;
			q_snprintf(line, sizeof(line), "\001%s: x", player->name);
			if (TR_Classify(line, strlen(line), &body) != i) player->reason = "ambiguous";
		}
		player->selectable = !*player->reason;
		if (*readable) q_strlcpy(player->display, readable, sizeof(player->display));
	}
	return count;
}

int CL_Translate_MenuSelections (cl_translate_player_t *players, int capacity)
{
	cl_translate_player_t live[MAX_SCOREBOARD];
	qboolean shown[TR_RULES] = {0};
	int i, count = 0, live_count;
	if (!players || capacity <= 0) return 0;
	live_count = CL_Translate_MenuPlayers(live, MAX_SCOREBOARD);
	for (i = 0; i < live_count && count < capacity; i++)
		if (live[i].language >= 0)
		{
			int rule = TR_RuleForName(live[i].name);
			if (rule >= 0) shown[rule] = true;
			players[count++] = live[i];
		}
	for (i = 0; i < tr_rule_count && count < capacity; i++)
		if (!shown[i])
		{
			cl_translate_player_t *player = &players[count++];
			memset(player, 0, sizeof(*player));
			player->slot = -1; player->language = tr_rules[i].language;
			player->remembered = true; player->reason = "saved";
			q_strlcpy(player->name, tr_rules[i].name, sizeof(player->name));
			q_strlcpy(player->display, tr_rules[i].name, sizeof(player->display));
		}
	return count;
}

int CL_Translate_ReadLanguage (void) { return tr_read_language; }
int CL_Translate_WriteLanguage (void) { return tr_write_language; }

void CL_Translate_MenuStatus (char *out, size_t size)
{
	int i, count = 0;
	Uint64 now = SDL_GetTicks();
	for (i = 0; i < MAX_SCOREBOARD; i++) if (tr_watch[i]) count++;
	if (tr_worker_failed) q_strlcpy(out, "Unavailable; restart to retry.", size);
	else if (now < tr_backoff_until)
		q_snprintf(out, size, "Paused; retry in %u seconds.", (unsigned int)((tr_backoff_until-now+999)/1000));
	else if (count) q_snprintf(out, size, "Following %d player%s; public chat.", count, count == 1 ? "" : "s");
	else q_strlcpy(out, "Translation is off.", size);
}

cl_translate_send_state_t CL_Translate_SendState (unsigned int *id, char *out, size_t size)
{
	if (id) *id = tr_send_id;
	if (out && size) q_strlcpy(out, tr_send_notice, size);
	return tr_outgoing ? TRANSLATE_SEND_PENDING : tr_send_state;
}

static void TR_WatchNotice (const char *message, qboolean batch)
{
	if (batch) q_strlcpy(tr_menu_notice, message, sizeof(tr_menu_notice));
	else TR_MenuNotice(message);
}

static qboolean TR_SetWatch (int slot, const char *expected_name, int language, qboolean batch)
{
	char name[MAX_SCOREBOARDNAME * 2 + 1], message[160];
	cl_translate_player_t players[MAX_SCOREBOARD];
	int i, j, count, start, recent = 0, dropped = 0;
	uint32_t recent_ids[3] = {0};
	Uint64 now;
	const char *unavailable = CL_Translate_UnavailableReason();
	if (unavailable) { TR_WatchNotice(unavailable, batch); return false; }
	if (slot < 0 || slot >= cl.maxclients || !expected_name || strcmp(expected_name, cl.scores[slot].name))
	{ TR_WatchNotice("The player list changed. Select the player again.", batch); return false; }
	if (language < -1 || language >= (int)countof(tr_codes)) return false;
	if (slot == TR_LocalSlot()) { TR_WatchNotice("Choose another player's chat to follow.", batch); return false; }
	/* Bulk activation already validated this stable main-thread roster snapshot. */
	if (language >= 0 && !batch)
	{
		count = CL_Translate_MenuPlayers(players, MAX_SCOREBOARD);
		for (i = 0; i < count && players[i].slot != slot; i++) {}
		if (i == count || !players[i].selectable)
		{ TR_WatchNotice("That player's chat cannot be selected.", batch); return false; }
	}
	CL_Translate_PlayerChanged(slot, expected_name);
	i = TR_RuleForName(expected_name);
	if (i >= 0 && tr_rules[i].language != language && !TR_SaveRule(expected_name, language))
	{ TR_WatchNotice("Could not update the saved translation rule.", batch); return false; }
	if (!TR_Name(name, sizeof(name), expected_name)) q_strlcpy(name, "player", sizeof(name));
	if (tr_watch[slot] == language + 1)
	{
		if (language < 0) q_snprintf(tr_menu_notice, sizeof(tr_menu_notice), "Already stopped translating %s.", name);
		else q_snprintf(tr_menu_notice, sizeof(tr_menu_notice), "Already translating %s into %s.", name, CL_Translate_LanguageName(language));
		return true;
	}
	/* A batch invalidates old targets once, before any new catch-up is queued. */
	if (tr_watch[slot] && !batch) TR_ClearWork();
	tr_watch[slot] = language + 1;
	if (language < 0)
	{
		q_snprintf(message, sizeof(message), "Stopped translating %s.", name);
		TR_WatchNotice(message, batch); return true;
	}
	q_snprintf(message, sizeof(message), "Translating %s's public chat to %s.", name, tr_languages[language]);
	TR_WatchNotice(message, batch);
	if (!batch) Con_Printf("Stop: translate off @%s\n", name);
	now = SDL_GetTicks();
	start = q_max(0, tr_history_count[slot] - 3);
	for (i = start; i < tr_history_count[slot]; i++)
		if (now - tr_history[slot][i].time <= TR_RECENT_AGE)
		{
			uint32_t evicted = 0;
			tr_submit_result_t result = TR_Submit(slot, &tr_history[slot][i], true, &evicted);
			if (result == TR_SUBMIT_QUEUED) recent_ids[i - start] = tr_id;
			else if (result == TR_SUBMIT_DROPPED) dropped++;
			/* A busy queue may immediately replace an earlier job from this
			 * batch. Do not announce that discarded job as a pending check. */
			if (evicted) for (j = 0; j < 3; j++) if (recent_ids[j] == evicted) recent_ids[j] = 0;
		}
	if (tr_watch[slot] != language + 1)
	{ TR_WatchNotice("Translation stopped; start the watch again.", batch); return false; }
	for (j = 0; j < 3; j++) if (recent_ids[j]) recent++;
	/* Count actual queued checks, rather than buffered, trivial or handled lines.
	 * A service/worker failure must not masquerade as an empty history. */
	if (recent) q_snprintf(message, sizeof(message), "Checking %d recent message%s.", recent, recent == 1 ? "" : "s");
	else if (dropped) q_strlcpy(message, "Recent messages could not be queued.", sizeof(message));
	else q_strlcpy(message, "Listening for new chat.", sizeof(message));
	TR_WatchNotice(message, batch);
	return true;
}

qboolean CL_Translate_MenuWatch (int slot, const char *expected_name, int language)
{
	return TR_SetWatch(slot, expected_name, language, false);
}

qboolean CL_Translate_MenuWatchSaved (int slot, const char *expected_name, int language)
{
	cl_translate_player_t players[MAX_SCOREBOARD];
	int i, count, rule;
	if (language < 0 || language >= (int)countof(tr_codes)) return false;
	count = CL_Translate_MenuPlayers(players, MAX_SCOREBOARD);
	for (i = 0; i < count; i++)
		if (players[i].slot == slot && expected_name && !strcmp(players[i].name, expected_name) && players[i].selectable) break;
	if (i == count)
	{ TR_MenuNotice("That player's chat cannot be selected."); return false; }
	rule = TR_RuleForName(expected_name);
	if ((rule < 0 || tr_rules[rule].language != language) && !TR_SaveRule(expected_name, language))
	{ TR_MenuNotice("Could not save the translation rule."); return false; }
	return TR_SetWatch(slot, expected_name, language, false);
}

qboolean CL_Translate_MenuRemember (int slot, const char *expected_name, qboolean remember)
{
	if (!TR_Live() || slot < 0 || slot >= cl.maxclients || !expected_name ||
		strcmp(expected_name, cl.scores[slot].name) || strcmp(expected_name, tr_names[slot]) || !tr_watch[slot]) return false;
	if (!TR_SaveRule(expected_name, remember ? tr_watch[slot] - 1 : -1))
	{ TR_MenuNotice("Could not save the translation rule."); return false; }
	return true;
}

qboolean CL_Translate_MenuEditPlayer (int slot, const char *expected_name, int language, qboolean remember)
{
	cl_translate_player_t players[CL_TRANSLATE_MAX_SELECTIONS];
	int i, count, rule;
	if (language < 0 || language >= (int)countof(tr_codes)) return false;
	count = CL_Translate_MenuSelections(players, CL_TRANSLATE_MAX_SELECTIONS);
	for (i = 0; i < count; i++)
		if (players[i].slot == slot && expected_name && !strcmp(players[i].name, expected_name) &&
			(players[i].selectable || (slot == -1 && players[i].remembered)) && players[i].language >= 0) break;
	if (i == count)
	{ TR_MenuNotice("The player selection changed. Select the player again."); return false; }
	if (slot == -1)
	{
		/* Saved names are already readable keys; do not dequake them again. */
		if (!TR_SaveNamedRule(expected_name, remember ? language : -1))
		{ TR_MenuNotice("Could not save the translation rule."); return false; }
		TR_MenuNotice(remember ? "Saved translation rule updated." : "Saved translation rule removed.");
		return true;
	}
	rule = TR_RuleForName(expected_name);
	/* Save both preferences together before changing the active watch. A disk
	 * failure leaves the current language and remembered rule intact. */
	if ((remember && (rule < 0 || tr_rules[rule].language != language)) || (!remember && rule >= 0))
		if (!TR_SaveRule(expected_name, remember ? language : -1))
		{ TR_MenuNotice("Could not save the translation rule."); return false; }
	return TR_SetWatch(slot, expected_name, language, false);
}

qboolean CL_Translate_MenuRemovePlayer (int slot, const char *expected_name)
{
	if (slot >= 0) return CL_Translate_MenuWatch(slot, expected_name, -1);
	return CL_Translate_MenuEditPlayer(slot, expected_name, 0, false);
}

static void TR_ApplyRules (void)
{
	cl_translate_player_t players[MAX_SCOREBOARD];
	char notice[sizeof(tr_menu_notice)];
	int i, count = CL_Translate_MenuPlayers(players, MAX_SCOREBOARD);
	q_strlcpy(notice, tr_menu_notice, sizeof(notice));
	/* Synchronize identities before any catch-up is queued, just as @everyone
	 * does, so a later stale slot cannot cancel earlier restored watches. */
	for (i = 0; i < count; i++)
		if (TR_RuleForName(players[i].name) >= 0 && players[i].selectable)
			CL_Translate_PlayerChanged(players[i].slot, players[i].name);
	for (i = 0; i < count; i++)
	{
		int rule = TR_RuleForName(players[i].name);
		if (rule >= 0 && players[i].selectable && players[i].language < 0)
			TR_SetWatch(players[i].slot, players[i].name, tr_rules[rule].language, true);
	}
	q_strlcpy(tr_menu_notice, notice, sizeof(tr_menu_notice));
	tr_rules_dirty = false;
}

static qboolean TR_WatchEveryone (int language, qboolean only_new)
{
	cl_translate_player_t players[MAX_SCOREBOARD];
	int i, count, eligible = 0, changed = 0, watched = 0;
	qboolean retarget = false;
	char message[160];
	const char *unavailable = CL_Translate_UnavailableReason();
	if (language < 0 || language >= (int)countof(tr_codes)) return false;
	if (unavailable) { TR_MenuNotice(unavailable); return false; }
	count = CL_Translate_MenuPlayers(players, MAX_SCOREBOARD);
	for (i = 0; i < count; i++)
		if (players[i].selectable && (!only_new || players[i].language < 0))
		{
			eligible++;
			if (players[i].language != language) changed++;
			if (players[i].language >= 0 && players[i].language != language) retarget = true;
		}
	if (!eligible) { TR_MenuNotice("No eligible players to translate."); return false; }
	if (!changed)
	{
		q_snprintf(tr_menu_notice, sizeof(tr_menu_notice), "Already translating %d player%s into %s.",
			eligible, eligible == 1 ? "" : "s", tr_languages[language]);
		return true;
	}
	/* Revoke stale identities before queuing any player's new catch-up work. */
	for (i = 0; i < count; i++)
		if (players[i].selectable && (!only_new || players[i].language < 0))
			CL_Translate_PlayerChanged(players[i].slot, players[i].name);
	if (retarget) TR_ClearWork();
	for (i = 0; i < count; i++)
		if (players[i].selectable && (!only_new || players[i].language < 0))
		{
			TR_SetWatch(players[i].slot, players[i].name, language, true);
			if (tr_watch[players[i].slot] == language + 1 && !strcmp(tr_names[players[i].slot], players[i].name)) watched++;
		}
	if (only_new)
		q_snprintf(message, sizeof(message), "Added %d player%s for translation to %s.",
			watched, watched == 1 ? "" : "s", tr_languages[language]);
	else
		q_snprintf(message, sizeof(message), "Translating %d player%s public chat to %s.",
			watched, watched == 1 ? "'s" : "s'", tr_languages[language]);
	TR_MenuNotice(message);
	Con_Printf("Stop: translate off\n");
	return watched > 0;
}

qboolean CL_Translate_MenuWatchEveryone (int language)
{
	return TR_WatchEveryone(language, true);
}

static qboolean TR_SaveEveryone (int language, qboolean only_new)
{
	cl_translate_player_t players[MAX_SCOREBOARD];
	tr_rule_t previous[TR_RULES];
	int i, count, saved = tr_rule_count, added = 0;
	if (!TR_Live() || language < 0 || language >= (int)countof(tr_codes)) return false;
	count = CL_Translate_MenuPlayers(players, MAX_SCOREBOARD);
	memcpy(previous, tr_rules, sizeof(previous));
	for (i = 0; i < count; i++)
		if (players[i].selectable && (!only_new || players[i].language < 0))
		{
			added++;
			if (!TR_UpdateRule(players[i].name, language)) break;
		}
	if (added && (i != count || !TR_WriteRules()))
	{
		memcpy(tr_rules, previous, sizeof(previous)); tr_rule_count = saved;
		TR_MenuNotice("Could not save the translation rules."); return false;
	}
	tr_rules_dirty = true;
	return TR_WatchEveryone(language, only_new);
}

qboolean CL_Translate_MenuWatchEveryoneRemembered (int language)
{
	return TR_SaveEveryone(language, true);
}

void CL_Translate_MenuStopAll (void)
{
	int saved = tr_rule_count;
	TR_ClearWork(); memset(tr_watch, 0, sizeof(tr_watch));
	tr_rules_paused = true;
	if (saved)
	{
		tr_rule_count = 0;
		if (!TR_WriteRules())
		{
			tr_rule_count = saved;
			TR_MenuNotice("Translation stopped; saved rules could not be removed."); return;
		}
	}
	TR_MenuNotice("Translation stopped.");
}

void CL_Translate_MenuReadLanguage (int language)
{
	if (language < 0 || language >= (int)countof(tr_codes)) return;
	tr_read_language = language;
	q_snprintf(tr_menu_notice, sizeof(tr_menu_notice), "New players will use %s.", tr_languages[language]);
}

void CL_Translate_MenuWriteLanguage (int language)
{
	if (language < 0 || language >= (int)countof(tr_codes)) return;
	tr_write_language = language;
	q_snprintf(tr_menu_notice, sizeof(tr_menu_notice), "Writing chat in %s.", tr_languages[language]);
}

static qboolean TR_QueueSend (int language, const char *text, qboolean team)
{
	tr_job_t job;
	const char *unavailable = CL_Translate_UnavailableReason();
	if (unavailable) { TR_Notice(unavailable, true); return false; }
	if (language < 0 || language >= (int)countof(tr_codes) || !text) return false;
	if (tr_outgoing) { TR_MenuNotice("A translated message is already pending."); return false; }
	if (SDL_GetTicks() < tr_backoff_until) { TR_Notice("service is temporarily paused.", true); return false; }
	memset(&job, 0, sizeof(job));
	job.slot = -1; job.language = language; job.outgoing = true; job.team = team; job.priority = 2;
	job.deadline = SDL_GetTicks() + TR_LIFETIME;
	if (!TR_FilteredBody(job.original, text, strlen(text)) || TR_Trivial(job.original))
	{ TR_Notice("enter a readable message of at most 512 characters.", true); return false; }
	tr_outgoing = TR_Enqueue(&job, NULL, NULL);
	if (!tr_outgoing) { TR_Notice("could not queue your message.", true); return false; }
	tr_send_id = job.id;
	tr_send_state = TRANSLATE_SEND_PENDING;
	q_snprintf(tr_send_notice, sizeof(tr_send_notice), team ? "Translating to %s for team..." : "Translating to %s...", tr_languages[language]);
	return true;
}

qboolean CL_Translate_MenuSend (int language, const char *text)
{
	return TR_QueueSend(language, text, false);
}

qboolean CL_Translate_Console (const char *line)
{
	const char *p = line;
	char language[24];
	size_t n;
	int target = -1, slot;
	qboolean save = false;
	while (*p == ' ' || *p == '\t') p++;
	if (q_strncasecmp(p, "translate", 9) || (p[9] && p[9] != ' ' && p[9] != '\t')) return false;
	p += 9;
	if (!tr_initialized || SDL_GetAtomicInt(&tr_stopping)) return true;
	if (strchr(p, '\n') || strchr(p, '\r')) { Con_Printf("Translation accepts one console line.\n"); return true; }
	while (*p == ' ' || *p == '\t') p++;
	if (!*p) { TR_Status(); return true; }
	if (*p == '@') target = 0; /* Shorthand always means English, independent of menu preferences. */
	else
	{
		n = strcspn(p, " \t");
		if (n >= sizeof(language)) { Con_Printf("Unknown language. Press Tab to choose.\n"); return true; }
		memcpy(language, p, n); language[n] = 0; p += n;
		while (*p == ' ' || *p == '\t') p++;
		if (!q_strcasecmp(language, "help"))
		{
			if (!*p) TR_Help(); else Con_Printf("Use translate help.\n");
			return true;
		}
		if (!q_strcasecmp(language, "off"))
		{
			if (!*p || (*p == '@' && TR_Everyone(p + 1)))
			{
				CL_Translate_MenuStopAll(); return true;
			}
			if (*p != '@')
			{ Con_Printf("Use translate off @player, or translate off.\n"); return true; }
			if (!TR_Live()) { Con_Printf("%s\n", CL_Translate_UnavailableReason()); return true; }
			slot = TR_FindPlayer(p+1);
			if (slot < 0) { TR_PlayerMatchError(p+1, slot); return true; }
			CL_Translate_MenuWatch(slot, cl.scores[slot].name, -1); return true;
		}
		if (!q_strcasecmp(language, "save")) { save = true; target = 0; }
		else target = TR_Language(language);
		if (target < 0) { Con_Printf("Unknown language. Press Tab to choose.\n"); return true; }
	}
	if (!save && !q_strncasecmp(p, "save", 4) && (!p[4] || p[4] == ' ' || p[4] == '\t'))
	{
		const char *next = p + 4;
		while (*next == ' ' || *next == '\t') next++;
		/* A prose message such as "save me a spot" remains outgoing text. */
		if (!*next || *next == '@') { save = true; p = next; }
	}
	if (save && (*p != '@' || !p[1]))
	{ Con_Printf("Use translate save @player, or translate <language> save @player.\n"); return true; }
	if (!TR_Live()) { Con_Printf("%s\n", CL_Translate_UnavailableReason()); return true; }
	if (!*p) { TR_Status(); return true; }
	if (*p == '@')
	{
		if (TR_Everyone(p + 1))
		{
			if (save) TR_SaveEveryone(target, false);
			else TR_WatchEveryone(target, false);
			return true;
		}
		slot = TR_FindPlayer(p + 1);
		if (slot < 0) { TR_PlayerMatchError(p+1, slot); return true; }
		if (slot == TR_LocalSlot()) { Con_Printf("Use translate <language> <message> to translate what you type.\n"); return true; }
		if (TR_Ignored(slot, cl.scores[slot].name)) { Con_Printf("That player is ignored.\n"); return true; }
		if (save) CL_Translate_MenuWatchSaved(slot, cl.scores[slot].name, target);
		else CL_Translate_MenuWatch(slot, cl.scores[slot].name, target);
	}
	else
	{
		TR_QueueSend(target, p, ctrlpressed);
	}
	return true;
}

const char *CL_Translate_CompletionStart (const char *line)
{
	const char *p = line;
	while (*p == ' ' || *p == '\t') p++;
	if (q_strncasecmp(p, "translate", 9) || (p[9] != ' ' && p[9] != '\t')) return NULL;
	p += 9;
	while (*p == ' ' || *p == '\t') p++;
	if (*p == '@') return p;
	p += strcspn(p, " \t");
	while (*p == ' ' || *p == '\t') p++;
	if (!q_strncasecmp(p, "save", 4) && (p[4] == ' ' || p[4] == '\t'))
	{
		p += 4;
		while (*p == ' ' || *p == '\t') p++;
	}
	return *p == '@' ? p : NULL;
}

static void TR_Completion (const char *partial)
{
	cl_translate_player_t players[MAX_SCOREBOARD];
	char readable_partial[MAXCMDLINE];
	int i, count;
	int player_arg = 2;
	qboolean shorthand = Cmd_Argc() >= 2 && Cmd_Argv(1)[0] == '@';
	if (Cmd_Argc() == 2 && !shorthand)
	{
		for (i = 0; i < (int)countof(tr_languages); i++)
		{
			char language[24];
			size_t j;
			q_strlcpy(language, tr_languages[i], sizeof(language));
			for (j = 0; language[j]; j++) language[j] = (char)tolower((unsigned char)language[j]);
			Con_AddToTabList(language, partial, NULL, NULL);
		}
		Con_AddToTabList("off", partial, NULL, NULL);
		Con_AddToTabList("help", partial, NULL, NULL);
		Con_AddToTabList("save", partial, NULL, NULL);
		return;
	}
	if (!shorthand && TR_Language(Cmd_Argv(1)) >= 0)
	{
		if (Cmd_Argc() == 3) Con_AddToTabList("save", partial, NULL, NULL);
		if (!q_strcasecmp(Cmd_Argv(2), "save")) player_arg = 3;
	}
	if (!TR_Live() || (!shorthand && (Cmd_Argc() <= player_arg ||
		(Cmd_Argv(player_arg)[0] && Cmd_Argv(player_arg)[0] != '@')))) return;
	/* The @ prefix is inserted with the name. Existing completion takes the
	 * entire match, so multiword names need no hand-entered quoting. Use the same
	 * eligibility as menu activation; completion must never offer a dead end. */
	count = CL_Translate_MenuPlayers(players, MAX_SCOREBOARD);
	/* Keep @, spaces and digits literal in the shared console Tab matcher. */
	if (!TR_Readable(readable_partial, sizeof(readable_partial), partial, strlen(partial))) return;
	for (i = 0; i < count; i++)
		if (players[i].selectable) { Con_AddToTabList("@everyone", readable_partial, NULL, NULL); break; }
	for (i = 0; i < count; i++)
	{
		char name[MAX_SCOREBOARDNAME + 4];
		if (!players[i].selectable) continue;
		q_snprintf(name, sizeof(name), q_strcasecmp(players[i].display, "everyone") ? "@%s" : "@\"%s\"", players[i].display);
		Con_AddToTabList(name, readable_partial, NULL, NULL);
	}
}

void CL_Translate_Init (void)
{
	cmd_function_t *command;
	tr_initialized = true;
	tr_filter = (int)cl_contentfilter.value;
	SDL_SetAtomicInt(&tr_generation, 1);
	SDL_SetAtomicInt(&tr_stopping, 0);
	{
		char directory[MAX_OSPATH];
		const char *root = host_parms && host_parms->userdir && host_parms->userdir != host_parms->basedir ?
			host_parms->userdir : com_basedir;
		tr_rules_path[0] = 0;
		if (root && *root && (size_t)q_snprintf(directory, sizeof(directory), "%s/id1", root) < sizeof(directory))
		{
			Sys_mkdir(directory);
			if ((size_t)q_snprintf(tr_rules_path, sizeof(tr_rules_path), "%s/chat_translate_rules.txt", directory) >= sizeof(tr_rules_path)) tr_rules_path[0] = 0;
		}
		TR_LoadRules();
		tr_rules_dirty = true;
		tr_rules_paused = false;
	}
#ifdef QSSM_TRANSLATE_TEST
	/* No runtime endpoint setting exists in release builds. Test builds accept
	 * only local HTTP, never a server-controlled or remote arbitrary URL. */
	{
		const char *url = getenv("QSSM_TRANSLATE_TEST_URL");
		if (url && (!strncmp(url, "http://127.0.0.1:", 17) || !strncmp(url, "http://localhost:", 17))) tr_endpoint = url;
	}
#endif
	Cmd_AddCommand("translate", TR_Command);
	/* Startup registration returns NULL; look up the registered command. */
	command = Cmd_FindCommand("translate");
	if (command) command->completion = TR_Completion;
}

void CL_Translate_Shutdown (void)
{
	SDL_SetAtomicInt(&tr_stopping, 1);
	TR_ClearWork();
	if (tr_condition) SDL_BroadcastCondition(tr_condition);
	if (tr_thread) SDL_WaitThread(tr_thread, NULL);
	tr_thread = NULL;
	if (tr_condition) SDL_DestroyCondition(tr_condition);
	if (tr_mutex) SDL_DestroyMutex(tr_mutex);
	tr_condition = NULL; tr_mutex = NULL;
	tr_initialized = false;
}
