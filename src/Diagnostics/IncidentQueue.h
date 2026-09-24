#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace actor3dguard::diagnostics
{
	enum class IncidentReason : std::uint8_t
	{
		InvalidSourceObject,
		UnreadableSourceObject,
		UnreadableStringEntry,
		UnreadableStringMetadata,
		NullShallowLink,
		ShallowCycle,
		ShallowDepthExceeded,
		ExcessiveStringLength,
		StringPayloadOverflow,
		UnreadableStringPayload,
		MissingStringTerminator,
		FaultDuringAssignment
	};

	enum class IncidentAction : std::uint8_t
	{
		Observed,
		SkipStringAssignment,
		PropagateException
	};

	struct Incident
	{
		std::uint64_t sessionId{ 0 };
		std::uint64_t incidentId{ 0 };
		std::uint64_t operationId{ 0 };
		// Raw actor address captured by the outer context hook.  It is diagnostic
		// provenance only; the deferred consumer never dereferences it or treats
		// it as a stable form handle.
		std::uint64_t actorObject{ 0 };
		std::uint64_t sourceObject{ 0 };
		std::uint64_t sourceEntry{ 0 };
		std::uint64_t destinationObject{ 0 };
		std::uint64_t destinationEntry{ 0 };
		std::uint64_t callsite{ 0 };
		std::uint64_t faultAddress{ 0 };
		std::uint64_t faultInstruction{ 0 };
		std::uint32_t runtimeCode{ 0 };
		std::uint32_t hookId{ 0 };
		std::uint32_t threadId{ 0 };
		std::uint32_t stringLength{ 0 };
		std::uint32_t faultCode{ 0 };
		std::uint32_t faultAccessType{ UINT32_MAX };
		std::uint16_t flags{ 0 };
		std::uint8_t  depth{ 0 };
		IncidentReason reason{ IncidentReason::InvalidSourceObject };
		IncidentAction action{ IncidentAction::Observed };
	};

	static_assert(std::is_trivially_copyable_v<Incident>);

	/**
	 * Bounded multi-producer/multi-consumer transport shared by every guard.
	 *
	 * Producers run inside engine callbacks, so the queue is a fixed-size
	 * lock-free ring: pushing never allocates, never blocks, and never performs
	 * I/O.  A full queue drops the record and counts it instead.  Each guard
	 * instantiates its own record type so log fields keep one meaning.
	 */
	template <class T, std::size_t N>
	class TypedIncidentQueue final
	{
	public:
		static constexpr std::size_t Capacity = N;

		TypedIncidentQueue() noexcept
		{
			for (std::size_t index = 0; index < N; ++index) {
				_buffer[index].sequence.store(index, std::memory_order_relaxed);
			}
		}

		TypedIncidentQueue(const TypedIncidentQueue&) = delete;
		TypedIncidentQueue& operator=(const TypedIncidentQueue&) = delete;

		[[nodiscard]] bool TryPush(const T& incident) noexcept
		{
			Cell* cell{};
			auto position = _enqueuePosition.load(std::memory_order_relaxed);
			for (;;) {
				cell = std::addressof(_buffer[position % N]);
				const auto sequence = cell->sequence.load(std::memory_order_acquire);
				const auto difference = static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(position);
				if (difference == 0) {
					if (_enqueuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
						break;
					}
				} else if (difference < 0) {
					_dropped.fetch_add(1, std::memory_order_relaxed);
					return false;
				} else {
					position = _enqueuePosition.load(std::memory_order_relaxed);
				}
			}

			cell->value = incident;
			cell->sequence.store(position + 1, std::memory_order_release);
			_pushed.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] bool TryPop(T& incident) noexcept
		{
			Cell* cell{};
			auto position = _dequeuePosition.load(std::memory_order_relaxed);
			for (;;) {
				cell = std::addressof(_buffer[position % N]);
				const auto sequence = cell->sequence.load(std::memory_order_acquire);
				const auto difference = static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(position + 1);
				if (difference == 0) {
					if (_dequeuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
						break;
					}
				} else if (difference < 0) {
					return false;
				} else {
					position = _dequeuePosition.load(std::memory_order_relaxed);
				}
			}

			incident = cell->value;
			cell->sequence.store(position + N, std::memory_order_release);
			_popped.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] std::uint64_t Dropped() const noexcept { return _dropped.load(std::memory_order_relaxed); }
		[[nodiscard]] std::uint64_t Pushed() const noexcept { return _pushed.load(std::memory_order_relaxed); }
		[[nodiscard]] std::uint64_t Popped() const noexcept { return _popped.load(std::memory_order_relaxed); }

	private:
		struct Cell
		{
			std::atomic<std::size_t> sequence{ 0 };
			T value{};
		};

		std::array<Cell, N> _buffer{};
		std::atomic<std::size_t> _enqueuePosition{ 0 };
		std::atomic<std::size_t> _dequeuePosition{ 0 };
		std::atomic<std::uint64_t> _dropped{ 0 };
		std::atomic<std::uint64_t> _pushed{ 0 };
		std::atomic<std::uint64_t> _popped{ 0 };
	};

	inline constexpr std::size_t IncidentQueueCapacity = 256;

	using IncidentQueue = TypedIncidentQueue<Incident, IncidentQueueCapacity>;

	[[nodiscard]] IncidentQueue& GetIncidentQueue() noexcept;
}
