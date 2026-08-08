# QSS-M Aura for Apple Watch

Aura is an independent watchOS app that connects directly to QSS-M over the
local network. It does not require an iPhone companion app.

## Setup

1. Open `watchOS/aura.xcodeproj`, choose your development team under
   **Signing & Capabilities**, and run the `aura` scheme on a paired Watch.
2. Start QSS-M and run `qwatch_pair`. This enables Aura and prints a single-use
   six-digit code that remains valid for five minutes.
3. Aura searches for QSS-M automatically. When it finds the Mac, enter only
   the six-digit code and select **Pair & Connect**.
4. If automatic discovery fails, choose **Enter Address Manually** and enter
   the Mac's LAN address. The default port is `27999`. The Watch Simulator can
   reach QSS-M on the same Mac at `127.0.0.1`.

Future launches reconnect automatically. Aura remembers up to three Macs and
stores a separate bearer token for each one in the Watch Keychain. Tap the
ambient color screen to reveal connection status and Settings for three
seconds. An authentication failure removes the stale credential and returns
to pairing instead of retrying indefinitely.

## Protocol and privacy

QSS-M advertises `_qssm-aura._tcp` with Bonjour and publishes the stable local
alias `qssm-aura.local`. The engine exposes only a bounded HTTP endpoint for
discovery, one-time pairing, and a read-only chunked Aura stream:

* `0`: black/off
* `1`: Pentagram/red
* `2`: Quad/blue
* `3`: both/purple

Pairing codes and bearer tokens use operating-system secure randomness. Codes
are single-use, expire after five minutes, and lock after repeated failures.
The engine stores its token in the active game directory as `qwatch.token`
with owner-only permissions on Unix-like systems. `qwatch_status` never prints
the bearer token.

Aura collects no analytics, tracking information, telemetry, timers, text, or
entity data. Traffic is intentionally limited to the local network, but it is
plain HTTP rather than TLS. Pair only on a trusted LAN; a device capable of
observing local traffic could read the Aura state or bearer credential.
