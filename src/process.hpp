/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ProcessResult
{
	bool started = false;
	bool timedOut = false;
	bool cancelled = false;
	// stdout grew past maxBinaryOutput; the process was killed.
	bool outputTooLarge = false;
	// -1 when the exit status could not be read.
	int exitCode = -1;
	// stderr, plus stdout unless it is captured in `binary`.
	std::string output;
	// stdout, when maxBinaryOutput > 0.
	std::vector<std::uint8_t> binary;
	// Why the process could not be started.
	std::string error;
};

// Runs args[0] (searched in PATH) with the given arguments.  No shell is
// involved, so arguments are passed exactly as given.  The process and its
// children are killed when the timeout passes or cancel becomes true.
// With maxBinaryOutput > 0, stdout is kept apart as binary data (a download
// written to "-") and limited to that many bytes.  With a stdoutSink, stdout
// is handed over as it arrives instead (on another thread); returning false
// from the sink stops the process.
using OutputSink = std::function<bool(const std::uint8_t* data, std::size_t size)>;
ProcessResult RunProcess(const std::vector<std::string>& args, int timeoutSeconds, const std::atomic<bool>& cancel,
	std::size_t maxOutput = 1024 * 1024, std::size_t maxBinaryOutput = 0, const OutputSink& stdoutSink = {});
