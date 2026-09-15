#include <catch2/catch_test_macros.hpp>

#include "game/save.hpp"
#include "game/script_items.hpp"
#include "game/script_walk.hpp"

using namespace starhaven;
using namespace starhaven::game;
using namespace starhaven::world;
using namespace starhaven::data;

namespace {

struct Tables {
    ItemStatsTable items;
    RandomItemTable random;
    StandardBonusTable standard;
    SpecialBonusTable special;

    explicit Tables(std::string_view equip = "Ring") {
        TextTable table;
        const std::string body = "Item #\tPic File\tName\tValue\tEquip Stat\tSkill "
                                 "Group\tMod1\tMod2\tmaterial\tID/Rep/St\n"
                                 "0\tblank\tEmpty\t0\tother\tMisc\t0\t0\t0\t0\n"
                                 "1\tbase\tSynthetic base\t10\t" +
                                 std::string(equip) +
                                 "\tMisc\t0\t12\t1\t5\n"
                                 "2\toverride\tSynthetic override\t20\tWeapon\tSword\t0\t0\t1\t0\n";
        REQUIRE(TextTable::parse_body(body, table) == TextTableError::None);
        REQUIRE(ItemStatsTable::parse(table, items) == ItemStatsError::None);
        REQUIRE(TextTable::parse_body("Item #\tPic File\t1\t2\t3\t4\t5\t6\n"
                                      "0\tblank\t0\t0\t0\t0\t0\t0\n"
                                      "1\tbase\t10\t10\t10\t10\t10\t10\n"
                                      "2\toverride\t0\t0\t0\t0\t0\t0\n"
                                      "Bonus chance by level %\t\t1\t2\t3\t4\t5\t6\n"
                                      "\tStandard\t100\t100\t100\t100\t100\t100\n"
                                      "\tSpecial\t0\t0\t0\t0\t0\t0\n"
                                      "Weapons\tSpecial %\t0\t0\t0\t0\t0\t0\n",
                                      table) == TextTableError::None);
        REQUIRE(RandomItemTable::parse(table, random) == RandomItemError::None);
        REQUIRE(TextTable::parse_body(
                    "Bonus Stat\tOf Name\tArm\tShld\tHelm\tBelt\tCape\tGaunt\tBoot\tRing\tAmul\n"
                    "Luck\tof Testing\t1\t1\t1\t1\t1\t1\t1\t1\t1\n"
                    "\tBonus range\n\tlvl\tmin\tmax\n"
                    "\t1\t3\t3\n\t2\t3\t3\n\t3\t3\t3\n"
                    "\t4\t3\t3\n\t5\t3\t3\n\t6\t3\t3\n",
                    table) == TextTableError::None);
        REQUIRE(StandardBonusTable::parse(table, standard) == StandardBonusError::None);
    }
};

MapScript script(const std::vector<ScriptStep>& steps) {
    std::vector<std::byte> bytes(48, std::byte{0});
    for (const auto& step : steps) {
        bytes.push_back(static_cast<std::byte>(4 + step.arguments.size()));
        bytes.push_back(static_cast<std::byte>(step.event_id & 0xffU));
        bytes.push_back(static_cast<std::byte>(step.event_id >> 8U));
        bytes.push_back(static_cast<std::byte>(step.sequence));
        bytes.push_back(static_cast<std::byte>(step.opcode));
        for (const auto byte : step.arguments) {
            bytes.push_back(static_cast<std::byte>(byte));
        }
    }
    MapScript out;
    REQUIRE(MapScript::parse(bytes, out) == MapScriptError::None);
    return out;
}

}  // namespace

TEST_CASE("script item layout is bounded and little endian", "[script-items]") {
    const ScriptStep valid{1, 0, kOpcodeGenerateItem, {6, 43, 0x78, 0x56, 0x34, 0x12}};
    const auto request = parse_script_item(valid);
    if (!request) {
        FAIL("complete request did not parse");
        return;
    }
    REQUIRE(request->level == 6);
    REQUIRE(request->type == 43);
    REQUIRE(request->item_id == 0x12345678U);
    for (std::size_t length = 0; length < valid.arguments.size(); ++length) {
        auto truncated = valid;
        truncated.arguments.resize(length);
        REQUIRE_FALSE(parse_script_item(truncated));
    }
    auto wrong = valid;
    wrong.opcode = kOpcodeGive;
    REQUIRE_FALSE(parse_script_item(wrong));
    auto padded = valid;
    padded.arguments.push_back(255);
    const auto padded_request = parse_script_item(padded);
    if (!padded_request) {
        FAIL("padded request did not parse");
        return;
    }
    REQUIRE(padded_request->item_id == request->item_id);
}

