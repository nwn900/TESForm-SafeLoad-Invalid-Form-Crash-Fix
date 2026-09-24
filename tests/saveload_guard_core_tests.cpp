#ifdef NDEBUG
#undef NDEBUG
#endif

#include "Runtime/SaveLoadGuardScan.h"
#include "Validation/SaveLoadGuardValidation.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace
{
	void TestExactScan()
	{
		std::array<std::uint8_t, 256> image{};
		constexpr std::size_t offset = 64;
		constexpr std::array<std::uint8_t, saveloadguard::runtime::ContractSize> pattern{
			0x48, 0x8B, 0x06, 0x48, 0x8B, 0xCE,
			0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00,
			0x48, 0x8D, 0x0D, 0xA0, 0x00, 0x00, 0x00,
			0x4D, 0x8B, 0xCF, 0x45, 0x8B, 0xC6,
			0x48, 0x8B, 0xD0, 0x4C, 0x89, 0x64, 0x24, 0x20,
			0xE8, 0x10, 0x00, 0x00, 0x00
		};
	std::memcpy(image.data() + offset, pattern.data(), pattern.size());
	// Warning call followed by the changes-map cleanup discriminator.
	const std::array<std::uint8_t, 5> cleanup{ 0x8B, 0x13, 0x49, 0x8B, 0x8D };
	std::memcpy(image.data() + offset + pattern.size(), cleanup.data(), cleanup.size());
	actor3dguard::validation::BufferMemoryReader reader(image.data(), image.size());
	const auto callsite = reinterpret_cast<std::uintptr_t>(image.data()) + offset + saveloadguard::runtime::PrefixSize;
	assert(saveloadguard::runtime::MatchesEditorIdCallsite(reader, callsite, false));
	const auto result = saveloadguard::runtime::DiscoverSaveLoadGuard(
		reader,
		reinterpret_cast<std::uintptr_t>(image.data()),
		image.size(),
		{});
	assert(result.Qualified());
	assert(result.exact);
	assert(result.callsite == callsite);
	assert(result.exactCandidates == 1);
	}

	void TestAmbiguousScanFailsClosed()
	{
		std::array<std::uint8_t, 512> image{};
		constexpr std::array<std::uint8_t, saveloadguard::runtime::ContractSize> pattern{
			0x48, 0x8B, 0x06, 0x48, 0x8B, 0xCE,
			0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00,
			0x48, 0x8D, 0x0D, 0xA0, 0x00, 0x00, 0x00,
			0x4D, 0x8B, 0xCF, 0x45, 0x8B, 0xC6,
			0x48, 0x8B, 0xD0, 0x4C, 0x89, 0x64, 0x24, 0x20,
			0xE8, 0x10, 0x00, 0x00, 0x00
		};
	std::memcpy(image.data() + 32, pattern.data(), pattern.size());
	std::memcpy(image.data() + 256, pattern.data(), pattern.size());
	const std::array<std::uint8_t, 5> cleanup{ 0x8B, 0x13, 0x49, 0x8B, 0x8D };
	std::memcpy(image.data() + 32 + pattern.size(), cleanup.data(), cleanup.size());
	std::memcpy(image.data() + 256 + pattern.size(), cleanup.data(), cleanup.size());
	actor3dguard::validation::BufferMemoryReader reader(image.data(), image.size());
	const auto result = saveloadguard::runtime::DiscoverSaveLoadGuard(
		reader,
		reinterpret_cast<std::uintptr_t>(image.data()),
		image.size(),
		{});
	assert(!result.Qualified());
		assert(result.exactCandidates == 2);
	}

	void TestRelaxedScanKeepsCleanupGate()
	{
		std::array<std::uint8_t, 256> image{};
		constexpr std::size_t offset = 48;
		// Keep the stable object/slot anchor, but vary the register moves that
		// are wildcarded on an unlisted PC patch.
		const std::array<std::uint8_t, saveloadguard::runtime::ContractSize> pattern{
			0x48, 0x8B, 0x06, 0x48, 0x8B, 0xCE,
			0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00,
			0x48, 0x8D, 0x0D, 0xA0, 0x00, 0x00, 0x00,
			0x4C, 0x8B, 0xC7, 0x44, 0x8B, 0xCE,
			0x48, 0x8B, 0xD0, 0x4C, 0x89, 0x64, 0x24, 0x20,
			0xE8, 0x10, 0x00, 0x00, 0x00
		};
		std::memcpy(image.data() + offset, pattern.data(), pattern.size());
		const std::array<std::uint8_t, 5> cleanup{ 0x8B, 0x13, 0x49, 0x8B, 0x8D };
		std::memcpy(image.data() + offset + pattern.size(), cleanup.data(), cleanup.size());
		actor3dguard::validation::BufferMemoryReader reader(image.data(), image.size());
		const auto callsite = reinterpret_cast<std::uintptr_t>(image.data()) + offset + saveloadguard::runtime::PrefixSize;
		assert(saveloadguard::runtime::MatchesEditorIdCallsite(reader, callsite, true));
		const auto result = saveloadguard::runtime::DiscoverSaveLoadGuard(
			reader,
			reinterpret_cast<std::uintptr_t>(image.data()),
			image.size(),
			{ .allowRelaxedSignatures = true });
		assert(result.Qualified());
		assert(!result.exact);
		assert(result.relaxedCandidates == 1);
	}

	void TestVtableValidation()
	{
		std::array<std::uint8_t, 1024> image{};
		constexpr std::size_t objectOffset = 0;
		constexpr std::size_t rdataOffset = 256;
		constexpr std::size_t textOffset = 768;
		const auto imageAddress = reinterpret_cast<std::uintptr_t>(image.data());
		const auto objectAddress = imageAddress + objectOffset;
		const auto rdataAddress = imageAddress + rdataOffset;
		const auto textAddress = imageAddress + textOffset;
		const auto vtable = rdataAddress + 32;
		const auto entry = textAddress + 64;
		std::memcpy(image.data() + objectOffset, &vtable, sizeof(vtable));
		std::memcpy(image.data() + objectOffset + saveloadguard::validation::FormIdOffset, "\x34\x12\x00\x01", 4);
		std::memcpy(image.data() + objectOffset + saveloadguard::validation::FormTypeOffset, "\x2A", 1);
		std::memcpy(image.data() + rdataOffset + 32 + saveloadguard::validation::EditorIdVtableOffset, &entry, sizeof(entry));
		actor3dguard::validation::BufferMemoryReader reader(
			image.data(), image.size(), image.data() + textOffset, image.size() - textOffset);
		const auto valid = saveloadguard::validation::ValidateEditorIdTarget(
			reader, objectAddress, textAddress, image.size() - textOffset, rdataAddress, 512);
		assert(valid.Callable());
		assert(valid.formId == 0x01001234u);
		assert(valid.formType == 0x2Au);

		const auto invalid = saveloadguard::validation::ValidateEditorIdTarget(
			reader, objectAddress + 512, textAddress, image.size() - textOffset, rdataAddress, 512);
		assert(invalid.reason == saveloadguard::validation::ValidationReason::UnreadableVtable);

		const auto nullForm = saveloadguard::validation::ValidateEditorIdTarget(
			reader, 0, textAddress, image.size() - textOffset, rdataAddress, 512);
		assert(nullForm.reason == saveloadguard::validation::ValidationReason::NullForm);
	}
}

int main()
{
	TestExactScan();
	TestAmbiguousScanFailsClosed();
	TestRelaxedScanKeepsCleanupGate();
	TestVtableValidation();
	return 0;
}
