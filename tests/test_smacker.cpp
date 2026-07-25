// Tests for the Smacker video decoder.
//
// Fixtures are SYNTHETIC: headers, Huffman trees and frame payloads are built
// here from the layout in docs/formats/smacker.md, including a hand-written
// bit writer that mirrors the format's LSB-first packing. No bytes from the
// game are involved.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "core/video/smacker.hpp"

using namespace starhaven::video;

namespace {

// Packs bits LSB-first within each byte, the order the format uses.
class BitWriter {
public:
    void bit(bool value) {
        if (bit_count_ % 8 == 0) {
            bytes_.push_back(std::byte{0});
        }
        if (value) {
            bytes_.back() |= static_cast<std::byte>(1u << (bit_count_ % 8));
        }
        ++bit_count_;
    }
    void bits(std::uint32_t value, int count) {
        for (int i = 0; i < count; ++i) {
            bit(((value >> i) & 1u) != 0);
        }
    }
    // A byte tree holding one leaf: present, one leaf node, then a terminator.
    void single_byte_tree(std::uint8_t value) {
        bit(true);        // present
        bit(false);       // this node is a leaf
        bits(value, 8);
        bit(false);       // terminator
    }
    // A 16-bit tree holding one leaf, which therefore decodes to `value`
    // without consuming any bits from the frame stream.
    void single_word_tree(std::uint16_t value) {
        bit(true);  // present
        single_byte_tree(static_cast<std::uint8_t>(value & 0xFF));
        single_byte_tree(static_cast<std::uint8_t>(value >> 8));
        for (int i = 0; i < 3; ++i) {
            bits(0, 16);  // the three cache slots start empty
        }
        bit(false);   // the big tree is a single leaf
        bit(false);   // terminator
    }
    void absent_tree() { bit(false); }

    [[nodiscard]] std::vector<std::byte> take() {
        while (bytes_.size() % 4 != 0) {
            bytes_.push_back(std::byte{0});
        }
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
    std::size_t bit_count_ = 0;
};

struct FrameSpec {
    std::uint8_t type = 0;                 // bit 0 = palette record present
    std::vector<std::byte> payload;        // palette record and/or video data
};

struct VideoSpec {
    const char* magic = "SMK2";
    std::uint32_t width = 8;
    std::uint32_t height = 8;
    std::int32_t frame_rate = -10000;      // 10 fps
    std::uint32_t flags = 0;
    std::vector<std::byte> trees;
    std::vector<FrameSpec> frames;
    std::uint32_t audio_size0 = 0;
};

void put_u32(std::vector<std::byte>& v, std::size_t off, std::uint32_t x) {
    v[off + 0] = static_cast<std::byte>(x & 0xFF);
    v[off + 1] = static_cast<std::byte>((x >> 8) & 0xFF);
    v[off + 2] = static_cast<std::byte>((x >> 16) & 0xFF);
    v[off + 3] = static_cast<std::byte>((x >> 24) & 0xFF);
}

// Assemble a whole .smk file. `frame_count` in the header excludes the ring
// frame, so a fixture with the ring flag must supply one extra FrameSpec.
std::vector<std::byte> make_video(const VideoSpec& spec) {
    const std::size_t table_frames = spec.frames.size();
    const std::size_t declared =
        (spec.flags & 1u) != 0 ? table_frames - 1 : table_frames;

    std::vector<std::byte> out(kSmackerHeaderSize, std::byte{0});
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<std::byte>(spec.magic[i]);
    }
    put_u32(out, 0x04, spec.width);
    put_u32(out, 0x08, spec.height);
    put_u32(out, 0x0C, static_cast<std::uint32_t>(declared));
    put_u32(out, 0x10, static_cast<std::uint32_t>(spec.frame_rate));
    put_u32(out, 0x14, spec.flags);
    put_u32(out, 0x18, spec.audio_size0);
    put_u32(out, 0x34, static_cast<std::uint32_t>(spec.trees.size()));
    put_u32(out, 0x38, 0);  // mmap size
    put_u32(out, 0x3C, 0);  // mclr size
    put_u32(out, 0x40, 0);  // full size
    put_u32(out, 0x44, 0);  // type size

    // Frame sizes: the stored value carries flags in its low two bits, so each
    // payload is padded to a multiple of four and bit 0 marks a keyframe.
    for (const auto& f : spec.frames) {
        REQUIRE(f.payload.size() % 4 == 0);
        std::vector<std::byte> field(4);
        put_u32(field, 0, static_cast<std::uint32_t>(f.payload.size()) | 1u);
        out.insert(out.end(), field.begin(), field.end());
    }
    for (const auto& f : spec.frames) {
        out.push_back(static_cast<std::byte>(f.type));
    }
    out.insert(out.end(), spec.trees.begin(), spec.trees.end());
    for (const auto& f : spec.frames) {
        out.insert(out.end(), f.payload.begin(), f.payload.end());
    }
    return out;
}

// A palette record: one length byte (in 4-byte units, counting itself) then
// the opcodes.
std::vector<std::byte> palette_record(const std::vector<std::uint8_t>& opcodes) {
    std::vector<std::byte> out;
    const std::size_t units = (opcodes.size() + 1 + 3) / 4;
    out.push_back(static_cast<std::byte>(units));
    for (std::uint8_t b : opcodes) {
        out.push_back(static_cast<std::byte>(b));
    }
    while (out.size() % 4 != 0) {
        out.push_back(std::byte{0});
    }
    return out;
}

}  // namespace

