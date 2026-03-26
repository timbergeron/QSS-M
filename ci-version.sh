#!/bin/sh

qssm_git_revision() {
	git rev-parse HEAD
}

qssm_git_short_revision() {
	if [ -n "${GITHUB_SHA:-}" ]; then
		printf '%s' "$GITHUB_SHA" | cut -c1-7
	else
		git rev-parse --short=7 HEAD
	fi
}

qssm_ci_version_suffix() {
	if [ -n "${QSSM_VERSION_SUFFIX:-}" ]; then
		printf '%s' "$QSSM_VERSION_SUFFIX"
	elif [ "${GITHUB_ACTIONS:-}" = "true" ]; then
		printf -- '-%s' "$(qssm_git_short_revision)"
	fi
}

qssm_build_cflags() {
	revision=$(qssm_git_revision)
	suffix=$(qssm_ci_version_suffix)

	if [ -n "$suffix" ]; then
		printf '%s' "-DQSS_REVISION=$revision -DQSSM_VER_SUFFIX=\\\"$suffix\\\""
	else
		printf '%s' "-DQSS_REVISION=$revision"
	fi
}

qssm_xcode_other_cflags() {
	suffix=$(qssm_ci_version_suffix)

	if [ -n "$suffix" ]; then
		printf '%s' "-DQSSM_VER_SUFFIX=\\\"$suffix\\\""
	fi
}
