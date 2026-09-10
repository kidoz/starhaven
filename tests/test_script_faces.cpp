#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

#include "core/world/map_session.hpp"
#include "game/inspect.hpp"
#include "game/save.hpp"
#include "game/script_faces.hpp"
#include "game/script_message.hpp"

using namespace starhaven;
using namespace starhaven::game;
using namespace starhaven::world;

namespace {
MapScript script(std::span<const ScriptStep> steps) {
    std::vector<std::byte> bytes(48);
    for (const auto& step : steps) {
        bytes.push_back(static_cast<std::byte>(4 + step.arguments.size()));
        bytes.push_back(static_cast<std::byte>(step.event_id & 0xffU));
        bytes.push_back(static_cast<std::byte>(step.event_id >> 8U));
        bytes.push_back(static_cast<std::byte>(step.sequence));
        bytes.push_back(static_cast<std::byte>(step.opcode));
        for (const auto byte : step.arguments)
            bytes.push_back(static_cast<std::byte>(byte));
    }
    MapScript result;
    REQUIRE(MapScript::parse(bytes, result) == MapScriptError::None);
    return result;
}

MapSession session() {
    MapSession map;
    map.kind = MapKind::Indoor;
    map.file_name = "Synthetic.blv";
    map.blv.vertices = {{-100, 100, -100}, {100, 100, -100}, {100, 100, 100}, {-100, 100, 100}};
    BlvFace wall;
    wall.normal_y = 65536;
    wall.vertex_count = 4;
    wall.vertex_ids = {0, 1, 2, 3};
    wall.attributes = kBlvFaceHasExtra | kFaceProjectXZ;
    wall.texture_name = "still";
    map.blv.faces.push_back(wall);
    BlvFaceExtra extra;
    extra.event_id = 5;
    map.blv.face_extras.push_back(extra);
    TextureAnimation animation;
    animation.frames = {{"FrameA", 8, true, true}, {"FrameB", 8, false, false}};
    animation.total = 16;
    map.texture_animations.push_back(animation);
    rebuild_indoor_collision(map);
    return map;
}
}  // namespace

TEST_CASE("face bit operands are bounded little endian and accept any nonzero set byte",
          "[script-faces]") {
    ScriptStep step{
        1,
        0,
        kOpcodeSetFaceBits,
        {0x78, 0x56, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x89, 255},
    };
    const auto change = parse_face_bits(step);
    if (!change) {
        FAIL("complete operand did not parse");
        return;
    }
    REQUIRE(change->index == 0x12345678U);
    REQUIRE(change->mask == 0x89abcdefU);
    REQUIRE(change->set);
    for (std::size_t length = 0; length < 9; ++length) {
        auto short_step = step;
        short_step.arguments.resize(length);
        REQUIRE_FALSE(parse_face_bits(short_step));
    }
    step.arguments.push_back(37);
    REQUIRE(parse_face_bits(step).has_value());
    step.opcode = kOpcodeSet;
    REQUIRE_FALSE(parse_face_bits(step));
}

TEST_CASE("face masks change collision and picking without touching adjacent faces",
          "[script-faces]") {
    auto map = session();
    map.blv.faces.push_back(map.blv.faces.front());
    FaceChanges memory;
    const auto before = map.blv.faces[0].attributes;
    const std::array open{FaceChange{0, 0x20000000U, true, {}}};
    REQUIRE(aimed_face(map, {0, 0, 0}, {0, 0, 1}).found());
    REQUIRE(apply_script_faces(map, open, memory) == 1);
    REQUIRE(map.blv.faces[0].ethereal());
    REQUIRE(map.blv.faces[1].attributes == before);
    REQUIRE(map.collision.size() == 1);
    REQUIRE_FALSE(aimed_face(map, {0, 0, 0}, {0, 0, 1}).found());
    const std::array close{FaceChange{0, 0x20000000U, false, {}}};
    REQUIRE(apply_script_faces(map, close, memory) == 1);
    REQUIRE(map.blv.faces[0].attributes == before);
    REQUIRE(map.collision.size() == 2);
    REQUIRE(aimed_face(map, {0, 0, 0}, {0, 0, 1}).found());
    map.kind = MapKind::Outdoor;
    REQUIRE(apply_script_faces(map, open, memory) == 0);
    REQUIRE(map.blv.faces[0].attributes == before);
    map.kind = MapKind::Indoor;
    const std::array invalid{
        FaceChange{2, 1, true, {}},
        FaceChange{std::numeric_limits<std::uint32_t>::max(), 1, true, {}},
    };
    REQUIRE(apply_script_faces(map, invalid, memory) == 0);
}

TEST_CASE("face operations preserve unrelated bits and their execution order", "[script-faces]") {
    auto map = session();
    FaceChanges memory;
    const auto before = map.blv.faces[0].attributes;
    const std::array changes{
        FaceChange{0, 0x20000010U, true, {}},
        FaceChange{0, 0x20000000U, false, {}},
        FaceChange{0, 0, false, {}},
    };
    REQUIRE(apply_script_faces(map, changes, memory) == 3);
    REQUIRE(map.blv.faces[0].attributes == (before | 0x10U));
    REQUIRE(map.collision.size() == 1);
    const std::array all{FaceChange{0, 0xffffffffU, false, {}}};
    REQUIRE(apply_script_faces(map, all, memory) == 1);
    REQUIRE(map.blv.faces[0].attributes == 0);
}

TEST_CASE("animation selection is per face and retexture follows the current mask",
          "[script-faces]") {
    auto map = session();
    FaceChanges memory;
    const auto shown = [&] {
        const auto& face = map.blv.faces[0];
        return texture_frame_name(face.texture_name, map.texture_animations, 8,
                                  (face.attributes & kFaceTextureAnimated) != 0);
    };
    const std::array start{
        FaceChange{0, kFaceTextureAnimated, true, {}},
        FaceChange{0, 0, false, "framea"},
    };
    REQUIRE(apply_script_faces(map, start, memory) == 2);
    REQUIRE(shown() == "FrameB");
    REQUIRE(texture_frame_name(map.blv.faces[0].texture_name, map.texture_animations, 8, false) ==
            "framea");
    const std::array stop{FaceChange{0, kFaceTextureAnimated, false, {}}};
    REQUIRE(apply_script_faces(map, stop, memory) == 1);
    REQUIRE(shown() == "framea");
    const std::array no_group{
        FaceChange{0, kFaceTextureAnimated, true, {}},
        FaceChange{0, 0, false, "still"},
    };
    REQUIRE(apply_script_faces(map, no_group, memory) == 2);
    REQUIRE((map.blv.faces[0].attributes & kFaceTextureAnimated) == 0);
    REQUIRE(shown() == "still");
    const std::array reverse{
        FaceChange{0, 0, false, "still"},
        FaceChange{0, kFaceTextureAnimated, true, {}},
    };
    REQUIRE(apply_script_faces(map, reverse, memory) == 2);
    REQUIRE((map.blv.faces[0].attributes & kFaceTextureAnimated) != 0);
}
