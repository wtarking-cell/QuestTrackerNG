#include "Settings.h"

#include <windows.h>

namespace Settings
{
	namespace
	{
		Values g_values{};

		constexpr const char* INI_PATH = ".\\Data\\SKSE\\Plugins\\QuestTrackerNG.ini";
		constexpr const char* SECTION = "General";
	}

	void Load()
	{
		g_values.toggleKey = static_cast<std::uint32_t>(
			::GetPrivateProfileIntA(SECTION, "ToggleKey", 0x40, INI_PATH));
		g_values.autoRefreshSeconds = static_cast<std::uint32_t>(
			::GetPrivateProfileIntA(SECTION, "AutoRefreshSeconds", 2, INI_PATH));

		if (g_values.toggleKey == 0 || g_values.toggleKey > 0xFF) {
			logger::warn("Invalid ToggleKey {} in ini, falling back to F6", g_values.toggleKey);
			g_values.toggleKey = 0x40;
		}
		if (g_values.autoRefreshSeconds > 3600) {
			logger::warn("AutoRefreshSeconds {} out of range, clamping to 3600", g_values.autoRefreshSeconds);
			g_values.autoRefreshSeconds = 3600;
		}

		logger::info("Settings: ToggleKey={:#x}, AutoRefreshSeconds={}",
			g_values.toggleKey, g_values.autoRefreshSeconds);
	}

	const Values& Get()
	{
		return g_values;
	}
}
