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
AS="${AS:-$TARGET-as}"
RANLIB="${RANLIB:-$TARGET-ranlib}"
AR="${AR:-$TARGET-ar}"
WINDRES="${WINDRES:-$TARGET-windres}"
STRIP="${STRIP:-$TARGET-strip}"

if [ -z "${PKG_CONFIG:-}" ]; then
	if command -v "$TARGET-pkg-config" >/dev/null 2>&1; then
		PKG_CONFIG="$TARGET-pkg-config"
	elif command -v "$TARGET-pkgconf" >/dev/null 2>&1; then
		PKG_CONFIG="$TARGET-pkgconf"
	else
		PKG_CONFIG="pkg-config"
	fi
fi

if [ "$PKG_CONFIG" = "pkg-config" ] && [ -z "${PKG_CONFIG_LIBDIR:-}" ]; then
	if [ -d "$PREFIX/$TARGET/lib/pkgconfig" ] || [ -d "$PREFIX/$TARGET/share/pkgconfig" ]; then
		PKG_CONFIG_LIBDIR="$PREFIX/$TARGET/lib/pkgconfig:$PREFIX/$TARGET/share/pkgconfig"
	elif [ -d "$PREFIX/lib/pkgconfig" ] || [ -d "$PREFIX/share/pkgconfig" ]; then
		PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
	elif [ -d "/usr/$TARGET/lib/pkgconfig" ] || [ -d "/usr/$TARGET/share/pkgconfig" ]; then
		PKG_CONFIG_LIBDIR="/usr/$TARGET/lib/pkgconfig:/usr/$TARGET/share/pkgconfig"
	elif [ -d "/usr/lib/$TARGET/pkgconfig" ]; then
		PKG_CONFIG_LIBDIR="/usr/lib/$TARGET/pkgconfig"
	elif [ -d "/usr/local/$TARGET/lib/pkgconfig" ] || [ -d "/usr/local/$TARGET/share/pkgconfig" ]; then
		PKG_CONFIG_LIBDIR="/usr/local/$TARGET/lib/pkgconfig:/usr/local/$TARGET/share/pkgconfig"
	fi
fi
export PATH CC AS AR RANLIB WINDRES STRIP PKG_CONFIG PKG_CONFIG_LIBDIR PKG_CONFIG_PATH

exec "$MAKE_CMD" USE_SDL2=1 CC="$CC" AS="$AS" RANLIB="$RANLIB" AR="$AR" WINDRES="$WINDRES" STRIP="$STRIP" PKG_CONFIG="$PKG_CONFIG" -f Makefile.w64 "$@"
