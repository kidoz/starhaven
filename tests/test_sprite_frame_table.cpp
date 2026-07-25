// Tests for the DSFT.BIN sprite frame table parser.
//
// Hermetic: every fixture is synthesized from the format described in
// docs/formats/dsft.md. No bytes are copied from a game archive.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include "core/world/sprite_frame_table.hpp"

using namespace starhaven::world;

namespace {

// Flag bits the format defines (see docs/formats/dsft.md).
constexpr std::uint32_t kNext = 0x1;
constexpr std::uint32_t kFirst = 0x4;
constexpr std::uint32_t kSingleView = 0x10;
constexpr std::uint32_t kDirectional = 0xE000;

struct Frame {
    std::string group;  // empty on a continuation frame
    std::string sprite;
    std::int32_t scale = 65536;
    std::uint32_t flags = 0;
    std::uint16_t palette = 0;
    std::uint16_t duration = 0;
    std::uint16_t length = 0;  // group total, first frame only
};

void put_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}

void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
}

void put_name(std::vector<std::uint8_t>& v, const std::string& s) {
    for (std::size_t i = 0; i < kSpriteFrameNameSize; ++i)
        v.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
}

std::vector<std::uint8_t> make_body(const std::vector<Frame>& frames,
                                    const std::vector<std::uint16_t>& lookup) {
    std::vector<std::uint8_t> v;
    put_u32(v, static_cast<std::uint32_t>(frames.size()));
    put_u32(v, static_cast<std::uint32_t>(lookup.size()));
    for (const Frame& f : frames) {
        put_name(v, f.group);
        put_name(v, f.sprite);
        for (int i = 0; i < 16; ++i)  // the runtime block, zero on disk
            v.push_back(0);
        put_u32(v, static_cast<std::uint32_t>(f.scale));
        put_u32(v, f.flags);
        put_u16(v, f.palette);
        put_u16(v, 0);
        put_u16(v, f.duration);
        put_u16(v, f.length);
    }
    for (const std::uint16_t x : lookup)
        put_u16(v, x);
    return v;
}

std::vector<std::byte> wrap(const std::vector<std::uint8_t>& body) {
    uLongf cap = compressBound(static_cast<uLong>(body.size()));
    std::vector<std::uint8_t> z(cap);
    REQUIRE(compress(z.data(), &cap, body.data(), static_cast<uLong>(body.size())) == Z_OK);
    z.resize(cap);

    std::vector<std::byte> out(48, std::byte{0});
    const char* name = "dsft.bin";
    std::memcpy(out.data(), name, std::strlen(name));
    for (const std::uint8_t b : z)
        out.push_back(static_cast<std::byte>(b));
    return out;
}

// A two-group fixture: a six-frame directional walk and a one-frame stand.
std::vector<Frame> sample_frames() {
    return {
        {"arc1wka", "arc1wkA", 45875, kFirst | kNext | kDirectional, 150, 3, 18},
        {"", "arc1wkB", 45875, kNext | kDirectional, 150, 3, 0},
        {"", "arc1wkC", 45875, kNext | kDirectional, 150, 3, 0},
        {"", "arc1wkD", 45875, kNext | kDirectional, 150, 3, 0},
        {"", "arc1wkE", 45875, kNext | kDirectional, 150, 3, 0},
        {"", "arc1wkF", 45875, kDirectional, 150, 3, 0},
        {"torch01", "torch01", 65536, kFirst | kSingleView, 12, 1, 1},
    };
}

std::vector<std::byte> sample_entry() {
    // The lookup is the groups in case-insensitive alphabetical order.
    return wrap(make_body(sample_frames(), {0, 6}));
}

}  // namespace

TEST_CASE("parses frames and their groups", "[sprite_frames]") {
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(sample_entry(), t) == SpriteFrameError::None);
    REQUIRE(t.size() == 7);
    REQUIRE(t.group_count() == 2);
    REQUIRE(t.frames()[0].group_name == "arc1wka");
    REQUIRE(t.frames()[0].sprite_name == "arc1wkA");
    REQUIRE(t.frames()[1].group_name.empty());
    REQUIRE(t.frames()[0].palette_id == 150);
    REQUIRE(t.frames()[0].duration == 3);
    REQUIRE(t.frames()[0].group_length == 18);
}

TEST_CASE("scale is 16.16 fixed point", "[sprite_frames]") {
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(sample_entry(), t) == SpriteFrameError::None);
    REQUIRE(t.frames()[6].scale_factor() == 1.0f);
    REQUIRE(t.frames()[0].scale_factor() > 0.69f);
    REQUIRE(t.frames()[0].scale_factor() < 0.71f);
}

