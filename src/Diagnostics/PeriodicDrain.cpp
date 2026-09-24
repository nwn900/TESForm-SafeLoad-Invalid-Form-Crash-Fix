#include "Diagnostics/PeriodicDrain.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace actor3dguard::diagnostics
{
	bool PeriodicDrain::Start(Callback callback, void* context, std::uint32_t intervalMs) noexcept
	{
		if (_timer || !callback || intervalMs == 0) {
			return false;
		}
		_callback = callback;
		_context = context;
		_timer = ::CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, void* self, PTP_TIMER) {
			static_cast<PeriodicDrain*>(self)->Tick();
		}, this, nullptr);
		if (!_timer) {
			_callback = nullptr;
			_context = nullptr;
			return false;
		}
		ULARGE_INTEGER due{};
		due.QuadPart = static_cast<ULONGLONG>(-static_cast<LONGLONG>(intervalMs) * 10000);
		FILETIME time{ due.LowPart, due.HighPart };
		::SetThreadpoolTimer(static_cast<PTP_TIMER>(_timer), &time, intervalMs, 0);
		return true;
	}

	void PeriodicDrain::Tick() noexcept
	{
		if (_running.test_and_set(std::memory_order_acquire)) {
			return;
		}
		_callback(_context);
		_running.clear(std::memory_order_release);
	}

	void PeriodicDrain::Stop() noexcept
	{
		if (!_timer) {
			return;
		}
		auto* timer = static_cast<PTP_TIMER>(_timer);
		::SetThreadpoolTimer(timer, nullptr, 0, 0);
		::WaitForThreadpoolTimerCallbacks(timer, TRUE);
		::CloseThreadpoolTimer(timer);
		_timer = nullptr;
		_callback = nullptr;
		_context = nullptr;
	}
}
