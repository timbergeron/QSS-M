"use strict";

const LABEL = "automated-dependency-update";

function markerFor(change) {
  return `<!-- qssm-dependency:${change.package}:${change.packageVersion} -->`;
}

// Every package an existing notification issue reports on, read back from the
// markers it was created with.
function markedPackages(body) {
  return [...(body || "").matchAll(/<!-- qssm-dependency:([^\s:]+):[^\s]+ -->/g)]
    .map((match) => match[1]);
}

// An open issue is superseded once every package it reports on appears in the
// notification being created, which always carries the newer version.
function findSuperseded(issues, unreported) {
  const packages = new Set(unreported.map((change) => change.package));
  return issues.filter((issue) => {
    if (issue.state !== "open" || issue.pull_request)
      return false;
    const reported = markedPackages(issue.body);
    return reported.length > 0 && reported.every((name) => packages.has(name));
  });
}

async function ensureLabel({ github, core, owner, repo }) {
  try {
    await github.rest.issues.getLabel({ owner, repo, name: LABEL });
  } catch (error) {
    if (error.status !== 404)
      throw error;
    try {
      await github.rest.issues.createLabel({
        owner,
        repo,
        name: LABEL,
        color: "0e8a16",
        description: "Created by the scheduled bundled dependency monitor",
      });
    } catch (createError) {
      // A concurrent run or a user may have created it after the GET.
      if (createError.status !== 422)
        throw createError;
      core.info(`Label ${LABEL} already exists.`);
    }
  }
}

module.exports = async function notifyDependencyUpdates({ github, context, core, updates, notifyUser }) {
  if (!Array.isArray(updates) || updates.length === 0) {
    core.info("No dependency changes found.");
    return { created: false, reported: 0 };
  }
  if (!notifyUser)
    throw new Error("A notification username is required.");

  const { owner, repo } = context.repo;
  await ensureLabel({ github, core, owner, repo });
  const issues = await github.paginate(github.rest.issues.listForRepo, {
    owner,
    repo,
    state: "all",
    labels: LABEL,
    per_page: 100,
  });

  const unreported = updates.filter((change) =>
    !issues.some((issue) => (issue.body || "").includes(markerFor(change)))
  );
  if (!unreported.length) {
    core.info("All dependency changes were already reported.");
    return { created: false, reported: 0 };
  }

  const rows = unreported.map((change) => {
    const reason = change.reasons.join("; ");
    return `| ${change.name} | ${change.repository} | ${change.vendored} | ` +
      `**${change.packageVersion}** | ${reason} | [details](${change.packageUrl}) |`;
  });
  const upstreamLinks = [...new Map(
    unreported
      .filter((change) => change.upstreamUrl)
      .map((change) => [change.upstreamUrl, `- [${change.name} upstream](${change.upstreamUrl})`])
  ).values()];
  const names = [...new Set(unreported.map((change) => change.name))];
  const reviewNotes = [
    "This is a review notification only. Verify compatibility, licensing, and packaging before updating bundled files.",
  ];
  if (unreported.some((change) => change.repository.startsWith("mingw"))) {
    reviewNotes.push(
      "Do not replace Windows DLLs without checking ABI compatibility and the transitive runtime requirements documented in `Windows/codecs/README.md`."
    );
  }

  const superseded = findSuperseded(issues, unreported);
  const supersedeNote = superseded.length
    ? [`Supersedes ${superseded.map((issue) => `#${issue.number}`).join(", ")}, closed as out of date.`, ""]
    : [];

  const body = [
    `@${notifyUser}, a monitored bundled dependency changed for QSS-M.`,
    "",
    "| Dependency | Source | Bundled | Available | Reason | Details |",
    "|---|---|---:|---:|---|---|",
    ...rows,
    "",
    ...reviewNotes,
    "",
    ...supersedeNote,
    ...(upstreamLinks.length ? ["Upstream projects:", "", ...upstreamLinks, ""] : []),
    ...unreported.map(markerFor),
  ].join("\n");

  const created = await github.rest.issues.create({
    owner,
    repo,
    title: `Bundled dependency change detected: ${names.join(", ")}`,
    body,
    labels: [LABEL],
  });

  // The @mention already creates a notification. Assignment adds a mobile
  // assignment notification, but must not prevent issue creation if a repo is
  // transferred to an organization or the configured user is not assignable.
  try {
    await github.rest.issues.addAssignees({
      owner,
      repo,
      issue_number: created.data.number,
      assignees: [notifyUser],
    });
  } catch (error) {
    core.warning(`Issue #${created.data.number} was created, but assignment to ${notifyUser} failed: ${error.message}`);
  }

  // Closing an older notification must never lose the newer one, so failures
  // are reported rather than thrown.
  let closed = 0;
  for (const issue of superseded) {
    try {
      await github.rest.issues.createComment({
        owner,
        repo,
        issue_number: issue.number,
        body: `Superseded by #${created.data.number}, which reports the current version.`,
      });
      await github.rest.issues.update({
        owner,
        repo,
        issue_number: issue.number,
        state: "closed",
        state_reason: "not_planned",
      });
      closed += 1;
    } catch (error) {
      core.warning(`Issue #${issue.number} could not be closed as superseded: ${error.message}`);
    }
  }

  core.info(`Created issue #${created.data.number} for ${unreported.length} dependency change(s).`);
  return { created: true, issueNumber: created.data.number, reported: unreported.length, closed };
};

module.exports.LABEL = LABEL;
module.exports.findSuperseded = findSuperseded;
