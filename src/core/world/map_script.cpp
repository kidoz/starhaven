#include "core/world/map_script.hpp"

#include <cstring>

#include "core/image/zlib_util.hpp"
#include "core/io/byte_reader.hpp"

namespace starhaven::world {

namespace {

using image::detail::inflate_all;

// The same 48-byte container the design tables use, with the unpacked size at
// 0x28 and zero meaning the entry is stored as it is.
constexpr std::size_t kContainerSize = 48;
constexpr std::size_t kUnpackedSizeOffset = 0x28;

bool unwrap(std::span<const std::byte> entry, std::vector<std::uint8_t>& out) {
    if (entry.size() < kContainerSize) {
        return false;
    }
    std::uint32_t unpacked = 0;
    std::memcpy(&unpacked, entry.data() + kUnpackedSizeOffset, sizeof(unpacked));
    const auto body = entry.subspan(kContainerSize);
    if (unpacked == 0) {
        out.assign(reinterpret_cast<const std::uint8_t*>(body.data()),
                   reinterpret_cast<const std::uint8_t*>(body.data()) + body.size());
        return true;
    }
    return inflate_all(body, out) && out.size() == unpacked;
}

}  // namespace

std::optional<ScriptItemRequest> parse_script_item(const ScriptStep& step) {
    if (step.opcode != kOpcodeGenerateItem || step.arguments.size() < 6) {
        return std::nullopt;
    }
    io::ByteReader reader(std::as_bytes(std::span(step.arguments)));
    const auto level = reader.read_u8();
    const auto type = reader.read_u8();
    const auto item = reader.read_u32_le();
    return ScriptItemRequest{level, type, item};
}

std::optional<DecorationChange> parse_decoration_change(const ScriptStep& step) {
    if (step.opcode != kOpcodeSetDecoration || step.arguments.size() < 6) {
        return std::nullopt;
    }
    io::ByteReader reader(std::as_bytes(std::span(step.arguments)));
    const auto index = reader.read_i32_le();
    const bool visible = reader.read_u8() != 0;
    std::string name;
    while (!reader.eof()) {
        const auto c = reader.read_u8();
        if (c == 0) {
            if (index < 0) {
                return std::nullopt;
            }
            return DecorationChange{static_cast<std::uint32_t>(index), visible, std::move(name)};
        }
        name += static_cast<char>(c);
    }
    return std::nullopt;  // The name must terminate inside this record.
}

MapScriptError MapScript::parse(std::span<const std::byte> entry, MapScript& out) {
    out.steps_.clear();

    std::vector<std::uint8_t> payload;
    if (!unwrap(entry, payload)) {
        return MapScriptError::BadContainer;
    }

    // Records are `size` then `size` bytes: an id, a sequence number, an
    // opcode and its arguments. All 83 shipped scripts are consumed exactly.
    constexpr std::size_t kHeaderBytes = 4;  // id, sequence, opcode
    std::size_t at = 0;
    while (at < payload.size()) {
        const std::size_t size = payload[at];
        if (size < kHeaderBytes || at + 1 + size > payload.size()) {
            out.steps_.clear();
            return MapScriptError::BadRecord;
        }
        ScriptStep step;
        step.event_id = static_cast<std::uint16_t>(payload[at + 1] | (payload[at + 2] << 8));
        step.sequence = payload[at + 3];
        step.opcode = payload[at + 4];
        step.arguments.assign(payload.begin() + static_cast<std::ptrdiff_t>(at + 5),
                              payload.begin() + static_cast<std::ptrdiff_t>(at + 1 + size));
        out.steps_.push_back(std::move(step));
        at += 1 + size;
    }
    return MapScriptError::None;
}

std::span<const ScriptStep> MapScript::event(std::uint16_t id) const noexcept {
    std::size_t first = steps_.size();
    std::size_t last = steps_.size();
    for (std::size_t i = 0; i < steps_.size(); ++i) {
        if (steps_[i].event_id != id) {
            continue;
        }
        if (first == steps_.size()) {
            first = i;
        }
        last = i + 1;
    }
    if (first == steps_.size()) {
        return {};
    }
    return std::span<const ScriptStep>{steps_.data() + first, last - first};
}

MapScriptError MapStrings::parse(std::span<const std::byte> entry, MapStrings& out) {
    out.strings_.clear();

    std::vector<std::uint8_t> payload;
    if (!unwrap(entry, payload)) {
        return MapScriptError::BadContainer;
    }

    std::string current;
    for (const std::uint8_t c : payload) {
        if (c == 0) {
            out.strings_.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(static_cast<char>(c));
    }
    // A payload that does not end in a terminator still ends a string.
    if (!current.empty()) {
        out.strings_.push_back(std::move(current));
    }
    return MapScriptError::None;
}

std::string_view MapStrings::at(std::size_t index) const noexcept {
    return index < strings_.size() ? std::string_view(strings_[index]) : std::string_view{};
}

}  // namespace starhaven::world
