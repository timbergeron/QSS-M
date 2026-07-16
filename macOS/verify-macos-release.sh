#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(
	CDPATH= cd -- "$(dirname -- "$0")" && pwd
)"

APP_PATH=${1:-"$SCRIPT_DIR/build/Release/QSS-M.app"}
ARCHIVE_PATH=${2:-"$SCRIPT_DIR/build/Release/QSS-M-macOS.zip"}
HEADER="$SCRIPT_DIR/../Quake/quakedef.h"
SHARED_CONFIG="$SCRIPT_DIR/Configurations/Shared.xcconfig"
EXPECTED_BUNDLE_ID=com.quakeone.qssm

fail()
{
	echo "error: $*" >&2
	exit 1
}

read_component()
{
	awk -v macro="$1" '$1 == "#define" && $2 == macro { print $3; found = 1; exit }
		END { if (!found) exit 1 }' "$HEADER"
}

validate_component()
{
	local name=$1
	local value=$2

	case "$value" in
		''|*[!0-9]*) fail "$name must be a decimal integer, got '$value'" ;;
	esac
	case "$value" in
		0|[1-9]*) ;;
		*) fail "$name must not contain leading zeroes, got '$value'" ;;
	esac
}

plist_value()
{
	/usr/libexec/PlistBuddy -c "Print :$2" "$1"
}

config_value()
{
	awk -F= -v setting="$1" '
		{
			key = $1
			gsub(/^[[:space:]]+|[[:space:]]+$/, "", key)
			if (key == setting) {
				value = $2
				gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
				print value
				exit
			}
		}' "$SHARED_CONFIG"
}

require_equal()
{
	local label=$1
	local actual=$2
	local expected=$3

	if [ "$actual" != "$expected" ]; then
		fail "$label is '$actual'; expected '$expected'"
	fi
}

require_arches()
{
	local binary=$1
	local archs
	local arch
	local count=0

	archs=$(lipo -archs "$binary")
	for arch in $archs; do
		case "$arch" in
			x86_64|arm64) ;;
			*) fail "$binary contains unexpected architecture '$arch'" ;;
		esac
		count=$((count + 1))
	done
	[ "$count" -eq 2 ] || fail "$binary is not exactly x86_64 and arm64: $archs"
	case " $archs " in *" x86_64 "*) ;; *) fail "$binary is missing x86_64" ;; esac
	case " $archs " in *" arm64 "*) ;; *) fail "$binary is missing arm64" ;; esac
}

minimum_macos()
{
	xcrun vtool -arch "$1" -show-build "$2" |
		awk '$1 == "minos" || $1 == "version" { print $2; exit }'
}

verify_app()
{
	local app=$1
	local plist="$app/Contents/Info.plist"
	local executable="$app/Contents/MacOS/QSS-M"
	local framework="$app/Contents/Frameworks/SDL2.framework/Versions/A/SDL2"
	local bad_dependency
	local entitlements

	[ -d "$app" ] || fail "application bundle not found at $app"
	[ -f "$plist" ] || fail "bundle Info.plist not found at $plist"
	[ -f "$executable" ] || fail "bundle executable not found at $executable"
	[ -f "$framework" ] || fail "embedded SDL2 framework not found at $framework"

	plutil -lint "$plist" >/dev/null
	require_equal CFBundleIdentifier "$(plist_value "$plist" CFBundleIdentifier)" "$EXPECTED_BUNDLE_ID"
	require_equal CFBundleName "$(plist_value "$plist" CFBundleName)" QSS-M
	require_equal CFBundleExecutable "$(plist_value "$plist" CFBundleExecutable)" QSS-M
	require_equal CFBundleShortVersionString "$(plist_value "$plist" CFBundleShortVersionString)" "$EXPECTED_VERSION"
	require_equal CFBundleVersion "$(plist_value "$plist" CFBundleVersion)" "$EXPECTED_BUILD_NUMBER"
	require_equal LSMinimumSystemVersion "$(plist_value "$plist" LSMinimumSystemVersion)" 10.13
	if /usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersionByArchitecture' "$plist" >/dev/null 2>&1; then
		fail "$plist contains obsolete LSMinimumSystemVersionByArchitecture metadata"
	fi

	require_arches "$executable"
	require_equal "x86_64 minimum macOS" "$(minimum_macos x86_64 "$executable")" 10.13
	require_equal "arm64 minimum macOS" "$(minimum_macos arm64 "$executable")" 11.0
	require_arches "$framework"

	bad_dependency=$(otool -L "$executable" | awk 'NR > 1 { print $1 }' |
		grep -E '^(/opt/homebrew|/usr/local|.*/vcpkg/)' || true)
	[ -z "$bad_dependency" ] || fail "bundle has non-system build-host dependencies: $bad_dependency"
	otool -L "$executable" | grep -F '@rpath/SDL2.framework/Versions/A/SDL2' >/dev/null ||
		fail "QSS-M does not link its embedded SDL2 framework through @rpath"

	codesign --verify --deep --strict --verbose=2 "$app"
	entitlements=$(codesign -d --entitlements - "$app" 2>/dev/null || true)
	case "$entitlements" in
		*com.apple.security.get-task-allow*)
			fail "$app contains the development-only get-task-allow entitlement"
			;;
	esac
}

