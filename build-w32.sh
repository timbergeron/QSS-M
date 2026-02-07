#!/bin/sh
set -eu

echo "Git URL:      https://github.com/timbergeron/QSS-M.git" > QSS-M-Revision.txt
echo "Git Revision: `git rev-parse HEAD`" >> QSS-M-Revision.txt
echo "Git Date:     `git log -1 --date=short --format=%cd`" >> QSS-M-Revision.txt
echo "Compile Date: `date`" >> QSS-M-Revision.txt
export SOURCE_DATE_EPOCH=$(git log -1 --date=short --format=%ct)

cd Quake/
MAKEARGS="${MAKEARGS:--j8}"
TARGET_TRIPLET="${TARGET_TRIPLET:-i686-w64-mingw32}"

# Make win32
export QSS_CFLAGS="-DQSS_REVISION=`git rev-parse HEAD`"
export QSS_LDFLAGS="-Wl,--allow-multiple-definition"

make -f Makefile.w32 clean

# shellcheck disable=SC2086
./build_cross_win32-sdl2.sh $MAKEARGS

if [ ! -f quakespasm.exe ]; then
	echo "ERROR: quakespasm.exe was not produced."
	exit 1
fi
mv quakespasm.exe QSS-M-w32.exe

GNUTLS_DLLS=""
DLL_SEARCH_DIRS="/usr/$TARGET_TRIPLET/bin /usr/lib/gcc/$TARGET_TRIPLET/* /opt/cross/$TARGET_TRIPLET/bin /opt/cross_win32/bin /opt/cross_win32/$TARGET_TRIPLET/bin ${CROSS_GNUTLS_DLL_DIRS:-}"
for pat in libgnutls-*.dll libnettle-*.dll libhogweed-*.dll libgmp-*.dll libidn2-*.dll libunistring-*.dll libtasn1-*.dll libiconv-*.dll libp11-kit-*.dll libffi-*.dll
do
	for dir in $DLL_SEARCH_DIRS; do
		for dll in $dir/$pat; do
			[ -f "$dll" ] || continue
			case " $GNUTLS_DLLS " in
				*" $dll "*) ;;
				*) GNUTLS_DLLS="$GNUTLS_DLLS $dll" ;;
			esac
		done
	done
done

# shellcheck disable=SC2086
zip -9j QSS-M-w32.zip ../Windows/codecs/x86/*.dll ../Windows/curl/lib/x86/libcurl.dll ../Windows/zlib/x86/zlib1.dll ../LICENSE.txt ../Quakespasm.html quakespasm.pak qssm.pak ../Quakespasm.txt ../Quakespasm-Spiked.txt ../Quakespasm-Music.txt ../Windows/SDL2/lib/SDL2.dll ../QSS-M-Revision.txt QSS-M-w32.exe $GNUTLS_DLLS
make -f Makefile.w32 clean
