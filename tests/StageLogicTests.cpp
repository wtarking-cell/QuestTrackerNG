// Unit tests for src/Core/StageLogic.h — the engine-independent layer
// shared with the SKSE plugin. No framework needed; exits non-zero on
// failure so it plugs straight into CTest and CI.

#include "Core/StageLogic.h"

#include <cstdio>

namespace
{
	int g_failures = 0;
	int g_checks = 0;
}

#define CHECK(expr)                                                          \
	do {                                                                     \
		++g_checks;                                                          \
		if (!(expr)) {                                                       \
			++g_failures;                                                    \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);      \
		}                                                                    \
	} while (0)

using namespace QuestTracker::Logic;

static void TestParseStageID()
{
	// happy path
	CHECK(ParseStageID("0") == std::uint16_t{ 0 });
	CHECK(ParseStageID("10") == std::uint16_t{ 10 });
	CHECK(ParseStageID("200") == std::uint16_t{ 200 });
	CHECK(ParseStageID("65535") == std::uint16_t{ 65535 });
	CHECK(ParseStageID("  42  ") == std::uint16_t{ 42 });
	CHECK(ParseStageID("\t7") == std::uint16_t{ 7 });
	CHECK(ParseStageID("00010") == std::uint16_t{ 10 });  // leading zeros, still 5 chars

	// rejections
	CHECK(!ParseStageID(""));
	CHECK(!ParseStageID("   "));
	CHECK(!ParseStageID("65536"));   // > uint16 max
	CHECK(!ParseStageID("99999"));   // > uint16 max
	CHECK(!ParseStageID("123456"));  // too many digits
	CHECK(!ParseStageID("-1"));
	CHECK(!ParseStageID("+5"));
	CHECK(!ParseStageID("1 0"));     // inner whitespace
	CHECK(!ParseStageID("0x10"));    // hex not allowed
	CHECK(!ParseStageID("ten"));
	CHECK(!ParseStageID("12a"));
	CHECK(!ParseStageID("1.5"));
}

static void TestFormatFormID()
{
	CHECK(FormatFormID(0x0) == "0x00000000");
	CHECK(FormatFormID(0xA2C94) == "0x000A2C94");
	CHECK(FormatFormID(0xFF000D62) == "0xFF000D62");
	CHECK(FormatFormID(0xFFFFFFFF) == "0xFFFFFFFF");
	CHECK(FormatFormID(0x1) == "0x00000001");
}

static void TestFilter()
{
	QuestRow row;
	row.name = "The Way of the Voice";
	row.editorID = "MQ105";
	row.formID = 0x0002610C;

	CHECK(MatchesFilter(row, ""));
	CHECK(MatchesFilter(row, "voice"));
	CHECK(MatchesFilter(row, "VOICE"));
	CHECK(MatchesFilter(row, "mq105"));
	CHECK(MatchesFilter(row, "0x0002610C"));
	CHECK(MatchesFilter(row, "2610c"));
	CHECK(!MatchesFilter(row, "dragonborn"));
	CHECK(!MatchesFilter(row, "MQ106"));

	// needle longer than haystack must not crash or match
	QuestRow tiny;
	tiny.name = "a";
	CHECK(!MatchesFilter(tiny, "abcdefghijklmnopqrstuvwxyz0x123456789"));
}

static void TestClassifyStageChange()
{
	CHECK(ClassifyStageChange(10, 20) == StageVerdict::kAdvance);
	CHECK(ClassifyStageChange(10, 10) == StageVerdict::kNoChange);
	CHECK(ClassifyStageChange(20, 10) == StageVerdict::kRegression);
	CHECK(ClassifyStageChange(0, 0) == StageVerdict::kNoChange);
	CHECK(ClassifyStageChange(0, 65535) == StageVerdict::kAdvance);
	CHECK(ClassifyStageChange(65535, 0) == StageVerdict::kRegression);
}

static void TestSortRows()
{
	std::vector<QuestRow> rows(4);
	rows[0].editorID = "ZZZ";
	rows[0].active = false;
	rows[1].editorID = "AAA";
	rows[1].active = false;
	rows[2].editorID = "MMM";
	rows[2].active = true;
	rows[3].editorID = "";  // ties broken by form ID
	rows[3].formID = 5;

	SortRows(rows);

	CHECK(rows[0].editorID == "MMM");  // active first
	CHECK(rows[1].editorID == "");     // then lexicographic editor ID
	CHECK(rows[2].editorID == "AAA");
	CHECK(rows[3].editorID == "ZZZ");

	// duplicate editor IDs: ordered by form ID, stable otherwise
	std::vector<QuestRow> dupes(2);
	dupes[0].editorID = "DLC1VQ";
	dupes[0].formID = 9;
	dupes[1].editorID = "DLC1VQ";
	dupes[1].formID = 3;
	SortRows(dupes);
	CHECK(dupes[0].formID == 3);
	CHECK(dupes[1].formID == 9);
}

