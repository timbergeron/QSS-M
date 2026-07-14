"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");

const notify = require("./notify-dependency-updates.js");

function fixture(overrides = {}) {
  const calls = { created: [], assigned: [], labelsCreated: [], paginatedWith: null };
  const core = {
    infos: [],
    warnings: [],
    info(message) { this.infos.push(message); },
    warning(message) { this.warnings.push(message); },
  };
  const github = {
    paginate: async (_method, args) => {
      calls.paginatedWith = args;
      return overrides.issues || [];
    },
    rest: {
      issues: {
        listForRepo() {},
        async getLabel() {
          if (overrides.labelMissing) {
            const error = new Error("not found");
            error.status = 404;
            throw error;
          }
        },
        async createLabel(args) {
          calls.labelsCreated.push(args);
        },
        async create(args) {
          calls.created.push(args);
          return { data: { number: 42 } };
        },
        async addAssignees(args) {
          calls.assigned.push(args);
          if (overrides.assignmentError)
            throw new Error("not assignable");
        },
      },
    },
  };
  return { calls, core, github };
}

const update = {
  name: "example",
  vendored: "1.0",
  repository: "mingw64",
  package: "example-package",
  packageVersion: "1.1-1",
  reasons: ["upstream version differs"],
  upstreamUrl: "https://example.test/upstream",
  packageUrl: "https://example.test/package",
};

test("does nothing when no changes are available", async () => {
  const { calls, core, github } = fixture();
  const result = await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [],
    notifyUser: "owner",
  });
  assert.deepEqual(result, { created: false, reported: 0 });
  assert.equal(calls.created.length, 0);
});

test("creates, mentions, and assigns a deduplicated notification issue", async () => {
  const { calls, core, github } = fixture();
  const result = await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [update],
    notifyUser: "owner",
  });
  assert.deepEqual(result, { created: true, issueNumber: 42, reported: 1 });
  assert.equal(calls.created.length, 1);
  assert.match(calls.created[0].body, /@owner/);
  assert.match(calls.created[0].body, /qssm-dependency:example-package:1\.1-1/);
  assert.deepEqual(calls.created[0].labels, [notify.LABEL]);
  assert.equal(calls.paginatedWith.labels, notify.LABEL);
  assert.deepEqual(calls.assigned[0].assignees, ["owner"]);
});

test("creates the dedicated label when it does not exist", async () => {
  const { calls, core, github } = fixture({ labelMissing: true });
  await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [update],
    notifyUser: "owner",
  });
  assert.equal(calls.labelsCreated.length, 1);
  assert.equal(calls.labelsCreated[0].name, notify.LABEL);
});

test("does not recreate an issue containing the same package marker", async () => {
  const marker = "<!-- qssm-dependency:example-package:1.1-1 -->";
  const { calls, core, github } = fixture({ issues: [{ body: marker }] });
  const result = await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [update],
    notifyUser: "owner",
  });
  assert.deepEqual(result, { created: false, reported: 0 });
  assert.equal(calls.created.length, 0);
});

test("keeps a created notification when assignment is unavailable", async () => {
  const { calls, core, github } = fixture({ assignmentError: true });
  const result = await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [update],
    notifyUser: "owner",
  });
  assert.equal(result.created, true);
  assert.equal(calls.created.length, 1);
  assert.equal(core.warnings.length, 1);
});
