#include "Diagnostics/SaveLoadIncidentQueue.h"

namespace saveloadguard::diagnostics
{
	namespace
	{
		IncidentQueue g_incidentQueue;
	}

	IncidentQueue& GetIncidentQueue() noexcept
	{
		return g_incidentQueue;
	}

	bool SameSignature(const Incident& lhs, const Incident& rhs) noexcept
	{
		return lhs.runtimeCode == rhs.runtimeCode &&
			lhs.callsite == rhs.callsite &&
			lhs.reason == rhs.reason &&
			lhs.action == rhs.action;
	}
}