static void TestNormalizeStages()
{
	std::vector<StageRow> stages;
	stages.push_back({ 200, false, false, true });
	stages.push_back({ 10, true, true, false });
	stages.push_back({ 10, false, false, false });  // duplicate of executed 10 (repeat stages)
	stages.push_back({ 50, false, false, false });

	NormalizeStages(stages);

	CHECK(stages.size() == 3);
	CHECK(stages[0].index == 10);
	CHECK(stages[0].executed);  // merged: executed wins
	CHECK(stages[0].startUp);
	CHECK(stages[1].index == 50);
	CHECK(!stages[1].executed);
	CHECK(stages[2].index == 200);
	CHECK(stages[2].shutDown);

	std::vector<StageRow> empty;
	NormalizeStages(empty);
	CHECK(empty.empty());
}

static void TestFormatStageLabel()
{
	CHECK(FormatStageLabel({ 10, true, true, false }, 20) == "10  (done) [start]");
	CHECK(FormatStageLabel({ 20, true, false, false }, 20) == "20  (current)");
	CHECK(FormatStageLabel({ 200, false, false, true }, 20) == "200 [finish]");
	CHECK(FormatStageLabel({ 50, false, false, false }, 20) == "50");
}

static void TestQuestTypeAndObjectiveLabels()
{
	CHECK(QuestTypeLabel(0) == "None");
	CHECK(QuestTypeLabel(1) == "Main Quest");
	CHECK(QuestTypeLabel(6) == "Miscellaneous");
	CHECK(QuestTypeLabel(11) == "Dragonborn");
	CHECK(QuestTypeLabel(200) == "Unknown");

	CHECK(ObjectiveStateLabel(0) == "dormant");
	CHECK(ObjectiveStateLabel(1) == "displayed");
	CHECK(ObjectiveStateLabel(2) == "completed");
	CHECK(ObjectiveStateLabel(3) == "failed");
	CHECK(ObjectiveStateLabel(99) == "?");
}

static void TestGrouping()
{
	std::vector<QuestRow> rows(3);
	rows[0].typeId = 1;
	rows[0].modName = "Skyrim.esm";
	rows[1].typeId = 6;
	rows[1].modName = "MyMod.esp";
	rows[2].typeId = 1;
	rows[2].modName = "";  // runtime-created

	CHECK(GroupKeyFor(rows[0], GroupMode::kType) == "Main Quest");
	CHECK(GroupKeyFor(rows[1], GroupMode::kMod) == "MyMod.esp");
	CHECK(GroupKeyFor(rows[2], GroupMode::kMod) == "(runtime)");
	CHECK(GroupKeyFor(rows[0], GroupMode::kNone).empty());

	const auto byType = DistinctGroups(rows, GroupMode::kType);
	CHECK(byType.size() == 2);  // Main Quest + Miscellaneous, deduped
	const auto byMod = DistinctGroups(rows, GroupMode::kMod);
	CHECK(byMod.size() == 3);
	CHECK(byMod[0] == "(runtime)");  // sorted

	// mod name participates in the text filter
	CHECK(MatchesFilter(rows[1], "mymod"));
	CHECK(!MatchesFilter(rows[0], "mymod"));
}

static void TestColumnSorting()
{
	CHECK(CompareCaseInsensitive("abc", "ABC") == 0);
	CHECK(CompareCaseInsensitive("abc", "abd") < 0);
	CHECK(CompareCaseInsensitive("ab", "abc") < 0);
	CHECK(CompareCaseInsensitive("b", "A") > 0);

	std::vector<QuestRow> rows(3);
	rows[0].name = "zeta";
	rows[0].formID = 1;
	rows[0].stage = 30;
	rows[0].active = true;  // rank 0
	rows[1].name = "Alpha";
	rows[1].formID = 2;
	rows[1].stage = 10;
	rows[1].completed = true;  // rank 2
	rows[2].name = "midway";
	rows[2].formID = 3;
	rows[2].stage = 20;  // rank 3 (stopped)

	CHECK(StateRank(rows[0]) == 0);
	CHECK(StateRank(rows[1]) == 2);
	CHECK(StateRank(rows[2]) == 3);

	SortRowsBy(rows, 0, true);  // name ascending, case-insensitive
	CHECK(rows[0].name == "Alpha");
	CHECK(rows[2].name == "zeta");

	SortRowsBy(rows, 4, false);  // stage descending
	CHECK(rows[0].stage == 30);
	CHECK(rows[2].stage == 10);

	SortRowsBy(rows, 5, true);  // state rank ascending
	CHECK(rows[0].active);
	CHECK(rows[2].name == "midway");

	SortRowsBy(rows, 3, true);  // form ID
	CHECK(rows[0].formID == 1);
	CHECK(rows[2].formID == 3);
}

int main()
{
	TestParseStageID();
	TestFormatFormID();
	TestFilter();
	TestClassifyStageChange();
	TestSortRows();
	TestNormalizeStages();
	TestFormatStageLabel();
	TestQuestTypeAndObjectiveLabels();
	TestGrouping();
	TestColumnSorting();

	std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
