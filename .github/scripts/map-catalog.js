"use strict";

const https = require("node:https");
const http = require("node:http");
const crypto = require("node:crypto");

const SOURCE = "https://maps.quakeworld.nu/all/";
const EXTRACTOR = 1;
const BATCH_SIZE = 10;
const TITLE_LIMIT = 127;
const ENTITY_LIMIT = 65536;
const CATALOG_PATH = "Misc/qssm_pak/misc/qw_map_titles.json";
const LIST_PATH = "Misc/qssm_pak/misc/qw_maps.txt";
const SNAPSHOT_PATH = ".github/map-catalog-snapshot.json";
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const digest = value => crypto.createHash("sha256").update(
  typeof value === "string" ? value : JSON.stringify(value)).digest("hex");
const compareNames = (a, b) => a.toLowerCase() < b.toLowerCase() ? -1 :
  a.toLowerCase() > b.toLowerCase() ? 1 : 0;

function validName(name) {
  return typeof name === "string" && name.length >= 5 && name.length < 59 &&
    /\.bsp$/i.test(name) && !/^[ #]|[^\x20-\x7e]|[/\\:"*?]/.test(name);
}

function validateNames(names) {
  if (!Array.isArray(names) || !names.length || names.length > 100000)
    throw new Error("Empty or oversized map list");
  for (let i = 0; i < names.length; i++)
    if (!validName(names[i]) || (i && compareNames(names[i - 1], names[i]) >= 0))
      throw new Error(`Invalid, duplicate or unsorted map filename: ${names[i]}`);
  return names;
}

function parseListing(html, onCollision = () => {}) {
  if (!/<(?:table|pre)\b/i.test(html)) throw new Error("Unrecognized map directory listing");
  const names = new Set();
  for (const match of html.matchAll(/\bhref\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/gi)) {
    const href = (match[1] ?? match[2] ?? match[3]).replace(/&amp;/g, "&").split(/[?#]/)[0];
    try {
      const name = decodeURIComponent(href);
      if (validName(name)) names.add(name);
    } catch { /* malformed URL escape */ }
  }
  const sorted = [...names].sort((a, b) => compareNames(a, b) || (a < b ? -1 : 1));
  const unique = [];
  for (const name of sorted) {
    if (unique.length && compareNames(unique[unique.length - 1], name) === 0)
      onCollision([unique[unique.length - 1], name]);
    else unique.push(name);
  }
  return validateNames(unique);
}

function parseList(text) {
  return validateNames(text.split(/\r?\n/).map(s => s.trim()).filter(s => s && !s.startsWith("#")));
}

function normalizeTitle(raw) {
  // Host_InitDeQuake's byte mapping. Decode BSP strings as Latin-1, never UTF-8.
  const table = Array(128).fill(0);
  for (let i = 1; i < 12; i++) table[i] = 35;
  table[9] = 9; table[10] = 10; table[13] = 13; table[12] = 32;
  for (const i of [1, 5, 14, 15, 28]) table[i] = 46;
  table[16] = 91; table[17] = 93;
  for (let i = 0; i < 10; i++) table[18 + i] = 48 + i;
  table[29] = 60; table[30] = 45; table[31] = 62;
  for (let i = 32; i < 128; i++) table[i] = i;
  table.push(...table);
  table[128] = 40; table[129] = 61; table[130] = 41;
  table[131] = 42; table[141] = 62;
  return [...raw].map(c => String.fromCharCode(table[c.charCodeAt(0)] || 32)).join("")
    .replace(/\\n|[^\x21-\x7e]/g, " ").replace(/ +/g, " ").trim().slice(0, TITLE_LIMIT).trimEnd();
}

function worldspawnTitle(bytes) {
  const text = bytes.toString("latin1");
  let pos = 0;
  function token() {
    for (;;) {
      while (pos < text.length && text.charCodeAt(pos) <= 32) pos++;
      if (text.slice(pos, pos + 2) !== "//") break;
      const end = text.indexOf("\n", pos);
      if (end < 0) return null;
      pos = end + 1;
    }
    if (pos >= text.length) return null;
    const c = text[pos++];
    if (c === '"') {
      const end = text.indexOf('"', pos);
      if (end < 0) return null;
      const value = text.slice(pos, end); pos = end + 1;
      return { value, quoted: true };
    }
    if ("{}():".includes(c)) return { value: c, quoted: false };
    const start = pos - 1;
    while (pos < text.length && text.charCodeAt(pos) > 32 && !"{}():".includes(text[pos])) pos++;
    return { value: text.slice(start, pos), quoted: false };
  }
  const first = token();
  if (!first) return null;
  if (first.quoted || first.value !== "{") throw new Error("Missing worldspawn entity");
  let classname = "", message = "";
  for (;;) {
    const key = token();
    if (!key) return null;
    if (!key.quoted && key.value === "}") {
      if (classname !== "worldspawn") throw new Error("First entity is not worldspawn");
      return normalizeTitle(message);
    }
    const value = token();
    if (!value) return null;
    if ((!key.quoted && "{}".includes(key.value)) || (!value.quoted && "{}".includes(value.value)))
      throw new Error("Malformed entity key/value");
    if (key.value === "classname") classname = value.value;
    if (key.value === "message") message = value.value;
  }
}

function bspHeader(bytes, size) {
  if (bytes.length !== 124 || !Number.isSafeInteger(size) || size < 124)
    throw new Error("Truncated BSP header");
  const version = bytes.readUInt32LE(0);
  if (![29, 0x42535032, 0x32505342, 0x51363420].includes(version))
    return { unsupported: true, version };
  const offset = bytes.readInt32LE(4), length = bytes.readInt32LE(8);
  if (offset < 124 || length <= 0 || offset > size || length > size - offset)
    throw new Error("Entity lump is outside the BSP");
  return { offset, length, version };
}

class Transport {
  constructor({ source = SOURCE, interval = 250, timeout = 15000, retries = 2 } = {}) {
    this.source = new URL(source); this.interval = interval; this.timeout = timeout;
    this.retries = retries; this.nextStart = 0; this.requests = 0; this.bytes = 0; this.collisions = [];
    this.agents = { "https:": new https.Agent({ keepAlive: true, maxSockets: 2 }),
      "http:": new http.Agent({ keepAlive: true, maxSockets: 2 }) };
  }
  close() { for (const agent of Object.values(this.agents)) agent.destroy(); }
  async request(url, headers, limit, accepted = [200], redirects = 0) {
    url = new URL(url);
    if (url.origin !== this.source.origin || !url.pathname.startsWith(this.source.pathname))
      throw new Error("Map request escaped configured source");
    let result;
    for (let attempt = 0; attempt <= this.retries; attempt++) {
      const start = Math.max(Date.now(), this.nextStart);
      this.nextStart = start + this.interval;
      await sleep(Math.max(0, start - Date.now()));
      this.requests++;
      try {
        result = await new Promise((resolve, reject) => {
          const impl = url.protocol === "https:" ? https : http;
          const req = impl.get(url, { agent: this.agents[url.protocol], headers: {
            "User-Agent": "QSS-M-map-catalog/1", "Accept-Encoding": "identity", ...headers,
          } }, res => {
            const fail = (message, retryable = false) => {
              const error = new Error(message); error.retryable = retryable;
              const retry = res.headers["retry-after"];
              error.retryAfter = /^\d+$/.test(retry || "") ? Number(retry) * 1000 :
                Math.max(0, Date.parse(retry) - Date.now()) || 0;
              res.destroy(); reject(error);
            };
            if ([301, 302, 303, 307, 308].includes(res.statusCode)) {
              res.destroy(); resolve({ redirect: res.headers.location }); return;
            }
            if (!accepted.includes(res.statusCode)) {
              fail(`HTTP ${res.statusCode} for ${url.pathname}`, res.statusCode === 429 || res.statusCode >= 500);
              return;
            }
            // A 304 has no body. Its representation headers may describe the
            // full BSP rather than the tiny range requested for revalidation.
            if (res.statusCode !== 304 && res.headers["content-encoding"] && res.headers["content-encoding"] !== "identity") {
              fail("Unexpected content encoding"); return;
            }
            if (res.statusCode !== 304 && Number(res.headers["content-length"]) > limit) {
              fail("Response exceeds byte limit"); return;
            }
            const chunks = []; let length = 0;
            res.on("data", chunk => {
              length += chunk.length; this.bytes += chunk.length;
              if (length > limit) fail("Response exceeds byte limit"); else chunks.push(chunk);
            });
            res.on("error", reject);
            res.on("end", () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
          });
          const timer = setTimeout(() => req.destroy(new Error("Map request timed out")), this.timeout);
          req.on("close", () => clearTimeout(timer));
          req.on("error", reject);
        });
        break;
      } catch (error) {
        if (error.retryable === false || attempt === this.retries) throw error;
        await sleep(Math.min(60000, Math.max(1000 * 2 ** attempt, error.retryAfter || 0)));
      }
    }
    // Follow redirects outside the retry loop: a destination failure must not
    // restart every prior hop and multiply retries across the redirect chain.
    if (Object.hasOwn(result, "redirect")) {
      if (!result.redirect || redirects >= 3) throw new Error("Invalid/excessive map redirects");
      return this.request(new URL(result.redirect, url), headers, limit, accepted, redirects + 1);
    }
    return result;
  }
  async listing() {
    const r = await this.request(this.source, {}, 16 * 1024 * 1024);
    return parseListing(r.body.toString("utf8"), pair => this.collisions.push(pair));
  }
  async range(name, start, end, conditional = {}) {
    const r = await this.request(new URL(encodeURIComponent(name), this.source),
      { Range: `bytes=${start}-${end}`, ...conditional }, end - start + 1, [206, 304]);
    if (r.status === 304) return r;
    const match = /^bytes (\d+)-(\d+)\/(\d+)$/.exec(r.headers["content-range"] || "");
    if (!match || Number(match[1]) !== start || Number(match[2]) !== end ||
        Number(match[3]) <= end || !Number.isSafeInteger(Number(match[3])) || r.body.length !== end - start + 1)
      throw new Error("Invalid or truncated Content-Range");
    r.size = Number(match[3]); return r;
  }
}

async function extractMap(transport, name, old, force = false) {
  const conditional = {};
  if (!force && old && old.status !== "unresolved") {
    if (old.etag) conditional["If-None-Match"] = old.etag;
    else if (old.modified) conditional["If-Modified-Since"] = old.modified;
  }
  const head = await transport.range(name, 0, 123, conditional);
  if (head.status === 304) {
    if (!Object.keys(conditional).length) throw new Error("Unexpected unconditional 304");
    return { ...old, name };
  }
  const record = { name, size: head.size, etag: head.headers.etag || "",
    modified: head.headers["last-modified"] || "" };
  const header = bspHeader(head.body, head.size);
  if (header.unsupported) return { ...record, status: "unsupported_bsp" };
  const pin = record.etag && !record.etag.startsWith("W/") ? record.etag : record.modified;
  for (let length = Math.min(4096, header.length);;) {
    const entity = await transport.range(name, header.offset, header.offset + length - 1,
      pin ? { "If-Range": pin } : {});
    if (entity.status !== 206 || entity.size !== head.size ||
        (record.etag && entity.headers.etag !== record.etag) ||
        (record.modified && entity.headers["last-modified"] !== record.modified))
      throw new Error("Map changed during extraction");
    const title = worldspawnTitle(entity.body);
    if (title !== null) {
      if (!pin) {
        const again = await transport.range(name, 0, 123);
        if (again.size !== head.size || !again.body.equals(head.body))
          throw new Error("Unversioned map changed during extraction");
      }
      return { ...record, status: title ? "title" : "no_title", title };
    }
    if (length >= header.length || length >= ENTITY_LIMIT)
      throw new Error("Incomplete worldspawn within entity byte limit");
    length = Math.min(length * 2, header.length, ENTITY_LIMIT);
  }
}

function catalogFor(records, source = SOURCE) {
  return { schema: 1, source, maps: records.filter(r => typeof r.title === "string")
    .map(({ name, title }) => ({ name, title })) };
}

function validateCatalog(catalog) {
  if (!catalog || catalog.schema !== 1 || catalog.source !== SOURCE || !Array.isArray(catalog.maps) || catalog.maps.length > 100000)
    throw new Error("Invalid title catalog schema/source");
  if (catalog.maps.length) validateNames(catalog.maps.map(m => m.name));
  for (const row of catalog.maps)
    if (typeof row.title !== "string" || row.title.length > TITLE_LIMIT || /[^\x20-\x7e]/.test(row.title))
      throw new Error(`Invalid title for ${row.name}`);
}

function validateSnapshot(snapshot, catalog, names, { allowExtractorMismatch = false } = {}) {
  validateCatalog(catalog); validateNames(names);
  if (!snapshot || snapshot.schema !== 1 || snapshot.source !== SOURCE ||
      !Number.isSafeInteger(snapshot.extractor) || snapshot.extractor < 1 ||
      (!allowExtractorMismatch && snapshot.extractor !== EXTRACTOR) ||
      !Array.isArray(snapshot.maps) || digest(catalog) !== snapshot.catalogDigest ||
      JSON.stringify(snapshot.maps.map(m => m.name)) !== JSON.stringify(names) ||
      JSON.stringify(catalogFor(snapshot.maps)) !== JSON.stringify(catalog))
    throw new Error("Map snapshot does not match catalog and filename list");
  for (const row of snapshot.maps) {
    if (!["title", "no_title", "unsupported_bsp", "unresolved"].includes(row.status) ||
        (row.status === "title" && !row.title) || (row.status === "no_title" && row.title !== ""))
      throw new Error(`Invalid extraction outcome for ${row.name}`);
  }
}

module.exports = { SOURCE, EXTRACTOR, BATCH_SIZE, TITLE_LIMIT, ENTITY_LIMIT, CATALOG_PATH, LIST_PATH,
  SNAPSHOT_PATH, digest, compareNames, validName, validateNames, parseListing, parseList,
  normalizeTitle, worldspawnTitle, bspHeader, Transport, extractMap, catalogFor, validateCatalog, validateSnapshot };
