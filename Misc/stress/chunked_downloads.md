# Chunked download pacing

`sv_downloadrate` limits the server's total chunked download traffic in KiB/s.
The default is `1024`, shared equally between active chunked downloads. Set it
in the server configuration, for example `sv_downloadrate 256` for a smaller
upload allowance. The setting is archived. Zero or negative values remove the
rate limit; the ceiling of 32 chunks per client per tick still applies. At
72 Hz that permits `32 × 1029 × 72 = 2,370,816` bytes/s per client (about
2.3 MiB/s), before transport overhead. Positive values, including fractions
below one, are clamped to 1–65536 KiB/s.

| Cvar | Default | Flags | Description |
| --- | --- | --- | --- |
| `sv_downloadrate` | `1024` | Archive | Total chunked download KiB/s, shared between active downloads; <= 0 unlimited; positive values clamp to 1–65536. |

The accounting includes chunk data and its download message header; network
transport overhead is additional. It does not limit gameplay, HTTP downloads,
or the older non-chunked download path. The token bucket retains at most 100 ms
of credit, bounded between one and 32 chunks, so stalls cannot accumulate a
large burst. An idle downloader's unused share is not redistributed. Chunked
traffic does not consult a client's `rate` setting; its limits are the server
allowance and the download request pacer.

The client starts with eight outstanding requests and grows its window up to
128, matching the server queue capacity. Loss reduces that window, at most
once per recovery interval. Retransmitted chunks cannot grow the window or
supply RTT samples. Growth also pauses when smoothed RTT exceeds twice the
aged minimum RTT estimate. That estimate rises toward smoothed RTT with a
ten-second time constant and drops immediately on a lower sample. This lets
old low samples lose influence when the path changes; adaptation depends on
elapsed time, including gaps between files, rather than the number of ACKs.
After slow start, growth deliberately allows four chunks
per RTT; the one-chunk increase roughly halved throughput in the lossy-transfer
simulation (171 versus 360 KiB/s at 100 ms RTT).
Smoothed RTT, its variation, and minimum RTT persist between files on the same
connection; each file still starts with a small window.

Requests are paced using elapsed time and measured response latency. Both the
receive and send paths service the pacer; frames still determine when it can
run. Up to 250 ms can accrue per call, capped at 32 requests of credit. Retries
back off to at most two seconds between attempts.
These are bounded heuristics, not a guarantee of optimal throughput or latency
on every connection.

If pending requests make no credited progress for five seconds, and unreliable
requests have never delivered credited data on this connection, the client
switches `nextdl` to the reliable stream. It batches up to eight requests per
message, with at most eight outstanding, to keep NQ's stop-and-wait reliable
stream from limiting transfers to one chunk per round trip. This accommodates
peers that reject unreliable string commands. Once unreliable requests have
worked, later stalls cannot select the fallback, even on a subsequent file.
Both that successful-path flag and the fallback persist across files and
cancellation, and reset on disconnect. An outage before any credited data can
still select the fallback. A peer that rejects both request paths still cannot
complete a download.

## File transitions

Updated servers send `//cl_downloadsequence <sequence> "<filename>"` in a
`svc_stufftext` immediately before each successful chunked start, within the
same reliable message. The sequence is the next outgoing unreliable packet
number. Updated clients reject earlier datagrams for that file, including
late old-file duplicates which would otherwise pass NQ's normal sequence check.
The marker is accepted only for the pending filename, before its file is open.

Older clients ignore the optional comment; older servers omit it. The ordinary
1024-byte chunk format is unchanged. Protection against old-file replies
requires support at both ends; legacy peers retain the previous behavior.
Disconnecting now removes an incomplete chunked download's temporary file.

## Verification

Run the deterministic scheduler, file I/O and server allowance tests with:

```sh
python3 Misc/stress/test_chunked_download.py
```

This compiles the production routines with AddressSanitizer and UBSan. It
checks bounded requests, retry timing, duplicate/reordered data, truncated
chunks, exact file contents, server queue bounds, and sharing the rate limit
between four clients. It also covers sequence-floor wraparound, missing sockets,
demo guards, connection RTT retention and aging, delay-based growth limits,
low-FPS credit, batched reliable requests, outage discrimination, and
reliable-stream backpressure. The slow-server loss case guards against the first
fixed-window optimization's throughput regression. Printed transfer timings
are simulations, not live network benchmarks.

Run the production Linux client through a local UDP peer with:

```sh
python3 Misc/stress/test_chunked_download_udp.py \
  --bin Quake/quakespasm --pak /path/to/id1/pak0.pak
```

This needs software OpenGL and a pak0 containing `maps/e1m1.bsp`; stress hooks
and registered assets are unnecessary. It covers request/reply loss, packet
reordering and duplication, consecutive files, cancellation, reconnects,
temporary-file cleanup, and old servers without the optional sequence marker.
Reliable-only peer cases verify fallback, its retention across files and
cancellation, and its reset on reconnect. A 100 ms reliable-ACK delay checks
batching over the stop-and-wait stream. A six-second outage at the start of the
second file checks that a previously successful unreliable path stays selected.
It checks the complete downloaded files byte for byte. The peer emulates the
server protocol; this is not a live FTE/ezQuake interoperability or multiplayer
server-load test.

For an older client build, `--mode oldclient` offers the new optional marker
without injecting old-file data that the older client cannot reject. This also
passed using the original download client implementation with current ABI
headers; it does not substitute for testing every released engine version.
