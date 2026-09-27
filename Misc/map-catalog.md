Map download title catalog

The Map Downloads menu displays and searches the embedded `worldspawn.message`
titles in `misc/qw_map_titles.json`. The runtime catalog is bundled in `qssm.pak`.
Opening the menu does not download descriptions. Maps without a known title
remain available by filename. The installed Levels menu retains its local BSP
titles and `mapdesc.json` cache.

The initial bundled snapshot covers 6,644 filenames: 5,866 titles, 740 confirmed
empty titles, 26 unsupported BSP formats, and 12 unresolved extractions. Those
last records have malformed/out-of-bounds entity data or exceed the extraction
limit; they remain downloadable by filename and are retried on future scans.

**Generate an update**

From the repository root, using Node.js 22 or newer:

```sh
node .github/scripts/update-map-catalog.js --output /tmp/qssm-map-catalog --max-minutes 135
```

The generator processes **ten maps per batch**, with two maps in flight and a
global request ceiling of four starts per second. It requests only the 124-byte
BSP header and enough entity bytes to parse worldspawn (initially up to 4 KiB,
at most 64 KiB). Map bytes exist only briefly in bounded memory buffers; no BSP
files are written. Each batch saves JSON progress. Disk use is the checkpoint,
candidate JSON/text files, and reports, not thousands of downloaded maps.

Incremental runs revalidate BSPs with their HTTP ETag or Last-Modified value.
Unchanged records reuse the saved title, including confirmed empty titles.
New/changed maps are extracted. HTTP validators are best-effort change detection;
use `--reextract-all` for an audit that rereads every title. The extractor supports
BSP29, both engine-supported BSP2 versions, and Quake64 headers.
Changing the extractor version also forces every title to be read again, even
when the server's validators are unchanged.

```sh
node .github/scripts/update-map-catalog.js --output /tmp/qssm-map-catalog --resume --max-minutes 135
node .github/scripts/update-map-catalog.js --output /tmp/qssm-map-audit --reextract-all --max-minutes 135
```

Resume requires the same source listing, baseline, extractor version and full
re-extraction setting. A changed listing/baseline needs a fresh output directory.
An interrupted run resumes completed batches; a soft time limit or `--limit N`
produces an incomplete report and exits with status 2. Only a completed scan
produces candidate files and an eligible notification. Use `--limit 30` with a
fresh output directory for a small pilot, then resume without the limit.
Partial reports distinguish pending maps from failed extractions. Reusing an
output directory clears its previous candidates and notification payload before
starting; a failed run writes an incomplete report. `--resume` preserves its
checkpoint, while a fresh run starts a new checkpoint.

**Review and apply**

The output directory contains:

| Output | Purpose |
| --- | --- |
| `Misc/qssm_pak/misc/qw_maps.txt` | Compatible filename snapshot, including its capture timestamp. |
| `Misc/qssm_pak/misc/qw_map_titles.json` | Runtime titles, sorted and joined by case-insensitive BSP filename. |
| `.github/map-catalog-snapshot.json` | Monitoring baseline, validators, extraction status, and catalog digest. |
| `report.json`, `report.md` | Additions, removals, changed titles, coverage, errors and transfer statistics. |
| `checkpoint.json` | Resumable progress; never package this file. |
| `updates.json` | Existing dependency notifier's update record. |

Review removals and extraction failures before applying the candidate. A failed
fetch retains the previous title and marks the record unresolved. A fully parsed
worldspawn without a message is `no_title`; that differs from an error. An
unsupported BSP has no catalog title. Missing source files are checked against a
second listing, and losing more than 5% of names stops generation for inspection.

HTML URL escapes are decoded into actual filenames before lookup and storage.
The engine encodes those filenames when requesting maps. The source currently
has a case alias (`testmapB.bsp` / `testmapb.bsp`); like the engine's map list, the
catalog keeps one case-insensitive entry, choosing the bytewise first spelling
deterministically. Such aliases are recorded in `report.json` for review.

Apply the three candidate files together to their matching repository paths.
Do not copy the checkpoint/report into `Misc/qssm_pak`. Ensure the baseline has
not changed since the scan; its digest is recorded in the checkpoint. Then run:

```sh
node .github/scripts/update-map-catalog.js --validate
node --test .github/scripts/*.test.js
python3 Misc/stress/test_download_map_titles.py
python3 Misc/stress/test_map_categories.py
bash Misc/qssm_pak/build.sh
```

The Python tests require a compiler and SDL3 headers; set `PKG_CONFIG_PATH` to
your SDL3 installation if necessary. The PAK builder uses `git ls-files`, so new
catalog files must be tracked before building. Existing platform release jobs
build and include `qssm.pak`. Ordinary builds use the reviewed bundled catalog
and do not contact the map source to regenerate it.

**Scheduled maintenance and delivery**

The existing `dependency-watch.yml` runs catalog generation monthly and on
manual dispatch. The `reextract_maps` manual input enables a full audit. The
separate scan job uploads the candidate and checkpoint even after an incomplete
run. It uses the same notification/issue deduplication module as the existing
dependency checks, with a content digest as the revision. Repeating an unchanged
candidate does not create another issue. Daily package checks retain their cadence.

Artifacts are retained for 90 days subject to repository limits; rerun the
generator if an artifact expires. Reviewed files committed to the repository are
the durable baseline. The workflow does not commit or merge updates itself.
The independent scan does not suppress DLL/controller update notifications when
the map source fails. PR CI validates the catalog offline and tests the generator
with local HTTP fixtures; Linux CI also runs the production C regression harness.

New titles reach users through the next installed QSS-M package. The client's
existing remote filename refresh can discover a map earlier; that entry displays
its filename until its title is bundled. The normal filename refresh interval
remains 180 days. `qwmaplist reload` requests a filename refresh and reloads local
title metadata; it does not fetch a new title catalog. `qwmaplist info` reports
title availability and coverage for the loaded filename list.

On upgrade, the engine compares valid `# refreshed=` timestamps and uses the
newer packaged/cached filename snapshot. Legacy cached files use their file
modification time. If timestamps cannot be compared, the existing cache takes
precedence. Titles always join by filename, so differing catalog/list snapshots
are safe. Invalid metadata falls back to filename-only display and downloads.

To roll back a catalog update, revert its filename list, title catalog and
monitoring snapshot together and rebuild `qssm.pak`.
