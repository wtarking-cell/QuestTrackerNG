#pragma once

// Engine-independent logic used by the UI layer. This header must not
// include any CommonLibSSE / Windows headers: it is compiled and unit
// tested on any platform (see tests/).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace QuestTracker::Logic
{
	// A snapshot row describing one quest. Filled from TESQuest on the
	// game's main thread, rendered from the D3D11 present hook.
	struct QuestRow
	{
		std::string   name;
		std::string   editorID;
		std::string   modName;  // plugin file that defines the quest
		std::uint32_t formID = 0;
		std::uint16_t stage = 0;
		std::uint8_t  typeId = 0;  // QUEST_DATA::Type
		bool          running = false;
		bool          active = false;
		bool          completed = false;
	};

	// One quest objective (the HUD/journal text shown to the player).
	struct ObjectiveRow
	{
		std::uint16_t index = 0;
		std::string   text;
		std::uint8_t  state = 0;  // QUEST_OBJECTIVE_STATE
	};

	// Journal category names for QUEST_DATA::Type.
	inline std::string_view QuestTypeLabel(std::uint8_t a_type)
	{
		switch (a_type) {
		case 0:  return "None";
		case 1:  return "Main Quest";
		case 2:  return "Mages Guild";
		case 3:  return "Thieves Guild";
		case 4:  return "Dark Brotherhood";
		case 5:  return "Companions";
		case 6:  return "Miscellaneous";
		case 7:  return "Daedric";
		case 8:  return "Side Quest";
		case 9:  return "Civil War";
		case 10: return "Dawnguard";
		case 11: return "Dragonborn";
		default: return "Unknown";
		}
	}

	inline std::string_view ObjectiveStateLabel(std::uint8_t a_state)
	{
		switch (a_state) {
		case 0:  return "dormant";
		case 1:  return "displayed";
		case 2:  return "completed";
		case 3:  return "failed";
		default: return "?";
		}
	}

	enum class GroupMode : std::uint8_t
	{
		kNone = 0,
		kType,
		kMod
	};

	inline std::string GroupKeyFor(const QuestRow& a_row, GroupMode a_mode)
	{
		switch (a_mode) {
		case GroupMode::kType:
			return std::string(QuestTypeLabel(a_row.typeId));
		case GroupMode::kMod:
			return a_row.modName.empty() ? std::string("(runtime)") : a_row.modName;
		default:
			return {};
		}
	}

	// Sorted, de-duplicated group headers for the given mode.
	inline std::vector<std::string> DistinctGroups(const std::vector<QuestRow>& a_rows, GroupMode a_mode)
	{
		std::vector<std::string> groups;
		for (const auto& row : a_rows) {
			groups.push_back(GroupKeyFor(row, a_mode));
		}
		std::sort(groups.begin(), groups.end());
		groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
		return groups;
	}

	// Parse a decimal quest stage entered by the user. Accepts surrounding
	// whitespace; rejects empty input, non-digits, and values > 65535.
	inline std::optional<std::uint16_t> ParseStageID(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t");
		if (first == std::string_view::npos) {
			return std::nullopt;
		}
		const auto last = a_text.find_last_not_of(" \t");
		a_text = a_text.substr(first, last - first + 1);

		if (a_text.empty() || a_text.size() > 5) {
			return std::nullopt;
		}

		std::uint32_t value = 0;
		for (const char c : a_text) {
			if (c < '0' || c > '9') {
				return std::nullopt;
			}
			value = value * 10 + static_cast<std::uint32_t>(c - '0');
		}
		if (value > 0xFFFF) {
			return std::nullopt;
		}
		return static_cast<std::uint16_t>(value);
	}

	// "0x000A2C94" — zero-padded 8-digit hex with prefix.
	inline std::string FormatFormID(std::uint32_t a_formID)
	{
		static constexpr char digits[] = "0123456789ABCDEF";
		std::string           out = "0x00000000";
		for (int i = 0; i < 8; ++i) {
			out[9 - i] = digits[(a_formID >> (i * 4)) & 0xF];
		}
		return out;
	}

	inline bool ContainsCaseInsensitive(std::string_view a_haystack, std::string_view a_needle)
	{
		if (a_needle.empty()) {
			return true;
		}
		if (a_needle.size() > a_haystack.size()) {
			return false;
		}
		const auto lower = [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		};
		const auto it = std::search(
			a_haystack.begin(), a_haystack.end(),
			a_needle.begin(), a_needle.end(),
			[&](char a, char b) { return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b)); });
		return it != a_haystack.end();
	}

	// Filter matches quest name, editor ID, defining mod, or form ID.
	inline bool MatchesFilter(const QuestRow& a_row, std::string_view a_filter)
	{
		if (a_filter.empty()) {
			return true;
		}
		return ContainsCaseInsensitive(a_row.name, a_filter) ||
		       ContainsCaseInsensitive(a_row.editorID, a_filter) ||
		       ContainsCaseInsensitive(a_row.modName, a_filter) ||
		       ContainsCaseInsensitive(FormatFormID(a_row.formID), a_filter);
	}

	// One stage of a quest, merged from the engine's executed/waiting
	// stage lists.
	struct StageRow
	{
		std::uint16_t index = 0;
		bool          executed = false;
		bool          startUp = false;
		bool          shutDown = false;
	};

	// Sort by stage index and merge duplicate indices (a stage can appear
	// in both engine lists when repeat stages are allowed).
	inline void NormalizeStages(std::vector<StageRow>& a_stages)
	{
		std::stable_sort(a_stages.begin(), a_stages.end(),
			[](const StageRow& a, const StageRow& b) { return a.index < b.index; });

		std::vector<StageRow> merged;
		merged.reserve(a_stages.size());
		for (const auto& stage : a_stages) {
			if (!merged.empty() && merged.back().index == stage.index) {
				merged.back().executed |= stage.executed;
				merged.back().startUp |= stage.startUp;
				merged.back().shutDown |= stage.shutDown;
			} else {
				merged.push_back(stage);
			}
		}
		a_stages = std::move(merged);
	}

	// "200  (current) [finish]" — label for the stage picker.
	inline std::string FormatStageLabel(const StageRow& a_stage, std::uint16_t a_currentStage)
	{
		std::string label = std::to_string(a_stage.index);
		if (a_stage.index == a_currentStage) {
			label += "  (current)";
		} else if (a_stage.executed) {
			label += "  (done)";
		}
		if (a_stage.startUp) {
			label += " [start]";
		}
		if (a_stage.shutDown) {
			label += " [finish]";
		}
		return label;
	}

	enum class StageVerdict
	{
		kAdvance,    // target > current: the normal case
		kNoChange,   // target == current
		kRegression  // target < current: usually unsafe unless the quest allows repeated stages
	};

	inline StageVerdict ClassifyStageChange(std::uint16_t a_current, std::uint16_t a_target)
	{
		if (a_target == a_current) {
			return StageVerdict::kNoChange;
		}
		return a_target > a_current ? StageVerdict::kAdvance : StageVerdict::kRegression;
	}

	// Stable presentation order: running+active first, then by editor ID,
	// then by form ID for rows without one.
	inline void SortRows(std::vector<QuestRow>& a_rows)
	{
		std::stable_sort(a_rows.begin(), a_rows.end(), [](const QuestRow& a, const QuestRow& b) {
			if (a.active != b.active) {
				return a.active;
			}
			if (a.editorID != b.editorID) {
				return a.editorID < b.editorID;
			}
			return a.formID < b.formID;
		});
	}
}
