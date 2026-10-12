# QSS-M chat translation

The client implementation is `Quake/cl_translate.c`. It contains no provider key.
The separate service owns the key, validates requests, limits upstream attempts,
and returns only a translation or a bounded outcome. No live key is needed for
the deterministic tests or mock service.

## Player commands

Open **Options > Console > Chat Translation** to see whose chat you are translating
including players added in the console. The menu follows the Bookmarks layout:
a scrolling list of names, with the selected player's language centered in the
tooltip below. The player list starts after a blank line below **Send Translated
Message**. Each player has their own target: use left/right for a quick language
change. Enter, clicking their row, or **e:edit** opens a Bookmarks-style card with
their name, language, and **Remember by name** checkbox. **Accept Changes** applies
both choices; Escape discards the edits. Use the footer's **backspace:remove** shortcut or press
Delete on that player's row to stop translating them.
**Add Translated Player(s)** opens a chooser containing only available players you have not
added. Pick **Translate to**, then Enter or click a name to add it and return to
the menu. **Remember by name** defaults to **No**. Choose **Yes** to save the
player's language and restore translation when their full name appears in later
games, including after restarting QSS-M. Matching is case-sensitive, ignores
Quake name colors, and still excludes your own name, ignored players, and
ambiguous names. Names identify the rule; they do not authenticate a person.
Saved names remain in the main menu while disconnected or absent from the current
server. Their language, Remember checkbox, and removal controls work offline;
editing those preferences makes no translation request. Add Player and Send
remain disabled outside multiplayer.
Changing a saved player's language updates their rule. Removing the player
forgets it. **Remove All** and `translate off` also forget all saved rules.
Rules live in `id1/chat_translate_rules.txt` under the engine's user directory,
independent of the current mod; configs, aliases, and server commands cannot
create them.

**@everyone (translate all players)** appears below the names after a blank line
and adds all remaining eligible players with that language,
preserving the targets of players already selected. This choice applies to new
players only. With **Remember by name: Yes**, it saves those particular names;
later arrivals are not automatically included. **Add Translated Player(s)** dims and becomes
inactive when no eligible players are available. **Remove All** clears the selection.
Both lists use the Bookmarks navigation: arrows, Home/End, Page Up/Down, mouse
wheel, a draggable scrollbar, and typing a name's initial. Long selected names
scroll within their row. Player names keep the scoreboard's original glyphs and
per-character colors, with the same content filter. Self, ignored, ambiguous and
unreadable names are omitted from the chooser.

**Send Translated Message** opens the public-message composer, with its own
**Translate to** language choice. It dims and becomes inactive outside a supported
multiplayer game, including single-player games and demos. The empty message
field shows a dimmed placeholder; entered text uses normal opacity.
The list, chooser, and composer share the Bookmarks title and divider. Footer
shortcuts add a player, edit their language, or remove them. Hold Ctrl (Command on macOS) for these keyboard
shortcuts, or click their footer labels. Escape cancels a chooser or returns
from the composer. There is one Chat Translation menu, with
no separate live-chat mode.
Opening, drawing, hovering, and editing a draft never upload anything. The
composer sends only when you activate Send or press Enter in the message field;
it keeps drafts on failure/cancellation and clears them after a successful send.

Reading defaults to English and writing to Spanish. The menu remembers these
choices for the current run; there are no translation language cvars or archived
preferences. Changing a player's language leaves everyone else's target intact.
Player selections last for the current game unless explicitly remembered by name.

Menu and Tab choices are English (`en`), Spanish (`es`), Portuguese (`pt`),
French (`fr`), German (`de`), Italian (`it`), Dutch (`nl`), Polish (`pl`),
Swedish (`sv`), Norwegian (`no`), Danish (`da`), Finnish (`fi`), Czech (`cs`),
Romanian (`ro`), Hungarian (`hu`), Turkish (`tr`), Indonesian (`id`), Malay (`ms`),
Vietnamese (`vi`), Filipino (`fil`), Croatian (`hr`), and Slovak (`sk`). The service
requests plain Latin letters and ASCII punctuation; accents are approximated
for the existing Quake font.

```text
translate @Rasta
translate save @Rasta
translate spanish save @Rasta
translate @everyone
translate save @everyone
translate spanish @everyone
translate spanish Anyone up for 2v2 on dm2?
translate off @Rasta
translate off
translate
translate help
```

