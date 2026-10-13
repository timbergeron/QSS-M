"""Exercise real alert snapshots/payloads without sending Discord webhooks.

Guards against repeated cards, hidden context changes, and stale follow-ups.
Run with: python3 Misc/stress/test_discord_followups.py
"""
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'Quake/discord.c').read_text()


def function(name):
    start = re.search(r'^static [^\n]+ ' + name + r'\([^;]*?\n\{', text, re.M).start()
    return text[start:text.index('\n}', start) + 2]


types_start = text.index('typedef enum\n{\n\tDISCORD_ALERT_MENTION,')
types_end = text.index('} discord_followup_context_t;', types_start) + len('} discord_followup_context_t;')
writer_start = text.index('typedef struct\n{\n\tchar *buffer;', types_end)
writer_end = text.index('} discord_alert_json_writer_t;', writer_start) + len('} discord_alert_json_writer_t;')
send_end = text.index('} discord_alert_send_result_t;', writer_end) + len('} discord_alert_send_result_t;')
constants = '\n'.join(re.findall(r'^#define DISCORD_(?:PAYLOAD_MAX|WEBHOOK_URL_MAX|ALERT_QUEUE_MAX|ALERT_MAPSHOT_WAIT|ALERT_FOLLOWUP_WINDOW) .+$', text, re.M))
source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#define MAX_SCOREBOARDNAME 32
#define MAX_QPATH 64
#include "strl_fn.h"
#define q_snprintf snprintf
#define q_strncasecmp strncasecmp
typedef bool qboolean;
static double realtime;
static unsigned char dequake[256];
''' + constants + '\n' + text[types_start:types_end] + '\n' + text[writer_start:send_end] + r'''
static discord_followup_context_t discord_followup_context;
static discord_alert_t discord_alert_queue[DISCORD_ALERT_QUEUE_MAX];
static int discord_alert_queue_count;
#define DISCORD_TEST_TRANSPORT_ERROR 1
#define DiscordTest_SetResult(status, code) ((void)0)
#define Con_DWarning(...) ((void)0)
static qboolean VID_HasMouseOrInputFocus(void) { return false; }
static void DiscordNotify_ReportFailure(void) {}
static int send_attempt;
static discord_alert_send_result_t DiscordAlert_StartJob(const discord_alert_t *alert) {
    (void)alert;
    return send_attempt++ == 0 ? DISCORD_ALERT_SEND_DROPPED : DISCORD_ALERT_SEND_RETRY;
}
static const char *server, *map, *mode;
static void DiscordAlert_CaptureContext(discord_alert_t *alert) {
    q_strlcpy(alert->server, server, sizeof(alert->server));
    q_strlcpy(alert->map, map, sizeof(alert->map));
    q_strlcpy(alert->mode, mode, sizeof(alert->mode));
    q_strlcpy(alert->timestamp, "2026-10-13T21:18:00Z", sizeof(alert->timestamp));
}
static qboolean Mapshot_ResolveURL(const char *name, const char *game, char *out, size_t size) {
    (void)name; (void)game;
    q_strlcpy(out, "https://example.com/map.webp", size);
    return true;
}
''' + '\n'.join(function(name) for name in (
    'DiscordAlert_CleanSpan', 'DiscordAlert_CleanText', 'DiscordAlert_EscapeMarkdown',
    'DiscordAlert_ParsePlayerChat', 'DiscordAlert_ParseDirectMessage',
    'DiscordAlert_ResetFollowup', 'DiscordAlert_IsFollowup',
    'DiscordAlert_RecordFollowupContext', 'DiscordAlert_PrepareFullCard',
    'DiscordAlert_BuildSnapshot', 'DiscordAlertJSON_Init',
    'DiscordAlertJSON_AppendBytes', 'DiscordAlertJSON_Append',
    'DiscordAlertJSON_AppendString', 'DiscordAlertJSON_AppendField',
    'DiscordAlert_BuildPayload', 'DiscordAlert_RemoveFirst',
    'DiscordAlert_PromoteFirstCompact', 'DiscordAlert_Enqueue', 'DiscordAlerts_Frame')) + r'''
