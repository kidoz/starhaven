#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core/image/palette.hpp"
#include "core/image/sprite.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"
#include "core/world/monster_list.hpp"
#include "core/world/sprite_frame_table.hpp"

namespace {

using namespace starhaven;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <--list | --check | <group>>\n"
              << "\n"
              << "Reads DSFT.BIN, the sprite frame table, from your own legal\n"
              << "installation's icons.lod.\n"
              << "\n"
              << "  --list          list the animation groups\n"
              << "  --check         verify the table against itself, SPRITES.LOD\n"
              << "                  and DMONLIST.BIN\n"
              << "  <group>         list one animation's frames\n"
              << "  --rows N        limit a listing to N rows\n"
              << "\n"
              << "Set " << platform::kInstallEnvVar << " to the install directory.\n";
}

bool load(const std::filesystem::path& data_dir, lod::LodArchive& icons,
          world::SpriteFrameTable& table) {
    if (lod::LodArchive::open(data_dir / "icons.lod", icons) != lod::LodError::None) {
        std::cerr << "error: could not open icons.lod\n";
        return false;
    }
    std::span<const std::byte> raw;
    if (icons.payload("DSFT.BIN", raw) != lod::LodArchive::PayloadError::None) {
        std::cerr << "error: icons.lod has no DSFT.BIN\n";
        return false;
    }
    if (const world::SpriteFrameError e = world::SpriteFrameTable::parse(raw, table);
        e != world::SpriteFrameError::None) {
        std::cerr << "error: could not parse DSFT.BIN (code " << static_cast<int>(e) << ")\n";
        return false;
    }
    return true;
}

int do_list(const world::SpriteFrameTable& table, std::size_t limit) {
    std::cout << table.size() << " frames in " << table.group_count() << " groups\n";
    std::size_t shown = 0;
    for (const auto& f : table.frames()) {
        if (f.group_name.empty())
            continue;
        if (shown++ >= limit)
            break;
        const auto frames = table.group(f.group_name);
        std::cout << "  " << f.group_name << "\t" << frames.size() << " frames\t" << f.group_length
                  << " ticks\tscale " << f.scale_factor()
                  << (f.view_directional() ? "\t5 views" : "") << "\n";
    }
    if (shown < table.group_count()) {
        std::cout << "... " << (table.group_count() - shown) << " more groups\n";
    }
    return 0;
}

int do_group(const world::SpriteFrameTable& table, const std::string& name) {
    const auto frames = table.group(name);
    if (frames.empty()) {
        std::cerr << "error: no animation named " << name << "\n";
        return 1;
    }
    std::cout << name << ": " << frames.size() << " frames, " << frames.front().group_length
              << " ticks, scale " << frames.front().scale_factor()
              << (frames.front().view_directional() ? ", 5 view directions" : "") << "\n";
    for (const auto& f : frames) {
        std::cout << "  " << world::SpriteFrameTable::sprite_entry(f, 0) << "\t" << f.duration
                  << " ticks\tflags 0x" << std::hex << f.flags << std::dec << "\n";
    }
    return 0;
}

