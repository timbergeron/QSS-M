#!/usr/bin/env node

"use strict";

const fs = require("node:fs");

const NAME = "SDL_GameControllerDB";
const REPOSITORY = "mdqinc/SDL_GameControllerDB";
const REPOSITORY_URL = `https://github.com/${REPOSITORY}`;
const SHA_PATTERN = /^[0-9a-f]{40}$/;

function snapshotCommit(contents) {
  const match = /^# QSS-M snapshot: commit ([0-9a-f]{40}) \(\d{4}-\d{2}-\d{2}\)$/m.exec(contents);
  if (!match)
    throw new Error("gamecontrollerdb.txt has no valid QSS-M snapshot marker.");
  return match[1];
}

function upstreamSnapshot(contents) {
  snapshotCommit(contents);
  return contents.replace(
    /^# QSS-M snapshot: commit [0-9a-f]{40} \(\d{4}-\d{2}-\d{2}\)\r?\n/m,
    ""
  );
}

function findChange(contents, pinnedContents, availableCommit) {
  if (!SHA_PATTERN.test(availableCommit))
    throw new Error(`Invalid upstream commit: ${availableCommit}`);

  const vendoredCommit = snapshotCommit(contents);
  if (upstreamSnapshot(contents) !== pinnedContents) {
    throw new Error(
      `Bundled gamecontrollerdb.txt does not match recorded commit ${vendoredCommit}.`
    );
  }
  if (vendoredCommit === availableCommit)
    return null;

  return {
    name: NAME,
    vendored: vendoredCommit.slice(0, 7),
    repository: "GitHub",
    package: REPOSITORY,
    baselinePackageVersion: vendoredCommit,
    available: availableCommit.slice(0, 7),
    packageVersion: availableCommit,
    reasons: ["upstream commit changed"],
    upstreamUrl: REPOSITORY_URL,
    packageUrl: `${REPOSITORY_URL}/compare/${vendoredCommit}...${availableCommit}`,
  };
}

function main(argv) {
  const [snapshotPath, pinnedPath, availableCommit, outputPath] = argv;
  if (!snapshotPath || !pinnedPath || !availableCommit || !outputPath) {
    throw new Error(
      "usage: check-gamecontrollerdb-update.js SNAPSHOT PINNED_SNAPSHOT AVAILABLE_COMMIT OUTPUT"
    );
  }

  const contents = fs.readFileSync(snapshotPath, "utf8");
  const pinnedContents = fs.readFileSync(pinnedPath, "utf8");
  const change = findChange(contents, pinnedContents, availableCommit);
  fs.writeFileSync(outputPath, `${JSON.stringify(change ? [change] : [], null, 2)}\n`);
  console.log(change
    ? `${NAME} changed from ${change.vendored} to ${change.available}.`
    : `${NAME} snapshot is current.`);
}

if (require.main === module) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    console.error(error.stack || error.message);
    process.exit(1);
  }
}

module.exports = {
  findChange,
  main,
  snapshotCommit,
  upstreamSnapshot,
};
