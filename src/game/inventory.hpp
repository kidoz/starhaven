#ifndef STARHAVEN_GAME_INVENTORY_HPP
#define STARHAVEN_GAME_INVENTORY_HPP

// What a character is carrying, on a grid of cells.
//
// An item's inventory icon lives in `icons.lod` under the picture name its
// `ITEMS.TXT` row gives, and all 229 of them resolve. The icons are 9 to 140
// pixels wide and 12 to 289 tall — no multiple of anything — so how many cells
// an item covers has to be a rule, and no table states one. See
// docs/formats/items.md.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/assets/asset_cache.hpp"
#include "core/data/item_stats.hpp"
#include "core/world/map_session.hpp"

namespace starhaven::game {

// The grid a character's pack is, in cells. This engine's size. `inferred`
inline constexpr int kPackWidth = 14;
inline constexpr int kPackHeight = 9;

// And how many pixels a cell is. `inferred`
inline constexpr int kCellSize = 32;

// How many cells an icon of this size covers.
//
// Not a plain ceiling: an icon is allowed to overhang by up to 14 pixels
// before it claims another cell. That threshold is what makes **all 229** item
// icons fit a pack this size; rounding up outright leaves one that cannot be
// carried at all. Chosen to fit rather than read from anywhere. `inferred`
inline constexpr int kCellOverhang = 14;

[[nodiscard]] constexpr int cells_across(int pixels) noexcept {
    if (pixels <= 0) {
        return 0;
    }
    const int cells = (pixels - kCellOverhang) / kCellSize + 1;
    return cells < 1 ? 1 : cells;
}

// One thing in a pack, at the top-left cell it occupies.
struct PackedItem {
    int item_id = 0;
    int x = 0;
    int y = 0;
    int width = 1;  // in cells
    int height = 1;

    // Whether its wearer knows what it is. Loot with an `ID/Rep/St`
    // difficulty arrives unknown, shown by its table's own unidentified
    // name until the Identify skill, a Scholar or a shop reveals it.
    bool identified = true;

    // The enchantment the generator rolled, carried with the thing: the
    // standard bonus row and its strength, or the special bonus row.
    int standard_bonus = 0;
    int standard_strength = 0;
    int special_bonus = 0;
    int charges = 0;  // a wand's casts, the generator's roll
};

// One character's pack.
class Pack {
public:
    // Put an item in the first place it fits, reading left to right and top to
    // bottom. Returns false when there is no such place, which is the only
    // reason a pick-up fails.
    bool add(int item_id, int width, int height, bool identified = true,
             int standard_bonus = 0, int standard_strength = 0, int special_bonus = 0,
             int charges = 0) {
        if (item_id <= 0 || width <= 0 || height <= 0 || width > kPackWidth ||
            height > kPackHeight) {
            return false;
        }
        for (int y = 0; y + height <= kPackHeight; ++y) {
            for (int x = 0; x + width <= kPackWidth; ++x) {
                if (free_at(x, y, width, height)) {
                    items_.push_back({item_id, x, y, width, height, identified, standard_bonus,
                                      standard_strength, special_bonus, charges});
                    return true;
                }
            }
        }
        return false;
    }

    // Reveal the item at a cell; false when nothing unknown is there.
    bool identify_at(int x, int y) {
        for (auto& item : items_) {
            if (x >= item.x && x < item.x + item.width && y >= item.y &&
                y < item.y + item.height && !item.identified) {
                item.identified = true;
                return true;
            }
        }
        return false;
    }

    // Put an item back exactly where it was, which is what a load does.
    // Refuses what would overlap or overflow, like any other placement.
    bool place(const PackedItem& item) {
        if (item.item_id <= 0 || item.x < 0 || item.y < 0 || item.width <= 0 ||
            item.height <= 0 || item.x + item.width > kPackWidth ||
            item.y + item.height > kPackHeight ||
            !free_at(item.x, item.y, item.width, item.height)) {
            return false;
        }
        items_.push_back(item);
        return true;
    }
    bool place(int item_id, int x, int y, int width, int height, bool identified = true) {
        PackedItem item;
        item.item_id = item_id;
        item.x = x;
        item.y = y;
        item.width = width;
        item.height = height;
        item.identified = identified;
        return place(item);
    }

