#pragma once

#include <atomic>
#include <cstdint>

namespace actor3dguard::diagnostics
{
	// Windows timer consumer; no F4SE task-table or engine calls. Start/Stop are
	// owner-thread operations and must never run from DllMain or the callback.
	class PeriodicDrain final
	{
	public:
		using Callback = void (*)(void*) noexcept;
		PeriodicDrain() noexcept = default;
		~PeriodicDrain() noexcept { Stop(); }
		PeriodicDrain(const PeriodicDrain&) = delete;
		PeriodicDrain& operator=(const PeriodicDrain&) = delete;
		[[nodiscard]] bool Start(Callback callback, void* context, std::uint32_t intervalMs = 250) noexcept;
		void Stop() noexcept;
		void Tick() noexcept;
	private:
		void* _timer{ nullptr };
		Callback _callback{ nullptr };
		void* _context{ nullptr };
		std::atomic_flag _running = ATOMIC_FLAG_INIT;
	};
}
