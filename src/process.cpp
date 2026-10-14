/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "process.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace
{
using Clock = std::chrono::steady_clock;

void appendOutput(std::string& output, const char* data, std::size_t size, std::size_t maxOutput)
{
	if (output.size() >= maxOutput) return;
	output.append(data, std::min(size, maxOutput - output.size()));
}

#if defined(_WIN32)
std::wstring widen(const std::string& text)
{
	if (text.empty()) return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
	return wide;
}

// Quotes one argument the way CommandLineToArgvW and the CRT parse it.
void appendQuoted(std::wstring& commandLine, const std::wstring& argument)
{
	if (!commandLine.empty()) commandLine.push_back(L' ');
	if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
	{
		commandLine += argument;
		return;
	}

	commandLine.push_back(L'"');
	for (auto it = argument.begin();; ++it)
	{
		std::size_t backslashes = 0;
		while (it != argument.end() && *it == L'\\')
		{
			++it;
			++backslashes;
		}
		if (it == argument.end())
		{
			commandLine.append(backslashes * 2, L'\\');
			break;
		}
		if (*it == L'"')
		{
			commandLine.append(backslashes * 2 + 1, L'\\');
			commandLine.push_back(*it);
		}
		else
		{
			commandLine.append(backslashes, L'\\');
			commandLine.push_back(*it);
		}
	}
	commandLine.push_back(L'"');
}

std::string lastErrorText(DWORD code)
{
	char* buffer = nullptr;
	const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, code, 0, reinterpret_cast<char*>(&buffer), 0, nullptr);
	std::string text = length && buffer ? std::string(buffer, length) : "error " + std::to_string(code);
	if (buffer) LocalFree(buffer);
	while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == '.')) text.pop_back();
	return text;
}
#endif
}

