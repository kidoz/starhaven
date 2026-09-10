#include "game/completion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

#include "game/save.hpp"
#include "game/script_walk.hpp"

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
    std::set<int> resolved{10};
    REQUIRE(audit_completion(small_manifest(), resolved, {}, {}).percent() == 12);
    resolved.insert(20);
    REQUIRE(audit_completion(small_manifest(), resolved, {}, {}).percent() == 25);
    std::set<int> awards{1, 2};
    std::set<int> notes{40};
    const auto report = audit_completion(small_manifest(), resolved, awards, notes);
    REQUIRE(report.percent() == 62);
    REQUIRE(report.done() == 5);
    awards.insert(3);
    notes.insert(41);
    resolved.insert(30);
    REQUIRE(audit_completion(small_manifest(), resolved, awards, notes).percent() == 100);
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

TEST_CASE("quest resolution survives subsequent assignments and saving", "[completion]") {
    using namespace starhaven;
    // Synthetic assignment 10, then a reward that clears it and assigns 20.
    std::vector<std::byte> bytes(48, std::byte{0});
    const std::vector<std::vector<std::uint8_t>> records{
        {9, 1, 0, 0, world::kOpcodeGive, world::kVarQuestBit, 10, 0, 0, 0},
        {5, 1, 0, 1, world::kOpcodeEnd, 0},
        {9, 2, 0, 0, world::kOpcodeTake, world::kVarQuestBit, 10, 0, 0, 0},
        {9, 2, 0, 1, world::kOpcodeGive, world::kVarQuestBit, 20, 0, 0, 0},
        {5, 2, 0, 2, world::kOpcodeEnd, 0},
    };
    for (const auto& record : records) {
        for (const auto byte : record) {
            bytes.push_back(static_cast<std::byte>(byte));
        }
    }
    world::MapScript script;
    REQUIRE(world::MapScript::parse(bytes, script) == world::MapScriptError::None);
    WalkState state;
    REQUIRE(walk_event(script, 1, state).ran);
    REQUIRE(audit_completion(small_manifest(), state.resolved_quests, {}, {}).quests.done == 0);
    REQUIRE(walk_event(script, 2, state).ran);
    REQUIRE(state.bits == std::set<int>{20});
    REQUIRE(state.resolved_quests == std::set<int>{10});
    REQUIRE(audit_completion(small_manifest(), state.resolved_quests, {}, {}).quests.done == 1);
    REQUIRE(walk_event(script, 2, state).ran);
    REQUIRE(state.resolved_quests.size() == 1);

    SaveState saved;
    saved.map_file = "Synthetic.odm";
    saved.bits = state.bits;
    saved.resolved_quests = state.resolved_quests;
    SaveState restored;
    REQUIRE(parse_save(save_text(saved), restored));
    REQUIRE(restored.resolved_quests == state.resolved_quests);
    REQUIRE(audit_completion(small_manifest(), restored.resolved_quests, {}, {}).quests.done == 1);
}
