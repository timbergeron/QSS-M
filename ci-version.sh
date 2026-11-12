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

# Prints MAJOR.MINOR.PATCH from Quake/quakedef.h, or nothing if unreadable.
qssm_source_version() {
	top=$(git rev-parse --show-toplevel 2>/dev/null) || return 0
	header="$top/Quake/quakedef.h"
	[ -f "$header" ] || return 0
	awk '$1 == "#define" && $2 == "QSSM_VER_MAJOR" { maj = $3 }
		$1 == "#define" && $2 == "QSSM_VER_MINOR" { min = $3 }
		$1 == "#define" && $2 == "QSSM_VER_PATCH" { pat = $3 }
		END { if (maj != "" && min != "" && pat != "") printf "%s.%s.%s", maj, min, pat }' "$header"
}

# True when CI is building a release tag (X.Y.Z or vX.Y.Z) that matches the
# source version. Release builds carry no suffix: the updater treats any "-"
# suffix as a prerelease, so a suffixed 1.7.0 would keep offering tag 1.7.0.
qssm_ci_is_release_tag() {
	[ "${GITHUB_REF_TYPE:-}" = "tag" ] || return 1
	tag=${GITHUB_REF_NAME:-}
	tag=${tag#v}
	case "$tag" in
		''|*[!0-9.]*) return 1 ;;
	esac
	source_version=$(qssm_source_version)
	if [ "$tag" != "$source_version" ]; then
		echo "warning: tag ${GITHUB_REF_NAME:-} does not match source version '${source_version}'; keeping commit suffix" >&2
		return 1
	fi
	return 0
}

qssm_ci_version_suffix() {
	if [ -n "${QSSM_VERSION_SUFFIX:-}" ]; then
		printf '%s' "$QSSM_VERSION_SUFFIX"
	elif [ "${GITHUB_ACTIONS:-}" = "true" ]; then
		if ! qssm_ci_is_release_tag; then
			printf -- '-%s' "$(qssm_git_short_revision)"
		fi
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
