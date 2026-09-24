#pragma once

#include "Diagnostics/IncidentQueue.h"
#include "Diagnostics/SaveLoadIncident.h"

#include <cstddef>

namespace saveloadguard::diagnostics
{
	inline constexpr std::size_t IncidentQueueCapacity = 256;
	using IncidentQueue = actor3dguard::diagnostics::TypedIncidentQueue<Incident, IncidentQueueCapacity>;

	[[nodiscard]] IncidentQueue& GetIncidentQueue() noexcept;

	[[nodiscard]] constexpr const char* ToString(IncidentReason reason) noexcept
	{
		switch (reason) {
		case IncidentReason::NullForm: return "NULL_FORM";
		case IncidentReason::UnreadableForm: return "UNREADABLE_FORM";
		case IncidentReason::UnreadableVtable: return "UNREADABLE_VTABLE";
		case IncidentReason::NonExecutableEditorId: return "NONEXECUTABLE_EDITOR_ID";
		case IncidentReason::EditorIdOutsideGameText: return "EDITOR_ID_OUTSIDE_GAME_TEXT";
		case IncidentReason::InvalidEditorIdResult: return "INVALID_EDITOR_ID_RESULT";
		case IncidentReason::FaultDuringEditorId: return "FAULT_DURING_EDITOR_ID";
		default: return "UNKNOWN";
		}
	}

	[[nodiscard]] constexpr const char* ToString(IncidentAction action) noexcept
	{
		return action == IncidentAction::Prevented ? "prevented" : "observed";
	}

	[[nodiscard]] bool SameSignature(const Incident& lhs, const Incident& rhs) noexcept;
}
