"use strict";

const assert = require("node:assert/strict");
const test = require("node:test");

const notify = require("./notify-dependency-updates.js");

function fixture(overrides = {}) {
  const calls = {
    created: [], assigned: [], labelsCreated: [], comments: [], closed: [], paginatedWith: null,
  };
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
        async createComment(args) {
          if (overrides.closeError)
            throw new Error("comments are locked");
          calls.comments.push(args);
        },
        async update(args) {
          calls.closed.push(args);
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
  assert.deepEqual(result, { created: true, issueNumber: 42, reported: 1, closed: 0 });
  assert.equal(calls.created.length, 1);
  assert.match(calls.created[0].body, /@owner/);
  assert.match(calls.created[0].body, /monitored bundled dependency changed/);
  assert.match(calls.created[0].body, /Do not replace Windows DLLs/);
  assert.match(calls.created[0].body, /qssm-dependency:example-package:1\.1-1/);
  assert.match(calls.created[0].title, /^Bundled dependency change detected:/);
  assert.deepEqual(calls.created[0].labels, [notify.LABEL]);
  assert.equal(calls.paginatedWith.labels, notify.LABEL);
  assert.deepEqual(calls.assigned[0].assignees, ["owner"]);
});

test("renders a controller database update without DLL-specific guidance", async () => {
  const { calls, core, github } = fixture();
  const commit = "8d9fefd7b810f2541f78cc7a8ccbd185bc84c7a5";
  const controllerUpdate = {
    name: "SDL_GameControllerDB",
    vendored: "513c72e",
    repository: "GitHub",
    package: "mdqinc/SDL_GameControllerDB",
    packageVersion: commit,
    reasons: ["upstream commit changed"],
    upstreamUrl: "https://github.com/mdqinc/SDL_GameControllerDB",
    packageUrl: `https://github.com/mdqinc/SDL_GameControllerDB/commit/${commit}`,
  };

  await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [controllerUpdate],
    notifyUser: "owner",
  });

  const body = calls.created[0].body;
  assert.ok(body.includes(
    `<!-- qssm-dependency:mdqinc/SDL_GameControllerDB:${commit} -->`
  ));
  assert.doesNotMatch(body, /Do not replace Windows DLLs/);
});

test("closes an open notification once a newer version supersedes it", async () => {
  const stale = {
    number: 7,
    state: "open",
    body: "<!-- qssm-dependency:example-package:1.0-1 -->",
  };
  const { calls, core, github } = fixture({ issues: [stale] });
  const result = await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [update],
    notifyUser: "owner",
  });

  assert.equal(result.closed, 1);
  assert.match(calls.created[0].body, /Supersedes #7, closed as out of date\./);
  assert.equal(calls.comments[0].issue_number, 7);
  assert.match(calls.comments[0].body, /Superseded by #42/);
  assert.deepEqual(calls.closed[0], {
    owner: "owner",
    repo: "repo",
    issue_number: 7,
    state: "closed",
    state_reason: "not_planned",
  });
});

test("keeps notifications that are closed, unrelated, or only partly superseded", async () => {
  const issues = [
    { number: 5, state: "closed", body: "<!-- qssm-dependency:example-package:1.0-1 -->" },
    { number: 6, state: "open", body: "<!-- qssm-dependency:other-package:2.0-1 -->" },
    {
      number: 7,
      state: "open",
      body: "<!-- qssm-dependency:example-package:1.0-1 -->\n" +
        "<!-- qssm-dependency:other-package:2.0-1 -->",
    },
    { number: 8, state: "open", body: "no markers here" },
  ];
  const { calls, core, github } = fixture({ issues });
  const result = await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [update],
    notifyUser: "owner",
  });

  assert.equal(result.closed, 0);
  assert.equal(calls.closed.length, 0);
  assert.doesNotMatch(calls.created[0].body, /Supersedes/);
});

test("keeps the new notification when an older one cannot be closed", async () => {
  const stale = {
    number: 7,
    state: "open",
    body: "<!-- qssm-dependency:example-package:1.0-1 -->",
  };
  const { calls, core, github } = fixture({ issues: [stale], closeError: true });
  const result = await notify({
    github,
    context: { repo: { owner: "owner", repo: "repo" } },
    core,
    updates: [update],
    notifyUser: "owner",
  });

  assert.equal(result.created, true);
  assert.equal(result.closed, 0);
  assert.equal(calls.closed.length, 0);
  assert.equal(core.warnings.length, 1);
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
