#include "Hooks/SaveLoadGuard.h"

#include "Diagnostics/IncidentCoalescer.h"
#include "Diagnostics/SaveLoadIncidentQueue.h"
#include "Platform/Logging.h"
#include "Runtime/SaveLoadGuardScan.h"
#include "Validation/MemoryReader.h"
#include "Validation/SaveLoadGuardValidation.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <F4SE/F4SE.h>
#if !defined(FALLOUTVR)
#include <REL/ASM.h>
#include <REL/Trampoline.h>
#include <REL/Utility.h>
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <memory>

namespace saveloadguard::hooks
{
	static_assert(sizeof(void*) == 8, "SaveLoadGuardF4 requires a Windows x64 process");

	namespace
	{
		constexpr std::uint32_t kHookId = 0x534C4744;  // "SLGD"
		constexpr char kInvalidEditorId[] = "<invalid-form>";

		using EditorIdFunction = const char* (*)(const void*);

		std::atomic<bool> g_active{ false };
		std::atomic<actor3dguard::GuardMode> g_mode{ actor3dguard::GuardMode::Off };
		std::atomic<bool> g_preventionQualified{ false };
		std::atomic<bool> g_allowUnqualifiedPrevention{ true };
		std::atomic<std::uintptr_t> g_callsite{ 0 };
		std::atomic<std::uintptr_t> g_gameTextAddress{ 0 };
		std::atomic<std::size_t> g_gameTextSize{ 0 };
		std::atomic<std::uintptr_t> g_gameRdataAddress{ 0 };
		std::atomic<std::size_t> g_gameRdataSize{ 0 };
		std::atomic<std::uint32_t> g_runtimeCode{ 0 };
		std::atomic<std::uint64_t> g_sessionId{ 0 };
		std::atomic<std::uint64_t> g_nextIncident{ 1 };
		std::atomic<std::uint64_t> g_prevented{ 0 };
		std::atomic<std::uint64_t> g_observed{ 0 };
		std::atomic<std::uint64_t> g_unlogged{ 0 };
		std::atomic<bool> g_workingPending{ false };
		std::atomic<bool> g_workingLogged{ false };
		std::atomic<std::uint64_t> g_lastDropped{ 0 };
		std::atomic_flag g_draining = ATOMIC_FLAG_INIT;

		const actor3dguard::validation::ProcessMemoryReader g_processReader;

		struct FaultResult
		{
			std::uint32_t exceptionCode{ 0 };
			std::uint64_t faultAddress{ 0 };
		};

#if defined(_MSC_VER)
		thread_local FaultResult* g_pendingFault = nullptr;

