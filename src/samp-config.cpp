/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "samp-config.hpp"
#include <cctype>
#include <fstream>

namespace
{
std::string trim(std::string value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
	{
		return {};
	}
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}
}

std::string readSampConfigValue(const std::string& key)
{
	std::ifstream file("server.cfg");
	std::string line;
	while (std::getline(file, line))
	{
		line = trim(line);
		if (line.empty() || line.front() == '#')
		{
			continue;
		}

		const auto separator = line.find_first_of(" \t");
		const std::string currentKey = separator == std::string::npos ? line : line.substr(0, separator);
		if (currentKey != key)
		{
			continue;
		}

		std::string value = separator == std::string::npos ? std::string() : trim(line.substr(separator + 1));
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
		{
			value = value.substr(1, value.size() - 2);
		}
		return value;
	}
	return {};
}
