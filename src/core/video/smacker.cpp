#include "core/video/smacker.hpp"

#include "core/io/byte_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace starhaven::video {

// --- BitReader -------------------------------------------------------------

bool BitReader::read_bit() noexcept {
    if (bit_pos_ >= max_bits_) {
        overrun_ = true;
        return false;
    }
    const std::size_t byte = bit_pos_ >> 3;
    const std::size_t bit = bit_pos_ & 7;
    ++bit_pos_;
    return (static_cast<unsigned>(data_[byte]) >> bit & 1u) != 0;
}

std::uint32_t BitReader::read_bits(int count) noexcept {
    std::uint32_t value = 0;
    for (int i = 0; i < count && i < 32; ++i) {
        if (read_bit()) {
            value |= 1u << i;
        }
    }
    return value;
}

void BitReader::skip_bits(int count) noexcept {
    const auto n = static_cast<std::size_t>(count < 0 ? 0 : count);
    if (n > max_bits_ - std::min(bit_pos_, max_bits_)) {
        overrun_ = true;
        bit_pos_ = max_bits_;
        return;
    }
    bit_pos_ += n;
}

namespace {

// Node tags for the two Huffman tree flavors. A branch node stores the index of
// its right child; the left child is always the next entry.
constexpr std::uint16_t kByteBranch = 0x8000;
constexpr std::uint16_t kByteIndexMask = 0x7FFF;
constexpr std::uint32_t kWordBranch = 0x80000000u;
constexpr std::uint32_t kWordCached = 0x40000000u;
constexpr std::uint32_t kWordIndexMask = 0x3FFFFFFFu;

// The 8-bit trees are only scaffolding: a pair of them supplies the low and
// high bytes of each leaf while the 16-bit tree is being built.
class ByteTree {
public:
    // Reads a tree from the bit stream. `present` false means the caller has
    // already established there is no presence bit to consume.
    // `with_presence` false is the audio-tree form: a dummy bit stands where
    // the presence bit would be, and the tree is always present.
    [[nodiscard]] bool build(BitReader& bits, bool with_presence = true) {
        nodes_.clear();
        size_ = 0;
        if (with_presence) {
            if (!bits.read_bit()) {
                nodes_.push_back(0);  // absent: a single zero-valued leaf
                size_ = 1;
                return true;
            }
        } else {
            bits.skip_bits(1);
        }
        if (!build_node(bits)) {
            return false;
        }
        (void)bits.read_bit();  // terminator
        return true;
    }

    [[nodiscard]] std::uint8_t lookup(BitReader& bits) const {
        if (nodes_.empty()) {
            return 0;
        }
        std::size_t index = 0;
        while (index < nodes_.size() && (nodes_[index] & kByteBranch) != 0) {
            if (bits.at_end()) {
                return 0;
            }
            index = bits.read_bit() ? (nodes_[index] & kByteIndexMask) : index + 1;
        }
        return index < nodes_.size() ? static_cast<std::uint8_t>(nodes_[index]) : 0;
    }

private:
    // A Smacker byte tree holds at most 256 leaves and 255 branches.
    static constexpr std::size_t kMaxNodes = 511;

    [[nodiscard]] bool build_node(BitReader& bits) {
        if (size_ >= kMaxNodes || bits.at_end()) {
            return false;
        }
        if (bits.read_bit()) {
            const std::size_t self = size_++;
            nodes_.resize(size_);
            if (!build_node(bits)) {
                return false;
            }
            nodes_[self] = static_cast<std::uint16_t>(kByteBranch | size_);
            return build_node(bits);
        }
        nodes_.push_back(static_cast<std::uint16_t>(bits.read_bits(8)));
        ++size_;
        return true;
    }

    std::vector<std::uint16_t> nodes_;
    std::size_t size_ = 0;
};

// The 16-bit trees that actually drive frame decoding. Each carries a
// three-entry most-recently-used cache; leaves may encode "the value that was
// in cache slot k" instead of a literal, and every decode moves the result to
// the front of that cache.
class WordTree {
public:
    [[nodiscard]] bool build(BitReader& bits) {
        nodes_.clear();
        size_ = 0;
        cache_ = {};

        if (!bits.read_bit()) {
            nodes_.push_back(0);  // absent: a single zero-valued leaf
            size_ = 1;
            return true;
        }

        ByteTree low;
        ByteTree high;
        if (!low.build(bits) || !high.build(bits)) {
            return false;
        }
        for (auto& slot : cache_) {
            const auto lo = static_cast<std::uint8_t>(bits.read_bits(8));
            const auto hi = static_cast<std::uint8_t>(bits.read_bits(8));
            slot = static_cast<std::uint16_t>((hi << 8) | lo);
        }

        if (!build_node(bits, low, high)) {
            return false;
        }
        nodes_.resize(size_);
        (void)bits.read_bit();  // terminator
        return true;
    }

