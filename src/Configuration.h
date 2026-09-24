#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace actor3dguard
{
	enum class GuardMode : std::uint8_t
	{
		Off,
		Observe,
		Prevent
	};

	struct Configuration
	{
		bool enabled{ true };
		GuardMode mode{ GuardMode::Prevent };
		bool detailedIncidents{ false };
		// Default prevention was explicitly requested. Static cleanup evidence
		// and verified callsite bytes remain mandatory even with this enabled.
		bool allowUnqualifiedSuppression{ true };
		std::uint32_t invalidEntries{ 0 };
		std::filesystem::path path{};
	};

	// Reads Data/F4SE/<iniFileName> beside the game executable. Every guard owns
	// one file name so two guards can be configured independently.
	[[nodiscard]] Configuration LoadConfiguration(std::string_view iniFileName) noexcept;

	// Actor skin guard's file name; kept for the original call site.
	[[nodiscard]] Configuration LoadConfiguration() noexcept;

	[[nodiscard]] const char* ToString(GuardMode mode) noexcept;
}
