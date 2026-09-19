#include "game/temporary_objects.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <limits>

using namespace starhaven;
using namespace starhaven::game;
using Catch::Approx;

namespace {
struct Resources {
    std::array<world::ObjectDescriptor, 10> objects;
    std::array<world::SpriteFrame, 2> frames;

    Resources() {
        objects[1].object_id = 1000;
        objects[1].flags = 0x194;
        objects[1].lifetime = 768;
        objects[1].radius = 2;
        objects[2] = objects[1];
        objects[2].object_id = 1050;
        objects[2].flags = 0x174;
        objects[3] = objects[1];
        objects[3].object_id = 1051;
        objects[3].flags = 0x13c;
        objects[3].sprite_frame_index = 1;
        objects[3].lifetime = 48;  // already compiled into simulation ticks
        objects[4] = objects[3];
        objects[4].object_id = 2081;
        objects[5] = objects[2];
        objects[5].object_id = 2100;
        objects[5].flags = 0x154;
        objects[6] = objects[3];
        objects[6].object_id = 2101;
        objects[6].lifetime = 80;  // deliberately different from the 1051 fixture
        objects[7] = objects[5];
        objects[7].object_id = 4070;
        objects[7].flags = 0x54;
        objects[7].lifetime = 256;
        objects[8] = objects[6];
        objects[8].object_id = 4071;
        objects[8].flags = 0x3c;
        objects[8].lifetime = 64;  // vary the resource lifetime from installed 80
        objects[9] = objects[2];
        objects[9].object_id = 8080;
        objects[9].lifetime = 24;  // explicit resource lifetime for a short test
        frames[0].flags = frames[1].flags = 4;
        frames[1].group_length = 99;  // runtime must not recompute the stored lifetime
    }
};

world::ObjectSpawnRequest request(std::uint32_t id = 1000) {
    world::ObjectSpawnRequest result;
    result.object_id = id;
    result.z = 100;
    result.speed = 128;
    result.count = 1;
    return result;
}

void spawn(TemporaryObjects& live, const Resources& resources,
           const world::ObjectSpawnRequest& req) {
    Mm6Random random{1};
    const auto result = live.spawn(req, resources.objects, resources.frames, random);
    REQUIRE(result.error == TemporarySpawnError::None);
    REQUIRE(result.created == req.count);
    REQUIRE(result.dropped == 0);
}

world::CollisionWorld floor() {
    world::CollisionWorld world;
    const std::array vertices{
        render::Vec3{-1000, 0, -1000},
        render::Vec3{1000, 0, -1000},
        render::Vec3{1000, 0, 1000},
        render::Vec3{-1000, 0, 1000},
    };
    world.add_polygon(vertices, {0, 1, 0});
    return world;
}
}  // namespace

TEST_CASE("temporary event launches preserve axes, signed speed and random consumption",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    Mm6Random random{7};
    auto req = request();
    req.x = 11;
    req.y = 22;
    req.speed = -128;
    const auto first = live.spawn(req, resources.objects, resources.frames, random);
    REQUIRE(first.created == 1);
    REQUIRE(random.state() == 7);
    REQUIRE(live.slots()[0].position.x == 11);
    REQUIRE(live.slots()[0].position.y == 100);
    REQUIRE(live.slots()[0].position.z == 22);
    REQUIRE(live.slots()[0].velocity.y == -128);
    REQUIRE(live.slots()[0].velocity.x == 0);
    REQUIRE(live.slots()[0].definition.frame == 0);  // valid group head
    req.speed = std::numeric_limits<std::int32_t>::max();
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).created == 1);
    REQUIRE(live.slots()[1].velocity.y == -1);  // original signed low word
    req.speed = 0;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).created == 1);
    REQUIRE(render::length(live.slots()[2].velocity) == 0);
    req.speed = 1000;
    req.scatter = true;
    Mm6Random expected{random.state()};
    (void)expected.next();
    (void)expected.next();
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).created == 1);
    REQUIRE(random.state() == expected.state());
    REQUIRE(live.slots()[3].velocity.y >= 706);
    REQUIRE(live.slots()[3].velocity.y <= 1000);
    req.count = 0;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).created == 0);
    REQUIRE(random.state() == expected.state());
}

TEST_CASE("temporary spawn failures leave state and random untouched", "[temporary-objects]") {
    Resources resources;
    TemporaryObjects live;
    Mm6Random random{7};
    auto req = request(1);
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::UnsupportedId);
    req = request(1050);
    resources.objects[3].object_id = 0;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::MissingDescriptor);
    req = request();
    resources.frames[0].flags = 0;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::BadFrame);
    REQUIRE(live.spawn(req, {}, {}, random).error == TemporarySpawnError::MissingDescriptor);
    REQUIRE(live.active_count() == 0);
    REQUIRE(random.state() == 7);
}

TEST_CASE("full pools consume scatter draws and reuse expired slots", "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    Mm6Random random{7};
    Mm6Random expected{7};
    auto req = request();
    req.count = 255;
    req.scatter = true;
    for (int i = 0; i < 4; ++i) {
        const auto result = live.spawn(req, resources.objects, resources.frames, random);
        REQUIRE(result.created == (i == 3 ? 235U : 255U));
        REQUIRE(result.dropped == (i == 3 ? 20U : 0U));
        for (int draw = 0; draw < 510; ++draw) {
            (void)expected.next();
        }
    }
    REQUIRE(live.active_count() == 1000);
    REQUIRE(live.slots().size() == 1000);
    REQUIRE(random.state() == expected.state());
    REQUIRE(live.advance(768, {}).expired == 1000);
    req.count = 1;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).created == 1);
    REQUIRE(live.slots().size() == 1000);
    REQUIRE(live.slots()[0].active);
    REQUIRE(live.active_count() == 1);
    live.clear();
    REQUIRE(live.slots().empty());
}

TEST_CASE("simulation ticks apply gravity only to the falling object and pause at zero",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    spawn(live, resources, request());
    spawn(live, resources, request(1050));
    REQUIRE(live.advance(0, {}).expired == 0);
    REQUIRE(live.slots()[0].age == 0);
    REQUIRE(live.slots()[0].position.y == 100);
    REQUIRE(live.advance(1, {}).detonations.empty());
    REQUIRE(live.slots()[0].velocity.y == 123);
    REQUIRE(live.slots()[0].position.y == Approx(100 + 123.0 / 128));
    REQUIRE(live.slots()[1].velocity.y == 128);
    REQUIRE(live.slots()[1].position.y == 101);
}

TEST_CASE("expiry transforms 1050 once and animation removes 1051", "[temporary-objects]") {
    Resources resources;
    resources.objects[1].lifetime = resources.objects[2].lifetime = 3;
    TemporaryObjects live;
    spawn(live, resources, request());
    spawn(live, resources, request(1050));
    const auto result = live.advance(3, {});
    REQUIRE(result.expired == 1);
    REQUIRE(result.detonations.size() == 1);
    REQUIRE(result.detonations[0].radius == 512);
    REQUIRE(live.active_count() == 1);
    REQUIRE(live.slots()[1].definition.id == 1051);
    REQUIRE(live.slots()[1].age == 0);
    REQUIRE(live.slots()[1].definition.lifetime == 48);
    REQUIRE(render::length(live.slots()[1].velocity) == 0);
    REQUIRE(live.advance(47, {}).expired == 0);
    REQUIRE(live.advance(1, {}).expired == 1);
    REQUIRE(live.advance(std::numeric_limits<std::uint32_t>::max(), {}).detonations.empty());
}

