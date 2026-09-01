#ifndef STARHAVEN_GAME_COMPLETION_HPP
#define STARHAVEN_GAME_COMPLETION_HPP

// The completion contract and its measurement (FC-0/FC-1 of the
// full-campaign plan). The manifest is frozen data: which journal bits the
// shipped campaign can actually reach, per category, as the census
// (`tools/completion_census.cpp`) counted them. The auditor reads live
// party state against it and answers with per-category lines and one
// percentage — floored, so only a fully closed manifest reads 100%.

#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

namespace starhaven::game {

enum class CompletionCategory : std::uint8_t { Quests, Awards, Autonotes };

[[nodiscard]] std::string_view completion_category_name(CompletionCategory category) noexcept;

// One journal bit the contract requires.
struct CompletionEntry {
    int bit = 0;
    CompletionCategory category = CompletionCategory::Quests;
};

// The shipped contract. Regenerating it is a census run away; changing a
// category or dropping an entry is a contract change.
[[nodiscard]] const std::vector<CompletionEntry>& completion_manifest();

struct CompletionLine {
    int done = 0;
    int total = 0;
};

struct CompletionReport {
    CompletionLine quests;
    CompletionLine awards;
    CompletionLine autonotes;

    [[nodiscard]] int done() const noexcept;
    [[nodiscard]] int total() const noexcept;
    [[nodiscard]] int percent() const noexcept;
};

// Measure the party's journal bits against a manifest.
[[nodiscard]] CompletionReport audit_completion(const std::vector<CompletionEntry>& manifest,
                                                const std::set<int>& bits,
                                                const std::set<int>& awards,
                                                const std::set<int>& autonotes);

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_COMPLETION_HPP
