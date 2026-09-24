#include "Runtime/SaveLoadGuardScan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace saveloadguard::runtime
{
	namespace
	{
		// Start six bytes before the call.  The prefix ties the indirect call to
		// the TESForm object in RSI and rules out unrelated slot-0x1D0 calls.
		constexpr std::array<std::uint8_t, ContractSize> kExactPattern{
			0x48, 0x8B, 0x06,                         // mov rax,[rsi]
			0x48, 0x8B, 0xCE,                         // mov rcx,rsi
			0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00,       // call [rax+1D0]
			0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00,// lea rcx,[rip+warning]
			0x4D, 0x8B, 0xCF,                         // mov r9,r15
			0x45, 0x8B, 0xC6,                         // mov r8d,r14d
			0x48, 0x8B, 0xD0,                         // mov rdx,rax
			0x4C, 0x89, 0x64, 0x24, 0x20,             // spill r12
			0xE8, 0x00, 0x00, 0x00, 0x00              // call SaveGameWarning
		};

		constexpr std::array<std::uint8_t, ContractSize> kExactMask{
			0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
			0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0x00, 0x00, 0x00, 0x00
		};

		// The relaxed anchor remains exact through the indirect call.  The
		// instructions immediately after the call still need to build a warning
		// call (LEA RCX,[RIP+...], then a direct E8) and pass the editor-ID result
		// in RDX.  The cleanup verifier below requires the RemoveChanges-shaped
		// sequence after the warning call as an additional discriminator.
		constexpr std::size_t RelaxedPrefixLength = 15;

		[[nodiscard]] bool MatchesBytes(
			const std::uint8_t* bytes,
			const std::array<std::uint8_t, ContractSize>& pattern,
			const std::array<std::uint8_t, ContractSize>& mask) noexcept
		{
			for (std::size_t index = 0; index < pattern.size(); ++index) {
				if ((bytes[index] & mask[index]) != (pattern[index] & mask[index])) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool IsStableAnchor(
			const std::uint8_t* bytes) noexcept
		{
			return bytes[0] == 0x48 && bytes[1] == 0x8B && bytes[2] == 0x06 &&
				bytes[3] == 0x48 && bytes[4] == 0x8B && bytes[5] == 0xCE &&
				bytes[6] == 0xFF && bytes[7] == 0x90 &&
				bytes[8] == 0xD0 && bytes[9] == 0x01 && bytes[10] == 0x00 && bytes[11] == 0x00;
		}

		[[nodiscard]] bool IsRelaxedWarningShape(
			const std::uint8_t* bytes,
			const std::size_t available) noexcept
		{
			if (available < RelaxedPrefixLength || !IsStableAnchor(bytes) ||
				bytes[12] != 0x48 || bytes[13] != 0x8D || bytes[14] != 0x0D) {
				return false;
			}

			// Search only the short post-call window.  We require the result move
			// into RDX and one direct warning call, without depending on encoded
			// displacements or a particular volatile-register choice.
			bool resultInRdx = false;
			bool warningCall = false;
			for (std::size_t index = 15; index + 2 < available && index < 48; ++index) {
				if (bytes[index] == 0x48 && bytes[index + 1] == 0x8B && bytes[index + 2] == 0xD0) {
					resultInRdx = true;
				}
				if (bytes[index] == 0xE8) {
					warningCall = true;
					break;
				}
			}
			return resultInRdx && warningCall;
		}

		[[nodiscard]] bool HasCleanupShape(
			const actor3dguard::validation::IMemoryReader& reader,
			const std::uintptr_t callsite) noexcept
		{
			// Read enough bytes after the warning call to cover the known cleanup
			// sequence.  The exact direct-call displacement remains wildcarded.
			constexpr std::size_t Window = 0x40;
			if (callsite > std::numeric_limits<std::uintptr_t>::max() - CallInstructionSize - Window) {
				return false;
			}
			std::array<std::uint8_t, CallInstructionSize + Window> bytes{};
			if (!reader.Read(callsite, bytes.data(), bytes.size())) {
				return false;
			}

			// The warning call is the first E8 in this window for the exact shape.
			// Locate a following `mov edx,[rbx]` and the RIP-relative changes-map
			// call; this is the branch that clears the load-table entry afterwards.
			for (std::size_t index = CallInstructionSize; index + 3 < bytes.size(); ++index) {
				if (bytes[index] != 0xE8) {
					continue;
				}
				for (std::size_t next = index + 5; next + 8 < bytes.size(); ++next) {
					if (bytes[next] == 0x8B && bytes[next + 1] == 0x13 &&
						bytes[next + 2] == 0x49 && bytes[next + 3] == 0x8B &&
						bytes[next + 4] == 0x8D) {
						return true;
					}
				}
				// A candidate with a warning call but no cleanup is not this
				// LoadGame mismatch branch.
				return false;
			}
			return false;
		}
	}

	bool MatchesEditorIdCallsite(
		const actor3dguard::validation::IMemoryReader& reader,
		const std::uintptr_t callsite,
		const bool relaxed) noexcept
	{
		if (callsite < PrefixSize || callsite > std::numeric_limits<std::uintptr_t>::max() - ContractSize) {
			return false;
		}

		std::array<std::uint8_t, ContractSize> bytes{};
		if (!reader.Read(callsite - PrefixSize, bytes.data(), bytes.size())) {
			return false;
		}
		if (MatchesBytes(bytes.data(), kExactPattern, kExactMask)) {
			return HasCleanupShape(reader, callsite);
		}
		if (!relaxed || !IsRelaxedWarningShape(bytes.data(), bytes.size())) {
			return false;
		}
		return HasCleanupShape(reader, callsite);
	}

	ScanResult DiscoverSaveLoadGuard(
		const actor3dguard::validation::IMemoryReader& reader,
		const std::uintptr_t textAddress,
		const std::size_t textSize,
		const ScanParameters& parameters) noexcept
	{
		ScanResult result{};
		if (textAddress == 0 || textSize < ContractSize ||
			textSize > 0x40000000ull || textSize > std::numeric_limits<std::uintptr_t>::max() - textAddress) {
			return result;
		}

		const auto textEnd = textAddress + textSize;
		std::array<std::uint8_t, TextScanWindow + ContractSize> block{};
		std::uintptr_t exactCallsite = 0;
		std::uintptr_t relaxedCallsite = 0;
		std::uint16_t exactCount = 0;
		std::uint16_t relaxedCount = 0;

		for (std::size_t offset = 0; offset < textSize;) {
			const auto begin = textAddress + offset;
			const auto remaining = static_cast<std::size_t>(textEnd - begin);
			const auto wanted = (std::min)(remaining, block.size());
			if (wanted < ContractSize) {
				// The preceding overlapped block already covered every possible
				// start position; fewer than ContractSize bytes cannot contain a
				// new candidate.
				break;
			}
			if (!reader.Read(begin, block.data(), wanted)) {
				return ScanResult{};
			}

			for (std::size_t index = 0; index + ContractSize <= wanted; ++index) {
				const auto* bytes = block.data() + index;
				if (bytes[0] != 0x48 || bytes[1] != 0x8B || bytes[2] != 0x06) {
					continue;
				}
				const auto candidate = begin + index + PrefixSize;
				if (MatchesBytes(bytes, kExactPattern, kExactMask) && HasCleanupShape(reader, candidate)) {
					if (exactCallsite == 0) {
						exactCallsite = candidate;
					}
					if (exactCount != (std::numeric_limits<std::uint16_t>::max)()) {
						++exactCount;
					}
					continue;
				}
				if (parameters.allowRelaxedSignatures && IsRelaxedWarningShape(bytes, wanted - index) &&
					HasCleanupShape(reader, candidate)) {
					if (relaxedCallsite == 0) {
						relaxedCallsite = candidate;
					}
					if (relaxedCount != (std::numeric_limits<std::uint16_t>::max)()) {
						++relaxedCount;
					}
				}
			}

			if (begin + wanted >= textEnd) {
				break;
			}
			const auto step = wanted > ContractSize ? wanted - ContractSize + 1 : wanted;
			if (step == 0 || offset > textSize - step) {
				break;
			}
			offset += step;
		}

		result.exactCandidates = exactCount;
		result.relaxedCandidates = relaxedCount;
		if (exactCount == 1) {
			result.callsite = exactCallsite;
			result.exact = true;
			result.unique = true;
			return result;
		}
		if (exactCount == 0 && relaxedCount == 1) {
			result.callsite = relaxedCallsite;
			result.exact = false;
			result.unique = true;
		}
		return result;
	}
}
