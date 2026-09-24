#pragma once

#include "Validation/MemoryReader.h"

#include <cstdint>

namespace saveloadguard::validation
{
	inline constexpr std::uintptr_t EditorIdVtableOffset = 0x1D0;
	inline constexpr std::size_t FormIdOffset = 0x14;
	inline constexpr std::size_t FormTypeOffset = 0x1A;

	enum class ValidationReason : std::uint8_t
	{
		None,
		NullForm,
		UnreadableForm,
		UnreadableVtable,
		NonExecutableEditorId,
		EditorIdOutsideGameText,
		InvalidEditorIdResult
	};

	struct ValidationReport
	{
		std::uintptr_t form{ 0 };
		std::uintptr_t vtable{ 0 };
		std::uintptr_t editorIdEntry{ 0 };
		std::uint32_t formId{ 0 };
		std::uint8_t formType{ 0 };
		ValidationReason reason{ ValidationReason::None };

		[[nodiscard]] bool Callable() const noexcept
		{
			return reason == ValidationReason::None && form != 0 && editorIdEntry != 0;
		}
	};

	// Validate only the memory needed by the callsite guard.  No TESForm method
	// or CommonLib field access is performed here: the object may be stale, and
	// the whole purpose of this probe is to decide whether dispatch is safe.
	[[nodiscard]] ValidationReport ValidateEditorIdTarget(
		const actor3dguard::validation::IMemoryReader& reader,
		std::uintptr_t form,
		std::uintptr_t gameTextAddress,
		std::size_t gameTextSize,
		std::uintptr_t gameRdataAddress,
		std::size_t gameRdataSize) noexcept;

	[[nodiscard]] bool IsEditorIdResultReadable(
		const actor3dguard::validation::IMemoryReader& reader,
		const char* result) noexcept;

	[[nodiscard]] const char* ToString(ValidationReason reason) noexcept;
}