TEST_CASE("explicit item ids retain generated bonuses charges and random consumption",
          "[script-items]") {
    for (const std::string_view equip : {"Ring", "weaponw"}) {
        const Tables tables{equip};
        ScriptItemState base;
        base.random = 1;
        ScriptItemState override_state = base;
        ScriptItemGenerator normal{tables.random, tables.items, tables.standard, tables.special,
                                   base};
        ScriptItemGenerator replaced{tables.random, tables.items, tables.standard, tables.special,
                                     override_state};
        auto original = normal.generate({3, 0, 0});
        const auto changed = replaced.generate({3, 0, 2});
        if (!original || !changed) {
            FAIL("synthetic generation failed");
            return;
        }
        REQUIRE(original->item_id == 1);
        REQUIRE(changed->item_id == 2);
        if (equip == "Ring") {
            REQUIRE(changed->standard_bonus == 1);
            REQUIRE(changed->standard_bonus_strength == 3);
        } else {
            REQUIRE(changed->charges > 0);
        }
        original->item_id = 2;
        REQUIRE(*original == *changed);
        REQUIRE(base.random == override_state.random);
        REQUIRE(base.artifacts.found == override_state.artifacts.found);
    }
}

TEST_CASE("invalid item requests do not consume random or artifact state", "[script-items]") {
    const Tables tables;
    ScriptItemState state;
    state.random = 17;
    ScriptItemGenerator generator{tables.random, tables.items, tables.standard, tables.special,
                                  state};
    for (const ScriptItemRequest request :
         {ScriptItemRequest{0, 0, 2}, {7, 0, 2}, {1, 44, 2}, {1, 0, 99}, {1, 0, UINT32_MAX}}) {
        REQUIRE_FALSE(generator.generate(request));
        REQUIRE(state.random == 17);
        REQUIRE(state.artifacts.found == ArtifactGenerationState{}.found);
    }
}

TEST_CASE("generated rewards are visible to following checks and not replayed by a modal",
          "[script-items]") {
    const Tables tables;
    ScriptItemState rewards;
    ScriptItemGenerator generator{tables.random, tables.items, tables.standard, tables.special,
                                  rewards};
    const auto events = script({
        {1, 0, kOpcodeGenerateItem, {3, 40, 2, 0, 0, 0}},
        {1, 1, kOpcodeCheck, {kVarItem, 2, 0, 0, 0, 3}},
        {1, 2, kOpcodeEnd, {}},
        {1, 3, kOpcodeShowMessage, {}},
        {1, 4, kOpcodeTake, {kVarItem, 2, 0, 0, 0}},
        {1, 5, kOpcodeGive, {kVarQuestBit, 7, 0, 0, 0}},
        {1, 6, kOpcodeEnd, {}},
    });
    WalkState state;
    const auto first = walk_event(events, 1, state, -1, "GLOBAL.EVT", nullptr, &generator);
    REQUIRE(first.generated_items.size() == 1);
    REQUIRE(first.failed_items.empty());
    REQUIRE(first.acted());
    if (!first.message) {
        FAIL("the generated item must satisfy the following check");
        return;
    }
    REQUIRE_FALSE(state.bits.contains(7));
    const auto seed = rewards.random;
    const auto last =
        walk_event(events, 1, state, first.message->resume_at, "GLOBAL.EVT", nullptr, &generator);
    REQUIRE(last.generated_items.empty());
    REQUIRE(last.taken == std::vector<int>{2});
    REQUIRE(state.items.empty());
    REQUIRE(state.bits.contains(7));
    REQUIRE(rewards.random == seed);
    WalkState missing_service;
    const auto failed = walk_event(events, 1, missing_service);
    REQUIRE(failed.failed_items == std::vector<std::uint8_t>{0});
    REQUIRE(failed.generated_items.empty());
}

