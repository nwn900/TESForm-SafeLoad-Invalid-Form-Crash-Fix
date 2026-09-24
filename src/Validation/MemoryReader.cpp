#include "Validation/MemoryReader.h"

#include <algorithm>
#include <limits>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <Windows.h>
#endif

namespace actor3dguard::validation
{
	namespace
	{
	#if defined(_MSC_VER)
		[[nodiscard]] int ReadFaultFilter(const unsigned long exceptionCode) noexcept
		{
			// The SEH boundary is deliberately limited to the memcpy probe below.
			// Continue-search for unrelated faults so the validator cannot hide a
			// bug in its own control flow or in a caller.
			return (exceptionCode == EXCEPTION_ACCESS_VIOLATION || exceptionCode == EXCEPTION_IN_PAGE_ERROR) ?
				EXCEPTION_EXECUTE_HANDLER :
				EXCEPTION_CONTINUE_SEARCH;
		}
	#endif

		[[nodiscard]] bool CheckedEnd(std::uintptr_t begin, std::size_t size, std::uintptr_t& end) noexcept
		{
			if (size > std::numeric_limits<std::uintptr_t>::max() - begin) {
				return false;
			}
			end = begin + size;
			return true;
		}

	#if defined(_WIN32)
		[[nodiscard]] bool ProtectionAllows(const DWORD protection, const MemoryAccess access) noexcept
		{
			switch (access) {
			case MemoryAccess::Read:
				return (protection & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
										 PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
			case MemoryAccess::Write:
				// A guard never writes through the queried pointer itself, but the
				// engine operation it is about to allow does. PAGE_WRITECOPY is a
				// writable protection for the writing process.
				return (protection & (PAGE_READWRITE | PAGE_WRITECOPY |
										 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
			case MemoryAccess::Execute:
				return (protection & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
										 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
			default:
				return false;
			}
		}
	#endif
	}

	bool BufferMemoryReader::IsAccessible(const std::uintptr_t address, const std::size_t size, const MemoryAccess access) const noexcept
	{
		if (address == 0) {
			return false;
		}

		std::uintptr_t requestedEnd{};
		if (!CheckedEnd(address, size, requestedEnd)) {
			return false;
		}
		if (size == 0) {
			return true;
		}

		if (access == MemoryAccess::Execute) {
			// Synthetic storage is never executable unless a window was supplied.
			std::uintptr_t executableEnd{};
			return _executableBegin != 0 &&
				CheckedEnd(_executableBegin, _executableSize, executableEnd) &&
				address >= _executableBegin && requestedEnd <= executableEnd;
		}

		if (_begin == 0) {
			return false;
		}

		std::uintptr_t bufferEnd{};
		if (!CheckedEnd(_begin, _size, bufferEnd)) {
			return false;
		}

		return address >= _begin && requestedEnd <= bufferEnd;
	}

	bool BufferMemoryReader::Read(const std::uintptr_t address, void* const destination, const std::size_t size) const noexcept
	{
		if (size == 0) {
			return destination != nullptr || address != 0;
		}
		if (destination == nullptr || !IsAccessible(address, size, MemoryAccess::Read)) {
			return false;
		}

		std::memcpy(destination, reinterpret_cast<const void*>(address), size);
		return true;
	}

#if defined(_WIN32)
	bool ProcessMemoryReader::IsAccessible(
		const std::uintptr_t address,
		const std::size_t size,
		const MemoryAccess access) const noexcept
	{
		if (address == 0) {
			return false;
		}

		std::uintptr_t requestedEnd{};
		if (!CheckedEnd(address, size, requestedEnd)) {
			return false;
		}

		if (size == 0) {
			return true;
		}

		auto current = address;
		while (current < requestedEnd) {
			MEMORY_BASIC_INFORMATION info{};
			if (::VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) != sizeof(info)) {
				return false;
			}
			if (info.State != MEM_COMMIT || info.Protect == PAGE_NOACCESS || (info.Protect & PAGE_GUARD) != 0) {
				return false;
			}
			if (!ProtectionAllows(info.Protect & 0xFFu, access)) {
				return false;
			}

			const auto regionBegin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
			std::uintptr_t regionEnd{};
			if (!CheckedEnd(regionBegin, info.RegionSize, regionEnd) || regionEnd <= current) {
				return false;
			}
			current = (std::min)(regionEnd, requestedEnd);
		}

		return true;
	}

	bool ProcessMemoryReader::Read(const std::uintptr_t address, void* const destination, const std::size_t size) const noexcept
	{
		if (size == 0) {
			return destination != nullptr || address != 0;
		}
		if (destination == nullptr || !IsReadable(address, size)) {
			return false;
		}

		// Keep SEH limited to this trivially-copyable probe.  The hook itself has
		// no C++ resource-owning frame that could be left half-unwound.
#		if defined(_MSC_VER)
		__try {
			std::memcpy(destination, reinterpret_cast<const void*>(address), size);
			return true;
		}
		__except (ReadFaultFilter(::GetExceptionCode())) {
			return false;
		}
#		else
		std::memcpy(destination, reinterpret_cast<const void*>(address), size);
		return true;
#		endif
	}
#endif
}
