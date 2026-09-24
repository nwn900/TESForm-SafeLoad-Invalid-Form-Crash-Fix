#pragma once

#include <cstdint>
#include <type_traits>

namespace saveloadguard::diagnostics
{
	enum class IncidentReason : std::uint8_t
	{
		NullForm,
		UnreadableForm,
		UnreadableVtable,
		NonExecutableEditorId,
		EditorIdOutsideGameText,
		InvalidEditorIdResult,
		FaultDuringEditorId
	};

	enum class IncidentAction : std::uint8_t
	{
		Prevented,
		Observed
	};

	struct Incident
	{
		std::uint64_t sessionId{ 0 };
		std::uint64_t incidentId{ 0 };
		std::uint64_t formAddress{ 0 };
		std::uint64_t vtableAddress{ 0 };
		std::uint64_t editorIdEntry{ 0 };
		std::uint64_t callsite{ 0 };
		std::uint64_t faultAddress{ 0 };
		std::uint32_t runtimeCode{ 0 };
		std::uint32_t threadId{ 0 };
		std::uint32_t formId{ 0 };
		std::uint32_t faultCode{ 0 };
		std::uint8_t formType{ 0 };
		IncidentReason reason{ IncidentReason::UnreadableVtable };
		IncidentAction action{ IncidentAction::Prevented };
	};

	static_assert(std::is_trivially_copyable_v<Incident>);
}
