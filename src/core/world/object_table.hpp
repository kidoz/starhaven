#ifndef STARHAVEN_CORE_WORLD_OBJECT_TABLE_HPP
#define STARHAVEN_CORE_WORLD_OBJECT_TABLE_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace starhaven::world {

// One global sprite-object descriptor from DOBJLIST.BIN.
struct ObjectDescriptor {
    std::string name;
    std::uint16_t object_id = 0;
    std::uint16_t radius = 0;
    std::uint16_t height = 0;
    std::uint16_t flags = 0;
    std::uint16_t sprite_frame_index = 0;
    std::uint16_t lifetime = 0;
    std::uint16_t speed = 0;
    std::uint8_t trail_red = 0;
    std::uint8_t trail_green = 0;
    std::uint8_t trail_blue = 0;
};

enum class ObjectTableError : std::uint8_t {
    None,
    TooSmall,
    NotCompressed,
    BadCount,
};

// Global object descriptors. A map sprite object's descriptor_index selects a
// row, whose object_id independently repeats the instance's object_id.
class ObjectTable {
public:
    [[nodiscard]] static ObjectTableError parse(std::span<const std::byte> entry, ObjectTable& out);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<ObjectDescriptor>& entries() const noexcept { return entries_; }
    [[nodiscard]] const ObjectDescriptor* at(std::size_t descriptor_index) const noexcept;

private:
    std::vector<ObjectDescriptor> entries_;
};

constexpr std::size_t kObjectDescriptorSize = 52;
constexpr std::size_t kObjectDescriptorNameSize = 32;

}  // namespace starhaven::world

#endif  // STARHAVEN_CORE_WORLD_OBJECT_TABLE_HPP
