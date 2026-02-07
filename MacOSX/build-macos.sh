#!/bin/bash
set -e

./setup-vcpkg.sh

xcodebuild -project QuakeSpasm.xcodeproj -target QSS-M -configuration Release

cat <<EOF > build/Release/Quakespasm-Spiked-Revision.txt
Git URL:      $(git config --get remote.origin.url)
Git Revision: $(git rev-parse HEAD)
Git Date:     $(git show --no-patch --no-notes --pretty='%ai' HEAD)
Compile Date: $(date)
EOF

# zip the files in `build/Release` to create the final archive for distribution
cd build/Release
rm -f QSS-M_MacOS.zip
zip --symlinks --recurse-paths QSS-M_MacOS.zip *