    [[nodiscard]] const std::vector<PackedItem>& items() const noexcept { return items_; }
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

    // The item occupying a cell, or nullptr. The whole rectangle answers, not
    // just the corner it was placed at.
    [[nodiscard]] const PackedItem* at(int x, int y) const noexcept {
        for (const auto& item : items_) {
            if (x >= item.x && x < item.x + item.width && y >= item.y && y < item.y + item.height) {
                return &item;
            }
        }
        return nullptr;
    }

    // Take out whatever covers a cell. Returns its item id, or 0.
    int remove(int x, int y) {
        for (auto it = items_.begin(); it != items_.end(); ++it) {
            if (x >= it->x && x < it->x + it->width && y >= it->y && y < it->y + it->height) {
                const int id = it->item_id;
                items_.erase(it);
                return id;
            }
        }
        return 0;
    }

    void clear() noexcept { items_.clear(); }

private:
    [[nodiscard]] bool free_at(int x, int y, int width, int height) const noexcept {
        for (int dy = 0; dy < height; ++dy) {
            for (int dx = 0; dx < width; ++dx) {
                if (at(x + dx, y + dy) != nullptr) {
                    return false;
                }
            }
        }
        return true;
    }

    std::vector<PackedItem> items_;
};

// How near the party has to be to something on the ground to pick it up, in
// world units. Half a terrain tile. `inferred`
inline constexpr float kPickUpRange = 256.0f;

// And how far above or below. The camera is the party's eyes, not its feet, so
// a thing on the ground is always some way below it; comparing in three
// dimensions makes the party unable to pick up anything it is standing on.
inline constexpr float kPickUpHeight = 512.0f;

// Take whatever the party is standing over, into the first pack it fits.
//
// The object leaves the map only if somebody can carry it: a full party walks
// over things and leaves them there, which is what the original does and is
// also the only honest thing to do without a place to put them.
//
// Returns what was picked up, for the message line, or empty.
template <std::size_t N>
std::string take_nearby(world::MapSession& session, const data::ItemStatsTable& items,
                        assets::AssetCache& cache, const render::Vec3& party,
                        std::array<Pack, N>& packs) {
    std::string taken;
    for (std::size_t i = 0; i < session.objects.size();) {
        const auto& object = session.objects[i];
        const float dx = object.position.x - party.x;
        const float dy = object.position.y - party.y;
        const float dz = object.position.z - party.z;
        const data::ItemStatsEntry* row =
            object.item_id > 0 ? items.at(static_cast<std::size_t>(object.item_id)) : nullptr;
        if (row == nullptr || dx * dx + dz * dz > kPickUpRange * kPickUpRange ||
            dy > kPickUpHeight || dy < -kPickUpHeight) {
            ++i;
            continue;
        }

        // How much room it needs is how big its picture is. An item whose art
        // is missing takes one cell rather than none: it is still an item.
        const render::Texture& icon = cache.icon(row->picture);
        const int width = std::max(1, cells_across(static_cast<int>(icon.width())));
        const int height = std::max(1, cells_across(static_cast<int>(icon.height())));

        bool carried = false;
        for (auto& pack : packs) {
            if (pack.add(object.item_id, width, height)) {
                carried = true;
                break;
            }
        }
        if (!carried) {
            ++i;
            continue;
        }
        taken = data::cp1252_to_utf8(row->name);
        session.objects.erase(session.objects.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return taken;
}

}  // namespace starhaven::game

#endif  // STARHAVEN_GAME_INVENTORY_HPP