`translate @Rasta` follows that player's chat in English, regardless of the menu's
reading preference. Select a language explicitly with `translate english @Rasta`
or `translate spanish @Rasta`. `@player` means listen; ordinary text after a
language means translate and send public chat. Add **save** before `@player` to
remember the rule across games and restarts: `translate save @Rasta` defaults to
English, and `translate spanish save @Rasta` saves Spanish. `translate save
@everyone` (or `translate portuguese save @everyone`) saves the currently eligible
names, including players already selected, using that target. Later arrivals
are not selected automatically. Tab completes **save** and then the player name.
Without **save**, a new watch lasts for the current connection; changing an
already remembered player's language also updates their existing rule.
Hold Ctrl while submitting the
console command to send its translation as team chat; Command also works on
macOS. The destination is captured at submission, so releasing or pressing the
modifier while translation is pending cannot change it. Watching `@player`
continues to follow public chat, and the menu composer sends public chat.
Type `tran` and press Tab to
complete the command; type `translate @` and press Tab to choose a player.
Tab completes languages and selectable player names through the console's existing
completion system, including multiword names. It omits your own name, ignored
players, and names that cannot be identified uniquely. Full language names and
the listed codes are accepted, without regard to case.
Missing players and ambiguous matches produce distinct errors and never become outgoing chat.
`translate @everyone` follows every currently eligible player in English;
`translate spanish @everyone` applies Spanish to all of them. These exclude your
own name, ignored players, ambiguous names and unsupported chat formats. Later
arrivals are not selected automatically; repeat the command to include them.
Tab offers `@everyone`, and `translate off @everyone` is equivalent to
`translate off`. To select a player literally named everyone, quote the name:
`translate @"everyone"`.
Quotes are optional around the entire player name. Repeating a watch command
keeps it enabled without repeating catch-up; use `translate off` to stop.

Starting a watch prints one acknowledgment and a stop shortcut:

```text
Translating Rasta's public chat to English.
Stop: translate off @Rasta
```

`translate help` shows command examples and short descriptions. It highlights
commands, separates entries with blank lines, and omits sentence-ending periods.
It is available while disconnected and does not start translation.

Watch activation also says `Checking 2 recent messages.` (using the actual count
queued, at most three), or `Listening for new chat.` when no eligible history
needs checking. Trivial game tokens, expired lines, and messages already handled
in the selected language are excluded from the count. If eligible recent checks
cannot be queued, the acknowledgment says so. The menu shows the same feedback.

Watching catches up with up to three messages from that player from the last
minute, oldest first, then follows new messages. Earlier results carry an
`[EN earlier]`, `[PT earlier]`, etc. label. Already handled messages are not repeated.
New chat takes priority over catch-up; explicitly typed outgoing messages take
priority over both. All requests expire five seconds after submission (live
incoming jobs expire five seconds after the original arrived). Catch-up is best
effort within that lifetime and the service allowance.

Original chat is displayed immediately. Translations appear as separate
attributed console/notify lines without another chat sound. Output uses the
engine's tested Unicode-to-Quake fallback; accents become readable approximations.
Labels use the uppercase target code: `[EN]`, `[PT]`, etc. for new chat,
`[EN earlier]` for catch-up. Only the tag uses the console's masked gold text;
the player name and translation stay unmasked. Outgoing messages appear through the normal server
echo, with no extra console confirmation. Progress and success feedback stay in
the menu; unchanged repeat-watch commands stay quiet.
Oversized outgoing translations are refused instead of silently truncated.
Failures do not send the original automatically. The typed command remains in
console history for retry, according to the player's existing history setting.

Watches are local and off by default. They survive a map change on the same
connection when scoreboard identity remains unchanged, and end on disconnect,
demo playback or a player identity change. Explicitly saved name rules restore
eligible watches when those names appear again. Ignore/filter changes discard local
history and queued work. Incoming team, direct, unknown-format, demo and CSQC-owned chat
are excluded. The public-chat pilot recognizes complete `\001name: text\n` lines
with a unique scoreboard match; this is a display heuristic, not authenticated
sender identity. A public name colliding with team syntax is rejected.