TEST_CASE("the bit reader consumes bits LSB-first", "[smacker]") {
    // 0b1010'0011 -> bits come out 1,1,0,0,0,1,0,1
    const std::vector<std::byte> data{std::byte{0xA3}};
    BitReader r{data};
    REQUIRE(r.read_bit());
    REQUIRE(r.read_bit());
    REQUIRE_FALSE(r.read_bit());
    REQUIRE_FALSE(r.read_bit());
    REQUIRE_FALSE(r.read_bit());
    REQUIRE(r.read_bit());
    REQUIRE_FALSE(r.read_bit());
    REQUIRE(r.read_bit());
    REQUIRE(r.at_end());
}

TEST_CASE("the bit reader reports an overrun instead of reading past the end",
          "[smacker]") {
    const std::vector<std::byte> data{std::byte{0xFF}};
    BitReader r{data};
    REQUIRE(r.read_bits(8) == 0xFF);
    REQUIRE_FALSE(r.overrun());
    REQUIRE(r.read_bits(4) == 0);
    REQUIRE(r.overrun());
}

TEST_CASE("a header is parsed and reported", "[smacker]") {
    VideoSpec spec;
    spec.width = 16;
    spec.height = 8;
    spec.frame_rate = -10000;
    spec.audio_size0 = 1234;
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto data = make_video(spec);

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::None);
    REQUIRE(decoder.info().width == 16);
    REQUIRE(decoder.info().height == 8);
    REQUIRE(decoder.info().frame_count == 1);
    REQUIRE(decoder.info().fps == 10.0);
    REQUIRE_FALSE(decoder.info().version4);
    REQUIRE(decoder.info().has_audio);
    REQUIRE_FALSE(decoder.info().ring_frame);
}

TEST_CASE("both frame-rate encodings yield the same fps", "[smacker]") {
    // Negative rates count ten-microsecond units; positive rates count whole
    // milliseconds. MM6 ships both, and reading a positive rate as microseconds
    // would report 10000 fps for the 14 videos that use it.
    SmackerDecoder decoder;

    VideoSpec negative;
    negative.frame_rate = -10000;
    negative.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto a = make_video(negative);
    REQUIRE(SmackerDecoder::load(a, decoder) == SmackerError::None);
    REQUIRE(decoder.info().fps == 10.0);

    VideoSpec positive;
    positive.frame_rate = 100;
    positive.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto b = make_video(positive);
    REQUIRE(SmackerDecoder::load(b, decoder) == SmackerError::None);
    REQUIRE(decoder.info().fps == 10.0);

    VideoSpec zero;
    zero.frame_rate = 0;
    zero.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto c = make_video(zero);
    REQUIRE(SmackerDecoder::load(c, decoder) == SmackerError::None);
    REQUIRE(decoder.info().fps == 10.0);
}

TEST_CASE("SMK4 is recognized and other magics are rejected", "[smacker]") {
    VideoSpec spec;
    spec.magic = "SMK4";
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto v4 = make_video(spec);
    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(v4, decoder) == SmackerError::None);
    REQUIRE(decoder.info().version4);

    spec.magic = "SMK1";
    auto bad = make_video(spec);
    REQUIRE(SmackerDecoder::load(bad, decoder) == SmackerError::BadMagic);
}

TEST_CASE("a buffer shorter than the header is rejected", "[smacker]") {
    const std::vector<std::byte> tiny(40, std::byte{0});
    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(tiny, decoder) == SmackerError::TooSmall);
}

TEST_CASE("zero or implausible dimensions are rejected", "[smacker]") {
    SmackerDecoder decoder;

    VideoSpec zero;
    zero.width = 0;
    zero.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto a = make_video(zero);
    REQUIRE(SmackerDecoder::load(a, decoder) == SmackerError::BadDimensions);

    VideoSpec big;
    big.height = 100000;
    big.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto b = make_video(big);
    REQUIRE(SmackerDecoder::load(b, decoder) == SmackerError::BadDimensions);
}

TEST_CASE("the ring-frame flag adds an entry to the frame tables", "[smacker]") {
    // Flag bit 0 is a trailing frame for seamless looping, not a picture-size
    // flag: the tables carry frame_count + 1 entries. Reading one entry too few
    // desynchronizes every frame offset, so this is the regression that matters.
    VideoSpec spec;
    spec.flags = 1;
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});  // ring
    auto data = make_video(spec);

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::None);
    REQUIRE(decoder.info().ring_frame);
    // The ring frame is not offered for playback.
    REQUIRE(decoder.info().frame_count == 2);
    REQUIRE(decoder.info().height == 8);  // and the picture is NOT doubled

    std::span<const std::uint8_t> pixels;
    REQUIRE(decoder.decode_frame(1, pixels) == SmackerError::None);
    REQUIRE(decoder.decode_frame(2, pixels) == SmackerError::NoSuchFrame);
}

