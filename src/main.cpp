#include "Hooks/Hooks.h"
#include "Plugin.h"
#include "Settings.h"
#include "UI/QuestTrackerUI.h"

#include <spdlog/sinks/basic_file_sink.h>

SKSEPluginInfo(
	.Version = { Plugin::VERSION_MAJOR, Plugin::VERSION_MINOR, Plugin::VERSION_PATCH, 0 },
	.Name = Plugin::NAME,
	.Author = Plugin::AUTHOR,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

namespace
{
	void InitializeLogging()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= std::format("{}.log", Plugin::NAME);

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			UI::QuestTrackerUI::Get().OnDataLoaded();
			logger::info("Data loaded - quest tracker armed (toggle key {:#x})",
				Settings::Get().toggleKey);
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	InitializeLogging();

	logger::info("{} v{}.{}.{} loading (runtime {})",
		Plugin::NAME, Plugin::VERSION_MAJOR, Plugin::VERSION_MINOR, Plugin::VERSION_PATCH,
		a_skse->RuntimeVersion().string());

	Settings::Load();

	SKSE::AllocTrampoline(64);
	Hooks::Install();

	if (const auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(MessageHandler);
	}

	logger::info("{} loaded", Plugin::NAME);
	return true;
}
