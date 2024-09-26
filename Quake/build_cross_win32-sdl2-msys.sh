#!/bin/sh

MAKE_CMD=make

# Use the default compilers and tools from MSYS2.
CC=gcc
AS=as
RANLIB=ranlib
AR=ar
WINDRES=windres
STRIP=strip

export CC AS AR RANLIB WINDRES STRIP

exec $MAKE_CMD USE_SDL2=1 -f Makefile.w32 "$@"
