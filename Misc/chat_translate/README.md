# QSS-M chat translation

The client implementation is `Quake/cl_translate.c`. It contains no provider key.
The separate service owns the key, validates requests, limits upstream attempts,
and returns only a translation or a bounded outcome. No live key is needed for
the deterministic tests or mock service.

## Player commands

Open **Options > Console > Chat Translation** to select players, choose reading
and writing languages, or open the public-message composer. Enter or click a
player to start/stop following them; arrows on player rows only navigate. The
player list scrolls and shows each watch's actual target, including watches
created in the console. Ignored or ambiguous names are visibly unavailable.
Opening, drawing, hovering, and editing a draft never upload anything. The
composer sends only when you activate Send or press Enter in the message field;
it keeps drafts on failure/cancellation and clears them after a successful send.

Reading defaults to English and writing to Spanish. Language preferences are
archived in `cl_translate_read_language` and `cl_translate_write_language`;
changing these cvars alone cannot start or retarget a watch or send a message.
Changing the reading language through the menu updates selected players for new
chat. Different targets set in the console appear as `mixed` until you choose a
common reading language. Player selections are never archived.

```text
translate @Rasta
translate spanish Anyone up for 2v2 on dm2?
translate off @Rasta
translate off
translate
translate help
```

`translate @Rasta` follows that player's chat in English, regardless of the menu's
reading preference. Select a language explicitly with `translate english @Rasta`
or `translate spanish @Rasta`. `@player` means listen; ordinary text after a
language means translate and send public chat. Type `translate @` and press Tab
to choose a player. Tab completes languages and selectable player names,
including multiword names; it omits your own name, ignored players, and names
that cannot be identified uniquely. `en` and `es` are accepted. Missing players
and ambiguous matches produce distinct errors and never become outgoing chat.
Quotes are optional around the entire player name. Repeating a watch command
keeps it enabled without repeating catch-up; use `translate off` to stop.

Starting a watch prints one acknowledgment and a stop shortcut:

```text
Translating Rasta's public chat to English.
Stop: translate off @Rasta
```

The first watch or outgoing send in a connection displays one shared disclosure
about timbergeron.com, OpenRouter, the model host, and recent context. Changing
maps, targets, or selected players does not repeat it. `translate help` explains
what is uploaded, including catch-up/context limits and public outgoing chat;
help is available while disconnected and does not start translation.

Watch activation also says `Checking 2 recent messages.` (using the actual count
queued, at most three), or `Listening for new chat.` when no eligible history
needs checking. Trivial game tokens, expired lines, and messages already handled
in the selected language are excluded from the count. If eligible recent checks
cannot be queued, the acknowledgment says so. The menu shows the same feedback.

Watching catches up with up to three messages from that player from the last
minute, oldest first, then follows new messages. Earlier results carry an
`[EN earlier]` or `[ES earlier]` label. Already handled messages are not repeated.
New chat takes priority over catch-up; explicitly typed outgoing messages take
priority over both. All requests expire five seconds after submission (live
incoming jobs expire five seconds after the original arrived). Catch-up is best
effort within that lifetime and the service allowance.

Original chat is displayed immediately. Translations appear as separate
attributed console/notify lines without another chat sound. Output uses the
engine's tested Unicode-to-Quake fallback; accents become readable approximations.
Labels are `[EN]` / `[ES]` for new chat, `[EN earlier]` / `[ES earlier]` for
catch-up, and `[Sent EN]` / `[Sent ES]` for outgoing confirmations. A confirmation
does not repeat the translated message body: the normal server echo displays it.
Outgoing progress stays in the menu; unchanged repeat-watch commands stay quiet.
Oversized outgoing translations are refused instead of silently truncated.
Failures do not send the original automatically. The typed command remains in
console history for retry, according to the player's existing history setting.

Watches are local and off by default. They survive a map change on the same
connection when scoreboard identity remains unchanged, and end on disconnect,
demo playback or a player identity change. Ignore/filter changes discard local
history and queued work. Team, direct, unknown-format, demo and CSQC-owned chat
are excluded. The public-chat pilot recognizes complete `\001name: text\n` lines
with a unique scoreboard match; this is a display heuristic, not authenticated
sender identity. A public name colliding with team syntax is rejected.

Uploads can only start through a directly submitted local console line or an
actual local menu activation. Aliases,
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
Outgoing requests contain only the explicitly typed message. No other players'
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
draft retention and layout bounds without a renderer or game files.

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

The October 12 menu update passed the 23 service tests and a client/menu harness
with 23 loopback mock requests under ASan/UBSan, plus console-completion and
Unicode regressions. The Linux client linked successfully. Actual offscreen
client captures verified the menu and composer with existing game assets and
synthetic SDL typing while disconnected. Updated `menu.c` and `cl_translate.c`
also cross-compile for Windows 64-bit; this does not replace native Windows/macOS
runtime validation.

## Contract

POST `application/json` to `/qssm/translate`, at most 4096 encoded UTF-8 bytes:

```json
{"version":1,"id":42,"target":"en","mode":"incoming","channel":"public","context":[{"speaker":"p1","text":"cambiamos mapa?"}],"message":{"speaker":"p1","text":"dale"}}
```

`mode` is `incoming` or `outgoing`; outgoing context must be empty. Only `public`
and the implemented `en`/`es` targets are accepted. At most three context lines;
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
