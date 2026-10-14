/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "log.hpp"
#include "samp-plugin.hpp"
#include <core.hpp>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

// Set by the SA-MP Load entry point; unused under open.mp.
logprintf_t logprintf = nullptr;

namespace
{
constexpr std::size_t MAX_QUEUED_MESSAGES = 512;

std::mutex g_queueMutex;
std::deque<std::pair<LogLevel, std::string>> g_queue;
std::size_t g_droppedMessages = 0;
std::thread::id g_mainThread;
bool g_mainThreadKnown = false;
ICore* g_core = nullptr;

void writeNow(LogLevel level, const std::string& message)
{
	if (g_core)
	{
		g_core->logLn(level, "[AudioCast] %s", message.c_str());
		return;
	}
	if (logprintf)
	{
		logprintf("[AudioCast] %s", message.c_str());
		return;
	}
	// Outside a server (tools and tests).
	std::fprintf(stderr, "[AudioCast] %s\n", message.c_str());
}

void writeLog(LogLevel level, const std::string& message)
{
	if (!g_mainThreadKnown || std::this_thread::get_id() != g_mainThread)
	{
		std::lock_guard<std::mutex> lock(g_queueMutex);
		if (g_queue.size() < MAX_QUEUED_MESSAGES) g_queue.emplace_back(level, message);
		else ++g_droppedMessages;
		return;
	}
	writeNow(level, message);
}
}

void AudioCastLogWarning(const std::string& message)
{
	writeLog(LogLevel::Warning, message);
}

void AudioCastLogMessage(const std::string& message)
{
	writeLog(LogLevel::Message, message);
}

void AudioCastLogInit(ICore* core)
{
	g_core = core;
	g_mainThread = std::this_thread::get_id();
	g_mainThreadKnown = true;
}

void AudioCastLogFlush()
{
	std::deque<std::pair<LogLevel, std::string>> pending;
	std::size_t dropped = 0;
	{
		std::lock_guard<std::mutex> lock(g_queueMutex);
		if (g_queue.empty() && g_droppedMessages == 0) return;
		pending.swap(g_queue);
		dropped = g_droppedMessages;
		g_droppedMessages = 0;
	}
	for (const auto& entry : pending)
	{
		writeNow(entry.first, entry.second);
	}
	if (dropped > 0)
	{
		writeNow(LogLevel::Warning, std::to_string(dropped) + " log message(s) were dropped");
	}
}
