#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>

namespace actor3dguard::validation
{
	/**
	 * Access class a probe asks about.
	 *
	 * Read is the validator's historical question.  Write and Execute exist
	 * because an engine operation that stores through a pointer and then calls
	 * through a vtable needs both answers before a guard may let it run: a page
	 * can be fully readable and still fault on the store (read-only protection)
	 * or on the call (no execute protection).
	 */
	enum class MemoryAccess : std::uint8_t
	{
		Read,
		Write,
		Execute
	};

	/**
	 * Read-only memory access used by the validator.
	 *
	 * The production implementation only reports committed, readable pages and
	 * performs the copy inside a tiny SEH probe.  The synthetic implementation
	 * lets the same validation code run against bounded test buffers.
	 */
	class IMemoryReader
	{
	public:
		virtual ~IMemoryReader() = default;

		[[nodiscard]] virtual bool IsAccessible(std::uintptr_t address, std::size_t size, MemoryAccess access) const noexcept = 0;

		[[nodiscard]] bool IsReadable(std::uintptr_t address, std::size_t size) const noexcept
		{
			return IsAccessible(address, size, MemoryAccess::Read);
		}

		[[nodiscard]] virtual bool Read(std::uintptr_t address, void* destination, std::size_t size) const noexcept = 0;

		template <class T>
		[[nodiscard]] std::optional<T> Read(std::uintptr_t address) const noexcept
			requires(std::is_trivially_copyable_v<T>)
		{
			T value{};
			if (!Read(address, std::addressof(value), sizeof(T))) {
				return std::nullopt;
			}
			return value;
		}
	};

	class BufferMemoryReader final : public IMemoryReader
	{
	public:
		BufferMemoryReader(const void* data, std::size_t size) noexcept :
			_begin(reinterpret_cast<std::uintptr_t>(data)),
			_size(size)
		{}

		/**
		 * Test double for a buffer that also contains executable bytes.
		 *
		 * Real heap storage is never executable, so a synthetic reader needs an
		 * explicit window to exercise the branch that validates a virtual
		 * destructor entry before it is called.
		 */
		BufferMemoryReader(
			const void* data,
			std::size_t size,
			const void* executableData,
			std::size_t executableSize) noexcept :
			_begin(reinterpret_cast<std::uintptr_t>(data)),
			_size(size),
			_executableBegin(reinterpret_cast<std::uintptr_t>(executableData)),
			_executableSize(executableSize)
		{}

		[[nodiscard]] bool IsAccessible(std::uintptr_t address, std::size_t size, MemoryAccess access) const noexcept override;
		[[nodiscard]] bool Read(std::uintptr_t address, void* destination, std::size_t size) const noexcept override;

	private:
		std::uintptr_t _begin{ 0 };
		std::size_t    _size{ 0 };
		std::uintptr_t _executableBegin{ 0 };
		std::size_t    _executableSize{ 0 };
	};

#if defined(_WIN32)
	class ProcessMemoryReader final : public IMemoryReader
	{
	public:
		[[nodiscard]] bool IsAccessible(std::uintptr_t address, std::size_t size, MemoryAccess access) const noexcept override;
		[[nodiscard]] bool Read(std::uintptr_t address, void* destination, std::size_t size) const noexcept override;
	};
#endif
}
