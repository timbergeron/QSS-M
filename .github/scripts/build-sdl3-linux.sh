#!/bin/sh
# Build and install a pinned SDL3 release for Linux CI.
#
# Usage: build-sdl3-linux.sh <version> <commit> <prefix>
#
# Fetches exactly <commit> (the commit behind SDL's release-<version> tag),
# fails unless SDL enabled the video, audio and joystick backends QSS-M relies
# on, and installs a shared library into <prefix>. A prefix already holding the
# same version and commit is reused, so the prefix can be cached.

set -eu

if [ "$#" -ne 3 ]; then
	echo "usage: $0 <version> <commit> <prefix>" >&2
	exit 2
fi
version=$1
commit=$2
prefix=$3
marker="$prefix/.qssm-sdl3-commit-no-rpath"

if [ -f "$marker" ] && [ "$(cat "$marker")" = "$commit" ] &&
	[ "$(PKG_CONFIG_PATH="$prefix/lib/pkgconfig" pkg-config --modversion sdl3 2>/dev/null)" = "$version" ]; then
	echo "SDL3 $version ($commit) is already installed in $prefix"
	exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git init -q "$work/src"
git -C "$work/src" fetch -q --depth 1 https://github.com/libsdl-org/SDL.git "$commit"
git -C "$work/src" checkout -q --detach FETCH_HEAD
actual=$(git -C "$work/src" rev-parse HEAD)
if [ "$actual" != "$commit" ]; then
	echo "SDL3 $version: fetched $actual, expected $commit" >&2
	exit 1
fi

# Keep the temporary CI install directory out of SDL's pkg-config link flags;
# release executables supply their own $ORIGIN path for the bundled runtime.
if ! cmake -S "$work/src" -B "$work/build" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$prefix" \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DSDL_SHARED=ON -DSDL_STATIC=OFF \
	-DSDL_RPATH=OFF \
	-DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
	-DSDL_X11=ON -DSDL_WAYLAND=ON \
	-DSDL_ALSA=ON -DSDL_PULSEAUDIO=ON -DSDL_PIPEWIRE=ON \
	-DSDL_HIDAPI=ON -DSDL_LIBUDEV=ON -DSDL_DBUS=ON \
	>"$work/configure.log" 2>&1; then
	cat "$work/configure.log"
	exit 1
fi
cat "$work/configure.log"

# SDL prints each option as "--   SDL_X11   (Wanted: ON): ON"; a wanted backend
# whose dependencies are missing reports OFF instead of failing the configure.
missing=
for option in SDL_X11 SDL_WAYLAND SDL_ALSA SDL_PULSEAUDIO SDL_PIPEWIRE SDL_HIDAPI SDL_LIBUDEV SDL_DBUS; do
	if ! grep -Eq "^-- +$option +\(Wanted: [^)]*\): (ON|TRUE|1)$" "$work/configure.log"; then
		missing="$missing $option"
	fi
done
if [ -n "$missing" ]; then
	echo "SDL3 $version was configured without:$missing" >&2
	exit 1
fi

cmake --build "$work/build"
rm -rf "$prefix"
cmake --install "$work/build"
echo "$commit" >"$marker"
echo "Installed SDL3 $(PKG_CONFIG_PATH="$prefix/lib/pkgconfig" pkg-config --modversion sdl3) in $prefix"