static void emit(double now, discord_alert_kind_t kind, const char *sender,
    const char *body, const char *new_server, const char *new_map, const char *new_mode) {
    discord_alert_t alert;
    char raw[1200], payload[DISCORD_PAYLOAD_MAX];
    const char *chat_body = NULL;
    realtime = now; server = new_server; map = new_map; mode = new_mode;
    if (kind == DISCORD_ALERT_MENTION) {
        snprintf(raw, sizeof(raw), "%s: %s", sender, body);
        chat_body = strstr(raw, ": ") + 2;
    } else if (kind == DISCORD_ALERT_DIRECT_MESSAGE) {
        snprintf(raw, sizeof(raw), "dm [%s]: %s", sender, body);
    } else {
        snprintf(raw, sizeof(raw), "%s", body);
    }
    DiscordAlert_BuildSnapshot(&alert, raw, chat_body, kind, kind == DISCORD_ALERT_TEST);
    if (!DiscordAlert_BuildPayload(&alert, payload, sizeof(payload))) abort();
    puts(payload);
    char tiny[8];
    if (DiscordAlert_BuildPayload(&alert, tiny, sizeof(tiny))) abort();
}
int main(void) {
    for (int i = 0; i < 256; ++i) dequake[i] = (unsigned char)i;
    emit(100, DISCORD_ALERT_MENTION, "st3lla . lef7y", "woods stop breaking quake", "quake.shmack.net:26000", "e1m2", "Deathmatch");
    emit(280, DISCORD_ALERT_MENTION, "st3lla . lef7y", "woods is anti america", "quake.shmack.net:26000", "dm6", "Deathmatch");
    emit(290, DISCORD_ALERT_MENTION, "another player", "woods hello", "quake.shmack.net:26000", "dm6", "Deathmatch");
    emit(300, DISCORD_ALERT_MENTION, "another player", "woods mode", "quake.shmack.net:26000", "dm6", "CTF");
    emit(301, DISCORD_ALERT_MENTION, "another player", "woods both", "quake.shmack.net:26000", "e1m2", "Deathmatch");
    emit(302, DISCORD_ALERT_MENTION, "another player", "woods unchanged", "quake.shmack.net:26000", "e1m2", "Deathmatch");
    emit(602, DISCORD_ALERT_MENTION, "another player", "woods boundary", "quake.shmack.net:26000", "e1m2", "Deathmatch");
    emit(902.1, DISCORD_ALERT_MENTION, "another player", "woods expired", "quake.shmack.net:26000", "e1m2", "Deathmatch");
    emit(903, DISCORD_ALERT_MENTION, "another player", "woods switched", "new server", "e1m2", "Deathmatch");
    emit(904, DISCORD_ALERT_TEST, "QSS-M", "test", "test server", "testmap", "CTF");
    emit(905, DISCORD_ALERT_MENTION, "another player", "woods after test", "new server", "e1m2", "Deathmatch");
    emit(906, DISCORD_ALERT_DIRECT_MESSAGE, "private sender", "secret", "new server", "e1m2", "Deathmatch");
    DiscordAlert_ResetFollowup();
    emit(907, DISCORD_ALERT_MENTION, "another player", "woods resumed", "new server", "e1m2", "Deathmatch");
    emit(1, DISCORD_ALERT_MENTION, "another player", "woods clock reset", "new server", "e1m2", "Deathmatch");
    emit(2, DISCORD_ALERT_MENTION, "another player", "woods escaped *text* \"quote\"", "new server", "map_*", "mode_*");
    emit(3, DISCORD_ALERT_MENTION, "another player", "woods missing context", "", "", "");
    emit(4, DISCORD_ALERT_MENTION, "another player", "woods still missing context", "", "", "");
    DiscordAlert_ResetFollowup();
    discord_alert_t alert;
    char payload[DISCORD_PAYLOAD_MAX];
    const char raw[] = "player: woods overflow";
    server = "queue server"; map = "e1m2"; mode = "Deathmatch"; realtime = 10;
    DiscordAlert_BuildSnapshot(&alert, raw, raw + 8, DISCORD_ALERT_MENTION, false);
    map = "dm6"; mode = "CTF";
    for (int i = 0; i <= DISCORD_ALERT_QUEUE_MAX; ++i) {
        realtime = 11 + i;
        DiscordAlert_BuildSnapshot(&alert, raw, raw + 8, DISCORD_ALERT_MENTION, false);
        DiscordAlert_Enqueue(&alert);
    }
    if (!DiscordAlert_BuildPayload(&discord_alert_queue[0], payload, sizeof(payload))) abort();
    puts(payload);
    discord_alert_queue_count = 0;
    map = "e1m2"; mode = "Deathmatch"; realtime = 20;
    DiscordAlert_BuildSnapshot(&alert, raw, raw + 8, DISCORD_ALERT_MENTION, false);
    DiscordAlert_Enqueue(&alert);
    DiscordAlerts_Frame(); /* Lose the only alert carrying the context change. */
    realtime = 21;
    DiscordAlert_BuildSnapshot(&alert, raw, raw + 8, DISCORD_ALERT_MENTION, false);
    if (!DiscordAlert_BuildPayload(&alert, payload, sizeof(payload))) abort();
    puts(payload);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='qssm-discord-followups-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run([*compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'Quake'),
                    'test.c', str(ROOT / 'Quake/strlcpy.c'), str(ROOT / 'Quake/strlcat.c'),
                    '-o', 'test'], cwd=work, check=True)
    output = subprocess.check_output([str(work / 'test')], text=True)

payloads = [json.loads(line) for line in output.splitlines()]
assert len(payloads) == 19
embeds = [payload['embeds'][0] for payload in payloads]
full_cards = {0, 7, 8, 9, 12, 13, 15, 16, 17, 18}
for index, embed in enumerate(embeds):
    if index in full_cards:
        assert 'footer' in embed, ('expected full card', index, embed)
    else:
        assert not {'fields', 'thumbnail', 'footer', 'timestamp'} & embed.keys(), ('expected compact follow-up', index, embed)
assert embeds[1]['author']['name'] == 'st3lla . lef7y'
assert embeds[1]['description'] == 'woods is anti america\n\n*Map changed to dm6*'
assert embeds[2]['author']['name'] == 'another player'
assert embeds[2]['description'] == 'woods hello'
assert embeds[3]['description'] == 'woods mode\n\n*Mode changed to CTF*'
assert embeds[4]['description'] == 'woods both\n\n*Map changed to e1m2*\n*Mode changed to Deathmatch*'
assert embeds[5]['description'] == 'woods unchanged'
assert embeds[11]['author']['name'] == 'private sender - direct message'
assert embeds[14]['description'] == 'woods escaped \\*text\\* "quote"\n\n*Map changed to map\\_\\**\n*Mode changed to mode\\_\\**'
assert payloads[14]['allowed_mentions'] == {'parse': []}
assert {field['name']: field['value'] for field in embeds[17]['fields']} == {
    'Server': 'queue server', 'Map': 'dm6', 'Mode': 'CTF'}
assert {field['name']: field['value'] for field in embeds[18]['fields']} == {
    'Server': 'queue server', 'Map': 'e1m2', 'Mode': 'Deathmatch'}
print('PASS: 19 alert payloads; rolling five-minute window, context changes, senders, DMs, resets, escaping, bounds, queue overflow and send failure')
