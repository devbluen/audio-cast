/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
	#ifndef WIN32
		#define WIN32
	#endif
#elif defined(__linux__) || defined(__linux) || defined(linux)
	#ifndef LINUX
		#define LINUX
	#endif
#elif defined(__FreeBSD__)
	#ifndef FREEBSD
		#define FREEBSD
	#endif
#endif

#include <cstddef>
#include <amx/amx.h>
#include <plugincommon.h>

using logprintf_t = void (*)(const char* format, ...);

extern logprintf_t logprintf;
