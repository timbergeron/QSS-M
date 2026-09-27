"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { SOURCE, EXTRACTOR, LIST_PATH, CATALOG_PATH, SNAPSHOT_PATH, digest, validName, parseListing,
  worldspawnTitle, normalizeTitle, bspHeader, Transport, extractMap, catalogFor, validateSnapshot } = require("./map-catalog.js");
const { generate } = require("./update-map-catalog.js");

function bsp(entity = '{"classname" "worldspawn" "message" "A title"}', version = 29) {
  const text = Buffer.from(entity, "latin1"), bytes = Buffer.alloc(124 + text.length);
  bytes.writeUInt32LE(version, 0); bytes.writeInt32LE(124, 4); bytes.writeInt32LE(text.length, 8);
  text.copy(bytes, 124); return bytes;
}

test("listing follows engine filename restrictions and reports case aliases deterministically", () => {
  assert.deepEqual(parseListing('<pre><a href="b.bsp">b</a><a href="a.bsp">a</a><a href="a.bsp">a</a><a href="../evil.bsp">e</a></pre>'), ["a.bsp", "b.bsp"]);
  for (const name of ["x", "../a.bsp", "x\\a.bsp", "x\na.bsp", 'a".bsp', "é.bsp", "#a.bsp", " a.bsp", "x".repeat(55) + ".bsp"])
    assert.equal(validName(name), false, name);
  const collisions = [];
  assert.deepEqual(parseListing('<pre><a href="a.bsp">a</a><a href="A.bsp">A</a></pre>', p => collisions.push(p)), ["A.bsp"]);
  assert.deepEqual(collisions, [["A.bsp", "a.bsp"]]);
  assert.deepEqual(parseListing('<pre><a href="dm3%2B%2B.bsp">x</a><a href="h%26k.bsp">x</a><a href="bad%2fmap.bsp">x</a></pre>'), ["dm3++.bsp", "h&k.bsp"]);
  assert.throws(() => parseListing('<html>Service unavailable</html>'));
});

test("supported BSP headers and overflow-safe entity bounds", () => {
  for (const version of [29, 0x42535032, 0x32505342, 0x51363420]) {
    const bytes = bsp(undefined, version);
    assert.equal(bspHeader(bytes.subarray(0, 124), bytes.length).offset, 124);
  }
  const bytes = bsp();
  bytes.writeInt32LE(0x7fffffff, 4);
  assert.throws(() => bspHeader(bytes.subarray(0, 124), bytes.length), /outside/);
  assert.throws(() => bspHeader(Buffer.alloc(10), 1000), /Truncated/);
  assert.equal(bspHeader(bsp(undefined, 30).subarray(0, 124), 200).unsupported, true);
});

test("worldspawn parsing distinguishes incomplete, empty and later messages", () => {
  const parse = s => worldspawnTitle(Buffer.from(s, "latin1"));
  assert.equal(parse('// start\n{"message" "A\\n B\nC" "classname" "worldspawn"}'), "A B C");
  assert.equal(parse('{"classname" "worldspawn"}{"message" "Wrong"}'), "");
  assert.equal(parse('{"classname" "worldspawn" "message" "truncated'), null);
  assert.equal(parse('{"classname" "worldspawn" "message" "ok"'), null);
  assert.throws(() => parse('{"classname" "light" "message" "Wrong"}'), /worldspawn/);
  assert.equal(parse('{"classname" "worldspawn" "message" "}"}'), "}");
  assert.equal(normalizeTitle("\xc1\xe2\xe3\t title"), "Abc title");
  assert.equal(normalizeTitle("x".repeat(200)).length, 127);
});

async function server(t, respond) {
  const s = http.createServer(respond);
  await new Promise(resolve => s.listen(0, "127.0.0.1", resolve));
  const transport = new Transport({ source: `http://127.0.0.1:${s.address().port}/all/`, interval: 0, retries: 0, timeout: 1000 });
  t.after(async () => { transport.close(); await new Promise(resolve => s.close(resolve)); });
  return transport;
}

function serveRange(req, res, bytes, etag = '"v1"') {
  if (req.headers["if-none-match"] === etag) { res.writeHead(304); res.end(); return; }
  const [, a, b] = /bytes=(\d+)-(\d+)/.exec(req.headers.range);
  res.writeHead(206, { ETag: etag, "Content-Range": `bytes ${a}-${b}/${bytes.length}` });
  res.end(bytes.subarray(Number(a), Number(b) + 1));
}

