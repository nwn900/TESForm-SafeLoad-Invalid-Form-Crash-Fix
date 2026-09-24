#include "Validation/SaveLoadGuardValidation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace saveloadguard::validation
{
	namespace
	{
		[[nodiscard]] bool CheckedEnd(
			const std::uintptr_t begin,
			const std::size_t size,
			std::uintptr_t& end) noexcept
		{
			if (size > std::numeric_limits<std::uintptr_t>::max() - begin) {
				return false;
			}
			end = begin + size;
			return true;
		}

		[[nodiscard]] bool InRange(
			const std::uintptr_t address,
			const std::size_t size,
			const std::uintptr_t rangeAddress,
			const std::size_t rangeSize) noexcept
		{
			if (rangeAddress == 0 || rangeSize == 0 || address < rangeAddress) {
				return false;
			}
			std::uintptr_t end{};
			if (!CheckedEnd(rangeAddress, rangeSize, end) || address > end) {
				return false;
			}
			std::uintptr_t requestedEnd{};
			return CheckedEnd(address, size, requestedEnd) && requestedEnd <= end;
		}
	}

	ValidationReport ValidateEditorIdTarget(
		const actor3dguard::validation::IMemoryReader& reader,
		const std::uintptr_t form,
		const std::uintptr_t gameTextAddress,
		const std::size_t gameTextSize,
		const std::uintptr_t gameRdataAddress,
		const std::size_t gameRdataSize) noexcept
	{
		ValidationReport report{};
		report.form = form;
		if (form == 0) {
			report.reason = ValidationReason::NullForm;
			return report;
		}
		if (!reader.IsReadable(form, FormTypeOffset + sizeof(std::uint8_t))) {
			report.reason = ValidationReason::UnreadableForm;
			return report;
		}

		// These two fields were already consumed by LoadGame, but copying them
		// into the incident makes a future crash report correlate the guard event
		// with the saved form without ever invoking a virtual accessor.
		(void)reader.Read(form + FormIdOffset, std::addressof(report.formId), sizeof(report.formId));
		(void)reader.Read(form + FormTypeOffset, std::addressof(report.formType), sizeof(report.formType));

		if (!reader.Read(form, std::addressof(report.vtable), sizeof(report.vtable)) || report.vtable == 0) {
			report.reason = ValidationReason::UnreadableVtable;
			return report;
		}
		if (!reader.IsReadable(report.vtable, EditorIdVtableOffset + sizeof(std::uintptr_t))) {
			report.reason = ValidationReason::UnreadableVtable;
			return report;
		}
		// Engine vtables live in the executable's read-only data image.  Requiring
		// the slot itself to be in that section prevents a freed heap object whose
		// first word happens to be readable from being dispatched.
		if (!InRange(report.vtable, EditorIdVtableOffset + sizeof(std::uintptr_t), gameRdataAddress, gameRdataSize)) {
			report.reason = ValidationReason::UnreadableVtable;
			return report;
		}

		const auto slotAddress = report.vtable + EditorIdVtableOffset;
		if (!reader.Read(slotAddress, std::addressof(report.editorIdEntry), sizeof(report.editorIdEntry)) ||
			report.editorIdEntry == 0 || !reader.IsAccessible(report.editorIdEntry, 1, actor3dguard::validation::MemoryAccess::Execute)) {
			report.reason = ValidationReason::NonExecutableEditorId;
			return report;
		}
		if (!InRange(report.editorIdEntry, 1, gameTextAddress, gameTextSize)) {
			report.reason = ValidationReason::EditorIdOutsideGameText;
			return report;
		}
		return report;
	}

	bool IsEditorIdResultReadable(
		const actor3dguard::validation::IMemoryReader& reader,
		const char* const result) noexcept
	{
		if (result == nullptr) {
			return false;
		}
		// SaveGameWarning consumes a C string.  A single readable byte is enough
		// to reject the -1/noncanonical result seen in malformed objects; limit the
		// scan so a missing terminator cannot turn this guard into an unbounded walk.
		constexpr std::size_t MaximumEditorIdLength = 512;
		const auto address = reinterpret_cast<std::uintptr_t>(result);
		for (std::size_t index = 0; index < MaximumEditorIdLength; ++index) {
			if (address > std::numeric_limits<std::uintptr_t>::max() - index ||
				!reader.IsReadable(address + index, 1)) {
				return false;
			}
			std::uint8_t value{};
			if (!reader.Read(address + index, std::addressof(value), sizeof(value))) {
				return false;
			}
			if (value == 0) {
				return true;
			}
		}
		return false;
	}

	const char* ToString(const ValidationReason reason) noexcept
	{
		switch (reason) {
		case ValidationReason::None: return "NONE";
		case ValidationReason::NullForm: return "NULL_FORM";
		case ValidationReason::UnreadableForm: return "UNREADABLE_FORM";
		case ValidationReason::UnreadableVtable: return "UNREADABLE_VTABLE";
		case ValidationReason::NonExecutableEditorId: return "NONEXECUTABLE_EDITOR_ID";
		case ValidationReason::EditorIdOutsideGameText: return "EDITOR_ID_OUTSIDE_GAME_TEXT";
		case ValidationReason::InvalidEditorIdResult: return "INVALID_EDITOR_ID_RESULT";
		default: return "UNKNOWN";
		}
	}
}
