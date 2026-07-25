#ifndef OPENMM6_CORE_VIDEO_SMACKER_HPP
#define OPENMM6_CORE_VIDEO_SMACKER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace openmm6::video {

// Outcome of loading or decoding a Smacker video. The decoder never throws.
enum class SmackerError {
    None,
    // Fewer bytes than the 104-byte header needs.
    TooSmall,
    // Magic is neither "SMK2" nor "SMK4".
    BadMagic,
    // Width/height/frame count are zero or implausibly large.
    BadDimensions,
    // The frame table or a frame's bytes extend past the end of the buffer.
    Truncated,
    // A Huffman tree in the header block is malformed.
    BadTrees,
    // The requested frame index does not exist.
    NoSuchFrame,
};

// A reader over a bit-packed stream, LSB-first within each byte, which is the
// order Smacker's Huffman codes use. Reads past the end return 0 and set
// `overrun`, so a malformed stream terminates decoding instead of looping.
class BitReader {
public:
    BitReader(std::span<const std::byte> data) noexcept
        : data_(data), max_bits_(data.size() * 8) {}

    [[nodiscard]] bool read_bit() noexcept;
    [[nodiscard]] std::uint32_t read_bits(int count) noexcept;
    void skip_bits(int count) noexcept;

    [[nodiscard]] bool at_end() const noexcept { return bit_pos_ >= max_bits_; }
    [[nodiscard]] bool overrun() const noexcept { return overrun_; }
    [[nodiscard]] std::size_t bit_position() const noexcept { return bit_pos_; }

private:
    std::span<const std::byte> data_;
    std::size_t bit_pos_ = 0;
    std::size_t max_bits_ = 0;
    bool overrun_ = false;
};

// How coded rows map onto output rows. Smacker can code a half-height picture
// and expand it vertically; MM6 never does, but the flags exist.
enum class YScale {
    None,       // one coded row per output row
    Interlace,  // coded row r paints output row 2r; odd rows are left alone
    Double,     // coded row r paints output rows 2r and 2r+1
};

// Static properties of a loaded video.
struct SmackerInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;   // output height, after any vertical scaling
    std::uint32_t frame_count = 0;  // playable frames, excluding any ring frame
    double fps = 0.0;
    bool version4 = false;      // "SMK4" rather than "SMK2"
    bool has_audio = false;     // audio tracks are present but not decoded
    bool ring_frame = false;    // a trailing frame exists for seamless looping
    YScale y_scale = YScale::None;
};

// A decoder for RAD Game Tools' Smacker video (`.smk`), the format MM6 uses for
// every entry in `Anims1.vid` / `Anims2.vid`. See docs/formats/smacker.md.
//
// This slice decodes **video only**. Audio track payloads are located and
// skipped, not decoded — openmm6 has no audio output yet.
//
// Frames are inter-coded: frame N is a delta against the buffer left by frame
// N-1, so `decode_frame` replays intervening frames when asked for a frame out
// of order.
class SmackerDecoder {
public:
    SmackerDecoder();
    ~SmackerDecoder();

    SmackerDecoder(const SmackerDecoder&) = delete;
    SmackerDecoder& operator=(const SmackerDecoder&) = delete;
    SmackerDecoder(SmackerDecoder&&) noexcept;
    SmackerDecoder& operator=(SmackerDecoder&&) noexcept;

    // Load a whole `.smk` file. The bytes are copied, so `data` need not
    // outlive the decoder.
    [[nodiscard]] static SmackerError load(std::span<const std::byte> data,
                                           SmackerDecoder& out);

    [[nodiscard]] const SmackerInfo& info() const noexcept { return info_; }

    // The current palette: 256 entries of 3 bytes (R, G, B). Palette updates
    // ride along with frames, so this reflects the last decoded frame.
    [[nodiscard]] std::span<const std::uint8_t> palette() const noexcept { return palette_; }

    // Decode up to and including `index`, leaving the result in the frame
    // buffer. Returns the palette-indexed pixels (width * height bytes).
    [[nodiscard]] SmackerError decode_frame(std::uint32_t index,
                                            std::span<const std::uint8_t>& out);

    // Decode a frame and expand it through the palette into RGBA (alpha 255).
    [[nodiscard]] SmackerError decode_frame_rgba(std::uint32_t index,
                                                 std::span<const std::uint8_t>& out);

    // Whether frame `index` carries a keyframe flag.
    [[nodiscard]] bool is_keyframe(std::uint32_t index) const noexcept;

private:
    struct Trees;

    // The 104-byte header, as parsed.
    struct Header {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t frame_count = 0;
        std::int32_t frame_rate = 0;
        std::uint32_t flags = 0;
        std::uint32_t audio_size[7] = {};
        std::uint32_t trees_size = 0;
        std::uint32_t mmap_size = 0;
        std::uint32_t mclr_size = 0;
        std::uint32_t full_size = 0;
        std::uint32_t type_size = 0;
        std::uint32_t audio_rate[7] = {};
    };

    [[nodiscard]] SmackerError parse_header();
    [[nodiscard]] SmackerError build_trees();
    [[nodiscard]] SmackerError decode_one(std::uint32_t index);
    [[nodiscard]] bool decode_video(BitReader& bits);
    void decode_palette(std::span<const std::byte> data);

    void fill_block(std::uint32_t x, std::uint32_t y, std::uint8_t color);
    void mono_block(std::uint32_t x, std::uint32_t y, std::uint8_t c0,
                    std::uint8_t c1, std::uint16_t bitmap);
    void full_block(BitReader& bits, std::uint32_t x, std::uint32_t y);
    void put_pixel(std::uint32_t x, std::uint32_t y, std::uint8_t color);

    Header header_{};
    SmackerInfo info_{};
    std::vector<std::byte> data_;

    std::vector<std::uint32_t> frame_sizes_;   // as stored, flag bits included
    std::vector<std::uint8_t> frame_types_;
    std::vector<std::uint64_t> frame_offsets_;  // frame_count + 1 entries
    std::vector<std::uint32_t> keyframes_;

    std::vector<std::uint8_t> palette_;      // 256 * 3
    std::vector<std::uint8_t> frame_buffer_;  // width * height, palette indices
    std::vector<std::uint8_t> rgba_;          // width * height * 4

    std::unique_ptr<Trees> trees_;
    std::size_t trees_offset_ = 0;
    std::uint32_t coded_height_ = 0;  // rows actually coded, before y scaling
    std::uint32_t table_frames_ = 0;  // frame table length, ring frame included
    std::uint32_t last_decoded_ = kNoFrame;

    static constexpr std::uint32_t kNoFrame = 0xFFFFFFFFu;
};

// Header layout constants (see docs/formats/smacker.md).
constexpr std::size_t kSmackerHeaderSize = 104;
constexpr std::size_t kSmackerAudioTracks = 7;
constexpr std::size_t kSmackerPaletteBytes = 768;

}  // namespace openmm6::video

#endif  // OPENMM6_CORE_VIDEO_SMACKER_HPP