TEST_CASE("a group runs to the next named frame", "[sprite_frames]") {
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(sample_entry(), t) == SpriteFrameError::None);
    REQUIRE(t.group("arc1wka").size() == 6);
    REQUIRE(t.group("torch01").size() == 1);
    REQUIRE(t.group("nosuch").empty());
}

TEST_CASE("group names match without regard to case", "[sprite_frames]") {
    // The shipped tables spell the same animation "ARC1STA" in one place and
    // "arc1sta" in another.
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(sample_entry(), t) == SpriteFrameError::None);
    REQUIRE(t.group("ARC1WKA").size() == 6);
    REQUIRE(t.group("Arc1WkA").size() == 6);
}

TEST_CASE("the flag bits describe the group structure", "[sprite_frames]") {
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(sample_entry(), t) == SpriteFrameError::None);
    REQUIRE(t.frames()[0].starts_group());
    REQUIRE(t.frames()[0].has_next());
    REQUIRE_FALSE(t.frames()[1].starts_group());
    REQUIRE_FALSE(t.frames()[5].has_next());  // the group's last frame
    REQUIRE(t.frames()[0].view_directional());
    REQUIRE_FALSE(t.frames()[6].view_directional());
}

TEST_CASE("animation time selects a frame and wraps", "[sprite_frames]") {
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(sample_entry(), t) == SpriteFrameError::None);

    // Six frames of three ticks each, eighteen in total.
    REQUIRE(t.frame_at("arc1wka", 0)->sprite_name == "arc1wkA");
    REQUIRE(t.frame_at("arc1wka", 2)->sprite_name == "arc1wkA");
    REQUIRE(t.frame_at("arc1wka", 3)->sprite_name == "arc1wkB");
    REQUIRE(t.frame_at("arc1wka", 17)->sprite_name == "arc1wkF");
    REQUIRE(t.frame_at("arc1wka", 18)->sprite_name == "arc1wkA");  // wraps
    REQUIRE(t.frame_at("arc1wka", 21)->sprite_name == "arc1wkB");
    REQUIRE(t.frame_at("nosuch", 0) == nullptr);
}

TEST_CASE("a group of zero length is a still image", "[sprite_frames]") {
    // Dividing by the length would be undefined; a still must come back
    // instead of crashing.
    std::vector<Frame> frames = {{"still", "still", 65536, kFirst | kSingleView, 0, 0, 0}};
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(wrap(make_body(frames, {0})), t) == SpriteFrameError::None);
    REQUIRE(t.frame_at("still", 0)->sprite_name == "still");
    REQUIRE(t.frame_at("still", 99999)->sprite_name == "still");
}

TEST_CASE("a view digit completes a directional sprite name", "[sprite_frames]") {
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(sample_entry(), t) == SpriteFrameError::None);
    REQUIRE(SpriteFrameTable::sprite_entry(t.frames()[0], 0) == "arc1wkA0");
    REQUIRE(SpriteFrameTable::sprite_entry(t.frames()[0], 4) == "arc1wkA4");
    // Out-of-range views clamp rather than build a name that cannot resolve.
    REQUIRE(SpriteFrameTable::sprite_entry(t.frames()[0], 9) == "arc1wkA4");
    REQUIRE(SpriteFrameTable::sprite_entry(t.frames()[0], -1) == "arc1wkA0");
    // A single-view frame's name is complete already.
    REQUIRE(SpriteFrameTable::sprite_entry(t.frames()[6], 3) == "torch01");
}

TEST_CASE("the lookup array is kept and bounds-checked", "[sprite_frames]") {
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(sample_entry(), t) == SpriteFrameError::None);
    REQUIRE(t.lookup().size() == 2);
    REQUIRE(t.lookup()[0] == 0);
    REQUIRE(t.lookup()[1] == 6);

    // An index past the last frame is rejected rather than followed.
    REQUIRE(SpriteFrameTable::parse(wrap(make_body(sample_frames(), {0, 99})), t) ==
            SpriteFrameError::BadLookup);
}

TEST_CASE("counts that do not account for the block are rejected", "[sprite_frames]") {
    auto body = make_body(sample_frames(), {0, 6});
    // Claim one frame more than the block holds.
    body[0] = 8;
    SpriteFrameTable t;
    REQUIRE(SpriteFrameTable::parse(wrap(body), t) == SpriteFrameError::BadCount);
}

TEST_CASE("malformed entries are rejected", "[sprite_frames]") {
    SpriteFrameTable t;
    const std::vector<std::byte> tiny(20, std::byte{0});
    REQUIRE(SpriteFrameTable::parse(tiny, t) == SpriteFrameError::TooSmall);

    std::vector<std::byte> junk(60, std::byte{0});
    REQUIRE(SpriteFrameTable::parse(junk, t) == SpriteFrameError::NotCompressed);
}
