#include "game/completion.hpp"

namespace starhaven::game {

std::string_view completion_category_name(CompletionCategory category) noexcept {
    switch (category) {
    case CompletionCategory::Quests:
        return "Quests";
    case CompletionCategory::Awards:
        return "Awards";
    case CompletionCategory::Autonotes:
        return "Chronicle";
    }
    return "Unknown";
}

const std::vector<CompletionEntry>& completion_manifest() {
    // Frozen journal IDs from tools/completion_census.cpp: 52 quests
    // (including opening assignment 81), 58 script-granted awards and
    // 93 script-settable chronicle notes. Quest completion reads separate
    // resolution history. This census does not prove campaign reachability;
    // see docs/explanation/campaign-completion.md.
    static const std::vector<CompletionEntry> manifest = {
        {81, CompletionCategory::Quests},     {82, CompletionCategory::Quests},
        {83, CompletionCategory::Quests},     {84, CompletionCategory::Quests},
        {86, CompletionCategory::Quests},     {88, CompletionCategory::Quests},
        {89, CompletionCategory::Quests},     {90, CompletionCategory::Quests},
        {95, CompletionCategory::Quests},     {96, CompletionCategory::Quests},
        {98, CompletionCategory::Quests},     {105, CompletionCategory::Quests},
        {107, CompletionCategory::Quests},    {110, CompletionCategory::Quests},
        {111, CompletionCategory::Quests},    {112, CompletionCategory::Quests},
        {113, CompletionCategory::Quests},    {114, CompletionCategory::Quests},
        {115, CompletionCategory::Quests},    {116, CompletionCategory::Quests},
        {118, CompletionCategory::Quests},    {119, CompletionCategory::Quests},
        {120, CompletionCategory::Quests},    {121, CompletionCategory::Quests},
        {122, CompletionCategory::Quests},    {124, CompletionCategory::Quests},
        {125, CompletionCategory::Quests},    {126, CompletionCategory::Quests},
        {128, CompletionCategory::Quests},    {129, CompletionCategory::Quests},
        {130, CompletionCategory::Quests},    {131, CompletionCategory::Quests},
        {134, CompletionCategory::Quests},    {136, CompletionCategory::Quests},
        {137, CompletionCategory::Quests},    {138, CompletionCategory::Quests},
        {139, CompletionCategory::Quests},    {140, CompletionCategory::Quests},
        {141, CompletionCategory::Quests},    {143, CompletionCategory::Quests},
        {144, CompletionCategory::Quests},    {145, CompletionCategory::Quests},
        {162, CompletionCategory::Quests},    {163, CompletionCategory::Quests},
        {164, CompletionCategory::Quests},    {165, CompletionCategory::Quests},
        {166, CompletionCategory::Quests},    {200, CompletionCategory::Quests},
        {201, CompletionCategory::Quests},    {204, CompletionCategory::Quests},
        {219, CompletionCategory::Quests},    {235, CompletionCategory::Quests},
        {2, CompletionCategory::Awards},      {3, CompletionCategory::Awards},
        {4, CompletionCategory::Awards},      {5, CompletionCategory::Awards},
        {6, CompletionCategory::Awards},      {7, CompletionCategory::Awards},
        {8, CompletionCategory::Awards},      {9, CompletionCategory::Awards},
        {10, CompletionCategory::Awards},     {11, CompletionCategory::Awards},
        {12, CompletionCategory::Awards},     {13, CompletionCategory::Awards},
        {14, CompletionCategory::Awards},     {15, CompletionCategory::Awards},
        {16, CompletionCategory::Awards},     {17, CompletionCategory::Awards},
        {18, CompletionCategory::Awards},     {19, CompletionCategory::Awards},
        {20, CompletionCategory::Awards},     {21, CompletionCategory::Awards},
        {22, CompletionCategory::Awards},     {23, CompletionCategory::Awards},
        {24, CompletionCategory::Awards},     {25, CompletionCategory::Awards},
        {26, CompletionCategory::Awards},     {27, CompletionCategory::Awards},
        {28, CompletionCategory::Awards},     {29, CompletionCategory::Awards},
        {30, CompletionCategory::Awards},     {31, CompletionCategory::Awards},
        {32, CompletionCategory::Awards},     {33, CompletionCategory::Awards},
        {34, CompletionCategory::Awards},     {36, CompletionCategory::Awards},
        {37, CompletionCategory::Awards},     {38, CompletionCategory::Awards},
        {39, CompletionCategory::Awards},     {40, CompletionCategory::Awards},
        {41, CompletionCategory::Awards},     {42, CompletionCategory::Awards},
        {43, CompletionCategory::Awards},     {45, CompletionCategory::Awards},
        {46, CompletionCategory::Awards},     {47, CompletionCategory::Awards},
        {48, CompletionCategory::Awards},     {49, CompletionCategory::Awards},
        {50, CompletionCategory::Awards},     {51, CompletionCategory::Awards},
        {52, CompletionCategory::Awards},     {53, CompletionCategory::Awards},
        {54, CompletionCategory::Awards},     {55, CompletionCategory::Awards},
        {56, CompletionCategory::Awards},     {57, CompletionCategory::Awards},
        {58, CompletionCategory::Awards},     {59, CompletionCategory::Awards},
        {60, CompletionCategory::Awards},     {61, CompletionCategory::Awards},
        {1, CompletionCategory::Autonotes},   {2, CompletionCategory::Autonotes},
        {3, CompletionCategory::Autonotes},   {11, CompletionCategory::Autonotes},
        {12, CompletionCategory::Autonotes},  {13, CompletionCategory::Autonotes},
        {14, CompletionCategory::Autonotes},  {17, CompletionCategory::Autonotes},
        {18, CompletionCategory::Autonotes},  {19, CompletionCategory::Autonotes},
        {20, CompletionCategory::Autonotes},  {21, CompletionCategory::Autonotes},
        {22, CompletionCategory::Autonotes},  {23, CompletionCategory::Autonotes},
        {24, CompletionCategory::Autonotes},  {25, CompletionCategory::Autonotes},
        {26, CompletionCategory::Autonotes},  {27, CompletionCategory::Autonotes},
        {28, CompletionCategory::Autonotes},  {29, CompletionCategory::Autonotes},
        {30, CompletionCategory::Autonotes},  {31, CompletionCategory::Autonotes},
        {32, CompletionCategory::Autonotes},  {33, CompletionCategory::Autonotes},
        {34, CompletionCategory::Autonotes},  {35, CompletionCategory::Autonotes},
        {36, CompletionCategory::Autonotes},  {37, CompletionCategory::Autonotes},
        {38, CompletionCategory::Autonotes},  {39, CompletionCategory::Autonotes},
        {40, CompletionCategory::Autonotes},  {41, CompletionCategory::Autonotes},
        {42, CompletionCategory::Autonotes},  {43, CompletionCategory::Autonotes},
        {44, CompletionCategory::Autonotes},  {45, CompletionCategory::Autonotes},
        {46, CompletionCategory::Autonotes},  {47, CompletionCategory::Autonotes},
        {48, CompletionCategory::Autonotes},  {49, CompletionCategory::Autonotes},
        {50, CompletionCategory::Autonotes},  {51, CompletionCategory::Autonotes},
        {52, CompletionCategory::Autonotes},  {53, CompletionCategory::Autonotes},
        {79, CompletionCategory::Autonotes},  {80, CompletionCategory::Autonotes},
        {81, CompletionCategory::Autonotes},  {82, CompletionCategory::Autonotes},
        {83, CompletionCategory::Autonotes},  {84, CompletionCategory::Autonotes},
        {85, CompletionCategory::Autonotes},  {86, CompletionCategory::Autonotes},
        {87, CompletionCategory::Autonotes},  {88, CompletionCategory::Autonotes},
        {89, CompletionCategory::Autonotes},  {90, CompletionCategory::Autonotes},
        {91, CompletionCategory::Autonotes},  {92, CompletionCategory::Autonotes},
        {93, CompletionCategory::Autonotes},  {94, CompletionCategory::Autonotes},
        {95, CompletionCategory::Autonotes},  {96, CompletionCategory::Autonotes},
        {98, CompletionCategory::Autonotes},  {99, CompletionCategory::Autonotes},
        {100, CompletionCategory::Autonotes}, {101, CompletionCategory::Autonotes},
        {102, CompletionCategory::Autonotes}, {103, CompletionCategory::Autonotes},
        {104, CompletionCategory::Autonotes}, {105, CompletionCategory::Autonotes},
        {106, CompletionCategory::Autonotes}, {107, CompletionCategory::Autonotes},
        {108, CompletionCategory::Autonotes}, {109, CompletionCategory::Autonotes},
        {110, CompletionCategory::Autonotes}, {111, CompletionCategory::Autonotes},
        {112, CompletionCategory::Autonotes}, {113, CompletionCategory::Autonotes},
        {114, CompletionCategory::Autonotes}, {115, CompletionCategory::Autonotes},
        {116, CompletionCategory::Autonotes}, {117, CompletionCategory::Autonotes},
        {118, CompletionCategory::Autonotes}, {119, CompletionCategory::Autonotes},
        {120, CompletionCategory::Autonotes}, {121, CompletionCategory::Autonotes},
        {122, CompletionCategory::Autonotes}, {123, CompletionCategory::Autonotes},
        {124, CompletionCategory::Autonotes}, {125, CompletionCategory::Autonotes},
        {126, CompletionCategory::Autonotes}, {127, CompletionCategory::Autonotes},
        {128, CompletionCategory::Autonotes},
    };
    return manifest;
}

int CompletionReport::done() const noexcept {
    return quests.done + awards.done + autonotes.done;
}

int CompletionReport::total() const noexcept {
    return quests.total + awards.total + autonotes.total;
}

int CompletionReport::percent() const noexcept {
    const int all = total();
    if (all == 0) {
        return 0;
    }
    return done() * 100 / all;
}

CompletionReport audit_completion(const std::vector<CompletionEntry>& manifest,
                                  const std::set<int>& resolved_quests, const std::set<int>& awards,
                                  const std::set<int>& autonotes) {
    const auto holds = [&resolved_quests, &awards,
                        &autonotes](const CompletionEntry& entry) -> bool {
        switch (entry.category) {
        case CompletionCategory::Quests:
            return resolved_quests.contains(entry.bit);
        case CompletionCategory::Awards:
            return awards.contains(entry.bit);
        case CompletionCategory::Autonotes:
            return autonotes.contains(entry.bit);
        }
        return false;
    };
    const auto line_of = [](CompletionReport& report,
                            CompletionCategory category) -> CompletionLine& {
        switch (category) {
        case CompletionCategory::Quests:
            return report.quests;
        case CompletionCategory::Awards:
            return report.awards;
        case CompletionCategory::Autonotes:
            return report.autonotes;
        }
        return report.quests;
    };

    CompletionReport report;
    for (const auto& entry : manifest) {
        CompletionLine& line = line_of(report, entry.category);
        ++line.total;
        if (holds(entry)) {
            ++line.done;
        }
    }
    return report;
}

}  // namespace starhaven::game
