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

python3 "$SCRIPT_DIR/Windows/build-static.py" --arch x86

cd Quake/
MAKEARGS="${MAKEARGS:--j8}"

# Make win32
export QSS_CFLAGS="$(qssm_build_cflags)"
export QSS_LDFLAGS="-Wl,--allow-multiple-definition"

make -f Makefile.w32 clean

# shellcheck disable=SC2086
./build_cross_win32.sh $MAKEARGS

if [ ! -f quakespasm.exe ]; then
	echo "ERROR: quakespasm.exe was not produced."
	exit 1
fi
mv quakespasm.exe QSS-M-w32.exe

# Recreate the archive so removed runtimes do not survive a rebuild.
rm -f QSS-M-w32.zip
# shellcheck disable=SC2086
zip -9j QSS-M-w32.zip ../LICENSE.txt ../Quakespasm.html quakespasm.pak qssm.pak gamecontrollerdb.txt ../Quakespasm.txt ../Quakespasm-Spiked.txt ../Quakespasm-Music.txt ../QSS-M-Revision.txt QSS-M-w32.exe ../build/windows-static/mingw-x86/prefix/third-party-licenses.txt ../Windows/static-sources.json
python3 "$SCRIPT_DIR/Windows/verify-static.py" QSS-M-w32.exe --archive QSS-M-w32.zip
make -f Makefile.w32 clean