test("extracts with bounded ranges, validators, and conditional reuse", async t => {
  const bytes = bsp(), requests = [];
  const transport = await server(t, (req, res) => { requests.push(req.headers); serveRange(req, res, bytes); });
  const row = await extractMap(transport, "a.bsp");
  assert.equal(row.title, "A title"); assert.equal(row.status, "title");
  assert.equal(requests[1]["if-range"], '"v1"');
  assert.deepEqual(await extractMap(transport, "a.bsp", row), row);
  assert.equal(requests.length, 3);
  assert.equal(transport.bytes, bytes.length);
});

test("rejects servers ignoring Range without buffering the full map", async t => {
  const transport = await server(t, (_req, res) => { res.writeHead(200); res.end(Buffer.alloc(100000)); });
  await assert.rejects(extractMap(transport, "a.bsp"), /HTTP 200/);
  assert.equal(transport.bytes, 0);
});

test("304 representation size does not count against the range body limit", async t => {
  const bytes = bsp();
  const transport = await server(t, (req, res) => {
    if (req.headers["if-none-match"]) {
      res.writeHead(304, { ETag: '"v1"', "Content-Length": 1000000 }); res.end();
    } else serveRange(req, res, bytes);
  });
  const row = await extractMap(transport, "a.bsp");
  assert.deepEqual(await extractMap(transport, "a.bsp", row), row);
  assert.equal(transport.requests, 3);
  assert.equal(transport.bytes, bytes.length);
});

test("rejects malformed ranges and changing representations", async t => {
  const bytes = bsp(); let request = 0;
  const transport = await server(t, (req, res) => serveRange(req, res, bytes, ++request === 1 ? '"v1"' : '"v2"'));
  await assert.rejects(extractMap(transport, "a.bsp"), /changed/);
  const bad = await server(t, (_req, res) => { res.writeHead(206, { "Content-Range": "bytes 1-124/500" }); res.end(Buffer.alloc(124)); });
  await assert.rejects(extractMap(bad, "a.bsp"), /Content-Range/);
});

test("expands incomplete worldspawn and bounds requests", async t => {
  const bytes = bsp('{"classname" "worldspawn" "padding" "' + "x".repeat(5000) + '" "message" "After padding"}');
  const transport = await server(t, (req, res) => serveRange(req, res, bytes));
  assert.equal((await extractMap(transport, "a.bsp")).title, "After padding");
  assert.equal(transport.requests, 3);
});

test("catalog validation catches baseline and data mismatch", () => {
  const maps = [{ name: "a.bsp", status: "title", title: "Title" }];
  const catalog = catalogFor(maps);
  const snapshot = { schema: 1, source: SOURCE, extractor: EXTRACTOR, maps, catalogDigest: digest(catalog) };
  validateSnapshot(snapshot, catalog, ["a.bsp"]);
  assert.throws(() => validateSnapshot(snapshot, catalog, ["b.bsp"]));
  assert.throws(() => validateSnapshot(snapshot, { ...catalog, maps: [{ name: "a.bsp", title: "bad\nline" }] }, ["a.bsp"]));
});

test("ten-map batches checkpoint and resume without creating BSP files", async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "qssm-catalog-test-"));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const output = path.join(root, "candidate"), names = Array.from({ length: 23 }, (_, i) => `a${String(i).padStart(2, "0")}.bsp`);
  fs.mkdirSync(path.dirname(path.join(root, LIST_PATH)), { recursive: true });
  fs.writeFileSync(path.join(root, LIST_PATH), names.join("\n") + "\n");
  const bytes = bsp();
  function transport() {
    return { requests: 0, bytes: 0, close() {}, async listing() { return names; },
      async range(_name, a, b) { this.requests++; this.bytes += b - a + 1;
        return { status: 206, size: bytes.length, headers: { etag: '"same"' }, body: bytes.subarray(a, b + 1) }; } };
  }
  const batches = [];
  const first = await generate({ root, output, transport: transport(), limit: 10, onBatch: s => batches.push(s) });
  assert.equal(first.complete, false);
  assert.equal(first.processed, 10); assert.equal(first.pending, 13); assert.equal(first.counts.unresolved, 0);
  assert.equal(fs.existsSync(path.join(output, CATALOG_PATH)), false);
  const second = await generate({ root, output, resume: true, transport: transport(), onBatch: s => batches.push(s) });
  assert.equal(second.complete, true); assert.equal(second.requests, 26);
  assert.equal(second.processed, 23); assert.equal(second.pending, 0);
  assert.deepEqual(batches.map(s => Number(s.split("/")[0])), [10, 20, 23]);
  const catalog = JSON.parse(fs.readFileSync(path.join(output, CATALOG_PATH)));
  const snapshot = JSON.parse(fs.readFileSync(path.join(output, SNAPSHOT_PATH)));
  validateSnapshot(snapshot, catalog, names);
  assert.equal(fs.readdirSync(output, { recursive: true }).some(s => s.endsWith(".bsp")), false);
});

