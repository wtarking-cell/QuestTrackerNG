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
		std::uint32_t formID = 0;
		std::uint16_t stage = 0;
		bool          running = false;
		bool          active = false;
		bool          completed = false;
	};

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

	// Filter matches quest name, editor ID, or the formatted form ID.
	inline bool MatchesFilter(const QuestRow& a_row, std::string_view a_filter)
	{
		if (a_filter.empty()) {
			return true;
		}
		return ContainsCaseInsensitive(a_row.name, a_filter) ||
		       ContainsCaseInsensitive(a_row.editorID, a_filter) ||
		       ContainsCaseInsensitive(FormatFormID(a_row.formID), a_filter);
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
