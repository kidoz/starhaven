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

# Core engine library. Reused by the application, tools, and tests so that
# parser behavior is exercised by the same code path in every target.
starhaven_core_sources = files(
  'src/core/io/byte_reader.cpp',
  'src/core/lod/lod_archive.cpp',
  'src/core/lod/game_lod_archive.cpp',
  'src/core/image/bitmap.cpp',
  'src/core/image/palette.cpp',
  'src/core/image/sprite.cpp',
  'src/core/image/zlib_util.cpp',
  'src/core/world/odm_map.cpp',
  'src/core/world/map_event.cpp',
  'src/core/world/tile_table.cpp',
  'src/core/world/blv_map.cpp',
  'src/core/render/rasterizer.cpp',
  'src/core/render/texture.cpp',
  'src/core/render/tile_set.cpp',
  'src/core/render/terrain_mesh.cpp',
  'src/core/video/vid_archive.cpp',
  'src/core/video/smacker.cpp',
  'src/core/platform/paths.cpp',
)

starhaven_core = static_library('starhaven_core',
  starhaven_core_sources,
  include_directories : starhaven_core_inc,
  dependencies : [zlib_dep, dependency('threads', required : false)],
)

starhaven_dep = declare_dependency(
  link_with : starhaven_core,
  include_directories : starhaven_core_inc,
  # Tests link starhaven_dep and also need zlib directly (test_bitmap compresses).
  sources : [],
  dependencies : zlib_dep,
)

# Application launcher. Kept thin; real logic lives in starhaven_core.
executable('starhaven', 'src/main.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# Standalone archive browser. Reads the user's own legal install in place.
executable('lod_browser', 'tools/lod_browser.cpp',
  dependencies : starhaven_dep,
  install : true,
)

# SDL3 for the viewer tools. Prefer a system install; the committed
# subprojects/sdl3.wrap declares `dependency_names = sdl3`, so Meson falls back
# to the wrap automatically when no system SDL3 is found.
sdl3_dep = dependency('sdl3')

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

# First-person walker: software-rasterizes an .odm terrain heightfield in 3D.
executable('walk_odm', 'tools/walk_odm.cpp',
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
