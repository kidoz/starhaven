#include "core/world/object_table.hpp"

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

#include <utility>

namespace starhaven::world {

namespace {

constexpr std::size_t kEntryHeaderSize = 48;

}  // namespace

ObjectTableError ObjectTable::parse(std::span<const std::byte> entry, ObjectTable& out) {
    out.entries_.clear();
    if (entry.size() <= kEntryHeaderSize) {
        return ObjectTableError::TooSmall;
    }

    std::vector<std::uint8_t> raw;
    if (!image::detail::inflate_all(entry.subspan(kEntryHeaderSize), raw)) {
        return ObjectTableError::NotCompressed;
    }
    if (raw.size() < 4) {
        return ObjectTableError::BadCount;
    }

    io::ByteReader r(std::as_bytes(std::span<const std::uint8_t>(raw)));
    const std::uint32_t count = r.read_u32_le();
    if (!r.ok() || static_cast<std::uint64_t>(count) * kObjectDescriptorSize + 4 != raw.size()) {
        return ObjectTableError::BadCount;
    }

    out.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 4 + static_cast<std::size_t>(i) * kObjectDescriptorSize;
        ObjectDescriptor descriptor;
        if (!r.seek(base) || !r.read_fixed_string(kObjectDescriptorNameSize, descriptor.name)) {
            return ObjectTableError::BadCount;
        }
        descriptor.object_id = r.read_u16_le();
        descriptor.radius = r.read_u16_le();
        descriptor.height = r.read_u16_le();
        descriptor.flags = r.read_u16_le();
        descriptor.sprite_frame_index = r.read_u16_le();
        descriptor.lifetime = r.read_u16_le();
        if (!r.skip(2)) {  // unknown field at +0x2c; zero in the shipped table
            return ObjectTableError::BadCount;
        }
        descriptor.speed = r.read_u16_le();
        descriptor.trail_red = r.read_u8();
        descriptor.trail_green = r.read_u8();
        descriptor.trail_blue = r.read_u8();
        if (!r.skip(1) || !r.ok()) {
            return ObjectTableError::BadCount;
        }
        out.entries_.push_back(std::move(descriptor));
    }
    return ObjectTableError::None;
}

const ObjectDescriptor* ObjectTable::at(std::size_t descriptor_index) const noexcept {
    return descriptor_index < entries_.size() ? &entries_[descriptor_index] : nullptr;
}

}  // namespace starhaven::world