ProcessResult RunProcess(const std::vector<std::string>& args, int timeoutSeconds, const std::atomic<bool>& cancel,
	std::size_t maxOutput, std::size_t maxBinaryOutput, const OutputSink& stdoutSink)
{
	ProcessResult result;
	if (args.empty())
	{
		result.error = "no program given";
		return result;
	}
	const auto deadline = Clock::now() + std::chrono::seconds(timeoutSeconds);
	const bool separate = maxBinaryOutput > 0 || static_cast<bool>(stdoutSink);
	std::atomic<bool> overLimit { false };
	std::atomic<bool> sinkStopped { false };

	// stdout goes to the sink or to `binary` when separated, otherwise it is
	// mixed with stderr in `output`.
	auto appendBinary = [&result, &overLimit, &sinkStopped, &stdoutSink, maxBinaryOutput](const char* data, std::size_t size)
	{
		if (overLimit || sinkStopped) return;
		if (stdoutSink)
		{
			if (!stdoutSink(reinterpret_cast<const std::uint8_t*>(data), size)) sinkStopped = true;
			return;
		}
		if (result.binary.size() + size > maxBinaryOutput)
		{
			overLimit = true;
			return;
		}
		result.binary.insert(result.binary.end(), reinterpret_cast<const std::uint8_t*>(data),
			reinterpret_cast<const std::uint8_t*>(data) + size);
	};

#if defined(_WIN32)
	std::wstring commandLine;
	for (const std::string& argument : args) appendQuoted(commandLine, widen(argument));

	SECURITY_ATTRIBUTES inheritable { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
	HANDLE outRead = nullptr;
	HANDLE outWrite = nullptr;
	HANDLE errRead = nullptr;
	HANDLE errWrite = nullptr;
	if (!CreatePipe(&outRead, &outWrite, &inheritable, 1 << 16) ||
		(separate && !CreatePipe(&errRead, &errWrite, &inheritable, 0)))
	{
		result.error = "CreatePipe failed: " + lastErrorText(GetLastError());
		for (HANDLE handle : { outRead, outWrite, errRead, errWrite })
		{
			if (handle) CloseHandle(handle);
		}
		return result;
	}
	SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
	if (errRead) SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
	HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr);

	// Only these handles may be inherited.  Without the list, a process
	// started at the same time by another worker could inherit a pipe and
	// keep it open after our child exits.
	HANDLE inherited[3];
	DWORD inheritedCount = 0;
	inherited[inheritedCount++] = outWrite;
	if (errWrite) inherited[inheritedCount++] = errWrite;
	if (nullInput != INVALID_HANDLE_VALUE) inherited[inheritedCount++] = nullInput;
	SIZE_T attributeSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
	std::vector<unsigned char> attributeStorage(attributeSize);
	auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
	const bool haveAttributes = InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize) &&
		UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, inheritedCount * sizeof(HANDLE), nullptr, nullptr);

	STARTUPINFOEXW startup {};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = nullInput != INVALID_HANDLE_VALUE ? nullInput : nullptr;
	startup.StartupInfo.hStdOutput = outWrite;
	startup.StartupInfo.hStdError = errWrite ? errWrite : outWrite;
	startup.lpAttributeList = haveAttributes ? attributes : nullptr;

	// The job kills the whole process tree on timeout or when it is closed.
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (job)
	{
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
	}

	PROCESS_INFORMATION process {};
	std::wstring mutableCommandLine = commandLine;
	const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
		(haveAttributes ? EXTENDED_STARTUPINFO_PRESENT : 0);
	const BOOL created = CreateProcessW(nullptr, mutableCommandLine.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr,
		&startup.StartupInfo, &process);
	const DWORD createError = created ? 0 : GetLastError();

	if (haveAttributes) DeleteProcThreadAttributeList(attributes);
	CloseHandle(outWrite);
	if (errWrite) CloseHandle(errWrite);
	if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);

	if (!created)
	{
		CloseHandle(outRead);
		if (errRead) CloseHandle(errRead);
		if (job) CloseHandle(job);
		result.error = "could not start '" + args[0] + "': " + lastErrorText(createError);
		return result;
	}

	if (job) AssignProcessToJobObject(job, process.hProcess);
	ResumeThread(process.hThread);
	CloseHandle(process.hThread);
	result.started = true;

	std::string output;
	std::thread outReader([&, outRead]
	{
		std::vector<char> buffer(1 << 16);
		DWORD read = 0;
		while (ReadFile(outRead, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0)
		{
			if (separate) appendBinary(buffer.data(), read);
			else appendOutput(output, buffer.data(), read, maxOutput);
		}
	});
	std::string errors;
	std::thread errReader;
	if (errRead)
	{
		errReader = std::thread([&errors, errRead, maxOutput]
		{
			char buffer[4096];
			DWORD read = 0;
			while (ReadFile(errRead, buffer, sizeof(buffer), &read, nullptr) && read > 0)
			{
				appendOutput(errors, buffer, read, maxOutput);
			}
		});
	}

	for (;;)
	{
		if (WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0) break;
		const bool cancelled = cancel.load() || sinkStopped;
		if (cancelled || overLimit || Clock::now() >= deadline)
		{
			result.cancelled = cancelled;
			result.timedOut = !cancelled && !overLimit;
			if (job) TerminateJobObject(job, 1);
			else TerminateProcess(process.hProcess, 1);
			WaitForSingleObject(process.hProcess, 5000);
			break;
		}
	}

	DWORD exitCode = 0;
	if (GetExitCodeProcess(process.hProcess, &exitCode)) result.exitCode = static_cast<int>(exitCode);
	CloseHandle(process.hProcess);
	// Killing the job also ends grandchildren that still hold the pipes.
	if (job)
	{
		TerminateJobObject(job, 1);
		CloseHandle(job);
	}
	outReader.join();
	if (errReader.joinable()) errReader.join();
	CloseHandle(outRead);
	if (errRead) CloseHandle(errRead);
	result.outputTooLarge = overLimit && !result.cancelled;
	if (sinkStopped) result.cancelled = true;
	result.output = separate ? std::move(errors) : std::move(output);
	return result;
#else
	int outPipe[2] = { -1, -1 };
	int errPipe[2] = { -1, -1 };
	if (pipe2(outPipe, O_CLOEXEC) != 0 || (separate && pipe2(errPipe, O_CLOEXEC) != 0))
	{
		result.error = std::string("pipe failed: ") + std::strerror(errno);
		for (int fd : { outPipe[0], outPipe[1], errPipe[0], errPipe[1] })
		{
			if (fd >= 0) close(fd);
		}
		return result;
	}

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_adddup2(&actions, outPipe[1], 1);
	posix_spawn_file_actions_adddup2(&actions, separate ? errPipe[1] : outPipe[1], 2);

	posix_spawnattr_t attributes;
	posix_spawnattr_init(&attributes);
	sigset_t noSignals;
	sigemptyset(&noSignals);
	sigset_t defaultSignals;
	sigemptyset(&defaultSignals);
	sigaddset(&defaultSignals, SIGPIPE);
	sigaddset(&defaultSignals, SIGINT);
	sigaddset(&defaultSignals, SIGTERM);
	sigaddset(&defaultSignals, SIGCHLD);
	posix_spawnattr_setsigmask(&attributes, &noSignals);
	posix_spawnattr_setsigdefault(&attributes, &defaultSignals);
	// Own process group, so a timeout can kill the whole tree.
	posix_spawnattr_setpgroup(&attributes, 0);
	posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

	std::vector<char*> argv;
	argv.reserve(args.size() + 1);
	for (const std::string& argument : args) argv.push_back(const_cast<char*>(argument.c_str()));
	argv.push_back(nullptr);

	pid_t pid = -1;
	const int spawnError = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
	posix_spawn_file_actions_destroy(&actions);
	posix_spawnattr_destroy(&attributes);
	close(outPipe[1]);
	if (separate) close(errPipe[1]);

	if (spawnError != 0)
	{
		close(outPipe[0]);
		if (separate) close(errPipe[0]);
		result.error = "could not start '" + args[0] + "': " + std::strerror(spawnError);
		return result;
	}
	result.started = true;

	std::vector<char> buffer(1 << 16);
	bool outOpen = true;
	bool errOpen = separate;
	while (outOpen || errOpen)
	{
		pollfd descriptors[2];
		nfds_t count = 0;
		if (outOpen) descriptors[count++] = pollfd { outPipe[0], POLLIN, 0 };
		if (errOpen) descriptors[count++] = pollfd { errPipe[0], POLLIN, 0 };
		const int ready = poll(descriptors, count, 100);
		if (ready < 0 && errno != EINTR) break;

		for (nfds_t index = 0; ready > 0 && index < count; ++index)
		{
			if (!(descriptors[index].revents & (POLLIN | POLLHUP | POLLERR))) continue;
			const bool isOut = descriptors[index].fd == outPipe[0];
			const ssize_t read = ::read(descriptors[index].fd, buffer.data(), buffer.size());
			if (read > 0)
			{
				if (isOut && separate) appendBinary(buffer.data(), static_cast<std::size_t>(read));
				else appendOutput(result.output, buffer.data(), static_cast<std::size_t>(read), maxOutput);
			}
			else if (read == 0 || (errno != EINTR && errno != EAGAIN))
			{
				if (isOut) outOpen = false;
				else errOpen = false;
			}
		}

		const bool cancelled = cancel.load() || sinkStopped;
		if (cancelled || overLimit || Clock::now() >= deadline)
		{
			result.cancelled = cancelled;
			result.outputTooLarge = !cancelled && overLimit;
			result.timedOut = !cancelled && !overLimit;
			kill(-pid, SIGKILL);
			kill(pid, SIGKILL);
			break;
		}
	}
	close(outPipe[0]);
	if (separate) close(errPipe[0]);

	int status = 0;
	pid_t waited = -1;
	do
	{
		waited = waitpid(pid, &status, 0);
	} while (waited < 0 && errno == EINTR);
	if (waited == pid)
	{
		if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
		else if (WIFSIGNALED(status)) result.exitCode = 128 + WTERMSIG(status);
	}
	else if (errno == ECHILD && !result.timedOut && !result.cancelled && !result.outputTooLarge)
	{
		// The host ignores SIGCHLD, so children are reaped automatically and
		// their status is lost.  Callers check what the tool produced.
		result.exitCode = 0;
	}
	return result;
#endif
}
