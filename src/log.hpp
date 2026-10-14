/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <string>

struct ICore;

// Safe to call from any thread.  Messages from worker and network threads are
// queued and written by AudioCastLogFlush on the server thread: SA-MP's
// logprintf is not thread-safe.
void AudioCastLogWarning(const std::string& message);
void AudioCastLogMessage(const std::string& message);

// Remembers the server thread and, in open.mp mode, the core used to log.
void AudioCastLogInit(ICore* core);
// Writes the queued messages.  Call it on every server tick.
void AudioCastLogFlush();
