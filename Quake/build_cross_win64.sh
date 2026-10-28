#!/bin/sh

# Change this script to meet your needs and/or environment.

TARGET="${TARGET:-x86_64-w64-mingw32}"
PREFIX="${CROSS_PREFIX:-${PREFIX:-/opt/cross_win64}}"

if [ -d "$PREFIX/bin" ]; then
	PATH="$PREFIX/bin:$PATH"
fi
export PATH

MAKE_CMD="${MAKE_CMD:-make}"

CC="${CC:-$TARGET-gcc}"
CXX="${CXX:-$TARGET-g++}"
AS="${AS:-$TARGET-as}"
RANLIB="${RANLIB:-$TARGET-ranlib}"
AR="${AR:-$TARGET-ar}"
WINDRES="${WINDRES:-$TARGET-windres}"
STRIP="${STRIP:-$TARGET-strip}"

export PATH CC CXX AS AR RANLIB WINDRES STRIP

exec "$MAKE_CMD" CC="$CC" CXX="$CXX" AS="$AS" RANLIB="$RANLIB" AR="$AR" WINDRES="$WINDRES" STRIP="$STRIP" -f Makefile.w64 "$@"