test("incremental failures retain old titles, successful recovery clears stale status, and unchanged runs do not notify", async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "qssm-catalog-update-"));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const maps = [{ name: "a.bsp", size: 200, etag: '"old"', modified: "", status: "title", title: "Old title" }];
  const catalog = catalogFor(maps), snapshot = { schema: 1, source: SOURCE, extractor: EXTRACTOR, catalogDigest: digest(catalog), maps };
  for (const [file, contents] of [[LIST_PATH, "# refreshed=100\na.bsp\n"], [CATALOG_PATH, JSON.stringify(catalog)], [SNAPSHOT_PATH, JSON.stringify(snapshot)]]) {
    fs.mkdirSync(path.dirname(path.join(root, file)), { recursive: true }); fs.writeFileSync(path.join(root, file), contents);
  }
  const output = path.join(root, "unchanged");
  let transport = { requests: 0, bytes: 0, close() {}, async listing() { return ["a.bsp"]; },
    async range(_name, _a, _b, headers) { assert.equal(headers["If-None-Match"], '"old"'); return { status: 304 }; } };
  await generate({ root, output, transport, onBatch() {} });
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(output, "updates.json"))), []);
  assert.equal(fs.readFileSync(path.join(output, LIST_PATH), "utf8"), "# refreshed=100\na.bsp\n");
  transport = { ...transport, async range() { throw new Error("temporary timeout"); } };
  const failed = path.join(root, "failed");
  const report = await generate({ root, output: failed, transport, onBatch() {} });
  assert.equal(report.counts.unresolved, 1); assert.equal(report.errors[0].name, "a.bsp");
  assert.equal(JSON.parse(fs.readFileSync(path.join(failed, CATALOG_PATH))).maps[0].title, "Old title");
  const stale = JSON.parse(fs.readFileSync(path.join(failed, SNAPSHOT_PATH)));
  assert.equal(stale.maps[0].etag, '"old"');
  fs.copyFileSync(path.join(failed, SNAPSHOT_PATH), path.join(root, SNAPSHOT_PATH));
  const bytes = bsp('{"classname" "worldspawn" "message" "New title"}');
  transport = { ...transport, async range(_name, a, b, headers) {
    if (a === 0) assert.equal(headers["If-None-Match"], undefined);
    return { status: 206, size: bytes.length, headers: { etag: '"new"' }, body: bytes.subarray(a, b + 1) };
  } };
  const recovered = path.join(root, "recovered");
  const recovery = await generate({ root, output: recovered, transport, onBatch() {} });
  assert.equal(recovery.counts.title, 1);
  assert.equal(JSON.parse(fs.readFileSync(path.join(recovered, CATALOG_PATH))).maps[0].title, "New title");
});

test("large listing drops abort without overwriting reviewed files", async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "qssm-catalog-drop-"));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.dirname(path.join(root, LIST_PATH)), { recursive: true });
  fs.writeFileSync(path.join(root, LIST_PATH), "a.bsp\nb.bsp\n");
  await assert.rejects(generate({ root, output: path.join(root, "candidate"),
    transport: { close() {}, async listing() { return ["a.bsp"]; } } }), /5%/);
  assert.equal(fs.readFileSync(path.join(root, LIST_PATH), "utf8"), "a.bsp\nb.bsp\n");
});