    [[nodiscard]] std::uint16_t decode(BitReader& bits) {
        if (nodes_.empty()) {
            return 0;
        }
        std::size_t index = 0;
        while (index < nodes_.size() && (nodes_[index] & kWordBranch) != 0) {
            if (bits.at_end()) {
                return 0;
            }
            index = bits.read_bit() ? (nodes_[index] & kWordIndexMask) : index + 1;
        }
        if (index >= nodes_.size()) {
            return 0;
        }

        std::uint32_t value = nodes_[index];
        if ((value & kWordCached) != 0) {
            value = cache_[value & kWordIndexMask];
        }
        const auto result = static_cast<std::uint16_t>(value);
        if (cache_[0] != result) {
            cache_[2] = cache_[1];
            cache_[1] = cache_[0];
            cache_[0] = result;
        }
        return result;
    }

    // Each frame starts from a cleared cache.
    void reset_cache() noexcept { cache_ = {}; }

private:
    // Bounds the tree so a malformed stream cannot allocate without limit.
    static constexpr std::size_t kMaxNodes = 1u << 20;

    [[nodiscard]] bool build_node(BitReader& bits, const ByteTree& low,
                                  const ByteTree& high) {
        if (bits.at_end()) {
            return true;  // a truncated tree simply ends here
        }
        if (size_ >= kMaxNodes) {
            return false;
        }
        if (bits.read_bit()) {
            const std::size_t self = size_++;
            if (size_ > nodes_.size()) {
                nodes_.resize(size_);
            }
            if (!build_node(bits, low, high)) {
                return false;
            }
            nodes_[self] = kWordBranch | static_cast<std::uint32_t>(size_);
            return build_node(bits, low, high);
        }

        const std::uint8_t lo = low.lookup(bits);
        const std::uint8_t hi = high.lookup(bits);
        auto value = static_cast<std::uint32_t>((hi << 8) | lo);
        // Leaves matching a cache slot are stored as a reference to it.
        for (std::uint32_t slot = 0; slot < cache_.size(); ++slot) {
            if (value == cache_[slot]) {
                value = kWordCached | slot;
                break;
            }
        }
        if (size_ >= nodes_.size()) {
            nodes_.resize(size_ + 1);
        }
        nodes_[size_++] = value;
        return true;
    }

    std::vector<std::uint32_t> nodes_;
    std::size_t size_ = 0;
    std::array<std::uint16_t, 3> cache_{};
};

// Block run lengths. Indices 0..58 count up; the last five jump to powers of
// two so one descriptor can cover a large run of blocks.
constexpr std::array<std::uint32_t, 64> kBlockRuns = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,  13,  14,  15,   16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,  29,  30,  31,   32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,  45,  46,  47,   48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048,
};

enum class BlockType : std::uint8_t {
    Mono = 0,
    Full = 1,
    Skip = 2,
    Fill = 3,
};

}  // namespace

// The four per-video Huffman trees, held out of the header so callers do not
// need the tree types.
struct SmackerDecoder::Trees {
    WordTree mmap;   // mono block bitmaps
    WordTree mclr;   // mono block color pairs
    WordTree full;   // full block pixels
    WordTree type;   // block type descriptors

    void reset_caches() noexcept {
        mmap.reset_cache();
        mclr.reset_cache();
        full.reset_cache();
        type.reset_cache();
    }
};

SmackerDecoder::SmackerDecoder() : palette_(kSmackerPaletteBytes, 0) {}
SmackerDecoder::~SmackerDecoder() = default;
SmackerDecoder::SmackerDecoder(SmackerDecoder&&) noexcept = default;
SmackerDecoder& SmackerDecoder::operator=(SmackerDecoder&&) noexcept = default;

SmackerError SmackerDecoder::load(std::span<const std::byte> data,
                                  SmackerDecoder& out) {
    out = SmackerDecoder{};
    if (data.size() < kSmackerHeaderSize) {
        return SmackerError::TooSmall;
    }
    out.data_.assign(data.begin(), data.end());

    if (const SmackerError e = out.parse_header(); e != SmackerError::None) {
        return e;
    }
    return out.build_trees();
}