[ -f "$HEADER" ] || fail "version header not found at $HEADER"
MAJOR=$(read_component QSSM_VER_MAJOR) || fail "could not parse QSSM_VER_MAJOR from $HEADER"
MINOR=$(read_component QSSM_VER_MINOR) || fail "could not parse QSSM_VER_MINOR from $HEADER"
PATCH=$(read_component QSSM_VER_PATCH) || fail "could not parse QSSM_VER_PATCH from $HEADER"
validate_component QSSM_VER_MAJOR "$MAJOR"
validate_component QSSM_VER_MINOR "$MINOR"
validate_component QSSM_VER_PATCH "$PATCH"
[ "$MINOR" -lt 1000 ] && [ "$PATCH" -lt 1000 ] ||
	fail "minor and patch versions must remain below 1000 for monotonic build numbers"
EXPECTED_VERSION="${MAJOR}.${MINOR}.${PATCH}"
EXPECTED_BUILD_NUMBER=$((MAJOR * 1000000 + MINOR * 1000 + PATCH))

[ -f "$SHARED_CONFIG" ] || fail "shared Xcode configuration not found at $SHARED_CONFIG"
require_equal "MARKETING_VERSION in Shared.xcconfig" \
	"$(config_value MARKETING_VERSION)" "$EXPECTED_VERSION"
require_equal "CURRENT_PROJECT_VERSION in Shared.xcconfig" \
	"$(config_value CURRENT_PROJECT_VERSION)" "$EXPECTED_BUILD_NUMBER"

verify_app "$APP_PATH"

[ -f "$ARCHIVE_PATH" ] || fail "release archive not found at $ARCHIVE_PATH"
unzip -tq "$ARCHIVE_PATH"

ARCHIVE_TOP_LEVEL=$(unzip -Z1 "$ARCHIVE_PATH" | awk -F/ 'NF { print $1 }' | LC_ALL=C sort -u)
EXPECTED_TOP_LEVEL=$(printf '%s\n' \
	LICENSE.txt \
	QSS-M.app \
	Quakespasm-Music.txt \
	Quakespasm-Spiked-Revision.txt \
	Quakespasm-Spiked.txt \
	Quakespasm.html \
	Quakespasm.txt \
	gamecontrollerdb.txt \
	macos_instructions.html \
	qssm.pak \
	quakespasm.pak | LC_ALL=C sort)
require_equal "archive top-level contents" "$ARCHIVE_TOP_LEVEL" "$EXPECTED_TOP_LEVEL"

TEMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/qssm-release.XXXXXX")
trap 'rm -rf "$TEMP_DIR"' EXIT
ditto -x -k "$ARCHIVE_PATH" "$TEMP_DIR"
verify_app "$TEMP_DIR/QSS-M.app"

echo "Verified QSS-M macOS ${EXPECTED_VERSION} (${EXPECTED_BUILD_NUMBER}) release archive"