TEST_CASE("floor contact bounces 1000 and transforms 1050 without tunneling",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto falling = request();
    falling.z = 10;
    falling.speed = -30000;
    spawn(live, resources, falling);
    falling.object_id = 1050;
    spawn(live, resources, falling);
    const auto result = live.advance(1, floor());
    REQUIRE(result.bounces == 1);
    REQUIRE(result.detonations.size() == 1);
    REQUIRE(live.slots()[0].velocity.y > 0);
    REQUIRE(live.slots()[0].position.y >= -1);
    REQUIRE(live.slots()[1].definition.id == 1051);
    REQUIRE(live.slots()[1].position.y == Approx(-1));
    REQUIRE(live.advance(1, floor()).detonations.empty());
}

TEST_CASE("small floor bounces settle and resume falling when support disappears",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto falling = request();
    falling.z = -1;
    falling.speed = -10;
    spawn(live, resources, falling);
    (void)live.advance(16, floor());
    REQUIRE(live.slots()[0].resting);
    REQUIRE(live.slots()[0].velocity.y == 0);
    const auto height = live.slots()[0].position.y;
    (void)live.advance(10, floor());
    REQUIRE(live.slots()[0].position.y == height);
    (void)live.advance(1, {});
    REQUIRE_FALSE(live.slots()[0].resting);
    REQUIRE(live.slots()[0].velocity.y < 0);
}

TEST_CASE("far impacts expire without detonation and tick batching preserves trajectories",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects batch;
    TemporaryObjects split;
    auto req = request(1050);
    req.speed = 30000;
    spawn(batch, resources, req);
    spawn(split, resources, req);
    (void)batch.advance(100, {});
    for (int tick = 0; tick < 100; ++tick) {
        (void)split.advance(1, {});
    }
    REQUIRE(batch.slots()[0].position.y == split.slots()[0].position.y);
    REQUIRE(batch.slots()[0].age == split.slots()[0].age);
    const auto result = batch.advance(768, {});
    REQUIRE(result.expired == 1);
    REQUIRE(result.detonations.empty());
    REQUIRE(batch.active_count() == 0);
}

TEST_CASE("embedded launches separate fully before continuing away from a surface",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto embedded = request();
    embedded.z = -2;  // center is one unit above the floor, radius two
    spawn(live, resources, embedded);
    const auto result = live.advance(1, floor());
    REQUIRE(result.bounces == 0);
    REQUIRE(live.slots()[0].position.y > -0.1f);
    REQUIRE(live.slots()[0].velocity.y == 123);
}

TEST_CASE("a wall cannot keep a settled object aloft after its floor disappears",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto falling = request();
    falling.z = -1;
    falling.speed = -10;
    spawn(live, resources, falling);
    (void)live.advance(1, floor());
    REQUIRE(live.slots()[0].resting);
    const auto height = live.slots()[0].position.y;
    world::CollisionWorld wall;
    const std::array vertices{
        render::Vec3{0, -100, -100},
        render::Vec3{0, 100, -100},
        render::Vec3{0, 100, 100},
        render::Vec3{0, -100, 100},
    };
    wall.add_polygon(vertices, {1, 0, 0});
    (void)live.advance(1, wall);
    REQUIRE_FALSE(live.slots()[0].resting);
    REQUIRE(live.slots()[0].position.y < height);
    REQUIRE(live.slots()[0].velocity.y < 0);
}

TEST_CASE("2081 moves without gravity and expires without impact or replacement",
          "[temporary-objects]") {
    Resources resources;
    // This family requires no 1051 replacement to exist.
    resources.objects[3].object_id = 0;
    TemporaryObjects live;
    auto req = request(2081);
    req.speed = 1000;
    spawn(live, resources, req);
    REQUIRE(live.slots().front().definition.lifetime == 48);
    REQUIRE_FALSE(live.slots().front().impact_definition.has_value());
    REQUIRE(live.advance(0, {}).expired == 0);
    const auto before_expiry = live.advance(47, {});
    REQUIRE(before_expiry.detonations.empty());
    REQUIRE(before_expiry.expired == 0);
    REQUIRE(live.slots().front().velocity.y == 1000);
    REQUIRE(live.slots().front().position.y == Approx(100 + 47000.0 / 128));
    const auto expired = live.advance(1, {});
    REQUIRE(expired.expired == 1);
    REQUIRE(expired.detonations.empty());
    REQUIRE(live.active_count() == 0);

    req.speed = -30000;
    req.z = 10;
    spawn(live, resources, req);
    const auto contact = live.advance(1, floor());
    REQUIRE(contact.expired == 0);
    REQUIRE(contact.detonations.empty());
    REQUIRE(live.active_count() == 1);
    REQUIRE(live.slots().front().definition.id == 2081);
    REQUIRE(live.slots().front().resting);
    REQUIRE(live.slots().front().velocity.y == 0);
    REQUIRE(live.advance(47, floor()).expired == 1);
}

TEST_CASE("2081 validates resources and full identifiers before allocation or random draws",
          "[temporary-objects]") {
    Resources resources;
    TemporaryObjects live;
    Mm6Random random{19};
    auto req = request(2081 + 65536);
    req.scatter = true;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::UnsupportedId);
    req.object_id = 2081;
    resources.objects[4].sprite_frame_index = 2;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::BadFrame);
    resources.objects[4].object_id = 0;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::MissingDescriptor);
    REQUIRE(live.active_count() == 0);
    REQUIRE(random.state() == 19);
}

TEST_CASE("2100 falls and transforms once into a stationary resource-timed 2101",
          "[temporary-objects]") {
    Resources resources;
    TemporaryObjects live;
    auto req = request(2100);
    req.speed = 512;
    spawn(live, resources, req);
    REQUIRE(live.advance(1, {}).detonations.empty());
    REQUIRE(live.slots().front().velocity.y == 507);
    REQUIRE(live.slots().front().position.y == Approx(100 + 507.0 / 128));
    live.clear();
    req.z = 10;
    req.speed = -30000;
    spawn(live, resources, req);
    const auto hit = live.advance(1, floor());
    REQUIRE(hit.detonations.size() == 1);
    REQUIRE(hit.detonations.front().radius == 512);
    REQUIRE(hit.expired == 0);
    REQUIRE(live.slots().front().definition.id == 2101);
    REQUIRE(live.slots().front().age == 0);
    REQUIRE(live.slots().front().definition.lifetime == 80);
    REQUIRE(render::length(live.slots().front().velocity) == 0);
    const auto at = live.slots().front().position;
    // Removing all support must not make an impact animation fall again.
    REQUIRE(live.advance(79, {}).detonations.empty());
    REQUIRE(live.active_count() == 1);
    REQUIRE(render::length(live.slots().front().position - at) == 0);
    REQUIRE(live.advance(1, {}).expired == 1);
    REQUIRE(live.active_count() == 0);

    resources.objects[5].lifetime = 2;
    req = request(2100);
    spawn(live, resources, req);
    REQUIRE(live.advance(2, {}).detonations.size() == 1);
    REQUIRE(live.slots().front().definition.id == 2101);
    REQUIRE(live.advance(80, {}).expired == 1);
}

