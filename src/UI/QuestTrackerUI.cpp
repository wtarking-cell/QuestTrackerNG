#include "UI/QuestTrackerUI.h"

#include "Input/InputTranslator.h"
#include "Settings.h"

#include <imgui.h>

namespace UI
{
	using namespace QuestTracker::Logic;

	QuestTrackerUI& QuestTrackerUI::Get()
	{
		static QuestTrackerUI instance;
		return instance;
	}

	// ----------------------------------------------------------------------
	// Visibility
	// ----------------------------------------------------------------------
	bool QuestTrackerUI::IsVisible() const
	{
		return visible_.load(std::memory_order_acquire);
	}

	void QuestTrackerUI::SetVisible(bool a_visible)
	{
		const bool was = visible_.exchange(a_visible, std::memory_order_acq_rel);
		if (!was && a_visible) {
			// ImGui state is only touched on the render thread; this just
			// flags the reset for the next overlay frame.
			Input::NotifyOverlayOpened();
			RequestRefresh();
		}
	}

	void QuestTrackerUI::Toggle()
	{
		SetVisible(!IsVisible());
	}

	bool QuestTrackerUI::MayToggle() const
	{
		if (!dataLoaded_.load(std::memory_order_acquire)) {
			return false;
		}
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		if (ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
			return false;
		}
		return true;
	}

	void QuestTrackerUI::OnDataLoaded()
	{
		dataLoaded_.store(true, std::memory_order_release);
	}

	// ----------------------------------------------------------------------
	// Main-thread tasks
	// ----------------------------------------------------------------------
	void QuestTrackerUI::RequestRefresh()
	{
		if (refreshPending_.exchange(true, std::memory_order_acq_rel)) {
			return;  // a refresh is already queued
		}
		if (const auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([]() { DoRefresh(); });
		} else {
			refreshPending_.store(false, std::memory_order_release);
		}
	}

	void QuestTrackerUI::DoRefresh()
	{
		auto& self = Get();
		self.refreshPending_.store(false, std::memory_order_release);

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			self.SetStatus("TESDataHandler unavailable");
			return;
		}

		const bool onlyRunning = self.onlyRunning_.load(std::memory_order_acquire);

		std::vector<QuestRow> rows;
		for (auto* quest : dataHandler->GetFormArray<RE::TESQuest>()) {
			if (!quest) {
				continue;
			}
			const bool running = quest->IsRunning();
			if (onlyRunning && !running) {
				continue;
			}

			QuestRow row;
			if (const char* name = quest->GetName()) {
				row.name = name;
			}
			if (const char* editorID = quest->GetFormEditorID()) {
				row.editorID = editorID;
			}
			row.formID = quest->GetFormID();
			row.stage = quest->GetCurrentStageID();
			row.running = running;
			row.active = quest->IsActive();
			row.completed = quest->IsCompleted();
			rows.push_back(std::move(row));
		}
		SortRows(rows);