		[[nodiscard]] int FaultFilter(
			const unsigned long exceptionCode,
			EXCEPTION_POINTERS* pointers) noexcept
		{
			if (exceptionCode != EXCEPTION_ACCESS_VIOLATION && exceptionCode != EXCEPTION_IN_PAGE_ERROR) {
				return EXCEPTION_CONTINUE_SEARCH;
			}
			if (g_pendingFault != nullptr) {
				g_pendingFault->exceptionCode = static_cast<std::uint32_t>(exceptionCode);
				if (pointers != nullptr && pointers->ExceptionRecord != nullptr &&
					pointers->ExceptionRecord->NumberParameters >= 2) {
					g_pendingFault->faultAddress = pointers->ExceptionRecord->ExceptionInformation[1];
				}
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}
#endif

		[[nodiscard]] std::uint64_t SessionId() noexcept
		{
			std::uint64_t current = g_sessionId.load(std::memory_order_acquire);
			if (current != 0) {
				return current;
			}
			const auto candidate = (static_cast<std::uint64_t>(::GetCurrentProcessId()) << 32) ^
				static_cast<std::uint64_t>(::GetTickCount64());
			(void)g_sessionId.compare_exchange_strong(current, candidate, std::memory_order_acq_rel);
			return g_sessionId.load(std::memory_order_acquire);
		}

		[[nodiscard]] diagnostics::IncidentAction ActionForCurrentMode() noexcept
		{
			const auto mode = g_mode.load(std::memory_order_acquire);
			const auto qualified = g_preventionQualified.load(std::memory_order_acquire);
			const auto allowUnqualified = g_allowUnqualifiedPrevention.load(std::memory_order_acquire);
			return mode == actor3dguard::GuardMode::Prevent && (qualified || allowUnqualified) ?
				diagnostics::IncidentAction::Prevented : diagnostics::IncidentAction::Observed;
		}

		[[nodiscard]] bool PreventionEnabled() noexcept
		{
			return g_mode.load(std::memory_order_acquire) == actor3dguard::GuardMode::Prevent &&
				(g_preventionQualified.load(std::memory_order_acquire) ||
					g_allowUnqualifiedPrevention.load(std::memory_order_acquire));
		}

		[[nodiscard]] bool QueueIncident(
			const validation::ValidationReport& report,
			const std::uintptr_t callsite,
			const diagnostics::IncidentReason reason,
			const diagnostics::IncidentAction action,
			const FaultResult* fault = nullptr) noexcept
		{
			diagnostics::Incident incident{};
			incident.sessionId = SessionId();
			incident.incidentId = g_nextIncident.fetch_add(1, std::memory_order_relaxed);
			incident.formAddress = report.form;
			incident.vtableAddress = report.vtable;
			incident.editorIdEntry = report.editorIdEntry;
			incident.callsite = callsite;
			incident.runtimeCode = g_runtimeCode.load(std::memory_order_relaxed);
			incident.threadId = ::GetCurrentThreadId();
			incident.formId = report.formId;
			incident.formType = report.formType;
			incident.reason = reason;
			incident.action = action;
			if (fault != nullptr) {
				incident.faultCode = fault->exceptionCode;
				incident.faultAddress = fault->faultAddress;
			}
			return diagnostics::GetIncidentQueue().TryPush(incident);
		}

		[[nodiscard]] diagnostics::IncidentReason MapReason(const validation::ValidationReason reason) noexcept
		{
			switch (reason) {
			case validation::ValidationReason::NullForm: return diagnostics::IncidentReason::NullForm;
			case validation::ValidationReason::UnreadableForm: return diagnostics::IncidentReason::UnreadableForm;
			case validation::ValidationReason::UnreadableVtable: return diagnostics::IncidentReason::UnreadableVtable;
			case validation::ValidationReason::NonExecutableEditorId: return diagnostics::IncidentReason::NonExecutableEditorId;
			case validation::ValidationReason::EditorIdOutsideGameText: return diagnostics::IncidentReason::EditorIdOutsideGameText;
			case validation::ValidationReason::InvalidEditorIdResult: return diagnostics::IncidentReason::InvalidEditorIdResult;
			default: return diagnostics::IncidentReason::UnreadableVtable;
			}
		}

		void LogIncident(const diagnostics::Incident& incident, const std::uint64_t repeatCount) noexcept
		{
			if (incident.reason == diagnostics::IncidentReason::FaultDuringEditorId) {
				ACTOR3D_WARN(
					"SaveLoadGuardF4: prevented fault in TESForm::GetFormEditorID; form={}; form_id=0x{:08X}; vtable={}; entry={}; callsite={}; fault_code=0x{:08X}; fault_address={}; incident={}",
					incident.formAddress,
					incident.formId,
					incident.vtableAddress,
					incident.editorIdEntry,
					incident.callsite,
					incident.faultCode,
					incident.faultAddress,
					incident.incidentId);
			} else if (incident.action == diagnostics::IncidentAction::Prevented) {
				ACTOR3D_WARN(
					"SaveLoadGuardF4: prevented invalid TESForm editor-ID dispatch; form={}; form_id=0x{:08X}; vtable={}; entry={}; reason={}; callsite={}; incident={}",
					incident.formAddress,
					incident.formId,
					incident.vtableAddress,
					incident.editorIdEntry,
					diagnostics::ToString(incident.reason),
					incident.callsite,
					incident.incidentId);
			} else {
				ACTOR3D_WARN(
					"SaveLoadGuardF4: observed invalid TESForm editor-ID dispatch; form={}; form_id=0x{:08X}; reason={}; callsite={}; incident={}",
					incident.formAddress,
					incident.formId,
					diagnostics::ToString(incident.reason),
					incident.callsite,
					incident.incidentId);
			}
			if (repeatCount > 1) {
				ACTOR3D_WARN(
					"SaveLoadGuardF4: repeated save-load editor-ID hazard; reason={}; repeated={}; prevented_total={}; observed_total={}",
					diagnostics::ToString(incident.reason),
					repeatCount - 1,
					g_prevented.load(std::memory_order_relaxed),
					g_observed.load(std::memory_order_relaxed));
			}
		}

		void DrainLogsImpl(const bool waitForTurn) noexcept
		{
			while (g_draining.test_and_set(std::memory_order_acquire)) {
				if (!waitForTurn) {
					return;
				}
				::SwitchToThread();
			}
			struct ReleaseDrain
			{
				~ReleaseDrain() { g_draining.clear(std::memory_order_release); }
			} release;

			if (g_workingPending.exchange(false, std::memory_order_acq_rel) &&
				!g_workingLogged.exchange(true, std::memory_order_acq_rel)) {
				ACTOR3D_INFO("SaveLoadGuardF4: working; first guarded save-load editor-ID dispatch observed.");
			}

			auto& queue = diagnostics::GetIncidentQueue();
			actor3dguard::diagnostics::TypedIncidentCoalescer<diagnostics::Incident> coalescer;
			diagnostics::Incident incident{};
			for (std::size_t processed = 0; processed < diagnostics::IncidentQueueCapacity && queue.TryPop(incident); ++processed) {
				(void)coalescer.TryAdd(incident);
			}

			actor3dguard::diagnostics::TypedIncidentAggregate<diagnostics::Incident> aggregate{};
			while (coalescer.TryPop(aggregate)) {
				LogIncident(aggregate.exemplar, aggregate.count);
			}
			if (coalescer.Untracked() != 0) {
				ACTOR3D_WARN(
					"SaveLoadGuardF4: incident coalescer saturated; aggregate_records_untracked={}; prevented_total={}; observed_total={}",
					coalescer.Untracked(),
					g_prevented.load(std::memory_order_relaxed),
					g_observed.load(std::memory_order_relaxed));
			}

			const auto unlogged = g_unlogged.exchange(0, std::memory_order_acq_rel);
			if (unlogged != 0) {
				ACTOR3D_WARN(
					"SaveLoadGuardF4: save-load editor-ID hazard encountered; bounded queue was full; unlogged_incidents={}; prevented_total={}; observed_total={}",
					unlogged,
					g_prevented.load(std::memory_order_relaxed),
					g_observed.load(std::memory_order_relaxed));
			}

			const auto dropped = queue.Dropped();
			const auto previousDropped = g_lastDropped.exchange(dropped, std::memory_order_acq_rel);
			if (dropped > previousDropped) {
				ACTOR3D_WARN(
					"SaveLoadGuardF4: incident queue saturated; incident_records_dropped={}; prevented_total={}; observed_total={}",
					dropped,
					g_prevented.load(std::memory_order_relaxed),
					g_observed.load(std::memory_order_relaxed));
			}
		}

#if !defined(FALLOUTVR)
		[[nodiscard]] bool DecodeIndirectCallTarget(
			const actor3dguard::validation::IMemoryReader& reader,
			const std::uintptr_t callsite,
			std::uintptr_t& target) noexcept
		{
			if (callsite > std::numeric_limits<std::uintptr_t>::max() - runtime::CallInstructionSize) {
				return false;
			}
			std::array<std::uint8_t, runtime::CallInstructionSize> bytes{};
			if (!reader.Read(callsite, bytes.data(), bytes.size()) || bytes[0] != 0xFF || bytes[1] != 0x15) {
				return false;
			}
			std::int32_t displacement{};
			std::memcpy(std::addressof(displacement), bytes.data() + 2, sizeof(displacement));
			const auto pointerAddress = static_cast<std::uintptr_t>(static_cast<std::int64_t>(callsite + runtime::CallInstructionSize) + displacement);
			return reader.Read(pointerAddress, std::addressof(target), sizeof(target));
		}

		[[nodiscard]] bool FitsRelative32(
			const std::uintptr_t source,
			const std::uintptr_t target) noexcept
		{
			if (source > std::numeric_limits<std::uintptr_t>::max() - runtime::CallInstructionSize) {
				return false;
			}
			const auto next = source + runtime::CallInstructionSize;
			if (target >= next) {
				return target - next <= static_cast<std::uintptr_t>((std::numeric_limits<std::int32_t>::max)());
			}
			return next - target <= static_cast<std::uintptr_t>(static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()) + 1u);
		}
#endif
	}

