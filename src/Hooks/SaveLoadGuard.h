#pragma once

#include "Configuration.h"

#include <cstdint>

namespace saveloadguard::hooks
{
	enum class InstallStatus : std::uint8_t
	{
		Installed,
		Disabled,
		AddressUnavailable,
		SignatureMismatch,
		HookConflict,
		TrampolineUnavailable
	};

	struct InstallReport
	{
		InstallStatus status{ InstallStatus::SignatureMismatch };
		std::uintptr_t callsite{ 0 };
		std::uintptr_t functionAddress{ 0 };
		std::uint16_t exactCandidates{ 0 };
		std::uint16_t relaxedCandidates{ 0 };
		bool exactContract{ false };
		bool preventionEnabled{ false };
		bool preventionQualified{ false };
	};

	[[nodiscard]] InstallReport Install(
		const actor3dguard::Configuration& configuration,
		std::uint32_t runtimeCode) noexcept;

	void DrainLogs() noexcept;
	void DrainLogsImmediately() noexcept;
	[[nodiscard]] bool IsActive() noexcept;
	[[nodiscard]] std::uint64_t PreventedTotal() noexcept;
	[[nodiscard]] std::uint64_t ObservedTotal() noexcept;
	[[nodiscard]] const char* ToString(InstallStatus status) noexcept;

	// This is the target of the six-byte FF 90 call in LoadGame.  It returns a
	// stable placeholder when the TESForm/vtable is stale, allowing LoadGame's
	// existing warning and table-cleanup path to continue.
	__declspec(noinline) const char* GuardedGetFormEditorID(const void* form) noexcept;
}
