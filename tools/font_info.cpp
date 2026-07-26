#include <algorithm>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core/image/font.hpp"
#include "core/lod/lod_archive.hpp"
#include "core/platform/paths.hpp"

namespace {

using namespace starhaven;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <--list | <font> [text]>\n"
              << "\n"
              << "Reads the bitmap fonts in your own legal installation's\n"
              << "icons.lod.\n"
              << "\n"
              << "  --list          list the fonts, their height and glyph count\n"
              << "  <font> [text]   draw text as ASCII art, to read it back\n"
              << "\n"
              << "Set " << platform::kInstallEnvVar << " to the install directory.\n";
}

bool is_font_name(const std::string& name) {
    if (name.size() < 4) {
        return false;
    }
    std::string tail = name.substr(name.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".fnt";
}

int do_list(lod::LodArchive& icons) {
    int found = 0;
    for (const auto& e : icons.entries()) {
        if (!is_font_name(e.name)) {
            continue;
        }
        std::span<const std::byte> raw;
        if (icons.payload(e.name, raw) != lod::LodArchive::PayloadError::None) {
            continue;
        }
        image::Font font;
        const image::FontError err = image::Font::parse(raw, font);
        std::cout << "  " << e.name << "\t";
        if (err != image::FontError::None) {
            std::cout << "does not decode (code " << static_cast<int>(err) << ")\n";
        } else {
            std::cout << "height " << font.height() << "\t" << font.glyph_count() << " glyphs\t"
                      << "chars " << static_cast<int>(font.first_char()) << ".."
                      << static_cast<int>(font.last_char()) << "\n";
        }
        ++found;
    }
    std::cout << found << " fonts\n";
    return 0;
}

// Draw the text as characters so it can be read back in a terminal: '#' is the
// glyph body, '.' its outline. This is the format's verification.
int do_draw(lod::LodArchive& icons, const std::string& name, const std::string& text) {
    std::span<const std::byte> raw;
    if (icons.payload(name, raw) != lod::LodArchive::PayloadError::None) {
        std::cerr << "error: no font named " << name << "\n";
        return 1;
    }
    image::Font font;
    if (const image::FontError e = image::Font::parse(raw, font); e != image::FontError::None) {
        std::cerr << "error: could not decode " << name << " (code " << static_cast<int>(e)
                  << ")\n";
        return 1;
    }
    std::cout << name << ": height " << font.height() << ", " << font.glyph_count() << " glyphs, \""
              << text << "\" is " << font.text_width(text) << " pixels wide\n";

    const int width = font.text_width(text);
    std::vector<std::string> rows(static_cast<std::size_t>(font.height()),
                                  std::string(static_cast<std::size_t>(std::max(width, 1)), ' '));
    int pen = 0;
    for (const char ch : text) {
        const image::FontGlyph* g = font.glyph(static_cast<std::uint8_t>(ch));
        if (g == nullptr) {
            continue;
        }
        for (int row = 0; row < font.height(); ++row) {
            for (int col = 0; col < g->width; ++col) {
                const int x = pen + g->left_spacing + col;
                if (x < 0 || x >= width) {
                    continue;
                }
                const std::uint8_t v =
                    g->pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(g->width) +
                              static_cast<std::size_t>(col)];
                if (v == image::kFontPixelBody) {
                    rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(x)] = '#';
                } else if (v == image::kFontPixelOutline) {
                    rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(x)] = '.';
                }
            }
        }
        pen += g->advance();
    }
    for (const auto& row : rows) {
        std::cout << row << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 2;
    }
    const std::string command = argv[1];

    const auto install = platform::install_from_env();
    if (!install) {
        std::cerr << "error: set " << platform::kInstallEnvVar << "\n";
        return 1;
    }
    lod::LodArchive icons;
    if (lod::LodArchive::open(*install / "data" / "icons.lod", icons) != lod::LodError::None) {
        std::cerr << "error: could not open icons.lod\n";
        return 1;
    }

    if (command == "--list") {
        return do_list(icons);
    }
    return do_draw(icons, command, argc == 3 ? argv[2] : "Might and Magic VI");
}