	InstallReport Install(
		const actor3dguard::Configuration& configuration,
		const std::uint32_t runtimeCode) noexcept
	{
		InstallReport report{};
		const auto effectiveMode = configuration.enabled ? configuration.mode : actor3dguard::GuardMode::Off;
		g_mode.store(effectiveMode, std::memory_order_release);
		g_allowUnqualifiedPrevention.store(configuration.allowUnqualifiedSuppression, std::memory_order_release);
		g_preventionQualified.store(false, std::memory_order_release);
		g_runtimeCode.store(runtimeCode, std::memory_order_release);

		if (g_active.load(std::memory_order_acquire)) {
			report.status = InstallStatus::Installed;
			report.callsite = g_callsite.load(std::memory_order_acquire);
			report.preventionQualified = g_preventionQualified.load(std::memory_order_acquire);
			report.preventionEnabled = PreventionEnabled();
			return report;
		}
		if (effectiveMode == actor3dguard::GuardMode::Off) {
			report.status = InstallStatus::Disabled;
			return report;
		}

#if defined(FALLOUTVR)
		// F4SEVR has a distinct image and vtable contract.  Keep this separate
		// artifact fail-closed until its LoadGame bytes are mapped independently.
		report.status = InstallStatus::SignatureMismatch;
		return report;
#else
		const auto module = REX::FModule::GetExecutingModule();
		const auto text = module.GetSection(".text");
		const auto rdata = module.GetSection(".rdata");
		const auto textAddress = text.GetAddress();
		const auto textSize = text.GetSize();
		const auto rdataAddress = rdata.GetAddress();
		const auto rdataSize = rdata.GetSize();
		if (textAddress == 0 || textSize == 0 || rdataAddress == 0 || rdataSize == 0) {
			report.status = InstallStatus::AddressUnavailable;
			return report;
		}

		actor3dguard::validation::ProcessMemoryReader reader;
		const auto scan = runtime::DiscoverSaveLoadGuard(reader, textAddress, textSize);
		report.callsite = scan.callsite;
		report.functionAddress = scan.functionAddress;
		report.exactCandidates = scan.exactCandidates;
		report.relaxedCandidates = scan.relaxedCandidates;
		report.exactContract = scan.exact;
		if (!scan.Qualified() || !runtime::MatchesEditorIdCallsite(reader, scan.callsite, !scan.exact)) {
			ACTOR3D_WARN(
				"SaveLoadGuardF4: LoadGame editor-ID callsite not found; exact_candidates={}; relaxed_candidates={}; no hook installed.",
				scan.exactCandidates,
				scan.relaxedCandidates);
			report.status = InstallStatus::SignatureMismatch;
			return report;
		}

		std::array<std::uint8_t, runtime::CallInstructionSize> originalBytes{};
		if (!reader.Read(scan.callsite, originalBytes.data(), originalBytes.size()) ||
			originalBytes[0] != 0xFF || originalBytes[1] != 0x90 ||
			originalBytes[2] != 0xD0 || originalBytes[3] != 0x01 ||
			originalBytes[4] != 0x00 || originalBytes[5] != 0x00) {
			report.status = InstallStatus::HookConflict;
			return report;
		}

		auto& trampoline = REL::GetTrampoline();
		if (trampoline.empty() || trampoline.free_size() < sizeof(std::uintptr_t)) {
			report.status = InstallStatus::TrampolineUnavailable;
			return report;
		}

		const auto thunkAddress = reinterpret_cast<std::uintptr_t>(&GuardedGetFormEditorID);
		std::uintptr_t branchPointer = 0;
		try {
				branchPointer = trampoline.allocate_branch6(thunkAddress);
				if (branchPointer == 0 || !FitsRelative32(scan.callsite, branchPointer)) {
					report.status = InstallStatus::TrampolineUnavailable;
					return report;
				}
				REL::ASM::CALL6 patch(scan.callsite, branchPointer);
				const auto patchWritten = REL::WriteSafeData(scan.callsite, patch);
				const auto patchFlushed = patchWritten &&
					::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<const void*>(scan.callsite), runtime::CallInstructionSize) != FALSE;
				if (!patchFlushed) {
					// A failed cache flush or write must never leave a partially
					// installed callsite behind. Restore the exact bytes observed
					// before the transaction and keep the hook inactive.
					if (patchWritten) {
						(void)REL::WriteSafeData(scan.callsite, originalBytes);
						(void)::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<const void*>(scan.callsite), runtime::CallInstructionSize);
					}
					report.status = InstallStatus::HookConflict;
					return report;
				}

