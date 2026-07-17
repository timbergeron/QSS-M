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
dmg_staging=
cleanup()
{
  rm -f "$link_log"
  if [ -n "$dmg_staging" ]; then
    rm -rf "$dmg_staging"
  fi
}
trap cleanup EXIT
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

# Package only distribution files. Xcode also leaves separately built products
# such as QSSDockTilePlugin.docktileplugin beside the application bundle.
release_files=(
  LICENSE.txt
  QSS-M.app
  Quakespasm-Music.txt
  Quakespasm-Spiked-Revision.txt
  Quakespasm-Spiked.txt
  Quakespasm.html
  Quakespasm.txt
  gamecontrollerdb.txt
  macos_instructions.html
  qssm.pak
  quakespasm.pak
)

cd build/Release
for release_file in "${release_files[@]}"; do
  if [ ! -e "$release_file" ]; then
    echo "Release file missing: $release_file" >&2
    exit 1
  fi
done
rm -f QSS-M-macOS.zip
zip --symlinks --recurse-paths QSS-M-macOS.zip "${release_files[@]}"

"$SCRIPT_DIR/verify-macos-release.sh" \
  "$SCRIPT_DIR/build/Release/QSS-M.app" \
  "$SCRIPT_DIR/build/Release/QSS-M-macOS.zip"

# QSS-M uses the directory containing the app as its game directory, so keep
# the app and its companion files together in one folder for drag installation.
dmg_staging=$(mktemp -d "${TMPDIR:-/tmp}/qssm-dmg.XXXXXX")
mkdir "$dmg_staging/QSS-M"
for release_file in "${release_files[@]}"; do
  ditto "$release_file" "$dmg_staging/QSS-M/$release_file"
done
ln -s /Applications "$dmg_staging/Applications"

rm -f QSS-M-macOS.dmg
hdiutil create \
  -volname QSS-M \
  -srcfolder "$dmg_staging" \
  -format UDZO \
  -imagekey zlib-level=9 \
  QSS-M-macOS.dmg
hdiutil verify QSS-M-macOS.dmg
