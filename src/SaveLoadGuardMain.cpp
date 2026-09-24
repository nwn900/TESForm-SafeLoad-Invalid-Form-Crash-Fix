#include "Configuration.h"
#include "Hooks/SaveLoadGuard.h"
#include "Platform/Logging.h"
#include "Platform/PluginExports.h"
#include "Runtime/RuntimeAdapter.h"

#include <F4SE/F4SE.h>

#include <atomic>
#include <string_view>

namespace
{
	constexpr std::string_view kPluginName{ "SaveLoadGuardF4" };
	constexpr std::uint32_t kPluginVersion{ 1 };

	actor3dguard::Configuration g_configuration{};
	REL::Version g_runtime{};
	REL::Version g_f4seVersion{};
	std::atomic<bool> g_postLoadAttempted{ false };

	void ReportInstall(const saveloadguard::hooks::InstallReport& report)
	{
		if (report.status == saveloadguard::hooks::InstallStatus::Installed) {
			ACTOR3D_INFO(
				"SaveLoadGuardF4: active; runtime={}; f4se={}; contract={}; callsite=0x{:X}; exact_candidates={}; relaxed_candidates={}",
				g_runtime.string(),
				g_f4seVersion.string(),
				report.exactContract ? "exact-scan" : "relaxed-scan",
				report.callsite,
				report.exactCandidates,
				report.relaxedCandidates);
			if (report.preventionEnabled) {
				if (report.preventionQualified) {
					ACTOR3D_INFO("SaveLoadGuardF4: prevent mode enabled; byte contract qualified; live gameplay qualification pending.");
				} else {
					ACTOR3D_WARN("SaveLoadGuardF4: prevent mode enabled; relaxed contract accepted; live gameplay qualification pending.");
				}
			} else {
				ACTOR3D_INFO("SaveLoadGuardF4: observe mode; invalid dispatches are recorded and replaced with a placeholder.");
			}
			if (actor3dguard::platform::StartPeriodicLogging([](void*) noexcept { saveloadguard::hooks::DrainLogs(); })) {
				ACTOR3D_INFO("SaveLoadGuardF4: incident logger running; flush interval=250ms.");
			} else {
				ACTOR3D_WARN("SaveLoadGuardF4: periodic logger unavailable; incidents flush on F4SE messages.");
			}
			return;
		}

		if (report.status == saveloadguard::hooks::InstallStatus::Disabled) {
			ACTOR3D_INFO("SaveLoadGuardF4: inactive; mode=off; no hooks installed.");
			return;
		}
		ACTOR3D_WARN(
			"SaveLoadGuardF4: inactive; runtime={}; f4se={}; reason={}; exact_candidates={}; relaxed_candidates={}; no hooks installed.",
			g_runtime.string(),
			g_f4seVersion.string(),
			saveloadguard::hooks::ToString(report.status),
			report.exactCandidates,
			report.relaxedCandidates);
	}

	void F4SEAPI MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		if (message == nullptr) {
			return;
		}
		switch (message->type) {
		case F4SE::MessagingInterface::kPostLoad:
			if (!g_postLoadAttempted.exchange(true, std::memory_order_acq_rel)) {
				ReportInstall(saveloadguard::hooks::Install(g_configuration, g_runtime.pack()));
			}
			break;
		case F4SE::MessagingInterface::kPostPostLoad:
		case F4SE::MessagingInterface::kPreLoadGame:
		case F4SE::MessagingInterface::kGameDataReady:
		case F4SE::MessagingInterface::kInputLoaded:
		case F4SE::MessagingInterface::kNewGame:
		case F4SE::MessagingInterface::kGameLoaded:
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kPreSaveGame:
		case F4SE::MessagingInterface::kPostSaveGame:
		case F4SE::MessagingInterface::kDeleteGame:
			saveloadguard::hooks::DrainLogs();
			break;
		default:
			break;
		}
	}
}

GUARD_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	if (a_f4se == nullptr || a_info == nullptr || a_f4se->IsEditor()) {
		return false;
	}
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = kPluginName.data();
	a_info->version = kPluginVersion;
	return true;
}

GUARD_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (a_f4se == nullptr) {
		return false;
	}
	const auto loaderVersion = a_f4se->F4SEVersion();
	if (!actor3dguard::runtime::IsSupportedF4SEABI(loaderVersion)) {
		return false;
	}

#if defined(FALLOUTVR)
	F4SE::Init(a_f4se);
	F4SE::AllocTrampoline(0x1000);
#else
	F4SE::InitInfo initInfo{};
	initInfo.log = false;
	initInfo.trampoline = true;
	initInfo.trampolineSize = 0x1000;
	F4SE::Init(a_f4se, initInfo);
#endif
	(void)actor3dguard::platform::InitializeLogging(
#if defined(FALLOUTVR)
		"Fallout4VR"
#else
		F4SE::GetSaveFolderName()
#endif
		, kPluginName);

	g_configuration = actor3dguard::LoadConfiguration("SaveLoadGuardF4.ini");
	g_runtime = a_f4se->RuntimeVersion();
	g_f4seVersion = loaderVersion;
	ACTOR3D_INFO(
		"SaveLoadGuardF4: loading; version=0.1.0; runtime={}; f4se={}; mode={}",
		g_runtime.string(),
		g_f4seVersion.string(),
		actor3dguard::ToString(g_configuration.mode));
	if (g_configuration.invalidEntries != 0) {
		ACTOR3D_WARN("SaveLoadGuardF4: ignored invalid configuration entries={}", g_configuration.invalidEntries);
	}

	const auto* messaging = F4SE::GetMessagingInterface();
	if (messaging == nullptr || messaging->Version() < F4SE::MessagingInterface::kVersion) {
		ACTOR3D_ERROR("SaveLoadGuardF4: F4SE messaging interface is unavailable or too old; no hooks installed.");
		return false;
	}
	if (!messaging->RegisterListener(MessageHandler)) {
		ACTOR3D_ERROR("SaveLoadGuardF4: failed to register F4SE message listener; no hooks installed.");
		return false;
	}
	return true;
}
