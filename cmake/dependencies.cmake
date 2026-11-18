# Third-party code, downloaded from the official releases (checked by SHA-256)
# and built as static libraries.  Nothing has to be installed on the build
# machine, and the plugin needs nothing installed on the server.
#
#   asio    - networking (header-only, the standalone edition of Boost.Asio)
#   libopus - decodes YouTube's Opus audio
#   fdk-aac - decodes YouTube's AAC audio (videos without an Opus stream)
#   LAME    - encodes the stream's MP3
#   miniz   - unpacks the managed yt-dlp

include(FetchContent)

if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(asio
	URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
	URL_HASH SHA256=755bd7f85a4b269c67ae0ea254907c078d408cce8e1a352ad2ed664d233780e8
)
FetchContent_Declare(opus
	URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
	URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
)
FetchContent_Declare(fdkaac
	URL https://github.com/mstorsjo/fdk-aac/archive/refs/tags/v2.0.3.tar.gz
	URL_HASH SHA256=e25671cd96b10bad896aa42ab91a695a9e573395262baed4e4a2ff178d6a3a78
)
FetchContent_Declare(lame
	URL https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
	URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
)
FetchContent_Declare(utf8proc
	URL https://github.com/JuliaStrings/utf8proc/archive/refs/tags/v2.10.0.tar.gz
	URL_HASH SHA256=6f4f1b639daa6dca9f80bc5db1233e9cbaa31a67790887106160b33ef743f136
)
FetchContent_Declare(miniz
	URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
	URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
)

# Opus options must be set before it is added.
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
# fdk-aac: a static library, nothing installed.  CMP0077 lets these plain
# variables override its option() defaults.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(FDK_AAC_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(FDK_AAC_INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING_SAVED ${BUILD_TESTING})
set(BUILD_TESTING OFF)
# asio, LAME and the miniz release have no top-level CMakeLists.txt: they are
# only downloaded.
FetchContent_MakeAvailable(asio opus fdkaac lame miniz utf8proc)
set(BUILD_TESTING ${BUILD_TESTING_SAVED})
unset(BUILD_SHARED_LIBS)
unset(CMAKE_POLICY_DEFAULT_CMP0077)

# --- asio -------------------------------------------------------------------
add_library(audiocast-asio INTERFACE)
target_include_directories(audiocast-asio SYSTEM INTERFACE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(audiocast-asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
if(WIN32)
	target_link_libraries(audiocast-asio INTERFACE ws2_32 mswsock)
endif()

# --- libopus ------------------------------------------------------------------
set_target_properties(opus PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- fdk-aac ------------------------------------------------------------------
# The library also holds the AAC encoder; the plugin only links the decoder
# (the tests use the encoder to make AAC files).
set_target_properties(fdk-aac PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
	target_compile_options(fdk-aac PRIVATE /W0)
else()
	target_compile_options(fdk-aac PRIVATE -w)
endif()

# --- LAME (encoder only) ------------------------------------------------------
set(AUDIOCAST_LAME_SOURCES)
foreach(name
	bitstream encoder fft gain_analysis id3tag lame mpglib_interface newmdct presets psymodel quantize
	quantize_pvt reservoir set_get tables takehiro util vbrquantize VbrTag version)
	list(APPEND AUDIOCAST_LAME_SOURCES "${lame_SOURCE_DIR}/libmp3lame/${name}.c")
endforeach()
add_library(audiocast-lame STATIC ${AUDIOCAST_LAME_SOURCES})
target_include_directories(audiocast-lame
	PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lame" "${lame_SOURCE_DIR}/libmp3lame"
	PUBLIC "${lame_SOURCE_DIR}/include"
)
target_compile_definitions(audiocast-lame PRIVATE HAVE_CONFIG_H)
set_target_properties(audiocast-lame PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
if(MSVC)
	target_compile_options(audiocast-lame PRIVATE /W0)
	target_compile_definitions(audiocast-lame PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(audiocast-lame PRIVATE -w)
	target_link_libraries(audiocast-lame PUBLIC m)
endif()

# --- miniz --------------------------------------------------------------------
add_library(audiocast-miniz STATIC "${miniz_SOURCE_DIR}/miniz.c")
target_include_directories(audiocast-miniz SYSTEM PUBLIC "${miniz_SOURCE_DIR}")
set_target_properties(audiocast-miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
	target_compile_options(audiocast-miniz PRIVATE /W0)
else()
	target_compile_options(audiocast-miniz PRIVATE -w)
endif()

# --- utf8proc -----------------------------------------------------------------
set_target_properties(utf8proc PROPERTIES POSITION_INDEPENDENT_CODE ON)
