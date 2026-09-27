#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { SOURCE, EXTRACTOR, BATCH_SIZE, CATALOG_PATH, LIST_PATH, SNAPSHOT_PATH, digest,
  parseList, Transport, extractMap, catalogFor, validateSnapshot } = require("./map-catalog.js");

function readJSON(file, fallback) {
  try { return JSON.parse(fs.readFileSync(file, "utf8")); }
  catch (e) { if (e.code === "ENOENT") return fallback; throw e; }
}
function atomicWrite(file, contents) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  try { fs.writeFileSync(`${file}.tmp`, contents); fs.renameSync(`${file}.tmp`, file); }
  finally { fs.rmSync(`${file}.tmp`, { force: true }); }
}
const json = value => `${JSON.stringify(value, null, 2)}\n`;

async function generate({ root, output, resume = false, force = false, limit = Infinity,
  minutes = 75, transport = new Transport(), onBatch = console.log }) {
  const started = Date.now(), errors = [];
  const checkpointPath = path.join(output, "checkpoint.json");
  // Refuse aliases of the baseline directory before removing old candidates.
  if (path.resolve(root) === path.resolve(output) ||
      (fs.existsSync(output) && fs.realpathSync(root) === fs.realpathSync(output))) {
    transport.close();
    throw new Error("Output directory must differ from the repository root");
  }
  try {
    // A repeated failed/incomplete run must not advertise an earlier success.
    for (const file of [CATALOG_PATH, LIST_PATH, SNAPSHOT_PATH, "report.json", "report.md"])
      fs.rmSync(path.join(output, file), { force: true });
    atomicWrite(path.join(output, "updates.json"), "[]\n");
    if (!resume) fs.rmSync(checkpointPath, { force: true });

    const old = readJSON(path.join(root, SNAPSHOT_PATH), null);
    const oldListText = fs.readFileSync(path.join(root, LIST_PATH), "utf8");
    const oldNames = parseList(oldListText);
    if (old) {
      validateSnapshot(old, readJSON(path.join(root, CATALOG_PATH)), oldNames, { allowExtractorMismatch: true });
      if (old.extractor !== EXTRACTOR) force = true;
    }
    const oldByName = new Map((old?.maps || []).map(r => [r.name.toLowerCase(), r]));
    const names = await transport.listing();
    const removed = oldNames.filter(n => !names.some(m => m.toLowerCase() === n.toLowerCase()));
    if (removed.length > oldNames.length * 0.05) throw new Error("Map listing lost over 5% of names; inspect source before updating");
    if (removed.length && digest(names) !== digest(await transport.listing()))
      throw new Error("Map listing changed while checking removals");
    const prior = resume ? readJSON(checkpointPath, null) : null;
    if (prior && (prior.schema !== 1 || !Array.isArray(prior.records) ||
      prior.source !== SOURCE || prior.extractor !== EXTRACTOR ||
      prior.listingDigest !== digest(names) || prior.force !== force || prior.baseDigest !== digest(old)))
      throw new Error("Checkpoint source/list/baseline/extractor changed; use a new output directory");
    errors.push(...(prior?.errors || []));
    const refreshed = prior?.refreshed || Math.floor(Date.now() / 1000);
    const completed = new Map((prior?.records || []).map(r => [r.name, r]));
    let attempted = 0, consecutiveFailures = 0;
    for (let offset = 0; offset < names.length; offset += BATCH_SIZE) {
      if (Date.now() - started > minutes * 60000 || attempted >= limit) break;
      const batch = names.slice(offset, offset + BATCH_SIZE).filter(n => !completed.has(n)).slice(0, limit - attempted);
      if (!batch.length) continue;
      let next = 0;
      // Two requests/maps in flight, ten per batch. Only metadata survives a batch.
      await Promise.all(Array.from({ length: 2 }, async () => {
        while (next < batch.length) {
          const name = batch[next++], before = oldByName.get(name.toLowerCase());
          attempted++;
          try {
            completed.set(name, await extractMap(transport, name, before, force));
            consecutiveFailures = 0;
          } catch (e) {
            errors.push({ name, error: e.message }); consecutiveFailures++;
            completed.set(name, { ...(before || {}), name, status: "unresolved" });
          }
        }
      }));
      const records = names.filter(n => completed.has(n)).map(n => completed.get(n));
      atomicWrite(checkpointPath, json({ schema: 1, source: SOURCE, extractor: EXTRACTOR,
        baseDigest: digest(old), listingDigest: digest(names), force, refreshed, records, errors }));
      if (batch.length) onBatch(`${records.length}/${names.length} maps; ${errors.length} errors; ${transport.bytes} response bytes; no BSP files stored`);
      if (consecutiveFailures >= 20) throw new Error("20 consecutive extraction failures; checkpoint retained");
    }
    const complete = completed.size === names.length;
    const records = names.map(name => completed.get(name) || { ...(oldByName.get(name.toLowerCase()) || {}), name, status: "unresolved" });
    const catalog = catalogFor(records);
    const snapshot = { schema: 1, source: SOURCE, extractor: EXTRACTOR, catalogDigest: digest(catalog), maps: records };
    validateSnapshot(snapshot, catalog, names);
    const unchangedNames = JSON.stringify(names) === JSON.stringify(oldNames);
    const listText = unchangedNames && /^# refreshed=\d+$/m.test(oldListText) ? oldListText :
      `# qssm-qw-maplist-v1\n# source=${SOURCE}\n# refreshed=${refreshed}\n# count=${names.length}\n${names.join("\n")}\n`;
    const added = names.filter(n => !oldNames.some(m => m.toLowerCase() === n.toLowerCase()));
    const changed = records.filter(r => oldByName.has(r.name.toLowerCase()) && r.title !== oldByName.get(r.name.toLowerCase()).title)
      .map(r => ({ name: r.name, before: oldByName.get(r.name.toLowerCase()).title ?? null, after: r.title ?? null }));
    const processed = names.filter(n => completed.has(n)).length, pending = names.length - processed;
    const counts = Object.fromEntries(["title", "no_title", "unsupported_bsp", "unresolved"].map(s =>
      [s, records.filter(r => completed.has(r.name) && r.status === s).length]));
    const report = { complete, processed, pending, counts, added, removed, changed, errors, caseCollisions: transport.collisions || [], requests: transport.requests,
      bytes: transport.bytes, elapsedSeconds: Math.round((Date.now() - started) / 1000), revision: digest(snapshot) };
    atomicWrite(path.join(output, "report.json"), json(report));
    atomicWrite(path.join(output, "report.md"), `Map title catalog update\n\nComplete scan: ${complete}\n\n` +
      `Maps: ${names.length}; processed: ${processed}; pending: ${pending}; titles: ${counts.title}; empty: ${counts.no_title}; unsupported: ${counts.unsupported_bsp}; unresolved: ${counts.unresolved}.\n\n` +
      `Added: ${added.length}; removed: ${removed.length}; changed titles: ${changed.length}.\n\n` +
      `Response bytes: ${transport.bytes}; temporary BSP files: 0. See report.json for all changes and failures.\n\n` +
      `Candidate revision: ${report.revision}\n\nReview all three candidate files together. Regenerate expired artifacts with the command in Misc/map-catalog.md.\n`);
    if (complete) {
      for (const [file, contents] of [[CATALOG_PATH, json(catalog)], [LIST_PATH, listText], [SNAPSHOT_PATH, json(snapshot)]])
        atomicWrite(path.join(output, file), contents);
    }
    const updates = complete && digest(snapshot) !== digest(old) ? [{
      name: "QuakeWorld map title catalog", repository: "QuakeWorld", package: "qssm/qw-map-catalog",
      vendored: old ? digest(old).slice(0, 12) : "none", packageVersion: report.revision,
      reasons: [`${added.length} added, ${removed.length} removed, ${changed.length} changed titles, ${counts.unresolved} unresolved`],
      upstreamUrl: SOURCE,
      packageUrl: process.env.GITHUB_SERVER_URL && process.env.GITHUB_REPOSITORY && process.env.GITHUB_RUN_ID ?
        `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}` : SOURCE,
    }] : [];
    atomicWrite(path.join(output, "updates.json"), json(updates));
    return report;
  } catch (error) {
    // Preserve the original failure even if the output disk itself is full.
    try {
      atomicWrite(path.join(output, "report.json"), json({ complete: false, error: error.message, errors,
        requests: transport.requests, bytes: transport.bytes }));
      atomicWrite(path.join(output, "report.md"), `Map title catalog update\n\nComplete scan: false\n\n` +
        `${error.message}\n\nNo candidate is eligible for use. Retained progress is in checkpoint.json, if present.\n`);
    } catch { /* report creation must not mask the generation failure */ }
    throw error;
  } finally { transport.close(); }
}

