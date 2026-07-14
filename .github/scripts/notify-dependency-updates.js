"use strict";

const LABEL = "automated-dependency-update";

function markerFor(change) {
  return `<!-- qssm-dependency:${change.package}:${change.packageVersion} -->`;
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
        description: "Created by the scheduled Windows dependency monitor",
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
      `**${change.packageVersion}** | ${reason} | [package](${change.packageUrl}) |`;
  });
  const upstreamLinks = [...new Map(
    unreported
      .filter((change) => change.upstreamUrl)
      .map((change) => [change.upstreamUrl, `- [${change.name} upstream](${change.upstreamUrl})`])
  ).values()];
  const names = [...new Set(unreported.map((change) => change.name))];

  const body = [
    `@${notifyUser}, a monitored Windows dependency package changed for QSS-M.`,
    "",
    "| Dependency | Repository | Bundled upstream | Available package | Reason | Source |",
    "|---|---|---:|---:|---|---|",
    ...rows,
    "",
    "This is a review notification only. Do not replace DLLs without checking ABI compatibility and the transitive runtime requirements documented in `Windows/codecs/README.md`.",
    "",
    ...(upstreamLinks.length ? ["Upstream projects:", "", ...upstreamLinks, ""] : []),
    ...unreported.map(markerFor),
  ].join("\n");

  const created = await github.rest.issues.create({
    owner,
    repo,
    title: `Windows dependency change detected: ${names.join(", ")}`,
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

  core.info(`Created issue #${created.data.number} for ${unreported.length} dependency change(s).`);
  return { created: true, issueNumber: created.data.number, reported: unreported.length };
};

module.exports.LABEL = LABEL;
