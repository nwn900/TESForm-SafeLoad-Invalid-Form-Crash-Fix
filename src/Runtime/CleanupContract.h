#pragma once

#include <cstdint>

namespace actor3dguard::runtime
{
	// Static cleanup facts are kept separate from the live qualification bit.
	// The mapped 1.10.163 disassembly shows a stack BSFixedString temporary,
	// ignored assignment return, a null-tolerant length read, and normal cleanup
	// that releases the temporary.  Those facts narrow the candidate continuation
	// but do not establish object lifetime or gameplay safety across worker races.
	struct CleanupContract
	{
		std::uint16_t destinationStackOffset{ 0 };
		std::uint16_t lookupResultStackOffset{ 0 };
		std::uint16_t commonContinuationOffset{ 0 };
		bool destinationInitialized{ false };
		bool assignmentReturnIgnored{ false };
		bool lengthHelperNullSafe{ false };
		bool normalCleanupNullSafe{ false };
		bool separateLookupRelease{ false };
		bool liveQualified{ false };

		[[nodiscard]] constexpr bool StaticEvidenceComplete() const noexcept
		{
			return destinationStackOffset != 0 &&
				lookupResultStackOffset != 0 &&
				commonContinuationOffset != 0 &&
				destinationInitialized &&
				assignmentReturnIgnored &&
				lengthHelperNullSafe &&
				normalCleanupNullSafe &&
				separateLookupRelease;
		}

		// This is the only predicate a future installer should use when deciding
		// whether leaving the destination unchanged is a qualified suppression.
		// Static evidence alone deliberately returns false until a live A/B and
		// lifetime test promotes the runtime-specific bit.
		[[nodiscard]] constexpr bool CanLeaveDestinationUnchanged() const noexcept
		{
			return StaticEvidenceComplete() && liveQualified;
		}
	};

	inline constexpr CleanupContract LegacyCleanupContract{
		.destinationStackOffset = 0x30,
		.lookupResultStackOffset = 0x48,
		.commonContinuationOffset = 0x18E,
		.destinationInitialized = true,
		.assignmentReturnIgnored = true,
		.lengthHelperNullSafe = true,
		.normalCleanupNullSafe = true,
		.separateLookupRelease = true,
		.liveQualified = false
	};

	// The local 1.11.221.0 image shows the same ownership questions through a
	// different compiler layout: the stack temporary remains at [rsp+0x30], the
	// flattened lookup result remains at [rsp+0x48], and both paths rejoin at
	// skin-scale +0x165.  This is static disassembly evidence only; no live
	// lifetime or continuation test has promoted the suppression bit.
	inline constexpr CleanupContract AlternateCleanupContract{
		.destinationStackOffset = 0x30,
		.lookupResultStackOffset = 0x48,
		.commonContinuationOffset = 0x165,
		.destinationInitialized = true,
		.assignmentReturnIgnored = true,
		.lengthHelperNullSafe = true,
		.normalCleanupNullSafe = true,
		.separateLookupRelease = true,
		.liveQualified = false
	};
}