TEST_CASE("full packs retain rewards through saves and deliver complete instances later",
          "[script-items]") {
    std::array<Pack, 4> packs;
    for (auto& pack : packs) {
        REQUIRE(pack.add(9, kPackWidth, kPackHeight));
    }
    const GeneratedItem reward{2, 1, 3, 5, 27, false};
    REQUIRE_FALSE(deliver_script_item(reward, 2, 3, packs));
    SaveState saved;
    saved.map_file = "Synthetic.blv";
    saved.script_items.random = 1234567890U;
    saved.script_items.artifacts.found[29] = true;
    saved.script_items.pending.push_back(reward);
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    REQUIRE(loaded.script_items.pending == std::vector<GeneratedItem>{reward});
    REQUIRE(loaded.script_items.random == saved.script_items.random);
    REQUIRE(loaded.script_items.artifacts.found == saved.script_items.artifacts.found);
    const Tables tables;
    ScriptItemGenerator before{tables.random, tables.items, tables.standard, tables.special,
                               saved.script_items};
    ScriptItemGenerator after{tables.random, tables.items, tables.standard, tables.special,
                              loaded.script_items};
    REQUIRE(before.generate({3, 40, 0}) == after.generate({3, 40, 0}));
    REQUIRE(saved.script_items.random == loaded.script_items.random);
    packs[2].clear();
    REQUIRE(deliver_script_item(loaded.script_items.pending.front(), 2, 3, packs));
    const auto& item = packs[2].items().front();
    REQUIRE(item.item_id == reward.item_id);
    REQUIRE(item.standard_bonus == reward.standard_bonus);
    REQUIRE(item.standard_strength == reward.standard_bonus_strength);
    REQUIRE(item.special_bonus == reward.special_bonus);
    REQUIRE(item.charges == reward.charges);
    REQUIRE(item.identified == reward.identified);
}

TEST_CASE("mixed grants and takes preserve instance order", "[script-items]") {
    const Tables tables;
    for (const bool generated_first : {false, true}) {
        const ScriptStep generated{1, 0, kOpcodeGenerateItem, {3, 40, 2, 0, 0, 0}};
        const ScriptStep plain{1, 1, kOpcodeGive, {kVarItem, 2, 0, 0, 0}};
        auto first = generated_first ? generated : plain;
        auto second = generated_first ? plain : generated;
        first.sequence = 0;
        second.sequence = 1;
        const auto event = script(
            {first, second, {1, 2, kOpcodeTake, {kVarItem, 2, 0, 0, 0}}, {1, 3, kOpcodeEnd, {}}});
        ScriptItemState rewards;
        ScriptItemGenerator generator{tables.random, tables.items, tables.standard, tables.special,
                                      rewards};
        WalkState state;
        const auto out = walk_event(event, 1, state, -1, {}, nullptr, &generator);
        std::array<Pack, 4> packs;
        apply_script_items(out.item_changes, packs, rewards, tables.items);
        REQUIRE(state.items == std::vector<int>{2});
        REQUIRE(rewards.pending.size() == 1);
        REQUIRE(rewards.pending.front().standard_bonus == (generated_first ? 0 : 1));
        REQUIRE(rewards.pending.front().identified == generated_first);
    }
}

TEST_CASE("reward saves reject invalid state transactionally and accept older versions",
          "[script-items]") {
    SaveState saved;
    saved.map_file = "Synthetic.blv";
    const auto text = save_text(saved);
    for (const int version : {1, 2, 3}) {
        auto old = text;
        old.replace(0, old.find('\n'), "starhaven-save\t" + std::to_string(version));
        const auto at = old.find("scriptitems\t");
        old.erase(at, old.find('\n', at) - at + 1);
        SaveState loaded;
        REQUIRE(parse_save(old, loaded));
        REQUIRE(loaded.script_items.pending.empty());
    }
    for (const std::string_view bad : {
             "reward\t0\t0\t0\t0\t0\t1\n",
             "reward\t1\t-1\t0\t0\t0\t1\n",
             "reward\t1\t0\t0\t0\t0\t2\n",
             "reward\t1\t0\t0\t0\t0\n",
             "scriptitems\t1\t0\n",
         }) {
        auto broken = text;
        broken.insert(broken.rfind("end\n"), bad);
        SaveState loaded = saved;
        loaded.gold = 999;
        REQUIRE_FALSE(parse_save(broken, loaded));
        REQUIRE(loaded.gold == 999);
    }
    for (const std::string_view metadata : {
             "",
             "scriptitems\t4294967296\t0\n",
             "scriptitems\t-1\t0\n",
             "scriptitems\t1\t1073741824\n",
             "scriptitems\t1\t-1\n",
         }) {
        auto broken = text;
        const auto at = broken.find("scriptitems\t");
        broken.replace(at, broken.find('\n', at) - at + 1, metadata);
        SaveState loaded = saved;
        loaded.gold = 999;
        REQUIRE_FALSE(parse_save(broken, loaded));
        REQUIRE(loaded.gold == 999);
    }
}

