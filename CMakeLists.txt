CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
cmake_policy(SET CMP0069 NEW)

PROJECT(QSS)

IF(NOT WIN32)
	SET(QS_LIBS ${QS_LIBS} m ${CMAKE_DL_LIBS})
ENDIF()

#other people seem to still care about msvc.
IF(CMAKE_C_COMPILER_ID MATCHES "GNU")
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=pointer-arith")	#block this gnuc extension
ENDIF()
IF(CMAKE_BUILD_TYPE MATCHES "Release")
	INCLUDE(CheckIPOSupported)
	check_ipo_supported(RESULT result)
	IF(result)
		SET(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	ENDIF()
ENDIF()

IF(NOT WIN32)
	FIND_PACKAGE(PkgConfig REQUIRED)
ENDIF()

IF(WIN32)
	SET(QS_LIBS ${QS_LIBS} wsock32 ws2_32 winmm opengl32)
	# Add Windows dependency include and library directories (matching vcxproj)
	SET(QS_DIRS ${QS_DIRS}
		${CMAKE_SOURCE_DIR}/Windows/codecs/include
		${CMAKE_SOURCE_DIR}/Windows/SDL2/include
		${CMAKE_SOURCE_DIR}/Windows/zlib/include
		${CMAKE_SOURCE_DIR}/Windows/curl/include
		${CMAKE_SOURCE_DIR}/Windows/misc/include
	)
	LINK_DIRECTORIES(
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64
		${CMAKE_SOURCE_DIR}/Windows/SDL2/lib64
		${CMAKE_SOURCE_DIR}/Windows/zlib/x64
		${CMAKE_SOURCE_DIR}/Windows/curl/lib/x64
	)
ENDIF()

#sdl2 stuff
IF(WIN32)
	SET(QS_DIRS ${QS_DIRS} ${CMAKE_SOURCE_DIR}/Windows/SDL2/include)
	SET(QS_LIBS ${QS_LIBS} SDL2 SDL2main)
ELSE()
	FIND_PACKAGE(SDL2 REQUIRED)
	SET(QS_LIBS ${QS_LIBS} ${SDL2_LIBRARIES})
	SET(QS_DIRS ${QS_DIRS} ${SDL2_INCLUDE_DIRS})
ENDIF()
SET(QS_DEFS ${QS_DEFS};USE_SDL2)

#opengl stuff
SET(OpenGL_GL_PREFERENCE LEGACY)
FIND_PACKAGE(OpenGL REQUIRED)
SET(QS_LIBS ${QS_LIBS} ${OPENGL_LIBRARIES} )

#other stuff
SET(QS_DEFS ${QS_DEFS};DO_USERDIRS=0)

#zlib
IF(WIN32)
	SET(QS_LIBS ${QS_LIBS} zlib1)
	SET(QS_DEFS ${QS_DEFS};USE_ZLIB)
ELSE()
	FIND_PACKAGE(ZLIB)
	IF(ZLIB_FOUND)
		SET(QS_LIBS ${QS_LIBS} ${ZLIB_LIBRARIES} )
		SET(QS_DEFS ${QS_DEFS};USE_ZLIB)
		SET(QS_DIRS ${QS_DIRS} ${ZLIB_INCLUDE_DIRS} )
	ELSE()
		MESSAGE(WARNING "zlib not available, pk3 support will be disabled.")
	ENDIF()
ENDIF()




### audio drivers
IF (1)
	SET(QS_FILES ${QS_FILES} Quake/snd_wave.c)
	SET(QS_DEFS ${QS_DEFS};USE_CODEC_WAVE)
ELSE()
	MESSAGE(WARNING "AudioCodec wave: missing, somehow...")
ENDIF()

IF(WIN32)
	# On Windows, use find_package or assume libs are available via vcxproj-style lib dirs
	# Audio codecs - add source files and defines matching the vcxproj configuration
	SET(QS_FILES ${QS_FILES} Quake/snd_flac.c)
	SET(QS_DEFS ${QS_DEFS};USE_CODEC_FLAC)
	SET(QS_FILES ${QS_FILES} Quake/snd_opus.c)
	SET(QS_DEFS ${QS_DEFS};USE_CODEC_OPUS)
	SET(QS_FILES ${QS_FILES} Quake/snd_vorbis.c)
	SET(QS_DEFS ${QS_DEFS};USE_CODEC_VORBIS)
	SET(QS_FILES ${QS_FILES} Quake/snd_mikmod.c)
	SET(QS_DEFS ${QS_DEFS};USE_CODEC_MIKMOD)
	SET(QS_FILES ${QS_FILES} Quake/snd_mp3.c)
	SET(QS_DEFS ${QS_DEFS};USE_CODEC_MP3)
	SET(QS_LIBS ${QS_LIBS} libvorbisfile libvorbis libopusfile libopus libFLAC libogg libmad libmikmod libcurl)
	SET(QS_DEFS ${QS_DEFS};_CRT_NONSTDC_NO_DEPRECATE;_CRT_SECURE_NO_WARNINGS;_WINSOCK_DEPRECATED_NO_WARNINGS)
ELSE()
	PKG_SEARCH_MODULE(FLAC flac)
	IF(FLAC_FOUND)
		SET(QS_LIBS ${QS_LIBS} ${FLAC_LIBRARIES})
		SET(QS_FILES ${QS_FILES} Quake/snd_flac.c)
		SET(QS_DEFS ${QS_DEFS};USE_CODEC_FLAC)
		MESSAGE("Found libFLAC support.")
	ELSE()
		MESSAGE(WARNING "AudioCodec FLAC: missing.")
	ENDIF()

	PKG_SEARCH_MODULE(OPUS opus)
	PKG_SEARCH_MODULE(OPUSFILE opusfile)
	IF(OPUS_FOUND AND OPUSFILE_FOUND)
		SET(QS_DIRS ${QS_DIRS} ${OPUS_INCLUDE_DIRS} )
		SET(QS_DIRS ${QS_DIRS} ${OPUSFILE_INCLUDE_DIRS} )
		SET(QS_LIBS ${QS_LIBS} ${OPUS_LIBRARIES})
		SET(QS_LIBS ${QS_LIBS} ${OPUSFILE_LIBRARIES})
		SET(QS_FILES ${QS_FILES} Quake/snd_opus.c)
		SET(QS_DEFS ${QS_DEFS};USE_CODEC_OPUS)
		MESSAGE("Found libopusfile support.")
	ELSE()
		MESSAGE(WARNING "no opusfile support.")
	ENDIF()

	PKG_SEARCH_MODULE(VORBISFILE vorbisfile)
	IF(VORBISFILE_FOUND)
		SET(QS_LIBS ${QS_LIBS} ${VORBISFILE_LIBRARIES})
		SET(QS_FILES ${QS_FILES} Quake/snd_vorbis.c)
		SET(QS_DEFS ${QS_DEFS};USE_CODEC_VORBIS)
		MESSAGE("Found libvorbisfile support.")
	ELSE()
		MESSAGE(WARNING "no vorbisfile support.")
	ENDIF()

	PKG_SEARCH_MODULE(MIKMOD libmikmod)
	IF(MIKMOD_FOUND)
		SET(QS_LIBS ${QS_LIBS} ${MIKMOD_LIBRARIES})
		SET(QS_FILES ${QS_FILES} Quake/snd_mikmod.c)
		SET(QS_DEFS ${QS_DEFS};USE_CODEC_MIKMOD)
		MESSAGE("Found libmikmod support.")
	ELSE()
		MESSAGE(WARNING "no mikmod support.")
	ENDIF()

	PKG_SEARCH_MODULE(MAD mad)
	IF(MAD_FOUND)
		SET(QS_LIBS ${QS_LIBS} ${MAD_LIBRARIES})
		SET(QS_FILES ${QS_FILES} Quake/snd_mp3.c)
		SET(QS_DEFS ${QS_DEFS};USE_CODEC_MP3)
		MESSAGE("Found libmad support.")
	ELSE()
		PKG_SEARCH_MODULE(MPG123 libmpg123)
		IF(MPG123_FOUND)
			SET(QS_FILES ${QS_FILES} Quake/snd_mpg123.c)
			SET(QS_DEFS ${QS_DEFS};USE_CODEC_MP3)
			MESSAGE("Found libmpg123 support.")
		ELSE()
			MESSAGE(WARNING "no mpg123 support.")
		ENDIF()
	ENDIF()

	PKG_SEARCH_MODULE(XMP libxmp)
	IF(XMP_FOUND)
		SET(QS_LIBS ${QS_LIBS} ${XMP_LIBRARIES})
		SET(QS_FILES ${QS_FILES} Quake/snd_xmp.c)
		SET(QS_DEFS ${QS_DEFS};USE_CODEC_XMP)
		MESSAGE("Found libxmp support.")
	ELSE()
		MESSAGE(WARNING "no xmp support.")
	ENDIF()
ENDIF()

IF(WIN32)
	SET(QS_DIRS ${QS_DIRS} ${CMAKE_SOURCE_DIR}/Windows/gnutls/include)
	SET(QS_DEFS ${QS_DEFS};USE_GNUTLS)
ELSE()
	FIND_PACKAGE(GnuTLS)
	IF(GNUTLS_FOUND)
		SET(QS_LIBS ${QS_LIBS} ${GNUTLS_LIBRARY})
		SET(QS_DEFS ${QS_DEFS};USE_GNUTLS)
		MESSAGE("Found gnutls support.")
	ELSE()
		MESSAGE(WARNING "no gnutls support.")
	ENDIF()
ENDIF()

INCLUDE_DIRECTORIES(${QS_DIRS})
ADD_EXECUTABLE(quakespasm
	${QS_FILES}
	Quake/bgmusic.c
#	Quake/cd_null.c
	Quake/cd_sdl.c
	Quake/cfgfile.c
	Quake/chase.c
	Quake/cl_demo.c
	Quake/cl_input.c
	Quake/cl_main.c
	Quake/cl_parse.c
	Quake/cl_tent.c
	Quake/cmd.c
	Quake/common.c
	Quake/console.c
	Quake/crc.c
	Quake/cvar.c
	Quake/fs_zip.c
	Quake/gl_draw.c
	Quake/gl_fog.c
	Quake/gl_mesh.c
	Quake/gl_model.c
	Quake/gl_refrag.c
	Quake/gl_rlight.c
	Quake/gl_rmain.c
	Quake/gl_rmisc.c
	Quake/gl_screen.c
	Quake/gl_sky.c
	Quake/gl_texmgr.c
	Quake/gl_vidsdl.c
	Quake/gl_warp.c
	Quake/host.c
	Quake/host_cmd.c
	Quake/ice/ice_main.c
	Quake/ice/ice_mdns.c
	Quake/ice/ice_stream.c
	Quake/ice/ice_socket.c
	Quake/ice/ice_gnutls.c
	Quake/ice/ice_quake.c
	Quake/ice/ice_sctp.c
	Quake/ice/json.c
	Quake/ice/md5.c
	Quake/ice/sha1.c
	Quake/ice/sha2.c
	Quake/image.c
	Quake/in_sdl.c
	Quake/iplog.c
	Quake/json.c
	Quake/keys.c
	Quake/location.c
#	Quake/lodepng.c
	Quake/main_sdl.c
	Quake/mathlib.c
	Quake/mdfour.c
	Quake/menu.c
	Quake/net_dgrm.c
	Quake/net_loop.c
	Quake/net_main.c
	$<$<NOT:$<PLATFORM_ID:Windows>>:Quake/net_bsd.c>
	$<$<NOT:$<PLATFORM_ID:Windows>>:Quake/net_udp.c>
	$<$<PLATFORM_ID:Windows>:Quake/net_win.c>
	$<$<PLATFORM_ID:Windows>:Quake/net_wins.c>
	$<$<PLATFORM_ID:Windows>:Quake/net_wipx.c>
	$<$<NOT:$<PLATFORM_ID:Windows>>:Quake/pl_linux.c>
	$<$<PLATFORM_ID:Windows>:Quake/pl_win.c>
	Quake/pmove.c
	Quake/pmovetst.c
	Quake/pr_cmds.c
	Quake/pr_edict.c
	Quake/pr_exec.c
	Quake/pr_ext.c
	Quake/r_alias.c
	Quake/r_brush.c
	Quake/r_part.c
	Quake/r_part_fte.c
	Quake/r_sprite.c
	Quake/r_world.c
	Quake/sbar.c
	Quake/snd_codec.c
	Quake/snd_dma.c
	Quake/snd_mem.c
	Quake/snd_mix.c
	Quake/snd_mp3tag.c
	Quake/snd_sdl.c
	Quake/snd_umx.c
	Quake/snd_voip.c
	Quake/strlcat.c
	Quake/strlcpy.c
	Quake/sv_main.c
	Quake/sv_move.c
	Quake/sv_phys.c
	Quake/sv_user.c
	$<$<NOT:$<PLATFORM_ID:Windows>>:Quake/sys_sdl_unix.c>
	$<$<PLATFORM_ID:Windows>:Quake/sys_sdl_win.c>
	Quake/view.c
	Quake/wad.c
	Quake/world.c
	Quake/zone.c
)
SET_TARGET_PROPERTIES(quakespasm PROPERTIES COMPILE_DEFINITIONS "${QS_DEFS}" )
TARGET_LINK_LIBRARIES(quakespasm ${QS_LIBS})

IF(WIN32)
	# Output exe and PDB directly to Windows/VisualStudio
	SET_TARGET_PROPERTIES(quakespasm PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Windows/VisualStudio"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/Windows/VisualStudio"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/Windows/VisualStudio"
		PDB_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Windows/VisualStudio"
		PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/Windows/VisualStudio"
		PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/Windows/VisualStudio"
	)
	# Copy required DLLs next to executable after build
	SET(QS_DLLS
		${CMAKE_SOURCE_DIR}/Windows/SDL2/lib64/SDL2.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libvorbis-0.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libvorbisfile-3.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libogg-0.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libopus-0.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libopusfile-0.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libFLAC-8.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libmad-0.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libmikmod-3.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libmpg123-0.dll
		${CMAKE_SOURCE_DIR}/Windows/codecs/x64/libxmp.dll
		${CMAKE_SOURCE_DIR}/Windows/curl/lib/x64/libcurl.dll
		${CMAKE_SOURCE_DIR}/Windows/zlib/x64/zlib1.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libgnutls-30.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libgmp-10.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libhogweed-6.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libnettle-8.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libp11-kit-0.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libidn2-0.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libffi-8.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libgcc_s_seh-1.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libwinpthread-1.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libbrotlicommon.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libbrotlidec.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libbrotlienc.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libssp-0.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libzstd.dll
		${CMAKE_SOURCE_DIR}/Windows/gnutls/x64/libtasn1-6.dll
	)
	FOREACH(DLL ${QS_DLLS})
		ADD_CUSTOM_COMMAND(TARGET quakespasm POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:quakespasm>
		)
	ENDFOREACH()
ENDIF()
