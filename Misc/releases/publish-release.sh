#!/bin/bash
# Collect a tagged release's CI packages, verify them, stage them in the
# timbergeron.github.io checkout and create a DRAFT GitHub release.
#
# Run after the release commit and its tag (e.g. 1.7.0) are pushed and the
# macOS, Linux and Windows (MinGW) workflows have finished for that tag.
#
#   Misc/releases/publish-release.sh 1.7.0 [--site ~/codedev/timbergeron.github.io]
#
# Rehearsal before tagging (downloads and checks the CI zips of a branch, signs
# macOS without notarizing, touches neither the site nor GitHub releases):
#
#   Misc/releases/publish-release.sh 1.7.0 --dry-run --ref qsrebase
#
# The macOS package is re-signed with Developer ID and notarized via
# macOS/sign-macos-release.sh (needs the `notary` notarytool profile).
#
# Nothing is committed or pushed. The GitHub release stays a draft until you
# publish it on GitHub (or: gh release edit <tag> --draft=false --latest).
set -euo pipefail

REPO=timbergeron/QSS-M
VERSION=${1:?usage: $0 <version> [--site DIR] [--dry-run --ref BRANCH]}
shift
SITE_DIR="$HOME/codedev/timbergeron.github.io"
DRY_RUN=0
REF=
while [ $# -gt 0 ]; do
	case "$1" in
		--site) SITE_DIR=$2; shift 2 ;;
		--dry-run) DRY_RUN=1; shift ;;
		--ref) REF=$2; shift 2 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done
REF=${REF:-$VERSION}
[ "$REF" = "$VERSION" ] || [ "$DRY_RUN" = 1 ] || { echo "--ref is only for --dry-run" >&2; exit 2; }

ROOT=$(git rev-parse --show-toplevel)
NOTES="$ROOT/Misc/releases/$VERSION.md"
[ -f "$NOTES" ] || { echo "missing release notes: $NOTES" >&2; exit 1; }
[ -d "$SITE_DIR/releases" ] || { echo "site checkout not found: $SITE_DIR" >&2; exit 1; }

