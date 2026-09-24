#pragma once

#include "Validation/MemoryReader.h"

#include <cstddef>
#include <cstdint>

namespace saveloadguard::runtime
{
	// Fallout 4 1.10.163 calls TESForm::GetFormEditorID through slot 0x3A
	// (byte offset 0x1D0) in the save-load type-mismatch warning branch.  The
	// six-byte indirect call is replaced in-place by the installer, so this
	// contract deliberately measures the complete instruction and never splits
	// the surrounding instruction stream.
	inline constexpr std::size_t CallInstructionSize = 6;
	inline constexpr std::size_t PrefixSize = 6;
	inline constexpr std::size_t ContractSize = 38;
	inline constexpr std::size_t TextScanWindow = 64u * 1024u;

	struct ScanParameters
	{
		// A relaxed match keeps the stable register/data-flow anchor and the
		// warning/cleanup shape, while allowing compiler-generated register moves
		// and branch immediates to change on an unlisted PC patch.
		bool allowRelaxedSignatures{ true };
	};

	struct ScanResult
	{
		std::uintptr_t callsite{ 0 };
		std::uintptr_t functionAddress{ 0 };
		std::uint16_t exactCandidates{ 0 };
		std::uint16_t relaxedCandidates{ 0 };
		bool exact{ false };
		bool unique{ false };

		[[nodiscard]] bool Qualified() const noexcept
		{
			return unique && callsite != 0;
		}
	};

	// Verify the exact 1.10.163 callsite shape (or the conservative relaxed
	// equivalent). `callsite` points at FF 90 D0 01 00 00.
	[[nodiscard]] bool MatchesEditorIdCallsite(
		const actor3dguard::validation::IMemoryReader& reader,
		std::uintptr_t callsite,
		bool relaxed = false) noexcept;

	// Scan an executable text section and return one uniquely qualified callsite.
	// Ambiguous or partial scans fail closed and return an empty result.
	[[nodiscard]] ScanResult DiscoverSaveLoadGuard(
		const actor3dguard::validation::IMemoryReader& reader,
		std::uintptr_t textAddress,
		std::size_t textSize,
		const ScanParameters& parameters = {}) noexcept;
}