TEST_CASE("2100 validates its own replacement atomically and obeys the far-impact cutoff",
          "[temporary-objects]") {
    Resources resources;
    TemporaryObjects live;
    Mm6Random random{19};
    auto req = request(2100);
    req.scatter = true;
    resources.objects[6].object_id = 0;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::MissingDescriptor);
    REQUIRE(live.active_count() == 0);
    REQUIRE(random.state() == 19);
    resources.objects[6].object_id = 2101;
    resources.objects[6].sprite_frame_index = 2;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::BadFrame);
    REQUIRE(random.state() == 19);
    req.object_id += 65536;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::UnsupportedId);
    resources.objects[6].sprite_frame_index = 1;
    resources.objects[5].lifetime = 24;
    req = request(2100);
    req.speed = 30000;
    spawn(live, resources, req);
    const auto far = live.advance(24, {});
    REQUIRE(far.expired == 1);
    REQUIRE(far.detonations.empty());
}

TEST_CASE("4070 settles on geometry and transforms only at expiry", "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto req = request(4070);
    spawn(live, resources, req);
    REQUIRE(live.advance(1, {}).detonations.empty());
    REQUIRE(live.slots().front().velocity.y == 123);
    live.clear();
    req.z = 10;
    req.speed = -30000;
    spawn(live, resources, req);
    const auto contact = live.advance(1, floor());
    REQUIRE(contact.bounces == 1);
    REQUIRE(contact.detonations.empty());
    REQUIRE(contact.expired == 0);
    REQUIRE(live.slots().front().definition.id == 4070);
    REQUIRE(live.slots().front().resting);
    REQUIRE(render::length(live.slots().front().velocity) == 0);
    REQUIRE(live.advance(254, floor()).detonations.empty());
    REQUIRE(live.slots().front().age == 255);
    const auto timeout = live.advance(1, floor());
    REQUIRE(timeout.detonations.size() == 1);
    REQUIRE(timeout.detonations.front().radius == 512);
    REQUIRE(timeout.expired == 0);
    REQUIRE(live.slots().front().definition.id == 4071);
    REQUIRE(live.slots().front().age == 0);
    const auto at = live.slots().front().position;
    REQUIRE(live.advance(63, {}).detonations.empty());
    REQUIRE(render::length(live.slots().front().position - at) == 0);
    const auto expired = live.advance(1, {});
    REQUIRE(expired.expired == 1);
    REQUIRE(expired.detonations.empty());
    REQUIRE(live.active_count() == 0);
}

TEST_CASE("4070 reflects from walls but distant geometry removes it", "[temporary-objects]") {
    const Resources resources;
    for (const float height : {110.0f, 5300.0f}) {
        // A vertical launch into a ceiling uses the same non-floor reflection
        // as a side wall, with no dependence on the scatter generator.
        world::CollisionWorld ceiling;
        const std::array vertices{
            render::Vec3{-1000, height, -1000},
            render::Vec3{1000, height, -1000},
            render::Vec3{1000, height, 1000},
            render::Vec3{-1000, height, 1000},
        };
        ceiling.add_polygon(vertices, {0, -1, 0});
        TemporaryObjects live;
        auto req = request(4070);
        req.speed = 30000;
        spawn(live, resources, req);
        const auto contact = live.advance(height < 5000 ? 1 : 24, ceiling);
        REQUIRE(contact.detonations.empty());
        if (height < 5000) {
            REQUIRE(contact.bounces == 1);
            REQUIRE(contact.expired == 0);
            REQUIRE(live.slots().front().definition.id == 4070);
            REQUIRE(live.slots().front().velocity.y < 0);
        } else {
            REQUIRE(contact.expired == 1);
            REQUIRE(live.active_count() == 0);
        }
    }
}

TEST_CASE("4070 validates its replacement before random draws and rejects distant expiry",
          "[temporary-objects]") {
    Resources resources;
    TemporaryObjects live;
    Mm6Random random{19};
    auto req = request(4070);
    req.scatter = true;
    resources.objects[8].object_id = 0;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::MissingDescriptor);
    resources.objects[8].object_id = 4071;
    resources.objects[8].sprite_frame_index = 2;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::BadFrame);
    req.object_id += 65536;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::UnsupportedId);
    REQUIRE(live.active_count() == 0);
    REQUIRE(random.state() == 19);
    resources.objects[8].sprite_frame_index = 1;
    resources.objects[7].lifetime = 24;
    req = request(4070);
    req.speed = 30000;
    spawn(live, resources, req);
    const auto far = live.advance(24, {});
    REQUIRE(far.expired == 1);
    REQUIRE(far.detonations.empty());
}

TEST_CASE("8080 flies without gravity and removes without a replacement or detonation",
          "[temporary-objects]") {
    const Resources resources;  // intentionally has no 8081 descriptor
    TemporaryObjects live;
    auto req = request(8080);
    spawn(live, resources, req);
    REQUIRE_FALSE(live.slots().front().impact_definition);
    const auto flight = live.advance(23, {});
    REQUIRE(flight.expired == 0);
    REQUIRE(flight.detonations.empty());
    REQUIRE(live.slots().front().velocity.y == 128);
    REQUIRE(live.slots().front().position.y == Approx(123));
    const auto timeout = live.advance(1, {});
    REQUIRE(timeout.expired == 1);
    REQUIRE(timeout.detonations.empty());
    REQUIRE(live.active_count() == 0);
    REQUIRE(live.advance(1, {}).expired == 0);

    req.z = 10;
    req.speed = -30000;
    spawn(live, resources, req);
    const auto contact = live.advance(1, floor());
    REQUIRE(contact.expired == 1);
    REQUIRE(contact.bounces == 0);
    REQUIRE(contact.detonations.empty());
    REQUIRE(live.active_count() == 0);
    REQUIRE(live.slots().size() == 1);  // removed slot reused

    req = request(8080);
    req.speed = 30000;
    spawn(live, resources, req);
    const auto distant = live.advance(24, {});
    REQUIRE(distant.expired == 1);
    REQUIRE(distant.detonations.empty());
}

TEST_CASE("8080 resource rejection is atomic and full-pool attempts retain scatter draws",
          "[temporary-objects]") {
    Resources resources;
    TemporaryObjects live;
    Mm6Random random{19};
    auto req = request(8080);
    req.scatter = true;
    resources.objects[9].object_id = 0;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::MissingDescriptor);
    resources.objects[9].object_id = 8080;
    resources.objects[9].sprite_frame_index = 2;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::BadFrame);
    req.object_id += 65536;
    REQUIRE(live.spawn(req, resources.objects, resources.frames, random).error ==
            TemporarySpawnError::UnsupportedId);
    REQUIRE(live.active_count() == 0);
    REQUIRE(random.state() == 19);

    req.object_id = 8080;
    req.count = 3;
    resources.objects[9].sprite_frame_index = 0;
    const auto full =
        live.spawn(req, resources.objects, resources.frames, random, kTemporaryObjectCapacity);
    REQUIRE(full.error == TemporarySpawnError::None);
    REQUIRE(full.created == 0);
    REQUIRE(full.dropped == 3);
    Mm6Random expected{19};
    for (int draw = 0; draw < 6; ++draw)
        (void)expected.next();
    REQUIRE(random.state() == expected.state());
}