New upload authorization can only come from a directly submitted local console
line or an actual local menu activation. Remembered name rules retain that
explicit authorization across games. Aliases,
binds, `exec`, startup scripts, stufftext and QC console interception cannot
start watches or outgoing translation. There are no upload-enabling cvars or
runtime endpoint settings. Settings/aliases cannot grant that privilege. A whole
translation line is consumed as data, so semicolons in its outgoing body never
execute another local console command.

## Data handling

To support catch-up, the client keeps at most six eligible messages per current
player in memory, even before activation. It does not create a transcript file
for translation. Nothing in that buffer is uploaded until a watch is started.
Each incoming request includes only the selected player's current message and
up to three preceding messages from that same player no older than 20 seconds.
Outgoing requests contain only the explicitly typed message and identify its
public/team destination. Team sends require the local console modifier action;
they never upload team-chat history. No other players'
messages, display prefixes, roster, server address, hostname, demo data or map
files are uploaded. Names mentioned inside chat bodies remain part of the text.

Selected chat goes through timbergeron.com to OpenRouter and its selected model
hosting provider. The proxy sees the client's
IP and uses it for rate limits. The application has no body logging, transcript
database, cache or public request inspector. Requests enforce OpenRouter's
`data_collection: deny` and `zdr: true` provider filters. These select endpoints
according to their published data policies; they do not erase billing metadata or
replace checking the provider's terms. Keep OpenRouter's optional prompt/response
logging disabled for the account. Configure reverse-proxy logs separately
without request bodies and publish the hosting site's short privacy notice.

## Run locally

From the repository root, create a private virtual environment and install the
one HTTP dependency:

```sh
python3 -m venv .codex-build/chat-translate/venv
.codex-build/chat-translate/venv/bin/pip install -r Misc/chat_translate/requirements.txt
mkdir -p .codex-build/chat-translate/state
.codex-build/chat-translate/venv/bin/python Misc/chat_translate/server.py \
  --mock --counter .codex-build/chat-translate/state/allowance.sqlite3
```

The mock recognizes `corpus.json` fixtures and returns uncertain for other text.
It is a contract/demo tool, not a general translator. `--mock-delay` exercises
expiry and cancellation. The service listens on loopback port 8787 by default.

Production clients use only `https://timbergeron.com/qssm/translate`, verified TLS,
and no redirects. A deliberately compiled `QSSM_TRANSLATE_TEST` build alone
accepts `QSSM_TRANSLATE_TEST_URL=http://127.0.0.1:8787/qssm/translate`. This is not
available in release builds. No API key belongs in a client environment/config.

## Tests and corpus

See [the October 12 release review](RELEASE_REVIEW.md) for the final audit,
corrected regressions, live provider rate-limit blocker, and remaining native
platform/pilot checks.

```sh
python3 Misc/chat_translate/build_corpus.py
bash Misc/chat_translate/run_tests.sh
```

Use `bash Misc/chat_translate/run_tests.sh --sanitize` for ASan/UBSan checks.

The standard-library core tests never call an inference provider. Installing `requirements.txt`
also enables HTTP adapter and provider-wire tests. The client harness compiles
the production translation module and menu input/draw logic with SDL/libcurl
against a loopback mock and engine stubs. It exercises parser, attribution,
context, catch-up, send, expiry, ignore/reset/identity changes and cancellation,
plus menu/console synchronization, passive navigation, roster changes, scrolling,
draft retention and layout bounds without a renderer or game files. Saved-rule
checks cover menu opt-in, restart/slot/color matching, duplicates and ignored
players, rule edits/removal, failed writes, and malformed local preference files.

The synthetic corpus includes English controls, Spanish/Portuguese slang,
mixed-language text, unknown text, embedded instructions and exact game tokens.
Expected translations illustrate meaning; they are not an exact-wording quality
oracle. No live quality/latency claim has been established. Before deployment,
review live-model results by humans, compare context/no-context replies, and
record false translations, unchanged errors, token preservation and p95 latency.

Linux validation on October 11, 2026 used SDL 3.4.2, libcurl 8.18.0, Python 3.14,
and aiohttp 3.14.5. The new module also cross-compiles with MinGW for Windows
32-bit and 64-bit; its existing common-header macro-redefinition warnings remain.
Windows/macOS build projects include the module. Full Windows/macOS binaries and
their DNS-cancellation behavior still need validation on their own platforms;
no macOS SDK/Xcode toolchain is available in this environment.
`bash Misc/chat_translate/build_linux.sh -j4` builds using the current host's SDL3
installation and ignores dependency files left by retired temporary SDK prefixes.

