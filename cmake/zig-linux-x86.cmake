# Cross-compiles the 32-bit Linux plugin from Windows with Zig (clang + a
# bundled glibc/libc++), so no Linux machine, WSL or Docker is needed.
#
# build.ps1 creates small wrapper scripts around zig.exe and passes their
# folder in the AUDIOCAST_ZIG_WRAPPERS environment variable.  The glibc
# version is the oldest one the binary may load on: 2.17 covers practically
# every Linux host still in use.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(NOT DEFINED ENV{AUDIOCAST_ZIG_WRAPPERS})
	message(FATAL_ERROR "AUDIOCAST_ZIG_WRAPPERS is not set; build Linux through build.ps1")
endif()
file(TO_CMAKE_PATH "$ENV{AUDIOCAST_ZIG_WRAPPERS}" AUDIOCAST_ZIG_WRAPPERS)

set(CMAKE_C_COMPILER "${AUDIOCAST_ZIG_WRAPPERS}/zig-cc.cmd")
set(CMAKE_CXX_COMPILER "${AUDIOCAST_ZIG_WRAPPERS}/zig-c++.cmd")
set(CMAKE_AR "${AUDIOCAST_ZIG_WRAPPERS}/zig-ar.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${AUDIOCAST_ZIG_WRAPPERS}/zig-ranlib.cmd" CACHE FILEPATH "" FORCE)

# Nothing is searched on the Windows host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