// Everything that can be checked without trusting the decode: the table's own
// redundancy, then its two joins.
int do_check(const std::filesystem::path& data_dir, lod::LodArchive& icons,
             const world::SpriteFrameTable& table) {
    int failures = 0;

    // 1. The flag bit that marks a group's first frame must agree with the
    //    frame carrying a group name, and the durations must sum to the
    //    declared length. Segment by position rather than by name: five names
    //    occur twice, so a name lookup would compare the wrong group.
    std::size_t flag_agrees = 0;
    std::size_t sums = 0;
    std::size_t groups = 0;
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& f = table.frames()[i];
        if (f.starts_group() == !f.group_name.empty())
            ++flag_agrees;
        if (f.group_name.empty())
            continue;
        ++groups;
        std::uint32_t total = 0;
        for (std::size_t j = i; j < table.size(); ++j) {
            if (j > i && !table.frames()[j].group_name.empty())
                break;
            total += table.frames()[j].duration;
        }
        if (total == f.group_length)
            ++sums;
    }
    std::cout << "flag 0x4 agrees with a named frame: " << flag_agrees << "/" << table.size()
              << "\n";
    std::cout << "durations sum to the group length: " << sums << "/" << groups << "\n";
    failures += (flag_agrees != table.size()) + (sums != groups);

    // Duplicate group names: a name lookup can only reach the first of them.
    std::vector<std::string> seen;
    std::vector<std::string> duplicates;
    for (const auto& f : table.frames()) {
        if (f.group_name.empty())
            continue;
        if (std::find(seen.begin(), seen.end(), f.group_name) != seen.end()) {
            duplicates.push_back(f.group_name);
        } else {
            seen.push_back(f.group_name);
        }
    }
    std::cout << "group names used more than once: " << duplicates.size();
    for (const auto& d : duplicates)
        std::cout << " " << d;
    std::cout << "\n";

    // 2. The file's own alphabetical index must name exactly the groups we
    //    found, in case-insensitive order.
    std::vector<std::string> sorted;
    for (const auto& f : table.frames()) {
        if (!f.group_name.empty())
            sorted.push_back(f.group_name);
    }
    std::sort(sorted.begin(), sorted.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](char x, char y) {
                                                return std::tolower(static_cast<unsigned char>(x)) <
                                                       std::tolower(static_cast<unsigned char>(y));
                                            });
    });
    std::size_t lookup_ok = 0;
    for (std::size_t i = 0; i < table.lookup().size() && i < sorted.size(); ++i) {
        if (table.frames()[table.lookup()[i]].group_name == sorted[i])
            ++lookup_ok;
    }
    std::cout << "lookup array matches the alphabetical group order: " << lookup_ok << "/"
              << table.lookup().size() << "\n";
    failures += (lookup_ok != table.lookup().size());

    // 3. Every frame's sprite must exist in SPRITES.LOD — under the plain name
    //    for a single-view frame, under name+digit for a directional one.
    lod::LodArchive sprites;
    if (lod::LodArchive::open(data_dir / "SPRITES.LOD", sprites) == lod::LodError::None) {
        std::size_t resolved = 0;
        std::size_t directional = 0;
        std::size_t missing = 0;
        std::size_t palette_agrees = 0;
        std::size_t palette_seen = 0;
        for (const auto& f : table.frames()) {
            if (f.sprite_name.empty())
                continue;
            if (f.view_directional())
                ++directional;
            const std::string entry = world::SpriteFrameTable::sprite_entry(f, 0);
            std::span<const std::byte> bytes;
            if (sprites.payload(entry, bytes) != lod::LodArchive::PayloadError::None) {
                ++missing;
                continue;
            }
            ++resolved;
            image::SpriteHeader header;
            if (image::read_sprite_header(bytes, header) == image::SpriteError::None) {
                ++palette_seen;
                if (header.palette_id == f.palette_id)
                    ++palette_agrees;
            }
        }
        std::cout << "frames whose sprite resolves: " << resolved << "/" << table.size() << " ("
                  << directional << " directional, " << missing << " with no art)\n";
        std::cout << "field +0x30 equals the sprite's own palette id: " << palette_agrees << "/"
                  << palette_seen << "\n";
    }

    // 4. Every animation name in DMONLIST.BIN must be a group here. This is
    //    the join that makes monsters animate.
    std::span<const std::byte> raw;
    world::MonsterList monsters;
    if (icons.payload("DMONLIST.BIN", raw) == lod::LodArchive::PayloadError::None &&
        world::MonsterList::parse(raw, monsters) == world::MonsterListError::None) {
        std::size_t named = 0;
        std::size_t found = 0;
        for (const auto& m : monsters.entries()) {
            for (const auto& a : m.animations) {
                if (a.empty())
                    continue;
                ++named;
                if (!table.group(a).empty())
                    ++found;
            }
        }
        std::cout << "DMONLIST animation names that are groups: " << found << "/" << named << "\n";
    }
    return failures == 0 ? 0 : 1;
}