The October 12 language, per-player menu, and team-send update passed 26 service tests and
a client/menu harness with 35 loopback mock requests under ASan/UBSan, plus
console-completion and Unicode regressions. The Linux client linked successfully.
Actual offscreen captures verified the main menu and composer with existing game
assets and synthetic SDL typing while disconnected. The matching service was
deployed with a rollback backup; synthetic Portuguese incoming and Vietnamese
outgoing requests succeeded with ASCII output and preserved `2v2`/`dm2` tokens.
The outgoing-team contract was also deployed and verified with synthetic Spanish
text; incoming team requests still return HTTP 400. The client checks cover
releasing/pressing the modifier after submission, unchanged team/public sends,
and failure without publishing a team message publicly.
These checks do not replace native Windows/macOS runtime or multilingual quality
evaluation.

The Everyone update passed the client/menu harness under ASan/UBSan with 38 mock
requests and a Linux build. It covers exclusions, quoted keyword collisions,
repeat commands without re-upload, batch retargeting/catch-up, new arrivals,
and both keyboard/mouse bulk addition without replacing individual targets.

## Contract

POST `application/json` to `/qssm/translate`, at most 4096 encoded UTF-8 bytes:

```json
{"version":1,"id":42,"target":"en","mode":"incoming","channel":"public","context":[{"speaker":"p1","text":"cambiamos mapa?"}],"message":{"speaker":"p1","text":"dale"}}
```

`mode` is `incoming` or `outgoing`; outgoing context must be empty. Incoming
`channel` must be `public`; outgoing accepts `public` or `team`. Only the 22
target codes listed above are accepted. At most three context lines;
512 UTF-8 bytes per text. Speaker aliases are `p1` through `p4`. Duplicate keys,
unknown fields, invalid UTF-8/NULs and unexpected types are rejected. IDs are
integral unsigned 32-bit values and are echoed by the service, never the model.

HTTP 200 outcomes:

```json
{"version":1,"id":42,"status":"translated","text":"Sure."}
{"version":1,"id":42,"status":"unchanged"}
{"version":1,"id":42,"status":"skipped","reason":"uncertain"}
```

Only `translated` has display text, at most 1024 UTF-8 bytes. Skip reasons are
`uncertain`, `blocked` or `invalid_output`. The client caps all responses at 8 KiB
and rejects extra/duplicate fields, mismatched IDs, invalid numbers and NULs.
Errors are bounded `{"error":"code"}` objects: 400 invalid schema/target/version,
413 body too large, 415 media type, 429 rate/daily limit, and 503 unavailable,
disabled, capacity, counter or upstream failure. Responses use `no-store`.
Retry-After is delta-seconds. No individual request is retried. The client drops
queued work during backoff and waits for fresh messages afterward.
Provider HTTP 429 uses `503 upstream_rate_limit`, with the provider's bounded
delta-seconds delay (5–300 seconds) or 60 seconds when it supplies no valid delay.
This is distinct from the service's per-IP/daily allowance, and never retries a
line or weakens provider privacy/routing requirements.

## Hosting and operator runbook

The production host is the existing Vultr VPS, which also runs Nullius and QSS-M
wiki. Translation has its own `qssm-chat-translate.service`, code/venv in
`/opt/qssm-chat-translate`, loopback port 8793, private environment in
`/etc/qssm-chat-translate/environment`, and durable state in
`/var/lib/qssm-chat-translate`. The nginx route already forwards the HTTPS endpoint
to that port. Nullius/wiki services and their credentials remain separate.

Run one instance behind the site's HTTPS routing, with an OpenRouter key in the
host's private secret store or environment. OpenRouter credits are paid; the
request cap below is independent of the account balance and any key spending
limit. `server.py` reads `OPENROUTER_API_KEY` only on the server. Never commit a populated environment
file, place the key in a URL, return provider errors, or log headers/payloads.
Credentials are sent in the upstream `Authorization: Bearer` header only. An old
`GEMINI_API_KEY` setting is ignored; there is no Google fallback.