async function main(args) {
  let root = path.resolve(__dirname, "../.."), output = "", resume = false, force = false;
  let limit = Infinity, minutes = 75, validate = false;
  for (let i = 0; i < args.length; i++) {
    switch (args[i]) {
      case "--root": root = path.resolve(args[++i]); break;
      case "--output": output = path.resolve(args[++i]); break;
      case "--resume": resume = true; break;
      case "--reextract-all": force = true; break;
      case "--limit": limit = Number(args[++i]); break;
      case "--max-minutes": minutes = Number(args[++i]); break;
      case "--validate": validate = true; break;
      default: throw new Error(`Unknown option: ${args[i]}`);
    }
  }
  if (validate) {
    validateSnapshot(readJSON(path.join(root, SNAPSHOT_PATH)), readJSON(path.join(root, CATALOG_PATH)),
      parseList(fs.readFileSync(path.join(root, LIST_PATH), "utf8")));
    console.log("Map title catalog, snapshot and filename list agree."); return;
  }
  if (!output || output === root || !(limit > 0) || (limit !== Infinity && !Number.isSafeInteger(limit)) ||
      !(minutes > 0) || !Number.isFinite(minutes))
    throw new Error("Usage: update-map-catalog.js --output DIR [--resume] [--limit N] [--max-minutes N] [--reextract-all], or --validate");
  const report = await generate({ root, output, resume, force, limit, minutes });
  console.log(JSON.stringify({ complete: report.complete, processed: report.processed, pending: report.pending,
    counts: report.counts, revision: report.revision }));
  if (!report.complete) process.exitCode = 2;
}
if (require.main === module) main(process.argv.slice(2)).catch(e => { console.error(e.stack); process.exitCode = 1; });
module.exports = { generate, atomicWrite, main };
