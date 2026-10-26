cmake_minimum_required(VERSION 3.25)
option(AUDIOCAST_BUILD_32BIT "Build the 32-bit binary used by 32-bit SA-MP/open.mp servers (GCC/Clang on Linux)" OFF)
set(AUDIOCAST_VERSION "1.0.0" CACHE STRING "AudioCast release version")

if(NOT AUDIOCAST_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
	message(FATAL_ERROR "AUDIOCAST_VERSION must use MAJOR.MINOR.PATCH format (got '${AUDIOCAST_VERSION}')")
endif()

# The architecture flags must be set before project() runs compiler ABI
# detection, otherwise CMake believes the target is 64-bit.
if(AUDIOCAST_BUILD_32BIT AND NOT WIN32)
	set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -m32")
	set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -m32")
endif()

project(audio-cast LANGUAGES C CXX VERSION ${AUDIOCAST_VERSION})
# The open.mp SDK contains older third-party CMakeLists.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
	@ONLY
)

option(AUDIOCAST_STATIC_RUNTIME "Link libgcc/libstdc++ statically (GCC on Linux)" ON)
option(AUDIOCAST_BUILD_TOOLS "Build the audio-cast-check tool" ON)
set(AUDIOCAST_OMP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/omp-sdk" CACHE PATH
	"Path to the open.mp SDK checkout (contains CMakeLists.txt)")
set(AUDIOCAST_SAMP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/samp-sdk" CACHE PATH
	"Path to the SA-MP AMX SDK root (contains amx/amx.h)")

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	add_compile_definitions(
		_CRT_SECURE_NO_WARNINGS
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		_WIN32_WINNT=0x0601
	)
elseif(UNIX OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
	if(AUDIOCAST_STATIC_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		add_link_options("-static-libgcc" "-static-libstdc++")
	endif()
	# Release binaries without debug information.
	add_link_options("$<$<CONFIG:Release>:-s>")
endif()

find_package(Threads REQUIRED)
# asio, libopus and LAME: downloaded and built statically.
include(cmake/dependencies.cmake)

if(NOT EXISTS "${AUDIOCAST_SAMP_SDK_DIR}/amx/amx.h")
	message(FATAL_ERROR "Could not find the AMX SDK. Set AUDIOCAST_SAMP_SDK_DIR to a directory containing amx/amx.h.")
endif()
if(NOT EXISTS "${AUDIOCAST_OMP_SDK_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "Could not find the open.mp SDK. Set AUDIOCAST_OMP_SDK_DIR to an open.mp SDK checkout.")
endif()

add_subdirectory("${AUDIOCAST_OMP_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/omp-sdk")
# Third-party headers: their warnings are not ours to fix.
foreach(sdk_target OMP-SDK glm::glm robin_hood span-lite string-view-lite)
	if(TARGET ${sdk_target})
		get_target_property(sdk_real_target ${sdk_target} ALIASED_TARGET)
		if(NOT sdk_real_target)
			set(sdk_real_target ${sdk_target})
		endif()
		set_target_properties(${sdk_real_target} PROPERTIES SYSTEM TRUE)
	endif()
endforeach()

# Sources without any server dependency, shared with the tests and tools.
add_library(audio-cast-core STATIC
	src/codepages.cpp
	src/config.cpp
	src/decoder.cpp
	src/js-runtime.cpp
	src/loader.cpp
	src/log.cpp
	src/mp3.cpp
	src/mp4.cpp
	src/process.cpp
	src/station.cpp
	src/stream-server.cpp
	src/text.cpp
	src/transcoder.cpp
	src/webm.cpp
	src/ytdlp.cpp
)
target_include_directories(audio-cast-core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(audio-cast-core SYSTEM PUBLIC ${AUDIOCAST_SAMP_SDK_DIR})
target_compile_definitions(audio-cast-core PUBLIC
	HAVE_STDINT_H=1
	PAWN_CELL_SIZE=32
)
target_link_libraries(audio-cast-core PUBLIC OMP-SDK Threads::Threads audiocast-asio audiocast-lame audiocast-miniz opus fdk-aac utf8proc)
set_target_properties(audio-cast-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
	target_link_libraries(audio-cast-core PUBLIC winhttp)
endif()

# One shared library exports both the open.mp component entry point and the
# classic SA-MP plugin entry points, so the same file works in components/
# (open.mp) and plugins/ (SA-MP or open.mp legacy plugin loading).
add_library(audio-cast SHARED
	src/main.cpp
	src/audiocast.cpp
	src/natives.cpp
	src/samp-main.cpp
	src/samp-amx.cpp
	src/samp-config.cpp
	src/samp-pawn.cpp
)
if(WIN32)
	target_sources(audio-cast PRIVATE src/plugin.def)
endif()
target_link_libraries(audio-cast PRIVATE audio-cast-core)

if(NOT WIN32)
	# Keep the bundled libraries' symbols private to the plugin.
	# Export only the plugin entry points; bind everything else locally.
	target_link_options(audio-cast PRIVATE "-Wl,-Bsymbolic"
		"-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/exports.map")
	set_property(TARGET audio-cast APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/exports.map")
	target_link_libraries(audio-cast PRIVATE rt)
endif()

foreach(target audio-cast audio-cast-core)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /MP /bigobj /utf-8)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra)
	endif()
endforeach()

set_target_properties(audio-cast PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	OUTPUT_NAME "audio-cast"
	PREFIX ""
)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/audio-cast.inc"
	"${CMAKE_BINARY_DIR}/pawno/include/audio-cast.inc"
	COPYONLY
)

function(audiocast_executable name source)
	add_executable(${name} ${source})
	target_link_libraries(${name} PRIVATE audio-cast-core)
	if(MSVC)
		target_compile_options(${name} PRIVATE /utf-8 /bigobj)
	endif()
endfunction()

# Loads one song like the plugin does, to check a server's setup.
if(AUDIOCAST_BUILD_TOOLS)
	audiocast_executable(audio-cast-check tools/loader-check.cpp)
endif()

if(BUILD_TESTING)
	audiocast_executable(audio-cast-tests tests/core-tests.cpp)
	add_test(NAME audio-cast-core COMMAND audio-cast-tests)

	audiocast_executable(audio-cast-stream-tests tests/stream-tests.cpp)
	add_test(NAME audio-cast-stream COMMAND audio-cast-stream-tests)

	audiocast_executable(audio-cast-transcoder-tests tests/transcoder-tests.cpp)
	add_test(NAME audio-cast-transcoder COMMAND audio-cast-transcoder-tests)

	set_tests_properties(audio-cast-core audio-cast-stream audio-cast-transcoder PROPERTIES TIMEOUT 60)
endif()
