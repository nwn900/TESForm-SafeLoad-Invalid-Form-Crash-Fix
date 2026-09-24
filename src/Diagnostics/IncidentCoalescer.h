#pragma once

#include "Diagnostics/IncidentQueue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace actor3dguard::diagnostics
{
	template <class T>
	struct TypedIncidentAggregate
	{
		T exemplar{};
		std::uint64_t count{ 0 };
	};

	/**
	 * Consumer-side coalescing for incident storms.
	 *
	 * Producers still enqueue one fixed-size record per operation; this bounded
	 * table only combines records after they have reached a deferred consumer.
	 * The record type supplies its own `SameSignature` overload, which is found
	 * by argument-dependent lookup, so every guard keeps one collapse rule that
	 * matches its own diagnostic fields.
	 */
	template <class T>
	class TypedIncidentCoalescer final
	{
	public:
		static constexpr std::size_t Capacity = 32;

		TypedIncidentCoalescer() noexcept = default;
		TypedIncidentCoalescer(const TypedIncidentCoalescer&) = delete;
		TypedIncidentCoalescer& operator=(const TypedIncidentCoalescer&) = delete;

		// Returns false when the bounded table is full and this signature could
		// not be represented.  A duplicate of an existing signature always
		// succeeds and increments that aggregate's count.
		[[nodiscard]] bool TryAdd(const T& incident) noexcept
		{
			for (auto& slot : _slots) {
				if (slot.used && SameSignature(slot.exemplar, incident)) {
					SaturatingIncrement(slot.count);
					SaturatingIncrement(_collapsed);
					return true;
				}
			}

			for (auto& slot : _slots) {
				if (!slot.used) {
					slot.exemplar = incident;
					slot.count = 1;
					slot.used = true;
					++_active;
					return true;
				}
			}

			SaturatingIncrement(_untracked);
			return false;
		}

		[[nodiscard]] bool TryPop(TypedIncidentAggregate<T>& aggregate) noexcept
		{
			for (auto& slot : _slots) {
				if (!slot.used) {
					continue;
				}
				aggregate.exemplar = slot.exemplar;
				aggregate.count = slot.count;
				slot = Slot{};
				--_active;
				return true;
			}
			return false;
		}

		void Reset() noexcept
		{
			for (auto& slot : _slots) {
				slot = Slot{};
			}
			_active = 0;
			_collapsed = 0;
			_untracked = 0;
		}

		[[nodiscard]] std::size_t Active() const noexcept { return _active; }
		[[nodiscard]] std::uint64_t Collapsed() const noexcept { return _collapsed; }
		[[nodiscard]] std::uint64_t Untracked() const noexcept { return _untracked; }

	private:
		struct Slot
		{
			T exemplar{};
			std::uint64_t count{ 0 };
			bool used{ false };
		};

		static void SaturatingIncrement(std::uint64_t& value) noexcept
		{
			if (value != (std::numeric_limits<std::uint64_t>::max)()) {
				++value;
			}
		}

		std::array<Slot, Capacity> _slots{};
		std::size_t _active{ 0 };
		std::uint64_t _collapsed{ 0 };
		std::uint64_t _untracked{ 0 };
	};

	using IncidentAggregate = TypedIncidentAggregate<Incident>;
	using IncidentCoalescer = TypedIncidentCoalescer<Incident>;

	// Collapse rule for the actor skin guard's record type.
	[[nodiscard]] bool SameSignature(const Incident& lhs, const Incident& rhs) noexcept;
}
