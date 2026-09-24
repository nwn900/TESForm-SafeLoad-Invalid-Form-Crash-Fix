#include "Platform/Logging.h"
#include "Diagnostics/PeriodicDrain.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <stdexcept>
#include <system_error>

namespace actor3dguard::platform
{
	namespace
	{
		std::atomic<spdlog::logger*> logger{ nullptr };
		diagnostics::PeriodicDrain* drain{ nullptr };

		// A rotating sink reuses .1/.2 files and can erase the only copy of a
		// useful crash record on the next launch. Reserve a fresh filename for
		// every process instead. CREATE_NEW makes the reservation atomic if two
		// game processes happen to start at the same time.
		[[nodiscard]] std::filesystem::path ReserveNumberedLogPath(
			const std::filesystem::path& directory,
			const std::string_view logName)
		{
			constexpr std::uint64_t MaximumLogNumber = 1'000'000;
			for (std::uint64_t number = 1; number <= MaximumLogNumber; ++number) {
				const auto candidate = directory / (std::string(logName) + "." + std::to_string(number) + ".log");
				const auto handle = ::CreateFileW(
					candidate.c_str(),
					GENERIC_WRITE,
					0,
					nullptr,
					CREATE_NEW,
					FILE_ATTRIBUTE_NORMAL,
					nullptr);
				if (handle != INVALID_HANDLE_VALUE) {
					::CloseHandle(handle);
					return candidate;
				}
				const auto error = ::GetLastError();
				if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
					continue;
				}
				throw std::system_error(
					static_cast<int>(error),
					std::system_category(),
					"cannot reserve numbered F4SE log file");
			}
			throw std::runtime_error("numbered F4SE log sequence is exhausted");
		}
	}

	bool InitializeLogging(std::string_view saveFolder, std::string_view logName) noexcept
	{
		if (logger.load(std::memory_order_acquire)) {
			return true;
		}
		try {
			PWSTR documents = nullptr;
			const auto result = ::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents);
			std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)> owner(documents, ::CoTaskMemFree);
			if (FAILED(result) || !documents) {
				return false;
			}
			const auto directory = std::filesystem::path(documents) / "My Games" / saveFolder / "F4SE";
			std::filesystem::create_directories(directory);
			const auto name = std::string(logName);
			const auto logPath = ReserveNumberedLogPath(directory, name);
			// The file was created exclusively above. Append mode keeps the
			// reservation intact and never truncates an earlier numbered log.
			auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), false);
			auto instance = std::make_unique<spdlog::logger>(name, std::move(sink));
			instance->set_pattern("[%T.%e] [%l] %v");
			instance->set_level(spdlog::level::info);
			instance->flush_on(spdlog::level::info);
			// Installed hooks live until process exit. Retain their independent
			// thread-safe sink across CRT teardown; the OS reclaims the handle and
			// storage. No callback joins or logger destruction in DLL detach.
			logger.store(instance.release(), std::memory_order_release);
			return true;
		} catch (...) {
			::OutputDebugStringW(L"F4SE guard: log file unavailable; guard remains available.\n");
			return false;
		}
	}

	void WriteLog(LogLevel level, std::string_view text) noexcept
	{
		try {
			if (auto* output = logger.load(std::memory_order_acquire)) {
				const auto severity = level == LogLevel::Info ? spdlog::level::info :
					level == LogLevel::Warning ? spdlog::level::warn : spdlog::level::err;
				output->log(severity, "{}", text);
			}
		} catch (...) {
			// A static string can outlive CRT teardown while the timer is active.
			::OutputDebugStringW(L"F4SE guard: log write failed.\n");
		}
	}

	bool StartPeriodicLogging(void (*callback)(void*) noexcept) noexcept
	{
		if (drain) {
			return true;
		}
		if (callback == nullptr) {
			return false;
		}
		try {
			auto consumer = std::make_unique<diagnostics::PeriodicDrain>();
			HMODULE module = nullptr;
			// Timer callbacks and installed hooks must not point into an unloaded
			// DLL. Pin after hook installation, outside DllMain.
			if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
					reinterpret_cast<LPCWSTR>(&StartPeriodicLogging), &module) ||
				!consumer->Start(callback, nullptr)) {
				return false;
			}
			// Process lifetime, as above. Never destroy/wait under loader lock.
			drain = consumer.release();
			return true;
		} catch (...) {
			return false;
		}
	}
}