test("failed or incomplete reruns discard successful candidates and notification payloads", async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "qssm-catalog-rerun-"));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.dirname(path.join(root, LIST_PATH)), { recursive: true });
  fs.writeFileSync(path.join(root, LIST_PATH), "a.bsp\nb.bsp\n");
  const output = path.join(root, "candidate"), bytes = bsp();
  const transport = { requests: 0, bytes: 0, close() {}, async listing() { return ["a.bsp", "b.bsp"]; },
    async range(_name, a, b) { return { status: 206, size: bytes.length,
      headers: { etag: '"v1"' }, body: bytes.subarray(a, b + 1) }; } };
  await generate({ root, output, transport, onBatch() {} });
  assert.ok(fs.existsSync(path.join(output, CATALOG_PATH)));
  const partial = await generate({ root, output, limit: 1, transport, onBatch() {} });
  assert.equal(partial.complete, false);
  for (const file of [CATALOG_PATH, LIST_PATH, SNAPSHOT_PATH])
    assert.equal(fs.existsSync(path.join(output, file)), false);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(output, "updates.json"))), []);
  const checkpoint = fs.readFileSync(path.join(output, "checkpoint.json"), "utf8");
  await assert.rejects(generate({ root, output, resume: true, onBatch() {},
    transport: { ...transport, async listing() { throw new Error("source is down"); } } }), /source is down/);
  const failed = JSON.parse(fs.readFileSync(path.join(output, "report.json")));
  assert.equal(failed.complete, false); assert.equal(failed.error, "source is down");
  assert.equal(fs.readFileSync(path.join(output, "checkpoint.json"), "utf8"), checkpoint);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(output, "updates.json"))), []);

  // A root alias must never remove reviewed files during candidate cleanup.
  const alias = path.join(root, "root-alias"); fs.symlinkSync(root, alias, "dir");
  await assert.rejects(generate({ root, output: alias, transport }), /directory must differ/);
  assert.equal(fs.readFileSync(path.join(root, LIST_PATH), "utf8"), "a.bsp\nb.bsp\n");
});

test("extractor changes force re-extraction instead of reusing unchanged validators", async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "qssm-catalog-version-"));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const maps = [{ name: "a.bsp", size: 200, etag: '"v1"', modified: "", status: "title", title: "Previous extraction" }];
  const catalog = catalogFor(maps);
  const snapshot = { schema: 1, source: SOURCE, extractor: EXTRACTOR + 1, catalogDigest: digest(catalog), maps };
  assert.throws(() => validateSnapshot(snapshot, catalog, ["a.bsp"]));
  for (const [file, contents] of [[LIST_PATH, "a.bsp\n"], [CATALOG_PATH, JSON.stringify(catalog)], [SNAPSHOT_PATH, JSON.stringify(snapshot)]]) {
    fs.mkdirSync(path.dirname(path.join(root, file)), { recursive: true }); fs.writeFileSync(path.join(root, file), contents);
  }
  const bytes = bsp(), output = path.join(root, "candidate");
  await generate({ root, output, onBatch() {}, transport: {
    requests: 0, bytes: 0, close() {}, async listing() { return ["a.bsp"]; },
    async range(_name, a, b, headers) {
      if (a === 0) assert.deepEqual(headers, {});
      return { status: 206, size: bytes.length, headers: { etag: '"v1"' }, body: bytes.subarray(a, b + 1) };
    },
  } });
  const result = JSON.parse(fs.readFileSync(path.join(output, SNAPSHOT_PATH)));
  assert.equal(result.extractor, EXTRACTOR); assert.equal(result.maps[0].title, "A title");
  validateSnapshot(result, JSON.parse(fs.readFileSync(path.join(output, CATALOG_PATH))), ["a.bsp"]);
});

test("unconditional 304, short response, redirect escape and timeouts fail within bounds", async t => {
  const noBody = await server(t, (_req, res) => { res.writeHead(304); res.end(); });
  await assert.rejects(extractMap(noBody, "a.bsp"), /unconditional/);
  const short = await server(t, (_req, res) => {
    res.writeHead(206, { "Content-Range": "bytes 0-123/500" }); res.end(Buffer.alloc(100));
  });
  await assert.rejects(extractMap(short, "a.bsp"), /truncated/);
  const redirect = await server(t, (_req, res) => { res.writeHead(302, { Location: "/outside/a.bsp" }); res.end(); });
  await assert.rejects(extractMap(redirect, "a.bsp"), /escaped/);
  const slow = await server(t, () => {}); slow.timeout = 25;
  await assert.rejects(extractMap(slow, "a.bsp"), /timed out/);
});

test("failed redirect destinations do not multiply retries across previous hops", async t => {
  let origins = 0, destinations = 0;
  const transport = await server(t, (req, res) => {
    if (req.url === "/all/a.bsp") {
      origins++; res.writeHead(302, { Location: "/all/destination.bsp" });
    } else { destinations++; res.writeHead(503); }
    res.end();
  });
  transport.retries = 1;
  await assert.rejects(extractMap(transport, "a.bsp"), /HTTP 503/);
  assert.equal(origins, 1); assert.equal(destinations, 2);
});
