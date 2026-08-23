# GNU Makefile for QuakeSpasm unix targets.
# You need the SDL library fully installed.
# "make DEBUG=1" to build a debug client.
# "make SDL_CONFIG=/path/to/sdl-config" for unusual SDL installations.
# "make DO_USERDIRS=1" to enable user directories support

# Enable/Disable user directories support
DO_USERDIRS=0

### Enable/Disable SDL2
USE_SDL2=1

### Enable the use of zlib, for compressed pk3s.
USE_ZLIB=1

### Enable the use of gnutls, for dtls/wss(tls) support
USE_GNUTLS=1

### Do we need to link the math library?
LINK_M ?= 1

### Enable/Disable codecs for streaming music support
USE_CODEC_WAVE=1
USE_CODEC_FLAC=0
USE_CODEC_MP3=1
USE_CODEC_VORBIS=1
USE_CODEC_OPUS=1
# either xmp or mikmod (or modplug)
USE_CODEC_MIKMOD=0
USE_CODEC_XMP=0
USE_CODEC_MODPLUG=0
USE_CODEC_UMX=0

# which library to use for mp3 decoding: mad or mpg123
MP3LIB=mad
# which library to use for ogg decoding: vorbis or tremor
VORBISLIB=vorbis

# ---------------------------
# Helper functions
# ---------------------------

check_gcc = $(shell if echo | $(CC) $(1) -Werror -S -o /dev/null -xc - > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi;)

# ---------------------------

