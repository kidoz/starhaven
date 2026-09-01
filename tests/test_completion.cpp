#include "game/completion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace starhaven::game;

namespace {

// A synthetic contract: two quests, one promotion-era quest, three awards,
// two chronicle notes.
std::vector<CompletionEntry> small_manifest() {
    return {
        {10, CompletionCategory::Quests},    {20, CompletionCategory::Quests},
        {30, CompletionCategory::Quests},    {1, CompletionCategory::Awards},
        {2, CompletionCategory::Awards},     {3, CompletionCategory::Awards},
        {40, CompletionCategory::Autonotes}, {41, CompletionCategory::Autonotes},
    };
}

}  // namespace

TEST_CASE("an empty journal completes nothing", "[completion]") {
    const auto report = audit_completion(small_manifest(), {}, {}, {});
    REQUIRE(report.quests.done == 0);
    REQUIRE(report.quests.total == 3);
    REQUIRE(report.awards.done == 0);
    REQUIRE(report.awards.total == 3);
    REQUIRE(report.autonotes.done == 0);
    REQUIRE(report.autonotes.total == 2);
    REQUIRE(report.done() == 0);
    REQUIRE(report.total() == 8);
    REQUIRE(report.percent() == 0);
}

TEST_CASE("the percentage is floored until the manifest closes", "[completion]") {
    std::set<int> bits{10};
    REQUIRE(audit_completion(small_manifest(), bits, {}, {}).percent() == 12);
    bits.insert(20);
    REQUIRE(audit_completion(small_manifest(), bits, {}, {}).percent() == 25);
    std::set<int> awards{1, 2};
    std::set<int> notes{40};
    const auto report = audit_completion(small_manifest(), bits, awards, notes);
    REQUIRE(report.percent() == 62);
    REQUIRE(report.done() == 5);
    awards.insert(3);
    notes.insert(41);
    bits.insert(30);
    REQUIRE(audit_completion(small_manifest(), bits, awards, notes).percent() == 100);
}

TEST_CASE("a bit in one category does not count for another", "[completion]") {
    // Award bit 1 exists, but the quest line wants quest bit 1.
    const std::set<int> awards{1};
    const auto report = audit_completion(small_manifest(), {}, awards, {});
    REQUIRE(report.awards.done == 1);
    REQUIRE(report.quests.done == 0);
}

TEST_CASE("the shipped manifest is well-formed", "[completion]") {
    const auto& manifest = completion_manifest();
    REQUIRE(manifest.size() == 203);
    REQUIRE(manifest.size() == 52 + 58 + 93);

    std::set<std::pair<int, int>> seen;
    for (const auto& entry : manifest) {
        REQUIRE(entry.bit > 0);
        REQUIRE(seen.emplace(entry.bit, static_cast<int>(entry.category)).second);
    }
    // The counts the census froze: 52 journal quests, 58 script-granted
    // awards, 93 script-settable chronicle notes.
    std::size_t quests = 0;
    std::size_t awards = 0;
    std::size_t autonotes = 0;
    for (const auto& entry : manifest) {
        switch (entry.category) {
        case CompletionCategory::Quests:
            ++quests;
            break;
        case CompletionCategory::Awards:
            ++awards;
            break;
        case CompletionCategory::Autonotes:
            ++autonotes;
            break;
        }
    }
    REQUIRE(quests == 52);
    REQUIRE(awards == 58);
    REQUIRE(autonotes == 93);

    // The opening seed the engine itself sets is among the quests.
    bool seeded = false;
    for (const auto& entry : manifest) {
        seeded = seeded || (entry.bit == 81 && entry.category == CompletionCategory::Quests);
    }
    REQUIRE(seeded);
}

TEST_CASE("category names read in the journal's words", "[completion]") {
    REQUIRE(completion_category_name(CompletionCategory::Quests) == "Quests");
    REQUIRE(completion_category_name(CompletionCategory::Awards) == "Awards");
    REQUIRE(completion_category_name(CompletionCategory::Autonotes) == "Chronicle");
}