SmackerError SmackerDecoder::parse_header() {
    io::ByteReader r{data_};

    std::array<std::byte, 4> magic{};
    if (!r.read_bytes(magic)) {
        return SmackerError::TooSmall;
    }
    const bool smk2 = std::memcmp(magic.data(), "SMK2", 4) == 0;
    const bool smk4 = std::memcmp(magic.data(), "SMK4", 4) == 0;
    if (!smk2 && !smk4) {
        return SmackerError::BadMagic;
    }
    info_.version4 = smk4;

    header_.width = r.read_u32_le();
    header_.height = r.read_u32_le();
    header_.frame_count = r.read_u32_le();
    header_.frame_rate = static_cast<std::int32_t>(r.read_u32_le());
    header_.flags = r.read_u32_le();
    for (auto& v : header_.audio_size) v = r.read_u32_le();
    header_.trees_size = r.read_u32_le();
    header_.mmap_size = r.read_u32_le();
    header_.mclr_size = r.read_u32_le();
    header_.full_size = r.read_u32_le();
    header_.type_size = r.read_u32_le();
    for (auto& v : header_.audio_rate) v = r.read_u32_le();
    // The trailing "dummy" u32 completes the 104-byte header.
    (void)r.read_u32_le();
    if (!r.ok()) {
        return SmackerError::TooSmall;
    }

    // Reject implausible geometry rather than allocating on a corrupt header.
    constexpr std::uint32_t kMaxDimension = 4096;
    constexpr std::uint32_t kMaxFrames = 100000;
    if (header_.width == 0 || header_.height == 0 ||
        header_.width > kMaxDimension || header_.height > kMaxDimension ||
        header_.frame_count == 0 || header_.frame_count > kMaxFrames) {
        return SmackerError::BadDimensions;
    }

    // Header flags. Bit 0 announces a trailing "ring" frame, which exists so a
    // player can loop seamlessly: it is a real entry in the frame tables, so
    // missing it desynchronizes every frame offset. Bits 1 and 2 select
    // vertical scaling of a half-height picture.
    constexpr std::uint32_t kFlagRingFrame = 0x01;
    constexpr std::uint32_t kFlagYInterlace = 0x02;
    constexpr std::uint32_t kFlagYDouble = 0x04;

    info_.ring_frame = (header_.flags & kFlagRingFrame) != 0;
    if ((header_.flags & kFlagYInterlace) != 0) {
        info_.y_scale = YScale::Interlace;
    } else if ((header_.flags & kFlagYDouble) != 0) {
        info_.y_scale = YScale::Double;
    }

    coded_height_ = header_.height;
    if (info_.y_scale != YScale::None) {
        header_.height = coded_height_ * 2;
    }

    // The frame tables cover the ring frame too, when there is one.
    table_frames_ = header_.frame_count + (info_.ring_frame ? 1u : 0u);

    // Frame sizes, then frame types, then the tree block, then the frames.
    const std::uint64_t sizes_bytes = static_cast<std::uint64_t>(table_frames_) * 4;
    const std::uint64_t types_end =
        kSmackerHeaderSize + sizes_bytes + table_frames_;
    if (types_end > data_.size()) {
        return SmackerError::Truncated;
    }

    frame_sizes_.resize(table_frames_);
    for (std::uint32_t i = 0; i < table_frames_; ++i) {
        if (!r.seek(kSmackerHeaderSize + static_cast<std::size_t>(i) * 4)) {
            return SmackerError::Truncated;
        }
        frame_sizes_[i] = r.read_u32_le();
    }
    frame_types_.resize(table_frames_);
    for (std::uint32_t i = 0; i < table_frames_; ++i) {
        if (!r.seek(static_cast<std::size_t>(kSmackerHeaderSize + sizes_bytes + i))) {
            return SmackerError::Truncated;
        }
        frame_types_[i] = r.read_u8();
    }
    if (!r.ok()) {
        return SmackerError::Truncated;
    }

    trees_offset_ = static_cast<std::size_t>(types_end);

    // Frame data begins after the tree block. The low two bits of each stored
    // size are flags (bit 0 marks a keyframe), not part of the length.
    frame_offsets_.assign(table_frames_ + 1, 0);
    frame_offsets_[0] = types_end + header_.trees_size;
    keyframes_.clear();
    for (std::uint32_t i = 0; i < table_frames_; ++i) {
        if ((frame_sizes_[i] & 1u) != 0) {
            keyframes_.push_back(i);
        }
        frame_offsets_[i + 1] =
            frame_offsets_[i] + (frame_sizes_[i] & ~std::uint32_t{3});
    }
    if (frame_offsets_.back() > data_.size()) {
        return SmackerError::Truncated;
    }

    frame_buffer_.assign(static_cast<std::size_t>(header_.width) * header_.height, 0);

    // Until a frame supplies one, use a grayscale ramp so a video that never
    // sends a palette still renders something meaningful.
    for (std::size_t i = 0; i < 256; ++i) {
        palette_[i * 3 + 0] = static_cast<std::uint8_t>(i);
        palette_[i * 3 + 1] = static_cast<std::uint8_t>(i);
        palette_[i * 3 + 2] = static_cast<std::uint8_t>(i);
    }

    info_.width = header_.width;
    info_.height = header_.height;
    info_.frame_count = header_.frame_count;
    // Frame rate: positive is milliseconds per frame, negative is a count of
    // ten-microsecond units, zero means the default 10 fps. Both signs occur in
    // MM6 — 100 (10 fps) and -10000 (also 10 fps) are the two common values.
    if (header_.frame_rate > 0) {
        info_.fps = 1000.0 / header_.frame_rate;
    } else if (header_.frame_rate < 0) {
        info_.fps = 100000.0 / -static_cast<double>(header_.frame_rate);
    } else {
        info_.fps = 10.0;
    }
    info_.has_audio = std::any_of(std::begin(header_.audio_size),
                                  std::end(header_.audio_size),
                                  [](std::uint32_t s) { return s != 0; });
    last_decoded_ = kNoFrame;
    return SmackerError::None;
}