TEST_CASE("8080 actor contacts replace only accepted hits and preserve non-actor removal",
          "[temporary-objects]") {
    Resources resources;
    std::vector definitions(resources.objects.begin(), resources.objects.end());
    auto replacement = resources.objects[3];
    replacement.object_id = 8081;
    replacement.lifetime = 16;
    definitions.push_back(replacement);
    bool accepted = true;
    int calls = 0;
    std::size_t target = 99;
    const std::array bodies{
        ObjectActor{8, {0, 130, 0}, 2, 10},
        ObjectActor{7, {0, 110, 0}, 2, 10},
    };
    ObjectContacts contacts{
        bodies,
        [&](std::size_t actor) {
            ++calls;
            target = actor;
            return accepted;
        },
    };
    world::CollisionWorld collision;
    auto req = request(8080);
    req.speed = 4096;  // crosses both bodies within a single tick
    TemporaryObjects live;
    Mm6Random random{1};
    bool missing = false;
    bool blocked = false;
    bool party_hit = false;
    SECTION("accepted hit uses first body and expires at replacement lifetime") {}
    SECTION("resisted hit removes without a replacement") {
        accepted = false;
    }
    SECTION("missing replacement still applies accepted actor response") {
        definitions.pop_back();
        missing = true;
    }
    SECTION("bad replacement frame follows the missing resource path") {
        definitions.back().sprite_frame_index = 99;
        missing = true;
    }
    SECTION("geometry before actor blocks the gate") {
        const std::array ceiling{
            render::Vec3{-100, 106, -100},
            render::Vec3{-100, 106, 100},
            render::Vec3{100, 106, 100},
            render::Vec3{100, 106, -100},
        };
        collision.add_polygon(ceiling, {0, -1, 0});
        blocked = true;
    }
    SECTION("earlier party removes before actor resistance") {
        contacts.party = ObjectParty{{0, 106, 0}, 2, 10};
        blocked = party_hit = true;
    }
    SECTION("earlier actor keeps its accepted replacement despite a later party") {
        contacts.party = ObjectParty{{0, 130, 0}, 2, 10};
    }
    SECTION("actor wins a tie with the party") {
        contacts.party = ObjectParty{{0, 110, 0}, 2, 10};
    }
    SECTION("party contact works without an actor callback") {
        contacts.apply = {};
        contacts.party = ObjectParty{{0, 110, 0}, 2, 10};
        blocked = party_hit = true;
    }
    SECTION("geometry wins a party tie") {
        contacts.bodies = {};
        contacts.party = ObjectParty{{0, 110, 0}, 2, 10};
        const std::array ceiling{
            render::Vec3{-100, 110, -100},
            render::Vec3{-100, 110, 100},
            render::Vec3{100, 110, 100},
            render::Vec3{100, 110, -100},
        };
        collision.add_polygon(ceiling, {0, -1, 0});
        blocked = true;
    }
    REQUIRE(live.spawn(req, definitions, resources.frames, random).created == 1);
    const auto hit = live.advance(1, collision, nullptr, &contacts);
    REQUIRE(hit.detonations.empty());
    REQUIRE(hit.party_contacts == (party_hit ? 1U : 0U));
    REQUIRE(hit.actor_redirects == 0);
    REQUIRE(calls == (blocked ? 0 : 1));
    REQUIRE(hit.actor_contacts == (blocked ? 0U : 1U));
    REQUIRE(hit.actor_accepted == (!blocked && accepted ? 1U : 0U));
    REQUIRE(hit.missing_actor_replacements == (missing ? 1U : 0U));
    if (!blocked)
        REQUIRE(target == 7);
    if (blocked || !accepted || missing) {
        REQUIRE(hit.expired == 1);
        REQUIRE(live.active_count() == 0);
    } else {
        const auto position = live.slots()[0].position;
        REQUIRE(position.y == Approx(105));  // center at 108, expanded lower cap
        REQUIRE(live.slots()[0].definition.id == 8081);
        REQUIRE(live.slots()[0].age == 0);
        REQUIRE(live.advance(15, collision, nullptr, &contacts).expired == 0);
        REQUIRE(live.slots()[0].position.y == position.y);
        REQUIRE(calls == 1);
        REQUIRE(live.advance(1, collision, nullptr, &contacts).expired == 1);
        REQUIRE(live.active_count() == 0);
    }
}

TEST_CASE("8080 actor sweep handles misses, overlap and distance guard", "[temporary-objects]") {
    Resources resources;
    resources.objects[9].lifetime = 768;
    auto req = request(8080);
    req.speed = 32767;
    ObjectActor body{0, {0, 5200, 0}, 2, 10};
    int calls = 0;
    std::uint32_t ticks = 24;
    std::size_t expected_contacts = 1;
    SECTION("distant contact removes before any resistance draw") {}
    SECTION("sideways miss flies through the same vertical interval") {
        body.position.x = 5;
        expected_contacts = 0;
    }
    SECTION("initial overlap contacts even without movement") {
        req.speed = 0;
        body.position.y = 100;
        ticks = 1;
    }
    const ObjectContacts contacts{
        std::span{&body, 1},
        [&](std::size_t) {
            ++calls;
            return false;
        },
    };
    TemporaryObjects live;
    spawn(live, resources, req);
    const auto step = live.advance(ticks, {}, nullptr, &contacts);
    REQUIRE(step.actor_contacts == expected_contacts);
    REQUIRE(calls == (req.speed == 0 ? 1 : 0));
    REQUIRE(step.expired == expected_contacts);
    REQUIRE(step.detonations.empty());
}

TEST_CASE("actor resistance callbacks retain chronological order across frame batches",
          "[temporary-objects]") {
    const Resources resources;
    const std::array bodies{
        ObjectActor{0, {0, 110, 0}, 2, 10},
        ObjectActor{1, {100, 106, 0}, 2, 10},
    };
    std::vector<std::size_t> order;
    const ObjectContacts contacts{
        bodies,
        [&](std::size_t actor) {
            order.push_back(actor);
            return false;
        },
    };
    TemporaryObjects live;
    auto req = request(8080);
    spawn(live, resources, req);
    req.x = 100;
    spawn(live, resources, req);
    SECTION("one batch") {
        (void)live.advance(8, {}, nullptr, &contacts);
    }
    SECTION("individual ticks") {
        for (int tick = 0; tick < 8; ++tick)
            (void)live.advance(1, {}, nullptr, &contacts);
    }
    REQUIRE(order == std::vector<std::size_t>{1, 0});
    REQUIRE(live.active_count() == 0);
}