TEST_CASE("the y-double flag doubles the picture height", "[smacker]") {
    VideoSpec spec;
    spec.flags = 4;  // Y-doubled
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto data = make_video(spec);

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::None);
    REQUIRE(decoder.info().height == 16);  // 8 coded rows -> 16 output rows
    REQUIRE(decoder.info().y_scale == YScale::Double);
}

TEST_CASE("a fill block paints its color across the picture", "[smacker]") {
    // The type tree is a single leaf, so every descriptor decodes to the same
    // value: type 3 (fill), run index 0 (one block), color 0x2A.
    BitWriter trees;
    trees.absent_tree();  // mmap
    trees.absent_tree();  // mclr
    trees.absent_tree();  // full
    trees.single_word_tree(0x2A03);

    VideoSpec spec;
    spec.width = 8;
    spec.height = 8;
    spec.trees = trees.take();
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto data = make_video(spec);

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::None);

    std::span<const std::uint8_t> pixels;
    REQUIRE(decoder.decode_frame(0, pixels) == SmackerError::None);
    REQUIRE(pixels.size() == 64);
    for (std::uint8_t p : pixels) {
        REQUIRE(p == 0x2A);
    }
}

TEST_CASE("a palette record sets literal, skipped and copied entries",
          "[smacker]") {
    // Opcodes: a literal RGB triple, a skip of one entry, then a copy.
    //
    // The copy opcode sources from the palette as it was *before* this record,
    // not from the entries this record has just written — so copying index 5
    // yields the old value there, which the default ramp makes (5, 5, 5).
    const std::vector<std::uint8_t> opcodes = {
        0x3F, 0x00, 0x20,  // entry 0 = (63, 0, 32) in 6-bit channels
        0x80,              // skip 1 entry (leaves entry 1 at the default)
        0x40, 0x05,        // copy 1 entry from old index 5 -> entry 2
    };

    VideoSpec spec;
    spec.frames.push_back({1, palette_record(opcodes)});
    auto data = make_video(spec);

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::None);
    std::span<const std::uint8_t> pixels;
    REQUIRE(decoder.decode_frame(0, pixels) == SmackerError::None);

    const auto palette = decoder.palette();
    // Six-bit channels widen to eight, so 0x3F becomes 0xFF rather than 0xFC.
    REQUIRE(palette[0] == 0xFF);
    REQUIRE(palette[1] == 0x00);
    REQUIRE(palette[2] == 0x82);
    // Entry 1 was skipped: it keeps the default grayscale ramp value.
    REQUIRE(palette[3] == 1);
    // Entry 2 came from the old palette's entry 5, not from the new entry 0.
    REQUIRE(palette[6] == 5);
    REQUIRE(palette[7] == 5);
    REQUIRE(palette[8] == 5);
}

TEST_CASE("frames expand to RGBA through the palette", "[smacker]") {
    BitWriter trees;
    trees.absent_tree();
    trees.absent_tree();
    trees.absent_tree();
    trees.single_word_tree(0x0003);  // fill with palette index 0

    VideoSpec spec;
    spec.width = 4;
    spec.height = 4;
    spec.trees = trees.take();
    // Frame 0 sets palette entry 0 to a known color, then fills with it.
    spec.frames.push_back({1, palette_record({0x3F, 0x3F, 0x00})});
    auto data = make_video(spec);

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::None);
    std::span<const std::uint8_t> rgba;
    REQUIRE(decoder.decode_frame_rgba(0, rgba) == SmackerError::None);
    REQUIRE(rgba.size() == 4 * 4 * 4);
    REQUIRE(rgba[0] == 0xFF);
    REQUIRE(rgba[1] == 0xFF);
    REQUIRE(rgba[2] == 0x00);
    REQUIRE(rgba[3] == 255);  // opaque
}

TEST_CASE("a frame index past the end is rejected", "[smacker]") {
    VideoSpec spec;
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto data = make_video(spec);

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::None);
    std::span<const std::uint8_t> pixels;
    REQUIRE(decoder.decode_frame(1, pixels) == SmackerError::NoSuchFrame);
}

TEST_CASE("frame data running past the buffer is rejected", "[smacker]") {
    VideoSpec spec;
    spec.frames.push_back({0, std::vector<std::byte>(8, std::byte{0})});
    auto data = make_video(spec);
    data.resize(data.size() - 4);  // drop the tail of the frame

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::Truncated);
}

TEST_CASE("a truncated frame table is rejected", "[smacker]") {
    VideoSpec spec;
    spec.frames.push_back({0, std::vector<std::byte>(4, std::byte{0})});
    auto data = make_video(spec);
    data.resize(kSmackerHeaderSize + 2);  // header, then a partial size entry

    SmackerDecoder decoder;
    REQUIRE(SmackerDecoder::load(data, decoder) == SmackerError::Truncated);
}