TEST_CASE("claimed chest rewards survive full packs and save reload without duplication",
          "[script-items][chests]") {
    std::array<Pack, 4> packs;
    for (auto& pack : packs) {
        REQUIRE(pack.add(9, kPackWidth, kPackHeight));
    }
    const std::vector<GeneratedItem> contents{{2, 1, 3, 5, 27, false}, {1, 0, 0, 0, 0, true}};
    std::set<int> opened;
    ScriptItemState rewards;
    REQUIRE(claim_chest_items(7, contents, opened, rewards));
    REQUIRE(opened.contains(7));
    for (const auto& item : rewards.pending) {
        REQUIRE_FALSE(deliver_script_item(item, 2, 3, packs));
    }
    REQUIRE(rewards.pending == contents);
    REQUIRE_FALSE(claim_chest_items(7, contents, opened, rewards));
    REQUIRE(rewards.pending == contents);

    SaveState saved;
    saved.map_file = "Synthetic.blv";
    saved.script_items = rewards;
    saved.opened_chests.assign(opened.begin(), opened.end());
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    opened = {loaded.opened_chests.begin(), loaded.opened_chests.end()};
    REQUIRE_FALSE(claim_chest_items(7, contents, opened, loaded.script_items));
    REQUIRE(loaded.script_items.pending == contents);

    packs[2].clear();
    for (const auto& item : loaded.script_items.pending) {
        REQUIRE(deliver_script_item(item, 2, 3, packs));
    }
    REQUIRE(packs[2].items().size() == 2);
    const auto& received = packs[2].items().front();
    REQUIRE(received.standard_bonus == contents[0].standard_bonus);
    REQUIRE(received.standard_strength == contents[0].standard_bonus_strength);
    REQUIRE(received.special_bonus == contents[0].special_bonus);
    REQUIRE(received.charges == contents[0].charges);
    REQUIRE(received.identified == contents[0].identified);
}

TEST_CASE("a partially delivered chest retains only its undelivered items",
          "[script-items][chests]") {
    std::array<Pack, 1> packs;
    const std::vector<GeneratedItem> contents{{1}, {2}};
    std::set<int> opened;
    ScriptItemState rewards;
    REQUIRE(claim_chest_items(4, contents, opened, rewards));
    REQUIRE(deliver_script_item(rewards.pending.front(), kPackWidth, kPackHeight, packs));
    rewards.pending.erase(rewards.pending.begin());
    REQUIRE_FALSE(deliver_script_item(rewards.pending.front(), 1, 1, packs));
    REQUIRE_FALSE(claim_chest_items(4, contents, opened, rewards));
    REQUIRE(rewards.pending == std::vector<GeneratedItem>{contents[1]});
    REQUIRE(packs[0].items().size() == 1);
    REQUIRE_FALSE(claim_chest_items(-1, contents, opened, rewards));
}

TEST_CASE("a quest can consume a waiting chest item without regenerating it after reload",
          "[script-items][chests]") {
    const Tables tables;
    std::array<Pack, 1> packs;
    REQUIRE(packs[0].add(9, kPackWidth, kPackHeight));
    const std::vector<GeneratedItem> contents{{2}};
    std::set<int> opened;
    ScriptItemState rewards;
    REQUIRE(claim_chest_items(7, contents, opened, rewards));
    REQUIRE_FALSE(deliver_script_item(contents[0], 1, 1, packs));
    const std::array<ScriptItemChange, 1> payment{{{contents[0], true}}};
    apply_script_items(payment, packs, rewards, tables.items);
    REQUIRE(rewards.pending.empty());
    SaveState saved;
    saved.map_file = "Synthetic.blv";
    saved.script_items = rewards;
    saved.opened_chests.assign(opened.begin(), opened.end());
    SaveState loaded;
    REQUIRE(parse_save(save_text(saved), loaded));
    opened = {loaded.opened_chests.begin(), loaded.opened_chests.end()};
    REQUIRE_FALSE(claim_chest_items(7, contents, opened, loaded.script_items));
    REQUIRE(loaded.script_items.pending.empty());
    REQUIRE(packs[0].items().size() == 1);
    REQUIRE(packs[0].items().front().item_id == 9);
}