TEST_CASE("scattered 8080 sweeps actor sides without tunneling", "[temporary-objects]") {
    const Resources resources;
    auto req = request(8080);
    req.scatter = true;
    req.speed = 16000;
    TemporaryObjects live;
    spawn(live, resources, req);
    const auto& object = live.slots().front();
    const auto delta = object.velocity * (1.0f / 128);
    const auto center = object.position + render::Vec3{0, object.definition.radius + 1, 0};
    ObjectActor body{0, center + delta * 0.5f - render::Vec3{0, 1, 0}, 1, 2};
    bool hit = true;
    SECTION("crossing the side within a tick") {}
    SECTION("outside the swept cylinder") {
        const render::Vec3 side{-delta.z, 0, delta.x};
        body.position = body.position + side * (10.0f / render::length(side));
        hit = false;
    }
    SECTION("passing above the body") {
        body.position.y -= 50;
        hit = false;
    }
    int calls = 0;
    const ObjectContacts contacts{
        std::span{&body, 1},
        [&](std::size_t) {
            ++calls;
            return false;
        },
    };
    const auto step = live.advance(1, {}, nullptr, &contacts);
    REQUIRE(step.actor_contacts == (hit ? 1U : 0U));
    REQUIRE(calls == (hit ? 1 : 0));
    REQUIRE(live.active_count() == (hit ? 0U : 1U));
}

TEST_CASE("1050 and 4070 character impact transforms once without a resistance gate",
          "[temporary-objects]") {
    const auto id = GENERATE(1050U, 4070U);
    CAPTURE(id);
    const Resources resources;
    TemporaryObjects live;
    auto req = request(id);
    req.speed = 4096;
    spawn(live, resources, req);
    const std::array actors{ObjectActor{0, {0, 110, 0}, 2, 10}};
    ObjectContacts contacts{actors, {}};
    int resistance_calls = 0;
    bool party_hit = false;
    bool geometry_hit = false;
    world::CollisionWorld collision;
    SECTION("actor contact needs no resistance callback") {}
    SECTION("actor transition ignores a rejecting resistance callback") {
        contacts.apply = [&](std::size_t) {
            ++resistance_calls;
            return false;
        };
    }
    SECTION("earlier party wins over a later actor") {
        contacts.party = ObjectParty{{0, 106, 0}, 2, 10};
        party_hit = true;
    }
    SECTION("earlier actor wins over a later party") {
        contacts.party = ObjectParty{{0, 130, 0}, 2, 10};
    }
    SECTION("actor wins an actor-party tie") {
        contacts.party = ObjectParty{{0, 110, 0}, 2, 10};
    }
    SECTION("party alone is contacted") {
        contacts.bodies = {};
        contacts.party = ObjectParty{{0, 110, 0}, 2, 10};
        party_hit = true;
    }
    SECTION("geometry wins a simultaneous contact and retains its family response") {
        const std::array ceiling{
            render::Vec3{-100, 110, -100},
            render::Vec3{-100, 110, 100},
            render::Vec3{100, 110, 100},
            render::Vec3{100, 110, -100},
        };
        collision.add_polygon(ceiling, {0, -1, 0});
        geometry_hit = true;
    }
    const auto step = live.advance(1, collision, nullptr, &contacts);
    REQUIRE(step.actor_contacts == (!party_hit && !geometry_hit ? 1U : 0U));
    REQUIRE(step.party_contacts == (party_hit ? 1U : 0U));
    REQUIRE(step.actor_accepted == 0);
    REQUIRE(step.actor_redirects == 0);
    REQUIRE(resistance_calls == 0);
    REQUIRE(step.expired == 0);
    const bool preserves_flight = geometry_hit && id == 4070;
    REQUIRE(step.detonations.size() == (preserves_flight ? 0U : 1U));
    if (preserves_flight) {
        REQUIRE(live.slots().front().definition.id == 4070);
    } else {
        REQUIRE(step.detonations.front().radius == 512);
        REQUIRE(live.slots().front().definition.id == id + 1);
        REQUIRE(live.slots().front().age == 0);
        const auto position = live.slots().front().position;
        REQUIRE(render::length(live.slots().front().velocity) == 0);
        const auto lifetime = resources.objects[id == 1050 ? 3 : 8].lifetime;
        const auto flight = live.advance(lifetime - 1, collision, nullptr, &contacts);
        REQUIRE(flight.detonations.empty());
        REQUIRE(flight.actor_contacts == 0);
        REQUIRE(flight.party_contacts == 0);
        REQUIRE(render::length(live.slots().front().position - position) == 0);
        REQUIRE(live.advance(1, collision, nullptr, &contacts).expired == 1);
        REQUIRE(live.active_count() == 0);
    }
}

TEST_CASE("settled 4070 remains sensitive to bodies entering its space", "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto req = request(4070);
    req.speed = 0;
    spawn(live, resources, req);
    const auto collision = floor();
    (void)live.advance(100, collision);
    REQUIRE(live.slots().front().resting);
    REQUIRE(live.slots().front().definition.id == 4070);
    ObjectContacts contacts;
    const std::array actors{ObjectActor{0, {0, 0, 0}, 2, 10}};
    SECTION("actor enters") {
        contacts.bodies = actors;
    }
    SECTION("party enters") {
        contacts.party = ObjectParty{{0, 0, 0}, 2, 10};
    }
    REQUIRE(live.advance(0, collision, nullptr, &contacts).detonations.empty());
    REQUIRE(live.advance(1, collision, nullptr, &contacts).detonations.size() == 1);
    REQUIRE(live.slots().front().definition.id == 4071);
}

TEST_CASE("far 1050 and 4070 character contacts remove without detonation or callbacks",
          "[temporary-objects]") {
    const auto id = GENERATE(1050U, 4070U);
    CAPTURE(id);
    const Resources resources;
    TemporaryObjects live;
    auto req = request(id);
    req.speed = 32767;
    spawn(live, resources, req);
    const std::array actors{ObjectActor{0, {0, 5500, 0}, 2, 10}};
    int calls = 0;
    ObjectContacts contacts{
        actors,
        [&](std::size_t) {
            ++calls;
            return true;
        },
    };
    SECTION("actor") {}
    SECTION("party") {
        contacts.bodies = {};
        contacts.party = ObjectParty{{0, 5500, 0}, 2, 10};
    }
    const auto step = live.advance(24, {}, nullptr, &contacts);
    REQUIRE(step.actor_contacts + step.party_contacts == 1);
    REQUIRE(step.detonations.empty());
    REQUIRE(step.expired == 1);
    REQUIRE(calls == 0);
}