SmackerError SmackerDecoder::build_trees() {
    trees_ = std::make_unique<Trees>();
    if (header_.trees_size == 0) {
        return SmackerError::None;  // every tree stays empty; frames decode as skips
    }
    if (trees_offset_ + header_.trees_size > data_.size()) {
        return SmackerError::Truncated;
    }

    BitReader bits{std::span<const std::byte>{data_}.subspan(trees_offset_,
                                                             header_.trees_size)};
    if (!trees_->mmap.build(bits) || !trees_->mclr.build(bits) ||
        !trees_->full.build(bits) || !trees_->type.build(bits)) {
        return SmackerError::BadTrees;
    }
    return SmackerError::None;
}

bool SmackerDecoder::is_keyframe(std::uint32_t index) const noexcept {
    return index < frame_sizes_.size() && (frame_sizes_[index] & 1u) != 0;
}

SmackerError SmackerDecoder::decode_frame(std::uint32_t index,
                                          std::span<const std::uint8_t>& out) {
    out = {};
    if (index >= header_.frame_count) {
        return SmackerError::NoSuchFrame;
    }

    // Frames are deltas, so reaching `index` means replaying from wherever the
    // buffer currently is: the next frame if we are already just behind it,
    // otherwise the nearest keyframe at or before it.
    std::uint32_t start = 0;
    bool clear = false;
    if (last_decoded_ != kNoFrame && last_decoded_ < index) {
        start = last_decoded_ + 1;
    } else if (last_decoded_ != index) {
        const auto it = std::upper_bound(keyframes_.begin(), keyframes_.end(), index);
        if (it != keyframes_.begin()) {
            start = *(it - 1);
        }
        clear = true;
    } else {
        out = frame_buffer_;
        return SmackerError::None;  // already decoded
    }

    if (clear) {
        std::fill(frame_buffer_.begin(), frame_buffer_.end(), std::uint8_t{0});
    }
    for (std::uint32_t i = start; i <= index; ++i) {
        if (const SmackerError e = decode_one(i); e != SmackerError::None) {
            return e;
        }
    }
    last_decoded_ = index;
    out = frame_buffer_;
    return SmackerError::None;
}

SmackerError SmackerDecoder::decode_frame_rgba(std::uint32_t index,
                                               std::span<const std::uint8_t>& out) {
    out = {};
    std::span<const std::uint8_t> indexed;
    if (const SmackerError e = decode_frame(index, indexed); e != SmackerError::None) {
        return e;
    }
    rgba_.resize(indexed.size() * 4);
    for (std::size_t i = 0; i < indexed.size(); ++i) {
        const std::size_t p = static_cast<std::size_t>(indexed[i]) * 3;
        rgba_[i * 4 + 0] = palette_[p + 0];
        rgba_[i * 4 + 1] = palette_[p + 1];
        rgba_[i * 4 + 2] = palette_[p + 2];
        rgba_[i * 4 + 3] = 255;
    }
    out = rgba_;
    return SmackerError::None;
}

