// Tests for the map session, the layer that loads either kind of map.
//
// Hermetic: these cover the decisions the session makes without game data —
// how it classifies a name, how it fails, and the axis convention every placed
// thing goes through. Loading a real map needs an installation and is covered
// by running the engine against one.
#include <catch2/catch_test_macros.hpp>

#include "core/assets/asset_cache.hpp"
#include "core/world/map_session.hpp"

using namespace starhaven;
using namespace starhaven::world;

TEST_CASE("MM6 world space maps to the renderer's Y-up space", "[map_session]") {
    // X/Y are horizontal and Z is up on disk; the renderer is Y-up. Every
    // placed thing goes through this one swap.
    const render::Vec3 v = to_render_space(10, 20, 30);
    REQUIRE(v.x == 10.0f);
    REQUIRE(v.y == 30.0f);
    REQUIRE(v.z == 20.0f);
}

TEST_CASE("a name that is neither .odm nor .blv is refused", "[map_session]") {
    assets::AssetCache cache;
    MapSession session;
    REQUIRE(load_map_session("nosuch.lod", "nosuch", "Outa1.txt", cache, session) ==
            MapSessionError::UnknownKind);
    REQUIRE(session.kind == MapKind::Unknown);
}

TEST_CASE("a missing archive is reported, not crashed on", "[map_session]") {
    assets::AssetCache cache;
    MapSession session;
    // The extension classifies before the archive is opened, so both kinds
    // reach the archive and fail there.
    REQUIRE(load_map_session("nosuch/Games.lod", "nosuch", "Outa1.Odm", cache, session) ==
            MapSessionError::NoArchive);
    REQUIRE(load_map_session("nosuch/Games.lod", "nosuch", "D01.blv", cache, session) ==
            MapSessionError::NoArchive);
}

TEST_CASE("the extension chooses the kind regardless of case", "[map_session]") {
    assets::AssetCache cache;
    MapSession session;
    // Games.lod spells these inconsistently: "OutA1.Odm" in the design table,
    // "outa1.odm" in the archive.
    REQUIRE(load_map_session("nosuch/Games.lod", "nosuch", "OUTA1.ODM", cache, session) ==
            MapSessionError::NoArchive);
    REQUIRE(session.kind == MapKind::Outdoor);
    REQUIRE(load_map_session("nosuch/Games.lod", "nosuch", "d01.BLV", cache, session) ==
            MapSessionError::NoArchive);
    REQUIRE(session.kind == MapKind::Indoor);
}

TEST_CASE("a map's title falls back to its file name", "[map_session]") {
    MapSession session;
    session.file_name = "zddb02.blv";
    REQUIRE(session.title() == "zddb02.blv");
    session.display_name = "Sweet Water";
    REQUIRE(session.title() == "Sweet Water");
}

TEST_CASE("an indoor map has no heightfield to sample", "[map_session]") {
    // Asking anyway must be defined: the movement step calls this every frame
    // for both kinds.
    MapSession session;
    session.kind = MapKind::Indoor;
    REQUIRE(session.terrain_height_at(1234.0f, -5678.0f) == 0.0f);
}
