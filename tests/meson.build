# Unit tests. Each test is a separate executable linked to starhaven_core and to
# Catch2 v3 (with main). Tests must be hermetic: they use synthetic fixtures only
# and never touch the user's game install.

test('byte_reader',
  executable('test_byte_reader', 'test_byte_reader.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('lod_archive',
  executable('test_lod_archive', 'test_lod_archive.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('game_lod',
  executable('test_game_lod', 'test_game_lod.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('bitmap',
  executable('test_bitmap', 'test_bitmap.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('sprite',
  executable('test_sprite', 'test_sprite.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('odm',
  executable('test_odm', 'test_odm.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('math3d',
  executable('test_math3d', 'test_math3d.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('rasterizer',
  executable('test_rasterizer', 'test_rasterizer.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('texture',
  executable('test_texture', 'test_texture.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('tile_set',
  executable('test_tile_set', 'test_tile_set.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('tile_table',
  executable('test_tile_table', 'test_tile_table.cpp',
    dependencies : [starhaven_dep, catch2_main, zlib_dep]),
  workdir : meson.current_build_dir(),
)

test('terrain_mesh',
  executable('test_terrain_mesh', 'test_terrain_mesh.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('map_event',
  executable('test_map_event', 'test_map_event.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('vid_archive',
  executable('test_vid_archive', 'test_vid_archive.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('smacker',
  executable('test_smacker', 'test_smacker.cpp',
    dependencies : [starhaven_dep, catch2_main]),
  workdir : meson.current_build_dir(),
)

test('blv',
  executable('test_blv', 'test_blv.cpp',
    dependencies : [starhaven_dep, catch2_main, zlib_dep]),
  workdir : meson.current_build_dir(),
)