SmackerError SmackerDecoder::decode_one(std::uint32_t index) {
    const std::uint64_t begin = frame_offsets_[index];
    const std::uint32_t size = frame_sizes_[index] & ~std::uint32_t{3};
    if (begin + size > data_.size()) {
        return SmackerError::Truncated;
    }
    const std::span<const std::byte> frame =
        std::span<const std::byte>{data_}.subspan(static_cast<std::size_t>(begin), size);

    std::size_t pos = 0;
    const std::uint8_t type = frame_types_[index];

    // Bit 0: a palette record leads the frame. Its first byte is the record
    // length in units of four bytes, counting itself.
    if ((type & 1u) != 0) {
        if (pos >= frame.size()) {
            return SmackerError::Truncated;
        }
        const auto units = static_cast<std::size_t>(frame[pos++]);
        std::size_t bytes = units * 4;
        bytes = (bytes > 0) ? bytes - 1 : 0;
        bytes = std::min(bytes, frame.size() - pos);
        if (bytes > 0) {
            decode_palette(frame.subspan(pos, bytes));
            pos += bytes;
        }
    }

    // Bits 1..7: one audio chunk per track, each prefixed by its own length.
    // This slice locates them only to find where the video data starts.
    for (std::size_t track = 0; track < kSmackerAudioTracks; ++track) {
        if ((type & (2u << track)) == 0) {
            continue;
        }
        if (pos + 4 > frame.size()) {
            return SmackerError::Truncated;
        }
        std::uint32_t chunk = 0;
        for (int b = 0; b < 4; ++b) {
            chunk |= static_cast<std::uint32_t>(frame[pos + b]) << (8 * b);
        }
        pos += 4;
        chunk &= 0x00FFFFFFu;   // the top byte is not part of the length
        if (chunk > 4) {
            const std::size_t skip = chunk - 4;
            if (skip > frame.size() - pos) {
                return SmackerError::Truncated;
            }
            pos += skip;
        }
    }

    if (pos >= frame.size()) {
        return SmackerError::None;  // palette-only frame; picture is unchanged
    }

    BitReader bits{frame.subspan(pos)};
    if (trees_) {
        trees_->reset_caches();
    }
    return decode_video(bits) ? SmackerError::None : SmackerError::Truncated;
}

bool SmackerDecoder::decode_video(BitReader& bits) {
    if (!trees_) {
        return true;
    }

    const std::uint32_t blocks_wide = (header_.width + 3) / 4;
    const std::uint32_t blocks_high = (coded_height_ + 3) / 4;
    const std::uint32_t total = blocks_wide * blocks_high;
    const std::uint32_t row_step = (info_.y_scale == YScale::None) ? 1u : 2u;
    const std::uint32_t block_step = 4 * row_step;

    std::uint32_t block = 0;
    while (block < total) {
        if (bits.at_end()) {
            // Running out mid-picture leaves the rest of the frame as it was,
            // which is what the format's skip blocks would have done anyway.
            return true;
        }
        const std::uint16_t descriptor = trees_->type.decode(bits);
        const auto type = static_cast<BlockType>(descriptor & 3u);
        const std::uint32_t run = kBlockRuns[(descriptor >> 2) & 0x3Fu];
        const auto data = static_cast<std::uint8_t>((descriptor >> 8) & 0xFFu);

        for (std::uint32_t i = 0; i < run && block < total; ++i, ++block) {
            const std::uint32_t x = (block % blocks_wide) * 4;
            const std::uint32_t y = (block / blocks_wide) * block_step;
            switch (type) {
                case BlockType::Skip:
                    break;  // keep the previous frame's pixels
                case BlockType::Fill:
                    fill_block(x, y, data);
                    break;
                case BlockType::Mono: {
                    const std::uint16_t colors = trees_->mclr.decode(bits);
                    const std::uint16_t bitmap = trees_->mmap.decode(bits);
                    mono_block(x, y, static_cast<std::uint8_t>(colors & 0xFF),
                               static_cast<std::uint8_t>(colors >> 8), bitmap);
                    break;
                }
                case BlockType::Full:
                    full_block(bits, x, y);
                    break;
            }
        }
    }
    return true;
}