TEST_CASE("1000, 2081 and 2100 redirect away from actors without transforming or resetting age",
          "[temporary-objects]") {
    const auto id = GENERATE(1000U, 2081U, 2100U);
    CAPTURE(id);
    const Resources resources;
    TemporaryObjects live;
    auto req = request(id);
    req.scatter = true;
    req.speed = 32767;
    spawn(live, resources, req);
    const auto before = live.slots().front();
    const auto delta = before.velocity * (1.0f / 128);
    const std::array actors{ObjectActor{0, {delta.x * 0.25f, 0, delta.z * 0.25f}, 4, 1000}};
    int gates = 0;
    int reactions = 0;
    ObjectContacts contacts{
        actors,
        [&](std::size_t) {
            ++gates;
            return false;
        },
    };
    contacts.react_to_actor = [&](std::size_t actor) {
        REQUIRE(actor == 0);
        ++reactions;
    };
    world::CollisionWorld collision;
    bool wall = false;
    SECTION("radial redirection and damping preserve flight") {}
    SECTION("remaining movement can reach a wall in the same tick") {
        const std::array vertices{
            render::Vec3{-20, -1000, -1000},
            render::Vec3{-20, 1000, -1000},
            render::Vec3{-20, 1000, 1000},
            render::Vec3{-20, -1000, 1000},
        };
        collision.add_polygon(vertices, {1, 0, 0});
        wall = true;
    }
    const auto step = live.advance(1, collision, nullptr, &contacts);
    REQUIRE(step.actor_contacts == 1);
    REQUIRE(step.actor_redirects == 1);
    REQUIRE(step.actor_accepted == 0);
    REQUIRE(gates == 0);
    REQUIRE(reactions == 1);
    const auto& after = live.slots().front();
    if (wall && id == 2100) {
        REQUIRE(step.detonations.size() == 1);
        REQUIRE(after.definition.id == 2101);
        REQUIRE(after.age == 0);
    } else {
        REQUIRE(step.detonations.empty());
        REQUIRE(after.definition.id == id);
        REQUIRE(after.age == 1);
        REQUIRE((wall ? after.velocity.x > 0 : after.velocity.x < 0));
        REQUIRE(step.bounces == (wall ? 1U : 0U));
        REQUIRE(after.velocity.z < 0);
        constexpr float kDamping = 58500.0f / 65536.0f;
        REQUIRE(std::hypot(after.velocity.x, after.velocity.z) ==
                Approx(std::hypot(before.velocity.x, before.velocity.z) * kDamping));
        const float gravity = id == 2081 ? 0.0f : 5.0f;
        REQUIRE(after.velocity.y == Approx((before.velocity.y - gravity) * kDamping));
        REQUIRE(after.definition.lifetime == before.definition.lifetime);
        REQUIRE(after.active);
    }
}

TEST_CASE("1000, 2081 and 2100 overlapping actors react once until separated",
          "[temporary-objects]") {
    const auto id = GENERATE(1000U, 2081U, 2100U);
    CAPTURE(id);
    const Resources resources;
    TemporaryObjects live;
    spawn(live, resources, request(id));
    std::array actors{ObjectActor{0, {0, 100, 0}, 20, 200}};
    int reactions = 0;
    ObjectContacts contacts{actors, {}};
    contacts.react_to_actor = [&](std::size_t) { ++reactions; };
    REQUIRE(live.advance(1, {}, nullptr, &contacts).actor_redirects == 1);
    REQUIRE(live.advance(10, {}, nullptr, &contacts).actor_contacts == 0);
    REQUIRE(reactions == 1);
    actors.front().position.x = 1000;
    (void)live.advance(1, {}, nullptr, &contacts);
    actors.front().position.x = 0;
    REQUIRE(live.advance(1, {}, nullptr, &contacts).actor_redirects == 1);
    REQUIRE(reactions == 2);
    REQUIRE(live.slots().front().age == 13);
    REQUIRE(live.slots().front().definition.id == id);
    const auto rest = live.advance(1000, floor());
    REQUIRE(rest.detonations.size() == (id == 2100 ? 1U : 0U));
    REQUIRE(rest.expired == 1);
    if (id == 1000)
        REQUIRE(rest.bounces > 0);
}

TEST_CASE("1000 character contact beyond the impact cutoff preserves gravity and its deadline",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto req = request(1000);
    req.speed = 32767;
    spawn(live, resources, req);
    const std::array actors{ObjectActor{0, {0, 5500, 0}, 2, 10}};
    ObjectContacts contacts{actors, {}};
    int reactions = 0;
    contacts.react_to_actor = [&](std::size_t) { ++reactions; };
    bool party = false;
    SECTION("actor") {}
    SECTION("party") {
        contacts.bodies = {};
        contacts.party = ObjectParty{{0, 5500, 0}, 2, 10};
        party = true;
    }
    std::size_t hits = 0;
    const auto& object = live.slots().front();
    for (std::uint32_t tick = 1; tick <= 24 && hits == 0; ++tick) {
        const auto velocity = object.velocity.y - 5;
        const auto step = live.advance(1, {}, nullptr, &contacts);
        REQUIRE(step.expired == 0);
        REQUIRE(step.detonations.empty());
        REQUIRE(object.age == tick);
        hits += step.actor_contacts + step.party_contacts;
        if (hits != 0) {
            REQUIRE(step.actor_redirects == (party ? 0U : 1U));
            REQUIRE(step.party_contacts == (party ? 1U : 0U));
            REQUIRE(object.position.y - object.origin.y > 5020);
            REQUIRE(object.velocity.y == Approx(velocity * 58500 / 65536));
        }
    }
    REQUIRE(hits == 1);
    REQUIRE(reactions == (party ? 0 : 1));
    REQUIRE(object.definition.id == 1000);
    REQUIRE(object.definition.lifetime == 768);
    const auto velocity = object.velocity.y;
    REQUIRE(live.advance(1, {}).expired == 0);
    REQUIRE(object.velocity.y == Approx(velocity - 5));
    REQUIRE(live.advance(767 - object.age, {}).expired == 0);
    const auto expiry = live.advance(1, {});
    REQUIRE(expiry.expired == 1);
    REQUIRE(expiry.detonations.empty());
    REQUIRE_FALSE(object.touching_actor);
    REQUIRE(live.active_count() == 0);
}

TEST_CASE("a settled 1000 skips character searches and expires without replacement",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto req = request(1000);
    req.z = 0;
    req.speed = 0;
    spawn(live, resources, req);
    const auto collision = floor();
    (void)live.advance(20, collision);
    const auto& object = live.slots().front();
    REQUIRE(object.resting);
    const std::array actors{ObjectActor{0, {0, 0, 0}, 20, 160}};
    ObjectContacts contacts{actors, {}};
    SECTION("actor") {}
    SECTION("party") {
        contacts.bodies = {};
        contacts.party = ObjectParty{{0, 0, 0}, 20, 160};
    }
    contacts.react_to_actor = [](std::size_t) { FAIL("settled object must not react an actor"); };
    const auto touch = live.advance(1, collision, nullptr, &contacts);
    REQUIRE(touch.actor_contacts == 0);
    REQUIRE(touch.party_contacts == 0);
    REQUIRE(object.definition.id == 1000);
    REQUIRE(object.age == 21);
    REQUIRE(render::length(object.velocity) == 0);
    const auto rest = live.advance(746, collision, nullptr, &contacts);
    REQUIRE(rest.actor_contacts == 0);
    REQUIRE(rest.party_contacts == 0);
    REQUIRE(rest.expired == 0);
    const auto expiry = live.advance(1, collision, nullptr, &contacts);
    REQUIRE(expiry.expired == 1);
    REQUIRE(expiry.detonations.empty());
}

