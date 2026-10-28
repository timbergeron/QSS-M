cmake_minimum_required(VERSION 3.16)
project(mad C)
add_library(mad STATIC bit.c decoder.c fixed.c frame.c huffman.c layer12.c layer3.c stream.c synth.c timer.c version.c)
target_compile_definitions(mad PRIVATE HAVE_CONFIG_H FPM_64BIT)
target_include_directories(mad PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
configure_file(msvc++/config.h config.h COPYONLY)
# The distributed mad.h selects x86 assembly; match the portable 64-bit arithmetic
# used to build the library on both Windows architectures and both compilers.
file(READ mad.h MAD_HEADER)
string(REPLACE "# define FPM_INTEL" "# define FPM_64BIT" MAD_HEADER "${MAD_HEADER}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mad.h" "${MAD_HEADER}")
install(TARGETS mad ARCHIVE DESTINATION lib)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mad.h" DESTINATION include)