void SmackerDecoder::put_pixel(std::uint32_t x, std::uint32_t y, std::uint8_t color) {
    if (x >= header_.width || y >= header_.height) {
        return;
    }
    frame_buffer_[static_cast<std::size_t>(y) * header_.width + x] = color;
    // Y-doubled videos paint the row below as well; interlaced ones leave it.
    if (info_.y_scale == YScale::Double && y + 1 < header_.height) {
        frame_buffer_[static_cast<std::size_t>(y + 1) * header_.width + x] = color;
    }
}

void SmackerDecoder::fill_block(std::uint32_t x, std::uint32_t y, std::uint8_t color) {
    const std::uint32_t step = (info_.y_scale == YScale::None) ? 1u : 2u;
    for (std::uint32_t row = 0; row < 4; ++row) {
        for (std::uint32_t col = 0; col < 4; ++col) {
            put_pixel(x + col, y + row * step, color);
        }
    }
}

void SmackerDecoder::mono_block(std::uint32_t x, std::uint32_t y, std::uint8_t c0,
                                std::uint8_t c1, std::uint16_t bitmap) {
    const std::uint32_t step = (info_.y_scale == YScale::None) ? 1u : 2u;
    for (std::uint32_t row = 0; row < 4; ++row) {
        for (std::uint32_t col = 0; col < 4; ++col) {
            const std::uint32_t bit = row * 4 + col;
            const std::uint8_t color = ((bitmap >> bit) & 1u) != 0 ? c1 : c0;
            put_pixel(x + col, y + row * step, color);
        }
    }
}

void SmackerDecoder::full_block(BitReader& bits, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t step = (info_.y_scale == YScale::None) ? 1u : 2u;

    // SMK4 prefixes full blocks with a mode selector; SMK2 always uses mode 0.
    int mode = 0;
    if (info_.version4) {
        if (bits.read_bit()) {
            mode = 1;
        } else if (bits.read_bit()) {
            mode = 2;
        }
    }

    // Each decoded word holds two pixels: low byte left, high byte right. The
    // right-hand pair of a row is always read before the left-hand pair.
    auto put_row = [&](std::uint32_t row, std::uint16_t left, std::uint16_t right) {
        const std::uint32_t ty = y + row * step;
        put_pixel(x + 0, ty, static_cast<std::uint8_t>(left & 0xFF));
        put_pixel(x + 1, ty, static_cast<std::uint8_t>(left >> 8));
        put_pixel(x + 2, ty, static_cast<std::uint8_t>(right & 0xFF));
        put_pixel(x + 3, ty, static_cast<std::uint8_t>(right >> 8));
    };

    switch (mode) {
        case 0:
            // Four rows, two words each.
            for (std::uint32_t row = 0; row < 4; ++row) {
                const std::uint16_t right = trees_->full.decode(bits);
                const std::uint16_t left = trees_->full.decode(bits);
                put_row(row, left, right);
            }
            break;
        case 1:
            // One word per row pair: its low byte fills the left half of both
            // rows, its high byte the right half.
            for (std::uint32_t half = 0; half < 2; ++half) {
                const std::uint16_t word = trees_->full.decode(bits);
                const auto lo = static_cast<std::uint8_t>(word & 0xFF);
                const auto hi = static_cast<std::uint8_t>(word >> 8);
                for (std::uint32_t row = 0; row < 2; ++row) {
                    const std::uint32_t ty = y + (half * 2 + row) * step;
                    put_pixel(x + 0, ty, lo);
                    put_pixel(x + 1, ty, lo);
                    put_pixel(x + 2, ty, hi);
                    put_pixel(x + 3, ty, hi);
                }
            }
            break;
        default:
            // Two words per row pair, repeated across both rows.
            for (std::uint32_t half = 0; half < 2; ++half) {
                const std::uint16_t right = trees_->full.decode(bits);
                const std::uint16_t left = trees_->full.decode(bits);
                for (std::uint32_t row = 0; row < 2; ++row) {
                    put_row(half * 2 + row, left, right);
                }
            }
            break;
    }
}

