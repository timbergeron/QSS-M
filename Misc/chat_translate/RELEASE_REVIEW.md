# Translation release review — October 12, 2026

Current decision: the proxy routing correction is deployed and passes public
incoming, outgoing, and already-English smoke checks. A full client release
still needs native-platform runtime checks and a multilingual multiplayer pilot.

Initial OpenRouter checks received HTTP 429 despite a valid, funded paid-tier
key. BaseTen was identified as the failing provider and is now excluded, with
the same model and privacy requirements. See the deployment results below.

## Corrections

- Catch-up context now uses the three messages preceding the particular line
  being translated. Previously a full six-line ring anchored context at the
  newest line, losing context for earlier catch-up messages.
- Request IDs remain unique across disconnects and map changes. Previously a
  canceled menu send could share an ID with a new console send and incorrectly
  clear the retained menu draft. Wrap still invalidates jobs before ID reuse.
- Catch-up acknowledgments exclude jobs replaced by another job in the same
  activation batch. Previously a busy queue could announce three checks with
  only one remaining.
- Provider HTTP 429 gets the dedicated `upstream_rate_limit` outcome and bounded
  backoff (5–300 seconds; 60-second default). Client feedback distinguishes this
  from its own per-IP and daily limits. This correction is deployed. No line
  retries or privacy changes were added; the separate routing exclusion is
  described below.

Each of the three client bugs was reproduced by a failing regression before
its fix. The regressions exercise production client/menu code with the real
SDL worker and libcurl against a loopback mock.

## Verification

| Check | Result |
| --- | --- |
| Service, contract, provider-wire, counter, installer tests | 24 passed |
| Client/menu harness with ASan and UBSan | Passed; 28 mock requests |
| Full-ring context, reconnect draft retention, busy-queue count | Passed |
| Truncated JSON at every byte boundary, Unicode fallback, completion | Passed |
| Opt-in, exclusions, generation cancellation, expiry, no duplicate echo | Passed |
| Linux client build | Passed |
| Final translation module, Windows 32-bit and 64-bit compilation | Passed; existing common-header macro warnings |
| Production binary key/test-endpoint markers | None present |
| VPS service | Active, runs as `qssm-translate`; deployed source matches the corrected proxy with BaseTen excluded |
| Credential / durable quota file permissions | 0600; state directory 0700 |
| nginx route | HTTPS, loopback upstream, 4 KiB body cap, no caching/body access log, bounded timeouts, source-IP override |
| Public invalid schema / wrong media / oversized body | 400 / 415 / 413 with bounded JSON and `no-store` |
| Main website | HTTP 200 |
| Live translation | Initial BaseTen HTTP 429 resolved by exclusion; all three public smoke checks passed |

No player chat was used in live verification. Operator diagnostics print only
credential/account booleans and bounded error categories, never credentials or
raw provider errors. The follow-up deployment restarted only translation;
credentials and the durable quota database were preserved.

## Remaining release gates

1. Verify native Windows/macOS builds, HTTPS certificate handling, and prompt
   cancellation/shutdown during HTTP and DNS failures. Cross-compilation does
   not establish those runtime behaviors; no macOS SDK is available here.
2. Run a real multilingual multiplayer pilot on the supported public-chat
   format and review translation usefulness and latency under player load.
   Deterministic mock tests do not establish model accuracy or a production
   latency guarantee.

These results establish the checks above, not an absence of all possible bugs.

## VPS deployment and provider investigation follow-up

The reviewed `server.py` 429/backoff correction is now deployed to
`/opt/qssm-chat-translate/server.py`, byte-identical to the source checkout.
The 24 service tests passed before deployment. The service was restarted and
verified active on loopback port 8793. A synthetic outgoing request through
`https://timbergeron.com/qssm/translate` returned HTTP 503 with the bounded
`upstream_rate_limit` error, `Retry-After: 60`, and `Cache-Control: no-store`.
The website root continued to return HTTP 200.

Authenticated diagnostics confirmed the installed OpenRouter key is paid-tier,
has a positive account balance, an available key allowance, and no exhausted
reported budget. One request with the production model and provider policy
returned upstream HTTP 429, identifying BaseTen in provider error metadata.
This identifies the failing upstream but does not establish BaseTen's internal
reason for throttling.

A separate synthetic diagnostic excluded BaseTen for that request only, keeping
`deepseek/deepseek-v4.1-flash`, `data_collection: deny`, `zdr: true`,
`require_parameters: true`, and `allow_fallbacks: false`. OpenRouter selected
Together and returned HTTP 200 in 265 ms; `Cambiamos a dm2?` translated to
`Switch to dm2?`, passed output validation, and preserved the map token.
Both diagnostic upstream attempts reserved the existing durable daily counter.
No player chat or raw provider errors were logged.

At the end of that diagnostic, production routing remained unchanged. The
deployment below applies the tested exclusion and resolves that routing gate.

## BaseTen exclusion deployment and public verification

Deployed October 12, 2026 at 03:56 UTC. The fixed service configuration adds
`provider.ignore: ["baseten"]`, using the canonical slug confirmed by
[OpenRouter's provider metadata](https://openrouter.ai/api/v1/providers) and
[provider-routing documentation](https://openrouter.ai/docs/guides/routing/provider-selection#ignoring-providers).
The fixed model, retention/data-collection filters, parameter requirements,
disabled fallbacks, deadlines, and allowance limits remain the same.
Aggregate metrics now include `ignored_providers` for operator verification.

The 24 service tests and sanitizer client/menu harness passed before deployment.
A guarded installer verified old/new source hashes, backed up only proxy code,
validated the installed routing policy, and restarted only translation. Deployed
`server.py` and `service.py` are byte-identical to the tested checkout. Rollback
code is in `/root/qssm-chat-translate-backups/before-baseten-exclusion-20261012T035616Z`.

Public endpoint results (synthetic data only):

| Check | Result | End-to-end time |
| --- | --- | --- |
| Spanish incoming chat -> English | HTTP 200, translated; `2v2` and `dm2` preserved | 482 ms |
| English outgoing chat -> Spanish | HTTP 200, translated; `2v2` and `dm2` preserved | 496 ms |
| Already-English incoming control | HTTP 200, unchanged | 347 ms |
| Invalid schema / wrong media / oversized body | 400 / 415 / 413, bounded JSON and `no-store` | Passed |
| Main website | HTTP 200 | Passed |

The service is active and the kill switch is absent. No individual request was
retried. These smoke checks establish working deployed routing, not model
accuracy or latency guarantees under player load. Native runtime validation and
the multilingual multiplayer pilot remain release gates.