		{
			std::scoped_lock guard(self.lock_);
			self.rows_ = std::move(rows);
		}
	}

	void QuestTrackerUI::RequestSetStage(std::uint32_t a_formID, std::uint16_t a_stage, bool a_force)
	{
		const auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			SetStatus("SKSE task interface unavailable");
			return;
		}
		tasks->AddTask([a_formID, a_stage, a_force]() {
			DoSetStage(a_formID, a_stage, a_force);
		});
	}

	// Runs on the game's main thread. Guards:
	//   1. re-resolve the quest by form ID (never trust a cached pointer),
	//   2. require a running quest unless the user forced it,
	//   3. dispatch the engine's own Papyrus native SetCurrentStageID via
	//      the VM, so all engine-side bookkeeping (stage fragments,
	//      objectives, story manager events) runs exactly as if a script
	//      had called Quest.SetCurrentStageID().
	void QuestTrackerUI::DoSetStage(std::uint32_t a_formID, std::uint16_t a_stage, bool a_force)
	{
		auto& self = Get();

		auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_formID);
		if (!quest) {
			self.SetStatus("Quest " + FormatFormID(a_formID) + " no longer exists");
			return;
		}

		if (!quest->IsRunning() && !a_force) {
			self.SetStatus("Quest is not running - tick 'Force' to set the stage anyway");
			return;
		}

		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
		if (!vm || !policy) {
			self.SetStatus("Papyrus VM unavailable");
			return;
		}

		const auto handle = policy->GetHandleForObject(
			static_cast<RE::VMTypeID>(RE::FormType::Quest), quest);
		if (handle == policy->EmptyHandle()) {
			self.SetStatus("Could not obtain a VM handle for the quest");
			return;
		}

		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		// NOTE: the VM takes the argument object; CommonLibSSE's own event
		// dispatch code never frees it after the call, so neither do we
		// (deleting here would risk a double free).
		auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(a_stage));
		const bool dispatched = vm->DispatchMethodCall(
			handle, "Quest"sv, "SetCurrentStageID"sv, args, callback);

		if (dispatched) {
			logger::info("SetCurrentStageID({}) dispatched for {} ({})",
				a_stage, quest->GetFormEditorID() ? quest->GetFormEditorID() : "<no editor id>",
				FormatFormID(a_formID));
			self.SetStatus("Stage " + std::to_string(a_stage) + " requested for " + FormatFormID(a_formID));
			self.RequestRefresh();
		} else {
			self.SetStatus("VM rejected SetCurrentStageID call");
			logger::error("DispatchMethodCall failed for {}", FormatFormID(a_formID));
		}
	}

	void QuestTrackerUI::SetStatus(std::string a_status)
	{
		std::scoped_lock guard(lock_);
		status_ = std::move(a_status);
	}

	// ----------------------------------------------------------------------
	// Rendering (render thread, inside an ImGui frame)
	// ----------------------------------------------------------------------
	void QuestTrackerUI::Draw()
	{
		const auto& settings = Settings::Get();
		if (settings.autoRefreshSeconds > 0) {
			const auto now = std::chrono::steady_clock::now();
			if (now - lastRefresh_ > std::chrono::seconds(settings.autoRefreshSeconds)) {
				lastRefresh_ = now;
				RequestRefresh();
			}
		}

		ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(60.0f, 60.0f), ImGuiCond_FirstUseEver);

		bool open = true;
		if (!ImGui::Begin("Quest Tracker (SKSE)", &open, ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			if (!open) {
				SetVisible(false);
			}
			return;
		}

		ImGui::SetNextItemWidth(260.0f);
		ImGui::InputTextWithHint("##filter", "filter: name / editor ID / form ID", filter_, sizeof(filter_));
		ImGui::SameLine();
		bool onlyRunning = onlyRunning_.load(std::memory_order_acquire);
		if (ImGui::Checkbox("Only running", &onlyRunning)) {
			onlyRunning_.store(onlyRunning, std::memory_order_release);
			RequestRefresh();
		}
		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			RequestRefresh();
		}

		DrawQuestTable();
		DrawStageEditor();

		{
			std::scoped_lock guard(lock_);
			if (!status_.empty()) {
				ImGui::Separator();
				ImGui::TextWrapped("%s", status_.c_str());
			}
		}

		ImGui::End();
		if (!open) {
			SetVisible(false);
		}
	}

	void QuestTrackerUI::DrawQuestTable()
	{
		std::vector<QuestRow> rows;
		{
			std::scoped_lock guard(lock_);
			rows = rows_;
		}

		const std::string_view filter{ filter_ };

		constexpr ImGuiTableFlags flags =
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

		const float footer = ImGui::GetFrameHeightWithSpacing() * 3.5f;
		if (ImGui::BeginTable("##quests", 5, flags, ImVec2(0.0f, -footer))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Editor ID", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Form ID", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 50.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableHeadersRow();

			int shown = 0;
			for (const auto& row : rows) {
				if (!MatchesFilter(row, filter)) {
					continue;
				}
				++shown;
				ImGui::TableNextRow();
				ImGui::PushID(static_cast<int>(row.formID));

				ImGui::TableSetColumnIndex(0);
				const bool selected = selectedFormID_ == row.formID;
				if (ImGui::Selectable(
						row.name.empty() ? "<unnamed>" : row.name.c_str(), selected,
						ImGuiSelectableFlags_SpanAllColumns)) {
					selectedFormID_ = row.formID;
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(row.editorID.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(FormatFormID(row.formID).c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", row.stage);
				ImGui::TableSetColumnIndex(4);
				if (row.completed) {
					ImGui::TextDisabled("done");
				} else if (row.active) {
					ImGui::TextUnformatted("active");
				} else if (row.running) {
					ImGui::TextUnformatted("running");
				} else {
					ImGui::TextDisabled("stopped");
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
			ImGui::Text("%d quest(s)", shown);
		}
	}

	void QuestTrackerUI::DrawStageEditor()
	{
		const QuestRow* selected = nullptr;
		std::vector<QuestRow> rows;
		{
			std::scoped_lock guard(lock_);
			rows = rows_;
		}
		for (const auto& row : rows) {
			if (row.formID == selectedFormID_) {
				selected = &row;
				break;
			}
		}

		ImGui::Separator();
		if (!selected) {
			ImGui::TextDisabled("Select a quest above to edit its stage.");
			return;
		}

		ImGui::Text("Selected: %s  [%s]  current stage %u",
			selected->name.empty() ? "<unnamed>" : selected->name.c_str(),
			FormatFormID(selected->formID).c_str(),
			selected->stage);

		ImGui::SetNextItemWidth(80.0f);
		ImGui::InputText("##stage", stageInput_, sizeof(stageInput_), ImGuiInputTextFlags_CharsDecimal);
		ImGui::SameLine();
		ImGui::Checkbox("Force", &force_);
		ImGui::SameLine();

		if (ImGui::Button("Set Stage")) {
			const auto parsed = ParseStageID(stageInput_);
			if (!parsed) {
				SetStatus("Enter a stage between 0 and 65535");
			} else {
				switch (ClassifyStageChange(selected->stage, *parsed)) {
				case StageVerdict::kNoChange:
					SetStatus("Quest is already at stage " + std::to_string(*parsed));
					break;
				case StageVerdict::kRegression:
					if (!force_) {
						SetStatus("Stage " + std::to_string(*parsed) +
						          " is lower than the current stage - tick 'Force' to allow regression");
						break;
					}
					[[fallthrough]];
				case StageVerdict::kAdvance:
					RequestSetStage(selected->formID, *parsed, force_);
					break;
				}
			}
		}
	}
}