SmackerAudioInfo SmackerDecoder::audio_info(std::size_t track) const {
    SmackerAudioInfo info;
    if (track >= kSmackerAudioTracks) {
        return info;
    }
    // The rate word packs flags above the rate. The stereo and depth bits are
    // confirmed against the chunks themselves, which restate both: they agree
    // on all 77 MM6 tracks, with no mismatches.
    //
    // Bits 31 and 30 are set on every MM6 track, so this data cannot say which
    // is "compressed" and which is "present"; the assignment below follows the
    // usual convention and nothing here depends on telling them apart.
    const std::uint32_t word = header_.audio_rate[track];
    info.compressed = (word & 0x80000000u) != 0;
    info.present = (word & 0x40000000u) != 0 && header_.audio_size[track] != 0;
    info.is_16bit = (word & 0x20000000u) != 0;
    info.stereo = (word & 0x10000000u) != 0;
    info.bink_audio = (word & 0x08000000u) != 0;
    info.sample_rate = word & 0x00FFFFFFu;
    return info;
}

bool SmackerDecoder::audio_chunk(std::uint32_t index, std::size_t track,
                                 std::span<const std::byte>& out) const {
    out = {};
    if (index >= frame_types_.size()) {
        return false;
    }
    const std::uint64_t begin = frame_offsets_[index];
    const std::uint32_t size = frame_sizes_[index] & ~std::uint32_t{3};
    if (begin + size > data_.size()) {
        return false;
    }
    const std::span<const std::byte> frame =
        std::span<const std::byte>{data_}.subspan(static_cast<std::size_t>(begin), size);

    std::size_t pos = 0;
    const std::uint8_t type = frame_types_[index];
    if ((type & 1u) != 0) {
        if (pos >= frame.size()) return false;
        const auto units = static_cast<std::size_t>(frame[pos++]);
        std::size_t bytes = units * 4;
        bytes = (bytes > 0) ? bytes - 1 : 0;
        pos += std::min(bytes, frame.size() - pos);
    }

    for (std::size_t t = 0; t < kSmackerAudioTracks; ++t) {
        if ((type & (2u << t)) == 0) {
            continue;
        }
        if (pos + 4 > frame.size()) return false;
        std::uint32_t chunk = 0;
        for (int b = 0; b < 4; ++b) {
            chunk |= static_cast<std::uint32_t>(frame[pos + b]) << (8 * b);
        }
        chunk &= 0x00FFFFFFu;   // the chunk length includes its own 4-byte field
        pos += 4;
        const std::size_t payload = (chunk > 4) ? chunk - 4 : 0;
        if (payload > frame.size() - pos) return false;
        if (t == track) {
            out = frame.subspan(pos, payload);
            return payload > 0;
        }
        pos += payload;
    }
    return false;
}

