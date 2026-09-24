#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace actor3dguard::platform
{
	enum class LogLevel { Info, Warning, Error };

	/**
	 * Create this module's numbered, append-only log under
	 * Documents/My Games/<saveFolder>/F4SE.
	 *
	 * Every guard links its own copy of this translation unit, so the logger and
	 * the periodic drain are per-DLL: one plugin's failures cannot silence or
	 * redirect another's log.
	 */
	[[nodiscard]] bool InitializeLogging(std::string_view saveFolder, std::string_view logName) noexcept;

	// Windows timer consumer for the guard's incident queue.  The callback must
	// be a non-capturing function living in the calling module.
	[[nodiscard]] bool StartPeriodicLogging(void (*callback)(void*) noexcept) noexcept;

	void WriteLog(LogLevel level, std::string_view text) noexcept;
	template <class... Args>
	void Log(LogLevel level, std::format_string<Args...> format, Args&&... args) noexcept
	{
		try {
			WriteLog(level, std::format(format, std::forward<Args>(args)...));
		} catch (...) {
			// Formatting/allocation failure must not escape an engine callback.
		}
	}
}

#define ACTOR3D_INFO(...) ::actor3dguard::platform::Log(::actor3dguard::platform::LogLevel::Info, __VA_ARGS__)
#define ACTOR3D_WARN(...) ::actor3dguard::platform::Log(::actor3dguard::platform::LogLevel::Warning, __VA_ARGS__)
#define ACTOR3D_ERROR(...) ::actor3dguard::platform::Log(::actor3dguard::platform::LogLevel::Error, __VA_ARGS__)
