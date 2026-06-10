#pragma once

namespace Settings
{
	struct Values
	{
		std::uint32_t toggleKey = 0x40;       // DIK_F6
		std::uint32_t autoRefreshSeconds = 2;  // 0 = manual refresh only
	};

	void          Load();
	const Values& Get();
}
