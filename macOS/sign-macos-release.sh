#!/bin/bash
# Developer ID sign, notarize and staple a QSS-M macOS release zip.
#
#   macOS/sign-macos-release.sh <in.zip> <out.zip> [--no-notarize]
#
# <in.zip> is the CI package (QSS-M-macOS.zip): QSS-M.app plus docs and paks
# at the archive root. <out.zip> keeps that layout with the app replaced by a
# signed (and, unless --no-notarize, notarized and stapled) copy.
#
# Environment:
#   QSSM_SIGN_IDENTITY  codesign identity (default: the keychain's
#                       "Developer ID Application" certificate)
#   QSSM_NOTARY_PROFILE notarytool keychain profile (default: notary), created
#                       once with `xcrun notarytool store-credentials notary`
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ENTITLEMENTS="$SCRIPT_DIR/Resources/QSS-M-Release.entitlements"
EXPECTED_BUNDLE_ID=com.quakeone.qssm

IN_ZIP=${1:?usage: $0 <in.zip> <out.zip> [--no-notarize]}
OUT_ZIP=${2:?usage: $0 <in.zip> <out.zip> [--no-notarize]}
NOTARIZE=1
[ "${3:-}" = "--no-notarize" ] && NOTARIZE=0
PROFILE=${QSSM_NOTARY_PROFILE:-notary}

fail() { echo "error: $*" >&2; exit 1; }

IDENTITY=${QSSM_SIGN_IDENTITY:-}
if [ -z "$IDENTITY" ]; then
	IDENTITY=$(security find-identity -v -p codesigning |
		awk -F'"' '/Developer ID Application/ { print $2; exit }')
	[ -n "$IDENTITY" ] || fail "no Developer ID Application identity in the keychain"
fi
TEAM_ID=$(sed -n 's/.*(\([A-Z0-9]\{10\}\))$/\1/p' <<<"$IDENTITY")
[ -n "$TEAM_ID" ] || fail "could not read a team ID from identity '$IDENTITY'"
echo "Signing as: $IDENTITY"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/qssm-sign.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
STAGE="$WORK/stage"
mkdir "$STAGE"
ditto -x -k "$IN_ZIP" "$STAGE"

APP=$(find "$STAGE" -maxdepth 1 -name '*.app' -type d | head -1)
[ -n "$APP" ] || fail "no .app at the root of $IN_ZIP"
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")" = "$EXPECTED_BUNDLE_ID" ] ||
	fail "unexpected bundle identifier in $APP"

# CI output is ad-hoc signed and may carry the get-task-allow debug entitlement;
# --force replaces every signature. Sign inside-out: nested code, then the app.
sign() { codesign --force --timestamp --options runtime --sign "$IDENTITY" "$@"; }

while IFS= read -r -d '' dylib; do
	sign "$dylib"
done < <(find "$APP/Contents" -name '*.dylib' -type f -print0)
for fw in "$APP"/Contents/Frameworks/*.framework; do
	[ -d "$fw" ] && sign "$fw"
done
for plugin in "$APP"/Contents/PlugIns/*; do
	[ -d "$plugin" ] && sign "$plugin"
done
sign --entitlements "$ENTITLEMENTS" "$APP"

# Every Mach-O in the bundle must now carry this team's signature.
while IFS= read -r -d '' f; do
	case "$(file -b "$f")" in *Mach-O*) ;; *) continue ;; esac
	team=$(codesign -dv "$f" 2>&1 | sed -n 's/^TeamIdentifier=//p')
	[ "$team" = "$TEAM_ID" ] || fail "$f is signed by team '$team', expected $TEAM_ID"
done < <(find "$APP" -type f -perm -u+x -print0)
codesign --verify --deep --strict --verbose=2 "$APP"
entitlements=$(codesign -d --entitlements - "$APP" 2>/dev/null || true)
case "$entitlements" in
	*get-task-allow*) fail "signed app still has get-task-allow" ;;
esac
# Capture first: grep -q closing the pipe early would trip pipefail.
details=$(codesign -dv "$APP" 2>&1)
grep -q 'flags=0x10000(runtime)' <<<"$details" || fail "hardened runtime not enabled"

if [ "$NOTARIZE" = 1 ]; then
	SUBMIT="$WORK/notarize.zip"
	ditto -c -k --keepParent "$APP" "$SUBMIT"
	echo "Submitting to Apple notary service (profile: $PROFILE)..."
	result=$(xcrun notarytool submit "$SUBMIT" --keychain-profile "$PROFILE" --wait --output-format json)
	status=$(plutil -extract status raw -o - - <<<"$result")
	id=$(plutil -extract id raw -o - - <<<"$result")
	if [ "$status" != Accepted ]; then
		xcrun notarytool log "$id" --keychain-profile "$PROFILE" >&2 || true
		fail "notarization $id finished with status $status"
	fi
	echo "Notarization $id accepted"
	xcrun stapler staple "$APP"
	xcrun stapler validate "$APP"
	spctl --assess --type execute --verbose=2 "$APP"
fi

rm -f "$OUT_ZIP"
ditto -c -k "$STAGE" "$OUT_ZIP"
echo "Wrote $OUT_ZIP"
