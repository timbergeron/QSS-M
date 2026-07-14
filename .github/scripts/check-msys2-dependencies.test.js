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
