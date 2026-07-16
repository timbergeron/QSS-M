#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(
  CDPATH= cd -- "$(dirname -- "$0")" && pwd
)"
. "$SCRIPT_DIR/../ci-version.sh"

cd "$SCRIPT_DIR"
./setup-vcpkg.sh

QSSM_XCODE_EXTRA_CFLAGS="$(qssm_xcode_other_cflags)"
xcodebuild_args=(
  -project QuakeSpasm.xcodeproj
  -target QSS-M
  -configuration Release
)

if [ -n "$QSSM_XCODE_EXTRA_CFLAGS" ]; then
  OTHER_CFLAGS_ARG="\$(inherited) $QSSM_XCODE_EXTRA_CFLAGS"
  xcodebuild_args+=(OTHER_CFLAGS="$OTHER_CFLAGS_ARG")
fi

link_log="$(mktemp "${TMPDIR:-/tmp}/qssm-xcodebuild.XXXXXX")"
trap 'rm -f "$link_log"' EXIT
xcodebuild "${xcodebuild_args[@]}" clean build 2>&1 | tee "$link_log"

if grep -Fq "was built for newer 'macOS' version" "$link_log"; then
  echo "Release build contains objects newer than the configured macOS deployment target."
  exit 1
fi

cat <<EOF > build/Release/Quakespasm-Spiked-Revision.txt
Git URL:      $(git config --get remote.origin.url)
Git Revision: $(git rev-parse HEAD)
Git Date:     $(git show --no-patch --no-notes --pretty='%ai' HEAD)
Compile Date: $(date)
EOF

cp "$SCRIPT_DIR/../Quake/gamecontrollerdb.txt" build/Release/

# zip the files in `build/Release` to create the final archive for distribution
cd build/Release
rm -f QSS-M-macOS.zip
zip --symlinks --recurse-paths QSS-M-macOS.zip *

"$SCRIPT_DIR/verify-macos-release.sh" \
  "$SCRIPT_DIR/build/Release/QSS-M.app" \
  "$SCRIPT_DIR/build/Release/QSS-M-macOS.zip"