SmackerError SmackerDecoder::decode_audio(std::uint32_t index, std::size_t track,
                                          SmackerAudioFrame& out) {
    out = SmackerAudioFrame{};
    if (index >= header_.frame_count || track >= kSmackerAudioTracks) {
        return SmackerError::NoSuchFrame;
    }
    const SmackerAudioInfo info = audio_info(track);
    if (!info.present || info.bink_audio) {
        return SmackerError::NoAudio;
    }

    std::span<const std::byte> chunk;
    if (!audio_chunk(index, track, chunk)) {
        return SmackerError::None;   // this frame simply carries no audio
    }

    const std::uint8_t channels = info.stereo ? 2 : 1;
    out.sample_rate = info.sample_rate;
    out.channels = channels;

    if (!info.compressed) {
        // Raw PCM. Eight-bit samples are unsigned, so recentre them.
        if (info.is_16bit) {
            const std::size_t count = chunk.size() / 2;
            out.samples.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                out.samples[i] = static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(chunk[i*2]) |
                    (static_cast<std::uint16_t>(chunk[i*2 + 1]) << 8));
            }
        } else {
            out.samples.resize(chunk.size());
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                out.samples[i] = static_cast<std::int16_t>(
                    (static_cast<int>(chunk[i]) - 128) << 8);
            }
        }
        return SmackerError::None;
    }

    BitReader bits{chunk};
    const std::uint32_t unpacked = bits.read_bits(32);
    constexpr std::uint32_t kMaxUnpacked = 10u * 1024 * 1024;
    if (unpacked == 0 || unpacked > kMaxUnpacked) {
        return SmackerError::Truncated;
    }
    if (!bits.read_bit()) {
        return SmackerError::None;   // the chunk declares itself empty
    }
    // The chunk restates its own layout; prefer it over the header, which is
    // only needed for the sample rate.
    const bool stereo = bits.read_bit();
    const bool is_16bit = bits.read_bit();
    out.channels = stereo ? 2 : 1;

    // One tree per byte per channel: two for 16-bit, doubled again for stereo.
    const std::size_t tree_count =
        static_cast<std::size_t>(is_16bit ? 2 : 1) * (stereo ? 2 : 1);
    std::array<ByteTree, 4> trees;
    for (std::size_t i = 0; i < tree_count; ++i) {
        if (!trees[i].build(bits, /*with_presence*/ false)) {
            return SmackerError::BadTrees;
        }
    }

    // Base samples, right channel first when stereo.
    const std::size_t channel_count = stereo ? 2u : 1u;
    std::array<std::int16_t, 2> base{};
    if (is_16bit) {
        for (std::size_t c = 0; c < channel_count; ++c) {
            const auto hi = static_cast<std::uint16_t>(bits.read_bits(8));
            const auto lo = static_cast<std::uint16_t>(bits.read_bits(8));
            base[channel_count - 1 - c] =
                static_cast<std::int16_t>((hi << 8) | lo);
        }
    } else {
        for (std::size_t c = 0; c < channel_count; ++c) {
            const auto v = static_cast<int>(bits.read_bits(8));
            base[channel_count - 1 - c] =
                static_cast<std::int16_t>((v - 128) << 8);
        }
    }

    const std::size_t bytes_per_sample = is_16bit ? 2u : 1u;
    const std::size_t total =
        (unpacked / (bytes_per_sample * channel_count)) * channel_count;
    out.samples.resize(total);

    std::size_t at = 0;
    for (std::size_t c = 0; c < channel_count && at < total; ++c) {
        out.samples[at++] = base[c];
    }

    // Deltas wrap rather than clamp: that is the format's arithmetic, and
    // clamping here would audibly distort loud passages.
    while (at < total && !bits.at_end()) {
        for (std::size_t c = 0; c < channel_count && at < total; ++c) {
            if (is_16bit) {
                const auto lo = static_cast<std::uint16_t>(trees[c*2].lookup(bits));
                const auto hi = static_cast<std::uint16_t>(trees[c*2 + 1].lookup(bits));
                const auto delta = static_cast<std::uint16_t>((hi << 8) | lo);
                const auto prev = static_cast<std::uint16_t>(base[c]);
                base[c] = static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(prev + delta));
            } else {
                const auto delta = static_cast<std::int8_t>(trees[c].lookup(bits));
                const auto prev = static_cast<std::uint8_t>((base[c] >> 8) + 128);
                const auto next = static_cast<std::uint8_t>(prev + delta);
                base[c] = static_cast<std::int16_t>((static_cast<int>(next) - 128) << 8);
            }
            out.samples[at++] = base[c];
        }
    }
    return SmackerError::None;
}

void SmackerDecoder::decode_palette(std::span<const std::byte> data) {
    // The record rebuilds the palette from the previous one: entries are either
    // skipped (keeping the old color), copied from an old index, or given as a
    // literal 6-bit RGB triple.
    const std::vector<std::uint8_t> previous = palette_;

    std::size_t pos = 0;
    std::size_t index = 0;
    while (pos < data.size() && index < 256) {
        const auto code = static_cast<std::uint8_t>(data[pos++]);

        if ((code & 0x80u) != 0) {
            index += (code & 0x7Fu) + 1u;
        } else if ((code & 0x40u) != 0) {
            if (pos >= data.size()) {
                break;
            }
            const auto source = static_cast<std::uint8_t>(data[pos++]);
            const std::uint32_t count = (code & 0x3Fu) + 1u;
            for (std::uint32_t i = 0; i < count && index < 256; ++i, ++index) {
                const std::size_t src = (static_cast<std::size_t>(source) + i) * 3;
                if (src + 2 >= previous.size()) {
                    break;
                }
                palette_[index * 3 + 0] = previous[src + 0];
                palette_[index * 3 + 1] = previous[src + 1];
                palette_[index * 3 + 2] = previous[src + 2];
            }
        } else {
            if (pos + 2 > data.size()) {
                break;
            }
            // Six-bit channels, widened to eight so white stays white.
            const auto expand = [](std::uint8_t six) {
                return static_cast<std::uint8_t>((six << 2) | (six >> 4));
            };
            const std::uint8_t r = code & 0x3Fu;
            const auto g = static_cast<std::uint8_t>(data[pos++]) & 0x3Fu;
            const auto b = static_cast<std::uint8_t>(data[pos++]) & 0x3Fu;
            palette_[index * 3 + 0] = expand(r);
            palette_[index * 3 + 1] = expand(static_cast<std::uint8_t>(g));
            palette_[index * 3 + 2] = expand(static_cast<std::uint8_t>(b));
            ++index;
        }
    }
}

}  // namespace starhaven::video