// Research mode: are the five views of a directional sprite compass headings,
// or angles relative to whoever is looking?
//
// A front or back view of a two-legged, two-armed creature is close to
// left-right symmetric; a profile is not. Measuring symmetry per view index
// answers the question without looking at a single picture.
int do_views(const std::filesystem::path& data_dir, const world::SpriteFrameTable& table) {
    lod::LodArchive sprites;
    if (lod::LodArchive::open(data_dir / "SPRITES.LOD", sprites) != lod::LodError::None) {
        std::cerr << "error: cannot open SPRITES.LOD\n";
        return 1;
    }
    // Only the silhouette matters, so any palette will do.
    image::Palette palette{};

    std::array<double, world::kSpriteViewCount> total{};
    std::array<std::size_t, world::kSpriteViewCount> counted{};
    std::size_t groups = 0;

    for (const auto& frame : table.frames()) {
        if (!frame.view_directional()) {
            continue;
        }
        std::array<double, world::kSpriteViewCount> score{};
        bool complete = true;
        for (int view = 0; view < world::kSpriteViewCount && complete; ++view) {
            std::span<const std::byte> raw;
            image::Sprite decoded;
            if (sprites.payload(world::SpriteFrameTable::sprite_entry(frame, view), raw) !=
                    lod::LodArchive::PayloadError::None ||
                image::decode_sprite(raw, palette, decoded) != image::SpriteError::None ||
                decoded.width == 0) {
                complete = false;
                break;
            }
            // The fraction of opaque pixels whose mirror image is also opaque.
            std::size_t opaque = 0;
            std::size_t mirrored = 0;
            for (int y = 0; y < decoded.height; ++y) {
                for (int x = 0; x < decoded.width; ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * decoded.width + x) * 4 + 3;
                    if (decoded.rgba[i] == 0) {
                        continue;
                    }
                    ++opaque;
                    const int mx = decoded.width - 1 - x;
                    const std::size_t j =
                        (static_cast<std::size_t>(y) * decoded.width + mx) * 4 + 3;
                    mirrored += decoded.rgba[j] != 0 ? 1 : 0;
                }
            }
            score[static_cast<std::size_t>(view)] =
                opaque == 0 ? 0.0 : static_cast<double>(mirrored) / static_cast<double>(opaque);
        }
        if (!complete) {
            continue;
        }
        ++groups;
        for (int view = 0; view < world::kSpriteViewCount; ++view) {
            total[static_cast<std::size_t>(view)] += score[static_cast<std::size_t>(view)];
            ++counted[static_cast<std::size_t>(view)];
        }
    }

    std::cout << groups << " directional frames have all five views\n";
    for (int view = 0; view < world::kSpriteViewCount; ++view) {
        const auto i = static_cast<std::size_t>(view);
        std::cout << "  view " << view << ": mean symmetry "
                  << (counted[i] == 0 ? 0.0 : total[i] / static_cast<double>(counted[i])) << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string command;
    std::size_t limit = 20;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--rows" && i + 1 < argc) {
            limit = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (command.empty()) {
            command = a;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (command.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    const auto install = platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << platform::kInstallEnvVar << "\n";
        return 1;
    }
    const std::filesystem::path data_dir = *install / "data";

    lod::LodArchive icons;
    world::SpriteFrameTable table;
    if (!load(data_dir, icons, table)) {
        return 1;
    }

    if (command == "--list")
        return do_list(table, limit);
    if (command == "--check")
        return do_check(data_dir, icons, table);
    if (command == "--views")
        return do_views(data_dir, table);
    if (command.rfind("--", 0) == 0) {
        print_usage(argv[0]);
        return 2;
    }
    return do_group(table, command);
}
