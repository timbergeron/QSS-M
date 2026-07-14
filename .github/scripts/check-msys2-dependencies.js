#!/usr/bin/env node

"use strict";

const fs = require("fs");
const path = require("path");

function parseDescription(contents) {
  const fields = {};
  let current = null;

  for (const line of contents.split(/\r?\n/)) {
    const marker = /^%([^%]+)%$/.exec(line);
    if (marker) {
      current = marker[1];
      fields[current] = [];
    } else if (current && line !== "") {
      fields[current].push(line);
    }
  }

  return Object.fromEntries(
    Object.entries(fields).map(([name, values]) => [name, values.join("\n")])
  );
}

function loadPackageDatabase(databasePath) {
  const packages = new Map();
  for (const entry of fs.readdirSync(databasePath, { withFileTypes: true })) {
    if (!entry.isDirectory())
      continue;

    const descPath = path.join(databasePath, entry.name, "desc");
    if (!fs.existsSync(descPath))
      continue;

    const fields = parseDescription(fs.readFileSync(descPath, "utf8"));
    if (fields.NAME && fields.VERSION)
      packages.set(fields.NAME, fields);
  }
  return packages;
}

// Remove a pacman epoch and packaging revision while preserving the upstream
// version. Examples: "2:1.3.2-2" -> "1.3.2", "0.15.1b-5" -> "0.15.1b".
function upstreamVersion(packageVersion) {
  return packageVersion.replace(/^\d+:/, "").replace(/-\d+(?:\.\d+)*$/, "");
}

function listDlls(root) {
  const found = [];

  function visit(directory) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const absolute = path.join(directory, entry.name);
      if (entry.isDirectory())
        visit(absolute);
      else if (entry.isFile() && entry.name.toLowerCase().endsWith(".dll"))
        found.push(path.relative(root, absolute).split(path.sep).join("/"));
    }
  }

  for (const relativeRoot of ["Windows/SDL2", "Windows/codecs", "Windows/curl", "Windows/zlib"]) {
    const dependencyRoot = path.join(root, relativeRoot);
    if (!fs.existsSync(dependencyRoot))
      throw new Error(`Bundled dependency directory is missing: ${relativeRoot}`);
    visit(dependencyRoot);
  }
  return found.sort();
}

function validateManifest(dependencies, repositoryRoot) {
  if (!Array.isArray(dependencies) || dependencies.length === 0)
    throw new Error("Dependency manifest must be a non-empty array.");

  const dependencyNames = new Set();
  const packageNames = new Set();
  const declaredDlls = new Set();

  for (const dependency of dependencies) {
    if (!dependency || typeof dependency.name !== "string" ||
        typeof dependency.vendored !== "string" ||
        !Array.isArray(dependency.dlls) || dependency.dlls.length === 0 ||
        !Array.isArray(dependency.packages) || dependency.packages.length === 0) {
      throw new Error("Every dependency requires name, vendored, dlls, and packages fields.");
    }
    if (dependencyNames.has(dependency.name))
      throw new Error(`Duplicate dependency name: ${dependency.name}`);
    dependencyNames.add(dependency.name);

    for (const dll of dependency.dlls) {
      if (typeof dll !== "string" || !dll.startsWith("Windows/") || !dll.toLowerCase().endsWith(".dll"))
        throw new Error(`Invalid DLL path for ${dependency.name}: ${dll}`);
      if (declaredDlls.has(dll))
        throw new Error(`DLL is declared more than once: ${dll}`);
      declaredDlls.add(dll);
    }

    for (const pkg of dependency.packages) {
      if (!pkg || typeof pkg.repository !== "string" || typeof pkg.name !== "string" ||
          typeof pkg.baseline !== "string") {
        throw new Error(`Invalid package entry for ${dependency.name}.`);
      }
      const key = `${pkg.repository}:${pkg.name}`;
      if (packageNames.has(key))
        throw new Error(`Package is declared more than once: ${key}`);
      packageNames.add(key);
    }
  }

  const actualDlls = listDlls(repositoryRoot);
  const undeclared = actualDlls.filter((dll) => !declaredDlls.has(dll));
  const missing = [...declaredDlls].filter((dll) => !actualDlls.includes(dll));
  if (undeclared.length || missing.length) {
    throw new Error([
      undeclared.length ? `Undeclared DLLs: ${undeclared.join(", ")}` : "",
      missing.length ? `Missing DLLs: ${missing.join(", ")}` : "",
    ].filter(Boolean).join("\n"));
  }
}

function findChanges(dependencies, databases) {
  const changes = [];
  const missing = [];

  for (const dependency of dependencies) {
    for (const pkg of dependency.packages) {
      const database = databases.get(pkg.repository);
      if (!database)
        throw new Error(`No database was supplied for repository ${pkg.repository}.`);

      const availablePackage = database.get(pkg.name);
      if (!availablePackage) {
        missing.push(`${pkg.repository}:${pkg.name}`);
        continue;
      }

      const available = upstreamVersion(availablePackage.VERSION);
      const reasons = [];
      if (availablePackage.VERSION !== pkg.baseline) {
        if (available !== upstreamVersion(pkg.baseline))
          reasons.push("upstream version changed");
        else
          reasons.push("MSYS2 package rebuild changed");
      }

      if (reasons.length) {
        changes.push({
          name: dependency.name,
          vendored: dependency.vendored,
          repository: pkg.repository,
          package: pkg.name,
          baselinePackageVersion: pkg.baseline,
          available,
          packageVersion: availablePackage.VERSION,
          reasons,
          upstreamUrl: availablePackage.URL || "",
          packageUrl: `https://packages.msys2.org/package/${pkg.name}`,
        });
      }
    }
  }

  if (missing.length)
    throw new Error(`Packages missing from the MSYS2 indexes: ${missing.join(", ")}`);
  return changes;
}

function main(argv) {
  const [manifestPath, repositoryRoot, outputPath, ...databaseArguments] = argv;
  if (!manifestPath || !repositoryRoot || !outputPath || databaseArguments.length === 0) {
    throw new Error(
      "usage: check-msys2-dependencies.js MANIFEST REPOSITORY_ROOT OUTPUT REPOSITORY=EXTRACTED_DB [...]"
    );
  }

  const dependencies = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
  validateManifest(dependencies, repositoryRoot);

  const databases = new Map();
  for (const argument of databaseArguments) {
    const separator = argument.indexOf("=");
    if (separator <= 0 || separator === argument.length - 1)
      throw new Error(`Invalid database argument: ${argument}`);
    const name = argument.slice(0, separator);
    const databasePath = argument.slice(separator + 1);
    if (databases.has(name))
      throw new Error(`Database was supplied more than once: ${name}`);
    databases.set(name, loadPackageDatabase(databasePath));
  }

  const changes = findChanges(dependencies, databases);
  fs.writeFileSync(outputPath, `${JSON.stringify(changes, null, 2)}\n`);
  console.log(changes.length
    ? `Found ${changes.length} dependency package change(s).`
    : "All monitored MSYS2 package baselines are current.");
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
  findChanges,
  listDlls,
  loadPackageDatabase,
  main,
  parseDescription,
  upstreamVersion,
  validateManifest,
};