			std::uintptr_t installedTarget = 0;
			if (!DecodeIndirectCallTarget(reader, scan.callsite, installedTarget) || installedTarget != thunkAddress) {
				std::array<std::uint8_t, runtime::CallInstructionSize> current{};
				if (reader.Read(scan.callsite, current.data(), current.size()) && current[0] == 0xFF && current[1] == 0x15) {
					(void)REL::WriteSafeData(scan.callsite, originalBytes);
					(void)::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<const void*>(scan.callsite), runtime::CallInstructionSize);
				}
				report.status = InstallStatus::HookConflict;
				return report;
			}
		} catch (...) {
			// The trampoline pool is process-lifetime; a failed allocation is safe
			// to leave unused.  Never publish the hook as active on partial writes.
			report.status = InstallStatus::TrampolineUnavailable;
			return report;
		}

		g_callsite.store(scan.callsite, std::memory_order_release);
		g_gameTextAddress.store(textAddress, std::memory_order_release);
		g_gameTextSize.store(textSize, std::memory_order_release);
		g_gameRdataAddress.store(rdataAddress, std::memory_order_release);
		g_gameRdataSize.store(rdataSize, std::memory_order_release);
		g_preventionQualified.store(scan.exact, std::memory_order_release);
		g_active.store(true, std::memory_order_release);
		report.status = InstallStatus::Installed;
		report.preventionQualified = scan.exact;
		report.preventionEnabled = PreventionEnabled();
		return report;
