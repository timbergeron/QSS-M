#!/bin/bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
	echo "usage: $0 <quakedef.h> <Info.plist> <output-stamp>" >&2
	exit 2
fi

HEADER=$1
PLIST=$2
OUTPUT_STAMP=$3

if [ ! -f "$HEADER" ]; then
	echo "error: version header not found at $HEADER" >&2
	exit 1
fi
if [ ! -f "$PLIST" ]; then
	echo "error: built Info.plist not found at $PLIST" >&2
	exit 1
fi

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
		''|*[!0-9]*)
			echo "error: $name must be a decimal integer, got '$value'" >&2
			exit 1
			;;
	esac
	case "$value" in
		0|[1-9]*) ;;
		*)
			echo "error: $name must not contain leading zeroes, got '$value'" >&2
			exit 1
			;;
	esac
}

MAJOR=$(read_component QSSM_VER_MAJOR) || {
	echo "error: could not parse QSSM_VER_MAJOR from $HEADER" >&2
	exit 1
}
MINOR=$(read_component QSSM_VER_MINOR) || {
	echo "error: could not parse QSSM_VER_MINOR from $HEADER" >&2
	exit 1
}
PATCH=$(read_component QSSM_VER_PATCH) || {
	echo "error: could not parse QSSM_VER_PATCH from $HEADER" >&2
	exit 1
}

validate_component QSSM_VER_MAJOR "$MAJOR"
validate_component QSSM_VER_MINOR "$MINOR"
validate_component QSSM_VER_PATCH "$PATCH"

if [ "$MINOR" -ge 1000 ] || [ "$PATCH" -ge 1000 ]; then
	echo "error: minor and patch versions must remain below 1000 for monotonic build numbers" >&2
	exit 1
fi

VERSION="${MAJOR}.${MINOR}.${PATCH}"
BUILD_NUMBER=$((MAJOR * 1000000 + MINOR * 1000 + PATCH))

/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString ${VERSION}" "$PLIST"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion ${BUILD_NUMBER}" "$PLIST"
/usr/bin/touch "$OUTPUT_STAMP"

echo "Set bundle version to ${VERSION} (${BUILD_NUMBER})"
