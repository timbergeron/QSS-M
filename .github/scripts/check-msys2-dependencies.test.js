"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const {
  findChanges,
  parseDescription,
  upstreamVersion,
  validateManifest,
} = require("./check-msys2-dependencies.js");
const {
  findChange: findControllerDbChange,
  snapshotCommit,
  upstreamSnapshot,
} = require("./check-gamecontrollerdb-update.js");

const vendoredControllerDb = "513c72e34569e0f471dde7aa26eecb23946c3ef7";
const availableControllerDb = "8d9fefd7b810f2541f78cc7a8ccbd185bc84c7a5";
const upstreamControllerDb = "# Game Controller DB\n# Source: upstream\n\nentry\n";
const controllerDbSnapshot = "# Game Controller DB\n# Source: upstream\n" +
  `# QSS-M snapshot: commit ${vendoredControllerDb} (2026-06-26)\n\nentry\n`;

test("parseDescription reads pacman fields and multiline values", () => {
  const fields = parseDescription("%NAME%\nexample\n\n%VERSION%\n1.2.3-4\n\n%DESC%\nline one\nline two\n");
  assert.deepEqual(fields, {
    NAME: "example",
    VERSION: "1.2.3-4",
    DESC: "line one\nline two",
  });
});

test("upstreamVersion removes pacman epochs and package revisions", () => {
  assert.equal(upstreamVersion("1.3.2-2"), "1.3.2");
  assert.equal(upstreamVersion("2:0.15.1b-5"), "0.15.1b");
  assert.equal(upstreamVersion("14.0.0.r179.g24aaa6147-1"), "14.0.0.r179.g24aaa6147");
});

test("findChanges detects upstream changes and package-only rebuilds", () => {
  const dependencies = [{
    name: "example",
    vendored: "1.2.3",
    dlls: ["Windows/example.dll"],
    packages: [
      { repository: "mingw32", name: "package-a", baseline: "1.2.3-1" },
      { repository: "mingw64", name: "package-b", baseline: "1.2.3-1" },
    ],
  }];
  const databases = new Map([
    ["mingw32", new Map([["package-a", { VERSION: "1.2.3-2", URL: "https://example.test" }]])],
    ["mingw64", new Map([["package-b", { VERSION: "1.3.0-1", URL: "https://example.test" }]])],
  ]);

  const changes = findChanges(dependencies, databases);
  assert.equal(changes.length, 2);
  assert.deepEqual(changes[0].reasons, ["MSYS2 package rebuild changed"]);
  assert.deepEqual(changes[1].reasons, ["upstream version changed"]);
});

test("findChanges treats a matching baseline as acknowledged even when the vendored DLL is older", () => {
  const dependencies = [{
    name: "intentionally-deferred",
    vendored: "12.0",
    dlls: ["Windows/codecs/example.dll"],
    packages: [{ repository: "mingw64", name: "package-a", baseline: "14.0-1" }],
  }];
  const databases = new Map([
    ["mingw64", new Map([["package-a", { VERSION: "14.0-1" }]])],
  ]);
  assert.deepEqual(findChanges(dependencies, databases), []);
});

test("findChanges fails closed when a monitored package disappears", () => {
  const dependencies = [{
    name: "missing",
    vendored: "1.0",
    dlls: ["Windows/missing.dll"],
    packages: [{ repository: "mingw64", name: "missing-package", baseline: "1.0-1" }],
  }];
  assert.throws(
    () => findChanges(dependencies, new Map([["mingw64", new Map()]])),
    /Packages missing/
  );
});

test("validateManifest requires complete, one-to-one DLL coverage", () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "qssm-dependency-test-"));
  try {
    for (const directory of ["SDL2", "codecs", "curl", "zlib"])
      fs.mkdirSync(path.join(root, "Windows", directory), { recursive: true });
    fs.writeFileSync(path.join(root, "Windows", "codecs", "example.dll"), "fixture");
    const manifest = [{
      name: "example",
      vendored: "1.0",
      dlls: ["Windows/codecs/example.dll"],
      packages: [{ repository: "mingw64", name: "example-package", baseline: "1.0-1" }],
    }];
    assert.doesNotThrow(() => validateManifest(manifest, root));

    fs.writeFileSync(path.join(root, "Windows", "codecs", "untracked.dll"), "fixture");
    assert.throws(() => validateManifest(manifest, root), /Undeclared DLLs/);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test("controller snapshot reads and removes the pinned commit marker", () => {
  assert.equal(snapshotCommit(controllerDbSnapshot), vendoredControllerDb);
  assert.equal(upstreamSnapshot(controllerDbSnapshot), upstreamControllerDb);
});

test("controller snapshot reports a newer upstream commit", () => {
  const change = findControllerDbChange(
    controllerDbSnapshot,
    upstreamControllerDb,
    availableControllerDb
  );
  assert.equal(change.name, "SDL_GameControllerDB");
  assert.equal(change.vendored, "513c72e");
  assert.equal(change.available, "8d9fefd");
  assert.equal(change.packageVersion, availableControllerDb);
  assert.match(
    change.packageUrl,
    new RegExp(`${vendoredControllerDb}\\.\\.\\.${availableControllerDb}$`)
  );
});

test("controller snapshot accepts current matching contents", () => {
  assert.equal(
    findControllerDbChange(
      controllerDbSnapshot,
      upstreamControllerDb,
      vendoredControllerDb
    ),
    null
  );
});

test("controller snapshot rejects mismatched contents", () => {
  assert.throws(
    () => findControllerDbChange(
      controllerDbSnapshot.replace("entry", "stale entry"),
      upstreamControllerDb,
      vendoredControllerDb
    ),
    /does not match recorded commit/
  );
});

test("controller snapshot rejects invalid markers and upstream commits", () => {
  assert.throws(() => snapshotCommit("# no snapshot here\n"), /snapshot marker/);
  assert.throws(
    () => snapshotCommit("# QSS-M snapshot: commit 513c72e (2026-06-26)\n"),
    /snapshot marker/
  );
  assert.throws(
    () => findControllerDbChange(
      controllerDbSnapshot,
      upstreamControllerDb,
      "not-a-commit"
    ),
    /Invalid upstream commit/
  );
});