Install your translation key through the hidden prompt, from your own terminal:

```sh
ssh -t vultr 'sudo python3 /opt/qssm-chat-translate/install_openrouter_key.py'
```

The installer accepts no key argument, refuses a noninteractive prompt, saves the
environment atomically with mode 0600, preserves other settings, and restarts only
translation. The disable file must remain present throughout installation. No key
is displayed or placed in shell history. If the deployment has no OpenRouter key
yet, it can start while disabled; removing the disable file without a key still
cannot dispatch upstream traffic. Verify the supplied key with synthetic text
before enabling player traffic.

The server uses OpenRouter's stateless Chat Completions endpoint with
[structured JSON output](https://openrouter.ai/docs/guides/features/structured-outputs).
The configured candidate is
[deepseek/deepseek-v4.1-flash](https://openrouter.ai/deepseek/deepseek-v4.1-flash),
prompt `qssm-chat-1`, reasoning disabled, output ceiling 512 tokens. These values
are centralized in `service.py`; no client can select them. Routing requires
parameter support, no data collection and zero data retention, selects for
latency, and disables provider fallbacks.

BaseTen is excluded with `provider.ignore: ["baseten"]` following observed
provider HTTP 429s; other eligible hosts remain selected by latency. This setting
is owned by the service and cannot be supplied by a game client. OpenRouter's
[provider-routing documentation](https://openrouter.ai/docs/guides/routing/provider-selection#ignoring-providers)
defines this exclusion, including matching all endpoints for the base slug.
Aggregate metrics record the configured exclusion for deployment verification.

If no matching endpoint is available, the request fails rather than weakening
those settings. Truncated, malformed, refused or tool-bearing outputs are
skipped without another call. Any
model/prompt changes require the same evaluation corpus. Recheck availability,
parameters, pricing and provider terms before the pilot, and record deployed
versions/results. Full-corpus translation quality and latency under player load
have not yet been established.

On October 12, 2026 the supplied OpenRouter key was verified, six synthetic
service checks passed in 251–320 ms, and the public pilot was enabled. The HTTPS
route returned an attributed-contract translation with HTTP 200, while the main
website stayed HTTP 200. Checks covered incoming/outgoing Spanish, an English
control, a contextual reply, nonsense, and an embedded instruction. Reported
reasoning-token usage was zero. These are smoke-test observations, not a
corpus-wide quality score or a latency guarantee. The 1000-request daily cap
remains active and its counter was preserved during the provider switch.

Initial pilot bounds: 30 requests/minute/IP, burst 10; 1000 durable upstream
reservations per UTC day; four upstream calls; a three-second total upstream
deadline; no server-side waiting queue. Reservations survive errors, cancellations,
restarts and deployments. Keep the SQLite database on a persistent private volume;
do not recreate it on deployment. A counter failure refuses upstream access.
Multiple independent volumes/instances are unsupported; an eventual serverless
adapter needs its own shared durable atomic counter.

Set `--disabled-file /private/state/translation.disabled`. Creating that file
disables new upstream dispatches without restarting. Removing it restores service
subject to quota and client backoff. For key rotation, disable translation first,
use the hidden installer, verify privately, and then restore service without
removing the counter. Aggregate metrics print once per minute and contain
outcomes, bounded latency samples, token counts and model/prompt versions only.
Estimate spending from measured tokens and current prices before raising the daily
request cap; it is a request ceiling, not a dollar ceiling.

Bind to loopback behind the proxy. Configure the exact proxy IP with
`--trusted-proxy 127.0.0.1/32` only if that is the actual local proxy. The proxy must
replace/append forwarding headers correctly; the adapter walks only trusted hops.
Example nginx location (adapt paths and routing in the website repository):

```nginx
location = /qssm/translate {
    client_max_body_size 4k;
    proxy_pass http://127.0.0.1:8787;
    proxy_set_header X-Forwarded-For $remote_addr;
    proxy_read_timeout 5s;
    proxy_cache off;
}
```

Ensure CDN/proxy caching is bypassed and error pages do not include request data.
The public endpoint has no player authentication; an embedded game token would
not provide that. The durable cap bounds paid upstream attempts even when callers
bypass the game. Shared-IP and chat-burst behavior still need a real pilot.