HOST_OS = $(shell uname|sed -e s/_.*//|tr '[:upper:]' '[:lower:]')

DEBUG   ?= 0

# ---------------------------
# build variables
# ---------------------------

CC ?= gcc
LINKER = $(CC)

STRIP ?= strip
PKG_CONFIG ?= pkg-config

GNUTLS_PKG_OK := $(shell $(PKG_CONFIG) --exists gnutls >/dev/null 2>&1 && echo 1 || echo 0)
GNUTLS_CFLAGS := $(shell $(PKG_CONFIG) --cflags gnutls 2>/dev/null)
GNUTLS_LIBS := $(shell $(PKG_CONFIG) --libs gnutls 2>/dev/null)

CPUFLAGS=
LDFLAGS =
DFLAGS ?=
CFLAGS ?= -Wall -Wno-trigraphs -MMD
CFLAGS += $(call check_gcc,-std=gnu11,)
CFLAGS += $(CPUFLAGS)
ifneq ($(DEBUG),0)
DFLAGS += -DDEBUG
CFLAGS += -g
do_strip=
else
DFLAGS += -DNDEBUG
CFLAGS += -O2
#CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += $(call check_gcc,-fweb,)
CFLAGS += $(call check_gcc,-frename-registers,)
cmd_strip=$(STRIP) $(1)
define do_strip
	$(call cmd_strip,$(1));
endef
endif

CFLAGS += $(QSS_CFLAGS)
LDFLAGS += $(QSS_LDFLAGS)
ifeq ($(DO_USERDIRS),1)
CFLAGS += -DDO_USERDIRS=1
endif

### X11BASE only gets used if its in an unusual place

X11DIRS := /usr/X11R7 /usr/local/X11R7 /usr/X11R6 /usr/local/X11R6
X11BASE_GUESS := $(shell \
	if [ -e /usr/include/X11/Xlib.h ] && \
		[ -e /usr/lib/libX11.a ]; then exit 0; fi; \
	if [ -e /usr/local/include/X11/Xlib.h ] && \
		[ -e /usr/local/lib/libX11.a ]; then exit 0; fi; \
	for DIR in $(X11DIRS); do \
            if [ -e $$DIR/include/X11/Xlib.h ] && \
		[ -e $$DIR/lib/libX11.a ]; then echo $$DIR; break; fi; \
	done )

X11BASE	?= $(X11BASE_GUESS)

ifneq ($(X11BASE),)
LDFLAGS+= -L$(X11BASE)/lib
CFLAGS += -I$(X11BASE)/include
endif

ifeq ($(USE_SDL2),1)
CFLAGS += -DUSE_SDL2
endif
#make use of LFS on 32bit even on systems.
CFLAGS += -D_FILE_OFFSET_BITS=64

ifeq ($(USE_SDL2),1)
SDL_CONFIG ?= sdl2-config
else
SDL_CONFIG ?= sdl-config
endif
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS   := $(shell $(SDL_CONFIG) --libs)

ifeq ($(HOST_OS),sunos)
NET_LIBS   :=-lsocket -lnsl -lresolv
else ifeq ($(HOST_OS),haiku)
NET_LIBS   :=-lnetwork
else
NET_LIBS   :=
endif

NET_LIBS   += -lcurl
CFLAGS     += $(shell $(PKG_CONFIG) --cflags libcurl)

ifneq ($(VORBISLIB),vorbis)
ifneq ($(VORBISLIB),tremor)
$(error Invalid VORBISLIB setting)
endif
endif
ifneq ($(MP3LIB),mpg123)
ifneq ($(MP3LIB),mad)
$(error Invalid MP3LIB setting)
endif
endif
ifeq ($(MP3LIB),mad)
mp3_obj=snd_mp3
lib_mp3dec=-lmad
endif
ifeq ($(MP3LIB),mpg123)
mp3_obj=snd_mpg123
lib_mp3dec=-lmpg123
CFLAGS+= -DMP3LIB_MPG123
endif
ifeq ($(VORBISLIB),vorbis)
cpp_vorbisdec=
lib_vorbisdec=-lvorbisfile -lvorbis -logg
endif
ifeq ($(VORBISLIB),tremor)
cpp_vorbisdec=-DVORBIS_USE_TREMOR
lib_vorbisdec=-lvorbisidec -logg
endif

CODECLIBS  :=
ifeq ($(USE_CODEC_WAVE),1)
CFLAGS+= -DUSE_CODEC_WAVE
endif
ifeq ($(USE_CODEC_FLAC),1)
CFLAGS+= -DUSE_CODEC_FLAC
CODECLIBS+= -lFLAC
endif
ifeq ($(USE_CODEC_OPUS),1)
# opus and opusfile put their *.h under <includedir>/opus,
# but they include the headers without the opus directory
# prefix and rely on pkg-config. ewww...
CFLAGS+= -DUSE_CODEC_OPUS
CFLAGS+= $(shell $(PKG_CONFIG) --cflags opusfile)
CODECLIBS+= $(shell $(PKG_CONFIG) --libs   opusfile)
CODECLIBS+= $(shell $(PKG_CONFIG) --libs   opus)
endif
ifeq ($(USE_CODEC_VORBIS),1)
CFLAGS+= -DUSE_CODEC_VORBIS $(cpp_vorbisdec)
CODECLIBS+= $(lib_vorbisdec)
endif
ifeq ($(USE_CODEC_MP3),1)
CFLAGS+= -DUSE_CODEC_MP3
CODECLIBS+= $(lib_mp3dec)
endif
ifeq ($(USE_CODEC_MIKMOD),1)
CFLAGS+= -DUSE_CODEC_MIKMOD
CODECLIBS+= -lmikmod
endif
ifeq ($(USE_CODEC_XMP),1)
CFLAGS+= -DUSE_CODEC_XMP
CODECLIBS+= -lxmp
endif
ifeq ($(USE_CODEC_MODPLUG),1)
CFLAGS+= -DUSE_CODEC_MODPLUG
CODECLIBS+= -lmodplug
endif
ifeq ($(USE_CODEC_UMX),1)
CFLAGS+= -DUSE_CODEC_UMX
endif

ifeq ($(HOST_OS),haiku)
COMMON_LIBS:= -lGL
else
COMMON_LIBS:= -lGL
ifeq ($(LINK_M),1)
COMMON_LIBS += -lm
endif
endif

ifeq ($(USE_ZLIB),1)
CFLAGS+= -DUSE_ZLIB
COMMON_LIBS+= -lz
endif

ifeq ($(USE_GNUTLS),1)
ifeq ($(GNUTLS_PKG_OK),1)
CFLAGS+= -DUSE_GNUTLS -DGNUTLS_STATIC $(GNUTLS_CFLAGS)
COMMON_LIBS+= $(GNUTLS_LIBS)
else
$(warning gnutls requested but not found via pkg-config; building without USE_GNUTLS)
endif
endif

LIBS := $(COMMON_LIBS) $(NET_LIBS) $(CODECLIBS)

# ---------------------------
# objects
# ---------------------------

ICEOBJS:= ice_gnutls.o \
	ice_main.o \
	ice_mdns.o \
	ice_quake.o \
	ice_sctp.o \
	ice_socket.o \
	ice_stream.o \
	ice_json.o \
	ice_md5.o \
	ice_sha1.o \
	ice_sha2.o
MUSIC_OBJS:= bgmusic.o \
	snd_codec.o \
	snd_flac.o \
	snd_wave.o \
	snd_vorbis.o \
	snd_opus.o \
	$(mp3_obj).o \
	snd_mp3tag.o \
	snd_mikmod.o \
	snd_modplug.o \
	snd_xmp.o \
	snd_umx.o
COMOBJ_SND := snd_voip.o snd_dma.o snd_mix.o snd_mem.o $(MUSIC_OBJS)
SYSOBJ_SND := snd_sdl.o
SYSOBJ_CDA := cd_sdl.o
SYSOBJ_INPUT := in_sdl.o
SYSOBJ_GL_VID:= gl_vidsdl.o
SYSOBJ_NET := net_bsd.o net_udp.o
SYSOBJ_SYS := pl_linux.o sys_sdl_unix.o
SYSOBJ_MAIN:= main_sdl.o

GLOBJS = \
	gl_refrag.o \
	gl_rlight.o \
	gl_rmain.o \
	gl_fog.o \
	gl_rmisc.o \
	r_part.o \
	r_part_fte.o \
	r_world.o \
	r_hulldebug.o \
	gl_screen.o \
	pong.o \
	gl_sky.o \
	gl_warp.o \
	$(SYSOBJ_GL_VID) \
	gl_draw.o \
	image.o \
	gl_texmgr.o \
	gl_mesh.o \
	r_sprite.o \
	r_alias.o \
	r_brush.o \
	gl_model.o

OBJS := strlcat.o \
	strlcpy.o \
	$(GLOBJS) \
	$(SYSOBJ_INPUT) \
	$(COMOBJ_SND) \
	$(SYSOBJ_SND) \
	$(SYSOBJ_CDA) \
	$(SYSOBJ_NET) \
	$(ICEOBJS) \
	net_address.o \
	net_hostname_cache.o \
	net_dgrm.o \
	net_loop.o \
	net_main.o \
	chase.o \
	cl_demo.o \
	cl_input.o \
	cl_main.o \
	discord_presence.o \
	mapshot.o \
	cl_parse.o \
	qwatch.o \
	cl_tent.o \
	console.o \
	iplog.o \
	keys.o \
	location.o \
	menu.o \
	update.o \
	sbar.o \
	view.o \
	wad.o \
	cmd.o \
	common.o \
	quake_data_import.o \
	json.o \
	fs_zip.o \
	f_modified.o \
	crc.o \
	cvar.o \
	cfgfile.o \
	host.o \
	host_cmd.o \
	mathlib.o \
	mdfour.o \
	pr_cmds.o \
	pr_ext.o \
	pr_edict.o \
	pr_exec.o \
	pmove.o \
	pmovetst.o \
	sv_main.o \
	sv_move.o \
	sv_phys.o \
	sv_user.o \
	world.o \
	zone.o \
	$(SYSOBJ_SYS) $(SYSOBJ_MAIN)

# ---------------------------
# targets / rules
# ---------------------------

.PHONY:	clean debug release

DEFAULT_TARGET := quakespasm
all: $(DEFAULT_TARGET)

%.o:	%.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $<
%.o:	ice/%.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $<
ice_json.o:	ice/json.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $<
ice_md5.o:	ice/md5.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $<
ice_sha1.o:	ice/sha1.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $<
ice_sha2.o:	ice/sha2.c
	$(CC) $(DFLAGS) -c $(CFLAGS) $(SDL_CFLAGS) -o $@ $<

quakespasm:	$(OBJS)
	$(LINKER) $(OBJS) $(LDFLAGS) $(LIBS) $(SDL_LIBS) -o $@
	$(call do_strip,$@)

release:	quakespasm
debug:
	$(error Use "make DEBUG=1")

clean:
	$(RM) *.o *.d $(DEFAULT_TARGET)

ifeq ($(HOST_OS),haiku)
QS_APP_DIR=$(shell finddir B_APPS_DIRECTORY)/QuakeSpasm/
install:	quakespasm
	mkdir -p $(QS_APP_DIR)
	cp quakespasm $(QS_APP_DIR)
	cp quakespasm.pak $(QS_APP_DIR)
else
install:	quakespasm
	cp quakespasm /usr/local/games/quake
endif

sinclude $(OBJS:.o=.d)
