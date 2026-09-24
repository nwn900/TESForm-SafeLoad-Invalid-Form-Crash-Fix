#include "Configuration.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>

namespace actor3dguard
{
	namespace
	{
		[[nodiscard]] std::string Trim(std::string value)
		{
			auto notSpace = [](const unsigned char character) { return std::isspace(character) == 0; };
			value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
			value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
			return value;
		}

		[[nodiscard]] std::string Lower(std::string value)
		{
			std::ranges::transform(value, value.begin(), [](const unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
			return value;
		}

		[[nodiscard]] bool ParseBool(const std::string& value, bool& result) noexcept
		{
			const auto lower = Lower(value);
			if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
				result = true;
				return true;
			}
			if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
				result = false;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool ParseMode(const std::string& value, GuardMode& result) noexcept
		{
			const auto lower = Lower(value);
			if (lower == "off") {
				result = GuardMode::Off;
				return true;
			}
			if (lower == "observe" || lower == "observation") {
				result = GuardMode::Observe;
				return true;
			}
			if (lower == "prevent" || lower == "prevention" || lower == "preventcrash") {
				result = GuardMode::Prevent;
				return true;
			}
			return false;
		}

		[[nodiscard]] std::filesystem::path DefaultPath(const std::string_view iniFileName) noexcept
		{
			try {
				wchar_t executable[MAX_PATH]{};
				const auto length = ::GetModuleFileNameW(nullptr, executable, MAX_PATH);
				if (length == 0 || length >= MAX_PATH) {
					return {};
				}
				return std::filesystem::path(std::wstring(executable, executable + length)).parent_path() / "Data" / "F4SE" / std::filesystem::path(iniFileName);
			} catch (...) {
				return {};
			}
		}
	}

	Configuration LoadConfiguration() noexcept
	{
		return LoadConfiguration("Actor3DGuardF4.ini");
	}

	Configuration LoadConfiguration(const std::string_view iniFileName) noexcept
	{
		Configuration config{};
		try {
			config.path = DefaultPath(iniFileName);
			if (config.path.empty()) {
				return config;
			}

			std::ifstream input(config.path);
			if (!input) {
				return config;
			}

			std::string line;
			while (std::getline(input, line)) {
				line = Trim(line);
				if (line.empty() || line.front() == '#' || line.front() == ';') {
					continue;
				}
				// Accept ordinary INI section headers. Settings remain global in
				// this first slice, but a section marker is not a malformed setting.
				if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
					continue;
				}
				const auto separator = line.find('=');
				if (separator == std::string::npos) {
					++config.invalidEntries;
					continue;
				}

				const auto key = Lower(Trim(line.substr(0, separator)));
				const auto value = Trim(line.substr(separator + 1));
				bool parsed = true;
				if (key == "enabled") {
					parsed = ParseBool(value, config.enabled);
				} else if (key == "mode") {
					parsed = ParseMode(value, config.mode);
				} else if (key == "detailedincidents" || key == "detailedincidentlogs") {
					parsed = ParseBool(value, config.detailedIncidents);
				} else if (key == "allowunqualifiedsuppression" || key == "experimentalsuppression") {
					parsed = ParseBool(value, config.allowUnqualifiedSuppression);
				} else {
					// Unknown settings are ignored so a future configuration file can
					// be used with an older binary without changing its defaults.
					continue;
				}
				if (!parsed) {
					++config.invalidEntries;
				}
			}

			if (!config.enabled) {
				config.mode = GuardMode::Off;
			}
		} catch (...) {
			// Configuration is optional. Keep the requested prevention defaults if a
			// filesystem or allocation error occurs while reading it.
			config.invalidEntries++;
		}
		return config;
	}

	const char* ToString(const GuardMode mode) noexcept
	{
		switch (mode) {
		case GuardMode::Off: return "off";
		case GuardMode::Observe: return "observe";
		case GuardMode::Prevent: return "prevent";
		default: return "unknown";
		}
	}
}
