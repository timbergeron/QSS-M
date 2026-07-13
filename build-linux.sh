#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/ci-version.sh"

cd "$SCRIPT_DIR"

echo "Git URL:      https://github.com/timbergeron/QSS-M.git" > QSS-M-Revision.txt
echo "Git Revision: `git rev-parse HEAD`" >> QSS-M-Revision.txt
echo "Git Date:     `git log -1 --date=short --format=%cd`" >> QSS-M-Revision.txt
echo "Compile Date: `date`" >> QSS-M-Revision.txt
export SOURCE_DATE_EPOCH=$(git log -1 --date=short --format=%ct)

cd Quake/
MAKEARGS="-j8"

# Make Linux64
export QSS_CFLAGS="$(qssm_build_cflags)"
export QSS_LDFLAGS="-Wl,--allow-multiple-definition"
make clean
make USE_SDL2=1 $MAKEARGS
mv quakespasm QSS-M-l64
zip -9j QSS-M-l64.zip ../LICENSE.txt ../Quakespasm.html quakespasm.pak qssm.pak gamecontrollerdb.txt ../Quakespasm.txt ../Quakespasm-Spiked.txt ../Quakespasm-Music.txt ../QSS-M-Revision.txt QSS-M-l64
make clean
