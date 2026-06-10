#pragma once

#include "Core/StageLogic.h"

namespace UI
{
	class QuestTrackerUI
	{
	public:
		static QuestTrackerUI& Get();

		// --- visibility (any thread) ------------------------------------
		[[nodiscard]] bool IsVisible() const;
		void               SetVisible(bool a_visible);
		void               Toggle();

		// Toggling is only sensible once game data is loaded and we are
		// not sitting in the main menu / a loading screen.
		[[nodiscard]] bool MayToggle() const;

		void OnDataLoaded();

		// --- render thread ------------------------------------------------
		void Draw();

		// --- main-thread work, posted via the SKSE task interface ---------
		void RequestRefresh();
		void RequestSetStage(std::uint32_t a_formID, std::uint16_t a_stage, bool a_force);

	private:
		QuestTrackerUI() = default;

		void DrawQuestTable();
		void DrawStageEditor();
		void SetStatus(std::string a_status);

		// Runs on the game's main thread.
		static void DoRefresh();
		static void DoSetStage(std::uint32_t a_formID, std::uint16_t a_stage, bool a_force);

		std::atomic<bool> visible_{ false };
		std::atomic<bool> dataLoaded_{ false };
		std::atomic<bool> refreshPending_{ false };

		mutable std::mutex                          lock_;
		std::vector<QuestTracker::Logic::QuestRow>  rows_;
		std::string                                 status_;

		// Read by the main-thread refresh task as well as the UI.
		std::atomic<bool> onlyRunning_{ true };

		// UI state (render thread only)
		char                                  filter_[128] = {};
		char                                  stageInput_[8] = {};
		bool                                  force_ = false;
		std::uint32_t                         selectedFormID_ = 0;
		std::chrono::steady_clock::time_point lastRefresh_{};
	};
}