TEST_CASE("2081 distant character contact preserves its original expiry without an impact action",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto req = request(2081);
    req.speed = 32767;
    spawn(live, resources, req);
    const std::array actors{ObjectActor{0, {0, 5500, 0}, 2, 10}};
    int reactions = 0;
    ObjectContacts contacts{actors, {}};
    contacts.react_to_actor = [&](std::size_t) { ++reactions; };
    bool party = false;
    SECTION("actor") {}
    SECTION("party") {
        contacts.bodies = {};
        contacts.party = ObjectParty{{0, 5500, 0}, 2, 10};
        party = true;
    }
    REQUIRE(live.advance(0, {}, nullptr, &contacts).actor_contacts == 0);
    const auto hit = live.advance(24, {}, nullptr, &contacts);
    REQUIRE(hit.actor_contacts == (party ? 0U : 1U));
    REQUIRE(hit.party_contacts == (party ? 1U : 0U));
    REQUIRE(hit.actor_redirects == (party ? 0U : 1U));
    REQUIRE(reactions == (party ? 0 : 1));
    REQUIRE(hit.expired == 0);
    REQUIRE(hit.detonations.empty());
    const auto& object = live.slots().front();
    REQUIRE(object.definition.id == 2081);
    REQUIRE(object.age == 24);
    REQUIRE(object.position.y > 5500);
    REQUIRE(object.velocity.y == Approx(32767.0f * 58500 / 65536));
    REQUIRE(live.advance(23, {}, nullptr, &contacts).expired == 0);
    const auto expiry = live.advance(1, {}, nullptr, &contacts);
    REQUIRE(expiry.expired == 1);
    REQUIRE(expiry.detonations.empty());
    REQUIRE(live.active_count() == 0);
    REQUIRE(reactions == (party ? 0 : 1));
}

TEST_CASE("distant 2100 character contact removes before reaction or detonation",
          "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto req = request(2100);
    req.speed = 32767;
    spawn(live, resources, req);
    const std::array actors{ObjectActor{0, {0, 5500, 0}, 2, 10}};
    int reactions = 0;
    ObjectContacts contacts{actors, {}};
    contacts.react_to_actor = [&](std::size_t) { ++reactions; };
    SECTION("actor") {}
    SECTION("party") {
        contacts.bodies = {};
        contacts.party = ObjectParty{{0, 5500, 0}, 2, 10};
    }
    const auto step = live.advance(24, {}, nullptr, &contacts);
    REQUIRE(step.actor_contacts + step.party_contacts == 1);
    REQUIRE(step.actor_redirects == 0);
    REQUIRE(reactions == 0);
    REQUIRE(step.expired == 1);
    REQUIRE(step.detonations.empty());
}

TEST_CASE("2100 party contact replaces once and respects earlier contacts", "[temporary-objects]") {
    const Resources resources;
    TemporaryObjects live;
    auto req = request(2100);
    req.speed = 4096;
    spawn(live, resources, req);
    ObjectContacts contacts;
    contacts.party = ObjectParty{{0, 110, 0}, 2, 10};
    std::array actors{ObjectActor{0, {0, 130, 0}, 2, 10}};
    std::size_t reactions = 0;
    int resistance_calls = 0;
    contacts.react_to_actor = [&](std::size_t) { ++reactions; };
    contacts.apply = [&](std::size_t) {
        ++resistance_calls;
        return true;
    };
    std::size_t expected_actors = 0;
    bool geometry_hit = false;
    world::CollisionWorld collision;
    SECTION("party alone") {}
    SECTION("party precedes an actor") {
        contacts.bodies = actors;
    }
    SECTION("actor deflection precedes party impact in the same tick") {
        actors.front().position.y = 106;
        contacts.bodies = actors;
        expected_actors = 1;
    }
    SECTION("actor wins a tie and then the party still triggers replacement") {
        actors.front().position.y = 110;
        contacts.bodies = actors;
        expected_actors = 1;
    }
    SECTION("geometry wins a party tie") {
        const std::array ceiling{
            render::Vec3{-100, 110, -100},
            render::Vec3{-100, 110, 100},
            render::Vec3{100, 110, 100},
            render::Vec3{100, 110, -100},
        };
        collision.add_polygon(ceiling, {0, -1, 0});
        geometry_hit = true;
    }
    REQUIRE(live.advance(0, collision, nullptr, &contacts).party_contacts == 0);
    const auto hit = live.advance(1, collision, nullptr, &contacts);
    REQUIRE(hit.party_contacts == (geometry_hit ? 0U : 1U));
    REQUIRE(hit.actor_contacts == expected_actors);
    REQUIRE(hit.actor_redirects == expected_actors);
    REQUIRE(reactions == expected_actors);
    REQUIRE(resistance_calls == 0);
    REQUIRE(hit.expired == 0);
    REQUIRE(hit.detonations.size() == 1);
    REQUIRE(hit.detonations.front().radius == 512);
    REQUIRE(live.slots().front().definition.id == 2101);
    REQUIRE(live.slots().front().age == 0);
    REQUIRE(render::length(live.slots().front().velocity) == 0);
    REQUIRE_FALSE(live.slots().front().touching_actor);
    const auto position = live.slots().front().position;
    const auto rest = live.advance(79, collision, nullptr, &contacts);
    REQUIRE(rest.party_contacts == 0);
    REQUIRE(rest.actor_contacts == 0);
    REQUIRE(rest.detonations.empty());
    REQUIRE(rest.expired == 0);
    REQUIRE(render::length(live.slots().front().position - position) == 0);
    REQUIRE(live.advance(1, collision, nullptr, &contacts).expired == 1);
    REQUIRE(live.active_count() == 0);
}

TEST_CASE(
    "1000 and 2081 party contact damps all velocity without redirecting or restarting the effect",
    "[temporary-objects]") {
    const auto id = GENERATE(1000U, 2081U);
    CAPTURE(id);
    const float gravity = id == 1000 ? 5.0f : 0.0f;
    const Resources resources;
    TemporaryObjects live;
    auto req = request(id);
    req.scatter = true;
    req.speed = 4096;
    SECTION("angled flight") {}
    SECTION("stationary overlap") {
        req.speed = 0;
    }
    spawn(live, resources, req);
    const auto before = live.slots().front();
    ObjectContacts contacts;
    contacts.party = ObjectParty{{0, 100, 0}, 64, 320};
    contacts.apply = [](std::size_t) -> bool {
        FAIL("party contact must not roll actor resistance");
        return false;
    };
    contacts.react_to_actor = [](std::size_t) { FAIL("party contact must not react an actor"); };
    REQUIRE(live.advance(0, {}, nullptr, &contacts).party_contacts == 0);
    const auto hit = live.advance(1, {}, nullptr, &contacts);
    REQUIRE(hit.party_contacts == 1);
    REQUIRE(hit.actor_contacts == 0);
    REQUIRE(hit.detonations.empty());
    REQUIRE(hit.expired == 0);
    const auto& after = live.slots().front();
    const auto velocity = (before.velocity - render::Vec3{0, gravity, 0}) * (58500.0f / 65536.0f);
    REQUIRE(after.velocity.x == Approx(velocity.x));
    REQUIRE(after.velocity.y == Approx(velocity.y));
    REQUIRE(after.velocity.z == Approx(velocity.z));
    REQUIRE(render::length(after.position - before.position - velocity * (1.0f / 128)) ==
            Approx(0).margin(0.0001f));
    REQUIRE(after.definition.id == id);
    REQUIRE(after.age == 1);
    REQUIRE(after.definition.lifetime == (id == 1000 ? 768U : 48U));
    REQUIRE(after.touching_party);
    REQUIRE(live.advance(1, {}, nullptr, &contacts).party_contacts == 0);
    REQUIRE(after.velocity.y == Approx(velocity.y - gravity));
    REQUIRE(after.age == 2);
}

