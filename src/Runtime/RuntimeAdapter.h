#pragma once

#include "Runtime/CleanupContract.h"

#if defined(FALLOUTVR)
#	include <REL/Relocation.h>
#else
#	include <REL/Version.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace actor3dguard::runtime
{
	// The plugin uses the post-0.6 F4SE interface layout.  Keep this check
	// available to the load entry point so an old loader is rejected before
	// CommonLib/F4SE initialization dereferences newer interface fields.
	[[nodiscard]] constexpr bool IsSupportedF4SEABI(const REL::Version version) noexcept
	{
		return version.major() == 0 && version.minor() >= 6;
	}

	struct HookContract
	{
		std::uint64_t actorScaleId{ 0 };
		std::uint64_t skinScaleId{ 0 };
		std::uint64_t fixedStringAssignId{ 0 };
		std::array<std::ptrdiff_t, 2> knownCallsiteOffsets{ 0, 0 };
		std::size_t scanWindow{ 0 };
		std::uint8_t expectedCallsites{ 0 };
		bool staticCallsitesVerified{ false };
		bool outerContextVerified{ false };
		bool signatureScanEnabled{ false };
		bool allowRelaxedSignatures{ false };
		CleanupContract cleanup{};
	};

	struct RuntimeAdapter
	{
		REL::Version gameVersion{};
		std::string_view family{};
		REL::Version f4seVersion{};
		HookContract skinBoneGuard{};
		bool known{ false };
		bool installable{ false };
		bool preventionQualified{ false };
	};

	// The one-argument lookup is the known three-component game-release table
	// and is used for loader-mismatch diagnostics.  The two-argument lookup
	// first requires a matching game/F4SE release pair (fourth file/build
	// components are ignored), then may return an address-free scan descriptor
	// for an unlisted PC patch; callers must still treat that descriptor as
	// unqualified.
	[[nodiscard]] const RuntimeAdapter* Find(REL::Version version) noexcept;
	[[nodiscard]] const RuntimeAdapter* Find(REL::Version gameVersion, REL::Version f4seVersion) noexcept;
	[[nodiscard]] std::string_view FamilyName(REL::Version version) noexcept;
}
