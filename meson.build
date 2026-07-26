project('starhaven', 'cpp',
        version : '0.1.0',
        default_options : ['warning_level=3', 'cpp_std=c++20'])

# Engine version surfaced by the launcher and tools. The generated header lands
# at the build directory root, so we include both the source tree and the build
# root to resolve it.
config_h = configure_file(
  input : 'src/config.h.in',
  output : 'config.h',
  configuration : configuration_data({
    'STARHAVEN_VERSION' : '"' + meson.project_version() + '"',
  }),
)

starhaven_core_inc = include_directories('src', '.')

# zlib decodes the zlib-compressed pixel data inside .LOD image entries
# (see docs/formats/bitmap.md). It is standard zlib, not a proprietary scheme.
zlib_dep = dependency('zlib')

# minimp3 decodes the game's MP3 music: the tracks are ordinary MP3 files, so
# playing them needs a decoder rather than reverse engineering. Header-only and
# public domain; see subprojects/minimp3.wrap.
minimp3_dep = dependency('minimp3', fallback: ['minimp3', 'minimp3_dep'])

# Core engine library. Reused by the application, tools, and tests so that
# parser behavior is exercised by the same code path in every target.
starhaven_core_sources = files(
  'src/core/random.cpp',
  'src/core/io/byte_reader.cpp',
  'src/core/lod/lod_archive.cpp',
  'src/core/lod/game_lod_archive.cpp',
  'src/core/image/bitmap.cpp',
  'src/core/image/palette.cpp',
  'src/core/image/sprite.cpp',
  'src/core/image/font.cpp',
  'src/core/image/zlib_util.cpp',
  'src/core/data/text_table.cpp',
  'src/core/data/item_generation.cpp',
  'src/core/data/item_stats.cpp',
  'src/core/data/map_stats.cpp',
  'src/core/data/monster_stats.cpp',
  'src/core/data/spell_stats.cpp',
  'src/core/data/building_stats.cpp',
  'src/core/data/game_data.cpp',
  'src/core/world/odm_map.cpp',
  'src/core/world/map_event.cpp',
  'src/core/world/tile_table.cpp',
  'src/core/world/monster_list.cpp',
  'src/core/world/object_table.cpp',
  'src/core/world/sprite_frame_table.cpp',
  'src/core/world/sound_table.cpp',
  'src/core/world/decoration_table.cpp',
  'src/core/world/blv_map.cpp',
  'src/core/world/map_session.cpp',
  'src/core/world/collision.cpp',
  'src/core/render/rasterizer.cpp',
  'src/core/render/texture.cpp',
  'src/core/render/tile_set.cpp',
  'src/core/render/terrain_mesh.cpp',
  'src/core/render/scene.cpp',
  'src/core/assets/asset_cache.cpp',
  'src/core/audio/snd_archive.cpp',
  'src/core/audio/wav.cpp',
  'src/core/audio/mp3.cpp',
  'src/core/video/vid_archive.cpp',
  'src/core/video/smacker.cpp',
  'src/core/platform/paths.cpp',
)

starhaven_core = static_library('starhaven_core',
  starhaven_core_sources,
  include_directories : starhaven_core_inc,
  dependencies : [zlib_dep, minimp3_dep,
                  dependency('threads', required : false)],
)

starhaven_dep = declare_dependency(
  link_with : starhaven_core,
  include_directories : starhaven_core_inc,
  # Tests link starhaven_dep and also need zlib directly (test_bitmap compresses).
  sources : [],
  dependencies : zlib_dep,
)

# SDL3 for the engine and its tools. Prefer a system install; the committed
# subprojects/sdl3.wrap declares `dependency_names = sdl3`, so Meson falls back
# to the wrap automatically when no system SDL3 is found.
sdl3_dep = dependency('sdl3')

# The engine itself: loads any map and renders it as a walkable world. The
# SDL-dependent parts live under src/game; everything below it is SDL-free.
executable('starhaven', 'src/main.cpp',
  dependencies : [starhaven_dep, sdl3_dep],
  install : true,
)

# Standalone archive browser. Reads the user's own legal install in place.
executable('lod_browser', 'tools/lod_browser.cpp',
  dependencies : starhaven_dep,
  install : true,
)


# Bitmap viewer: decodes one .LOD image entry and shows it in an SDL3 window.
# Only built when SDL3 is available.
executable('view_bitmap', 'tools/view_bitmap.cpp',
  dependencies : [starhaven_dep, sdl3_dep],
  install : true,
)

# ODM inspector: decompresses one .odm map from Games.lod and prints its header.
executable('odm_info', 'tools/odm_info.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# Heightmap viewer: renders an .odm heightmap as grayscale in an SDL3 window.
executable('view_heightmap', 'tools/view_heightmap.cpp',
  dependencies : [starhaven_dep, sdl3_dep],
  install : true,
)

# Music player: decodes and plays one of the installation's MP3 tracks.
executable('play_music', 'tools/play_music.cpp',
  dependencies : [starhaven_dep, sdl3_dep],
  install : true,
)

# Sound-effect player: decodes one entry of Sounds/Audio.snd and plays it.
executable('play_sound', 'tools/play_sound.cpp',
  dependencies : [starhaven_dep, sdl3_dep],
  install : true,
)

# Smacker player: decodes one video from the install's Anims/*.vid archives.
executable('play_smk', 'tools/play_smk.cpp',
  dependencies : [starhaven_dep, sdl3_dep],
  install : true,
)

# Indoor-map inspector: decompresses one .blv and prints geometry stats.
executable('blv_info', 'tools/blv_info.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# Event-data inspector: decompresses one .ddm/.dlv file and prints stats.
executable('ddm_info', 'tools/ddm_info.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# Data-table inspector: reads the tab-separated design tables in icons.lod.
executable('data_info', 'tools/data_info.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# Sprite-frame-table inspector: reads DSFT.BIN and verifies its joins.
executable('sft_info', 'tools/sft_info.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# Font inspector: lists the bitmap fonts and draws text as ASCII art.
executable('font_info', 'tools/font_info.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# Tile-table probe: surveys the user's install for the ground-tile lookup
# chain. Read-only; reports names, sizes and counts, never pixel data.
executable('tile_probe', 'tools/tile_probe.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# Unit tests. Built as separate executables, never as the app itself.
# default_options silences Catch2's own self-test suite (per the engineer skill).
catch2 = dependency('catch2',
  version : '>=3.0.0',
  default_options : ['tests=false', 'install=false'],
  fallback : ['catch2', 'catch2_dep'])
catch2_main = dependency('catch2-with-main',
  default_options : ['tests=false', 'install=false'],
  fallback : ['catch2', 'catch2_with_main_dep'])

subdir('tests')
