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
			if (const auto* file = quest->GetFile(0)) {
				row.modName = std::string(file->GetFilename());
			}
			row.formID = quest->GetFormID();
			row.stage = quest->GetCurrentStageID();
			row.typeId = static_cast<std::uint8_t>(quest->data.questType.get());
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

	void QuestTrackerUI::RequestStages(std::uint32_t a_formID)
	{
		if (const auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_formID]() { DoFetchStages(a_formID); });
		}
	}

	// Runs on the game's main thread. The engine keeps a quest's stages in
	// two lists: executedStages (already run) and waitingStages (not yet
	// run); together they are every stage the quest defines.
	void QuestTrackerUI::DoFetchStages(std::uint32_t a_formID)
	{
		auto& self = Get();

		std::vector<StageRow>     stages;
		std::vector<ObjectiveRow> objectives;
		if (const auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_formID)) {
			const auto append = [&stages](const RE::TESQuestStage& a_stage, bool a_executed) {
				StageRow row;
				row.index = a_stage.data.index;
				row.executed = a_executed;
				row.startUp = a_stage.data.flags.any(RE::QUEST_STAGE_DATA::Flag::kStartUpStage);
				row.shutDown = a_stage.data.flags.any(RE::QUEST_STAGE_DATA::Flag::kShutDownStage);
				stages.push_back(row);
			};

			if (quest->executedStages) {
				for (const auto& stage : *quest->executedStages) {
					append(stage, true);
				}
			}
			if (quest->waitingStages) {
				for (const auto* stage : *quest->waitingStages) {
					if (stage) {
						append(*stage, false);
					}
				}
			}
			NormalizeStages(stages);

			// Objectives: the player-facing journal/HUD text for the quest.
			for (const auto* objective : quest->objectives) {
				if (!objective) {
					continue;
				}
				ObjectiveRow row;
				row.index = objective->index;
				if (const char* text = objective->displayText.c_str()) {
					row.text = text;
				}
				row.state = static_cast<std::uint8_t>(objective->state.get());
				objectives.push_back(std::move(row));
			}
			std::stable_sort(objectives.begin(), objectives.end(),
				[](const ObjectiveRow& a, const ObjectiveRow& b) { return a.index < b.index; });
		}

		{
			std::scoped_lock guard(self.lock_);
			self.stages_ = std::move(stages);
			self.objectives_ = std::move(objectives);
			self.stagesFormID_ = a_formID;
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
			self.RequestStages(a_formID);
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
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		ImGui::Combo("##group", &groupMode_, "No grouping\0By type\0By mod\0");

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
		std::vector<QuestRow>  filtered;
		filtered.reserve(rows.size());
		for (auto& row : rows) {
			if (MatchesFilter(row, filter)) {
				filtered.push_back(std::move(row));
			}
		}

		const float footer = ImGui::GetFrameHeightWithSpacing() * 4.0f + 132.0f;
		ImGui::BeginChild("##questarea", ImVec2(0.0f, -footer));

		const auto mode = static_cast<GroupMode>(groupMode_);
		if (mode == GroupMode::kNone) {
			DrawTableRows(filtered, "all");
		} else {
			for (const auto& group : DistinctGroups(filtered, mode)) {
				std::vector<QuestRow> subset;
				for (const auto& row : filtered) {
					if (GroupKeyFor(row, mode) == group) {
						subset.push_back(row);
					}
				}
				const std::string header =
					group + "  (" + std::to_string(subset.size()) + ")###grp_" + group;
				if (ImGui::CollapsingHeader(header.c_str())) {
					DrawTableRows(subset, group.c_str());
				}
			}
		}

		ImGui::EndChild();
		ImGui::Text("%d quest(s)", static_cast<int>(filtered.size()));
	}

	void QuestTrackerUI::DrawTableRows(const std::vector<QuestRow>& a_rows, const char* a_id)
	{
		constexpr ImGuiTableFlags flags =
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable;

		ImGui::PushID(a_id);
		if (ImGui::BeginTable("##quests", 6, flags)) {
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Editor ID", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch, 0.6f);
			ImGui::TableSetupColumn("Form ID", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 50.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableHeadersRow();

			for (const auto& row : a_rows) {
				ImGui::TableNextRow();
				ImGui::PushID(static_cast<int>(row.formID));

				ImGui::TableSetColumnIndex(0);
				const bool selected = selectedFormID_ == row.formID;
				if (ImGui::Selectable(
						row.name.empty() ? "<unnamed>" : row.name.c_str(), selected,
						ImGuiSelectableFlags_SpanAllColumns)) {
					if (selectedFormID_ != row.formID) {
						selectedFormID_ = row.formID;
						stageInput_[0] = '\0';
						RequestStages(row.formID);
					}
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(row.editorID.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(row.modName.empty() ? "(runtime)" : row.modName.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(FormatFormID(row.formID).c_str());
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", row.stage);
				ImGui::TableSetColumnIndex(5);
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
		}
		ImGui::PopID();
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

		// Stage picker (left) + objectives panel (right). Clicking a stage
		// fills the input box below.
		std::vector<StageRow>     stages;
		std::vector<ObjectiveRow> objectives;
		{
			std::scoped_lock guard(lock_);
			if (stagesFormID_ == selectedFormID_) {
				stages = stages_;
				objectives = objectives_;
			}
		}
		if (stages.empty()) {
			ImGui::TextDisabled("(no stage data loaded yet)");
		} else {
			const auto   picked = ParseStageID(stageInput_);
			const ImVec2 size(220.0f, 126.0f);
			if (ImGui::BeginListBox("##stagelist", size)) {
				for (const auto& stage : stages) {
					const bool isPicked = picked && *picked == stage.index;
					if (ImGui::Selectable(FormatStageLabel(stage, selected->stage).c_str(), isPicked)) {
						std::snprintf(stageInput_, sizeof(stageInput_), "%u",
							static_cast<unsigned>(stage.index));
					}
				}
				ImGui::EndListBox();
			}
			ImGui::SameLine();
			ImGui::BeginChild("##objectives", ImVec2(0.0f, 126.0f));
			ImGui::TextDisabled("Objectives");
			if (objectives.empty()) {
				ImGui::TextDisabled("(this quest has no objectives)");
			} else {
				for (const auto& objective : objectives) {
					ImGui::TextWrapped("[%u] %s - %s",
						static_cast<unsigned>(objective.index),
						std::string(ObjectiveStateLabel(objective.state)).c_str(),
						objective.text.empty() ? "<no text>" : objective.text.c_str());
				}
			}
			ImGui::EndChild();
		}

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