TEST_CASE("1000 and 2081 party overlap rearms after separation or missing context",
          "[temporary-objects]") {
    const auto id = GENERATE(1000U, 2081U);
    CAPTURE(id);
    const float gravity = id == 1000 ? 5.0f : 0.0f;
    const Resources resources;
    TemporaryObjects live;
    spawn(live, resources, request(id));
    ObjectContacts contacts;
    const ObjectParty party{{0, 100, 0}, 64, 320};
    contacts.party = party;
    REQUIRE(live.advance(1, {}, nullptr, &contacts).party_contacts == 1);
    REQUIRE(live.advance(10, {}, nullptr, &contacts).party_contacts == 0);
    SECTION("body moves away") {
        contacts.party->position.x = 1000;
        REQUIRE(live.advance(1, {}, nullptr, &contacts).party_contacts == 0);
    }
    SECTION("party context omitted") {
        contacts.party.reset();
        REQUIRE(live.advance(1, {}, nullptr, &contacts).party_contacts == 0);
    }
    SECTION("all contact context omitted") {
        REQUIRE(live.advance(1, {}).party_contacts == 0);
    }
    contacts.party = party;
    REQUIRE(live.advance(1, {}, nullptr, &contacts).party_contacts == 1);
    REQUIRE(live.slots().front().age == 13);
    constexpr float kDamping = 58500.0f / 65536.0f;
    REQUIRE(live.slots().front().velocity.y ==
            Approx(((128 - gravity) * kDamping - 12 * gravity) * kDamping));
    const std::uint32_t lifetime = id == 1000 ? 768 : 48;
    REQUIRE(live.advance(lifetime - 14, {}, nullptr, &contacts).expired == 0);
    const auto expiry = live.advance(1, {}, nullptr, &contacts);
    REQUIRE(expiry.expired == 1);
    REQUIRE(expiry.detonations.empty());
    REQUIRE_FALSE(live.slots().front().touching_party);
    // Reusing the inactive slot must permit a fresh contact with the same party.
    spawn(live, resources, request(id));
    REQUIRE(live.advance(1, {}, nullptr, &contacts).party_contacts == 1);
}

TEST_CASE("1000 and 2081 actor and party contacts remain distinct within the same tick",
          "[temporary-objects]") {
    const auto id = GENERATE(1000U, 2081U);
    CAPTURE(id);
    const float gravity = id == 1000 ? 5.0f : 0.0f;
    const Resources resources;
    TemporaryObjects live;
    auto req = request(id);
    req.speed = 4096;
    spawn(live, resources, req);
    std::array actors{ObjectActor{0, {0, 100, 0}, 2, 200}};
    ObjectContacts contacts{actors, {}};
    contacts.party = ObjectParty{{0, 110, 0}, 2, 200};
    bool party_first = false;
    SECTION("actor precedes party") {}
    SECTION("actor wins a tie") {
        contacts.party->position.y = 100;
    }
    SECTION("party precedes actor") {
        actors.front().position.y = 110;
        contacts.party->position.y = 100;
        party_first = true;
    }
    int reactions = 0;
    contacts.react_to_actor = [&](std::size_t) {
        REQUIRE(live.slots().front().touching_party == party_first);
        ++reactions;
    };
    const auto hit = live.advance(1, {}, nullptr, &contacts);
    REQUIRE(hit.actor_contacts == 1);
    REQUIRE(hit.actor_redirects == 1);
    REQUIRE(hit.party_contacts == 1);
    REQUIRE(reactions == 1);
    REQUIRE(hit.detonations.empty());
    constexpr float kDamping = 58500.0f / 65536.0f;
    REQUIRE(live.slots().front().velocity.y == Approx((4096 - gravity) * kDamping * kDamping));
    const auto overlap = live.advance(1, {}, nullptr, &contacts);
    REQUIRE(overlap.actor_contacts == 0);
    REQUIRE(overlap.party_contacts == 0);
}

TEST_CASE("1000 and 2081 party contact continues to geometry and respects geometry ties",
          "[temporary-objects]") {
    const auto id = GENERATE(1000U, 2081U);
    CAPTURE(id);
    const float gravity = id == 1000 ? 5.0f : 0.0f;
    const Resources resources;
    TemporaryObjects live;
    auto req = request(id);
    req.speed = 4096;
    spawn(live, resources, req);
    ObjectContacts contacts;
    contacts.party = ObjectParty{{0, 100, 0}, 2, 10};
    bool tied = false;
    SECTION("party before ceiling") {}
    SECTION("ceiling wins exact tie") {
        contacts.party->position.y = 110;
        tied = true;
    }
    world::CollisionWorld collision;
    const std::array ceiling{
        render::Vec3{-100, 110, -100},
        render::Vec3{-100, 110, 100},
        render::Vec3{100, 110, 100},
        render::Vec3{100, 110, -100},
    };
    collision.add_polygon(ceiling, {0, -1, 0});
    const auto hit = live.advance(1, collision, nullptr, &contacts);
    REQUIRE(hit.party_contacts == (tied ? 0U : 1U));
    REQUIRE(hit.bounces == 1);
    REQUIRE(hit.detonations.empty());
    REQUIRE(hit.expired == 0);
    REQUIRE(live.slots().front().velocity.y ==
            Approx(-(4096 - gravity) * (tied ? 1.0f : 58500.0f / 65536.0f)));
    REQUIRE(live.slots().front().age == 1);
    REQUIRE(live.slots().front().definition.id == id);
}

TEST_CASE("party contacts remain gated by object family and impact flag", "[temporary-objects]") {
    Resources resources;
    resources.objects[2].flags &= ~0x40U;
    resources.objects[5].flags &= ~0x40U;
    resources.objects[9].flags &= ~0x40U;
    ObjectContacts contacts;
    contacts.party = ObjectParty{{0, 100, 0}, 64, 320};
    for (const auto id : {1050U, 8080U, 2100U}) {
        TemporaryObjects live;
        spawn(live, resources, request(id));
        const auto step = live.advance(1, {}, nullptr, &contacts);
        REQUIRE(step.party_contacts == 0);
        REQUIRE(step.detonations.empty());
        REQUIRE(live.slots().front().definition.id == id);
    }
}

TEST_CASE("8080 party removal needs no replacement even at distant contacts",
          "[temporary-objects]") {
    const Resources resources;  // deliberately no 8081 descriptor
    TemporaryObjects live;
    auto req = request(8080);
    ObjectContacts contacts;
    contacts.party = ObjectParty{{0, 100, 0}, 2, 10};
    std::uint32_t ticks = 1;
    SECTION("initial overlap") {
        req.speed = 0;
    }
    SECTION("distant party") {
        req.speed = 32767;
        contacts.party->position.y = 5500;
        ticks = 23;
    }
    spawn(live, resources, req);
    REQUIRE(live.advance(0, {}, nullptr, &contacts).party_contacts == 0);
    const auto hit = live.advance(ticks, {}, nullptr, &contacts);
    REQUIRE(hit.party_contacts == 1);
    REQUIRE(hit.actor_contacts == 0);
    REQUIRE(hit.missing_actor_replacements == 0);
    REQUIRE(hit.detonations.empty());
    REQUIRE(hit.expired == 1);
    REQUIRE(live.active_count() == 0);
    const auto later = live.advance(100, {}, nullptr, &contacts);
    REQUIRE(later.party_contacts == 0);
    REQUIRE(later.expired == 0);
    REQUIRE(later.detonations.empty());
}
