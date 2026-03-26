#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
. "$REPO_DIR/ci-version.sh"

cd "$REPO_DIR"

echo "Git URL:      https://github.com/timbergeron/QSS-M.git" > QSS-M-Revision.txt
echo "Git Revision: `git rev-parse HEAD`" >> QSS-M-Revision.txt
echo "Git Date:     `git log -1 --date=short --format=%cd`" >> QSS-M-Revision.txt
echo "Compile Date: `date`" >> QSS-M-Revision.txt
export SOURCE_DATE_EPOCH=$(git log -1 --date=short --format=%ct)

cd Quake/
if command -v nproc >/dev/null 2>&1; then
  MAKEARGS="-j`nproc`"
else
  MAKEARGS="-j2"
fi

export QSS_CFLAGS="$(qssm_build_cflags)"
export QSS_LDFLAGS="-Wl,--allow-multiple-definition"

make clean
make USE_SDL2=1 DEBUG=1 $MAKEARGS
mv quakespasm quakespasm-valgrind