#endif
	}

	__declspec(noinline) const char* GuardedGetFormEditorID(const void* const form) noexcept
	{
		if (!g_active.load(std::memory_order_acquire)) {
			// A partially installed transaction cannot safely reconstruct the
			// original FF 90 call.  Returning a stable string keeps the warning path
			// alive; installation completes before normal game jobs are scheduled.
			return kInvalidEditorId;
		}
		g_workingPending.store(true, std::memory_order_release);

		const auto callsiteReturn = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
		const auto configuredCallsite = g_callsite.load(std::memory_order_acquire);
		const auto callsite = configuredCallsite != 0 ? configuredCallsite :
			(callsiteReturn >= runtime::CallInstructionSize ? callsiteReturn - runtime::CallInstructionSize : callsiteReturn);
		const auto report = validation::ValidateEditorIdTarget(
			g_processReader,
			reinterpret_cast<std::uintptr_t>(form),
			g_gameTextAddress.load(std::memory_order_acquire),
			g_gameTextSize.load(std::memory_order_acquire),
			g_gameRdataAddress.load(std::memory_order_acquire),
			g_gameRdataSize.load(std::memory_order_acquire));
		if (!report.Callable()) {
			const auto action = ActionForCurrentMode();
			if (action == diagnostics::IncidentAction::Prevented) {
				g_prevented.fetch_add(1, std::memory_order_relaxed);
			} else {
				g_observed.fetch_add(1, std::memory_order_relaxed);
			}
			if (!QueueIncident(report, callsite, MapReason(report.reason), action)) {
				g_unlogged.fetch_add(1, std::memory_order_relaxed);
			}
			DrainLogsImmediately();
			return kInvalidEditorId;
		}

		const auto function = reinterpret_cast<EditorIdFunction>(report.editorIdEntry);
		const bool protectCall = PreventionEnabled();
		const char* result = nullptr;
		if (protectCall) {
			FaultResult fault{};
#if defined(_MSC_VER)
			auto* previousFault = g_pendingFault;
			g_pendingFault = &fault;
			__try {
				result = function(form);
			} __except (FaultFilter(::GetExceptionCode(), GetExceptionInformation())) {
				g_pendingFault = previousFault;
				const auto action = diagnostics::IncidentAction::Prevented;
				g_prevented.fetch_add(1, std::memory_order_relaxed);
				if (!QueueIncident(report, callsite, diagnostics::IncidentReason::FaultDuringEditorId, action, &fault)) {
					g_unlogged.fetch_add(1, std::memory_order_relaxed);
				}
				DrainLogsImmediately();
				return kInvalidEditorId;
			}
			g_pendingFault = previousFault;
#else
			result = function(form);
#endif
		} else {
			result = function(form);
		}

		if (!validation::IsEditorIdResultReadable(g_processReader, result)) {
			validation::ValidationReport invalidResult = report;
			invalidResult.reason = validation::ValidationReason::InvalidEditorIdResult;
			const auto action = protectCall ? diagnostics::IncidentAction::Prevented : diagnostics::IncidentAction::Observed;
			if (action == diagnostics::IncidentAction::Prevented) {
				g_prevented.fetch_add(1, std::memory_order_relaxed);
			} else {
				g_observed.fetch_add(1, std::memory_order_relaxed);
			}
			if (!QueueIncident(invalidResult, callsite, diagnostics::IncidentReason::InvalidEditorIdResult, action)) {
				g_unlogged.fetch_add(1, std::memory_order_relaxed);
			}
			DrainLogsImmediately();
			return kInvalidEditorId;
		}
		return result;
	}

	void DrainLogs() noexcept
	{
		DrainLogsImpl(false);
	}

	void DrainLogsImmediately() noexcept
	{
		DrainLogsImpl(true);
	}

	bool IsActive() noexcept
	{
		return g_active.load(std::memory_order_acquire);
	}

	std::uint64_t PreventedTotal() noexcept
	{
		return g_prevented.load(std::memory_order_relaxed);
	}

	std::uint64_t ObservedTotal() noexcept
	{
		return g_observed.load(std::memory_order_relaxed);
	}

	const char* ToString(const InstallStatus status) noexcept
	{
		switch (status) {
		case InstallStatus::Installed: return "installed";
		case InstallStatus::Disabled: return "disabled";
		case InstallStatus::AddressUnavailable: return "address_unavailable";
		case InstallStatus::SignatureMismatch: return "signature_mismatch";
		case InstallStatus::HookConflict: return "hook_conflict";
		case InstallStatus::TrampolineUnavailable: return "trampoline_unavailable";
		default: return "unknown";
		}
	}
}