# The tag must exist on GitHub and match the source version.
SHA=$(gh api "repos/$REPO/commits/$REF" -q .sha)
REMOTE_HEADER=$(gh api "repos/$REPO/contents/Quake/quakedef.h?ref=$REF" -q .content | base64 --decode)
SOURCE_VERSION=$(printf '%s\n' "$REMOTE_HEADER" | awk '
	$1 == "#define" && $2 == "QSSM_VER_MAJOR" { maj = $3 }
	$1 == "#define" && $2 == "QSSM_VER_MINOR" { min = $3 }
	$1 == "#define" && $2 == "QSSM_VER_PATCH" { pat = $3 }
	END { printf "%s.%s.%s", maj, min, pat }')
if [ "$SOURCE_VERSION" != "$VERSION" ]; then
	echo "$REF has quakedef.h version $SOURCE_VERSION, expected $VERSION" >&2
	[ "$DRY_RUN" = 1 ] || exit 1
	VERSION=$SOURCE_VERSION
	echo "dry run: checking $REF as $VERSION" >&2
fi
echo "$REF -> $SHA"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/qssm-release-$VERSION.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# workflow file | artifact name | release asset platform | required root entries
PACKAGES="macos.yml|QSS-M-macOS.zip|macos|qssm.pak quakespasm.pak
linux.yml|QSS-M-l64.zip|linux64|QSS-M-l64 qssm.pak quakespasm.pak
windows-mingw.yml|QSS-M-w64.zip|win64|QSS-M-w64.exe qssm.pak quakespasm.pak
windows-mingw.yml|QSS-M-w32.zip|win32|QSS-M-w32.exe qssm.pak quakespasm.pak"

run_for()
{
	# Tag pushes and manual re-runs on the tag both report the tag as
	# headBranch; newest first.
	gh run list -R "$REPO" --workflow "$1" --commit "$SHA" \
		--json databaseId,status,conclusion,headBranch \
		-q "[.[] | select(.headBranch == \"$REF\")][0] | \"\(.databaseId) \(.status) \(.conclusion)\""
}

ASSETS=()
# The list is read from fd 3 so commands in the loop cannot eat it from stdin.
while IFS='|' read -r workflow artifact platform required <&3; do
	read -r run_id status conclusion <<<"$(run_for "$workflow")"
	if [ -z "${run_id:-}" ] || [ "$run_id" = "null" ]; then
		echo "no $workflow run for $REF at $SHA" >&2; exit 1
	fi
	if [ "$status" != completed ] || [ "$conclusion" != success ]; then
		echo "$workflow run $run_id is $status/$conclusion" >&2; exit 1
	fi

	# The packages are uploaded with archive: false, so the artifact's raw
	# bytes are the package zip itself. (gh run download would unpack it.)
	read -r artifact_id digest <<<"$(gh api "repos/$REPO/actions/runs/$run_id/artifacts" \
		-q ".artifacts[] | select(.name == \"$artifact\") | \"\(.id) \(.digest)\"")"
	[ -n "${artifact_id:-}" ] || { echo "run $run_id has no artifact $artifact" >&2; exit 1; }

	out="$WORK/QSS-M_v${VERSION}_${platform}.zip"
	gh api "repos/$REPO/actions/artifacts/$artifact_id/zip" > "$out"
	actual="sha256:$(shasum -a 256 "$out" | cut -d' ' -f1)"
	[ "$actual" = "$digest" ] || { echo "$artifact digest $actual != $digest" >&2; exit 1; }

	# The in-game updater requires these at the archive root.
	listing=$(unzip -Z1 "$out")
	for f in $required; do
		grep -qx "$f" <<<"$listing" || { echo "$out missing $f" >&2; exit 1; }
	done
	if [ "$platform" = macos ]; then
		grep -qE '^[^/]+\.app/Contents/MacOS/QSS-M$' <<<"$listing" ||
			{ echo "$out has no root .app bundle" >&2; exit 1; }
		exe=$(grep -E '^[^/]+\.app/Contents/MacOS/QSS-M$' <<<"$listing" | head -1)
	else
		exe=$(printf '%s\n' $required | head -1)
	fi

	# Release builds must report a bare version: the updater treats any
	# "-suffix" as a prerelease and would offer this same release forever.
	ver_strings=$(unzip -p "$out" "$exe" | strings | { grep -oE "QSS-M $VERSION[-+]?[0-9a-f]*" || true; } | sort -u)
	if ! grep -qx "QSS-M $VERSION" <<<"$ver_strings" ||
		grep -q "QSS-M $VERSION-" <<<"$ver_strings"; then
		echo "$exe in $out reports: $ver_strings (expected bare $VERSION)" >&2
		[ "$DRY_RUN" = 1 ] || exit 1
		echo "dry run: continuing (branch builds always carry a suffix)" >&2
	fi

	# CI's macOS build is ad-hoc signed; ship a Developer ID signed,
	# notarized and stapled app so Gatekeeper and TCC grants stay stable.
	if [ "$platform" = macos ]; then
		if [ "$DRY_RUN" = 1 ]; then
			"$ROOT/macOS/sign-macos-release.sh" "$out" "$out.signed" --no-notarize
		else
			"$ROOT/macOS/sign-macos-release.sh" "$out" "$out.signed"
		fi
		mv "$out.signed" "$out"
	fi

	echo "ok  $(basename "$out")  $(shasum -a 256 "$out" | cut -c1-16)  run $run_id"
	ASSETS+=("$out")
done 3<<<"$PACKAGES"

if [ "$DRY_RUN" = 1 ]; then
	echo "Dry run OK: ${#ASSETS[@]} packages checked; site and GitHub releases untouched"
	exit 0
fi

cp "${ASSETS[@]}" "$SITE_DIR/releases/"
echo "Staged ${#ASSETS[@]} zips in $SITE_DIR/releases (not committed)"

if gh release view "$VERSION" -R "$REPO" >/dev/null 2>&1; then
	echo "release $VERSION already exists; uploading assets with --clobber"
	gh release upload "$VERSION" -R "$REPO" --clobber "${ASSETS[@]}"
else
	gh release create "$VERSION" -R "$REPO" --verify-tag --draft \
		--title "QSS-M $VERSION" --notes-file "$NOTES" "${ASSETS[@]}"
fi
echo "Draft release ready: https://github.com/$REPO/releases (review, then publish as Latest)"
