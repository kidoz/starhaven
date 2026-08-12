// Startup-media fixtures are synthetic Smacker structures assembled here.
// They contain no frames, audio, or other bytes copied from the game.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "game/startup_media.hpp"

using namespace starhaven::game;

namespace {

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

    void absent_tree() { bit(false); }

    void single_byte_tree(std::uint8_t value) {
        bit(true);
        bit(false);
        bits(value, 8);
        bit(false);
    }

    void single_word_tree(std::uint16_t value) {
        bit(true);
        single_byte_tree(static_cast<std::uint8_t>(value & 0xff));
        single_byte_tree(static_cast<std::uint8_t>(value >> 8));
        for (int i = 0; i < 3; ++i) {
            bits(0, 16);
        }
        bit(false);
        bit(false);
    }

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

void put_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[offset + static_cast<std::size_t>(i)] =
            static_cast<std::byte>((value >> (8 * i)) & 0xff);
    }
}

[[nodiscard]] std::vector<std::byte> one_frame_video() {
    BitWriter trees;
    trees.absent_tree();
    trees.absent_tree();
    trees.absent_tree();
    trees.single_word_tree(0x2a03);  // fill one block with palette index 0x2a
    std::vector<std::byte> tree_bytes = trees.take();

    std::vector<std::byte> bytes(starhaven::video::kSmackerHeaderSize, std::byte{0});
    std::copy_n(reinterpret_cast<const std::byte*>("SMK2"), 4, bytes.begin());
    put_u32(bytes, 0x04, 8);
    put_u32(bytes, 0x08, 8);
    put_u32(bytes, 0x0c, 1);
    put_u32(bytes, 0x10, static_cast<std::uint32_t>(-10000));
    put_u32(bytes, 0x34, static_cast<std::uint32_t>(tree_bytes.size()));

    const std::uint32_t frame_size = 4;
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::byte>(((frame_size | 1u) >> (8 * i)) & 0xff));
    }
    bytes.push_back(std::byte{0});  // frame type
    bytes.insert(bytes.end(), tree_bytes.begin(), tree_bytes.end());
    bytes.insert(bytes.end(), frame_size, std::byte{0});
    return bytes;
}

}  // namespace

TEST_CASE("missing and malformed optional reels report distinct outcomes", "[startup_media]") {
    StartupMediaController media;

    auto report = media.begin("missing", {});
    REQUIRE(report.has_value());
    const StartupMediaReport missing = report.value_or(StartupMediaReport{});
    REQUIRE(missing.outcome == StartupMediaOutcome::Unavailable);
    REQUIRE(missing.reel == "missing");
    REQUIRE_FALSE(media.active());

    const std::vector<std::byte> malformed(32, std::byte{0});
    report = media.begin("malformed", malformed);
    REQUIRE(report.has_value());
    const StartupMediaReport malformed_report = report.value_or(StartupMediaReport{});
    REQUIRE(malformed_report.outcome == StartupMediaOutcome::DecodeFailed);
    REQUIRE(malformed_report.reel == "malformed");
    REQUIRE_FALSE(media.active());
}

TEST_CASE("natural completion presents the last frame before releasing the reel",
          "[startup_media]") {
    StartupMediaController media;
    const auto bytes = one_frame_video();
    REQUIRE(media.begin("synthetic", bytes).has_value() == false);
    REQUIRE(media.active());

    StartupMediaFrame frame;
    REQUIRE_FALSE(media.advance(frame).has_value());
    REQUIRE(frame.width == 8);
    REQUIRE(frame.height == 8);
    REQUIRE(frame.rgba.size() == std::size_t{8} * 8 * 4);
    REQUIRE(frame.fps == 10.0);
    REQUIRE(media.active());

    const auto report = media.advance(frame);
    REQUIRE(report.has_value());
    const StartupMediaReport finished = report.value_or(StartupMediaReport{});
    REQUIRE(finished.outcome == StartupMediaOutcome::Finished);
    REQUIRE(finished.reel == "synthetic");
    REQUIRE(frame.rgba.empty());
    REQUIRE_FALSE(media.active());
    REQUIRE(media.reel().empty());
}

TEST_CASE("skip releases decoder and audio state immediately", "[startup_media]") {
    StartupMediaController media;
    const auto bytes = one_frame_video();
    REQUIRE(media.begin("synthetic", bytes).has_value() == false);

    const auto report = media.skip();

    REQUIRE(report.has_value());
    const StartupMediaReport skipped = report.value_or(StartupMediaReport{});
    REQUIRE(skipped.outcome == StartupMediaOutcome::Skipped);
    REQUIRE_FALSE(media.active());
    StartupMediaFrame frame;
    REQUIRE_FALSE(media.advance(frame).has_value());
    REQUIRE(frame.rgba.empty());
}

TEST_CASE("window close can release an active reel and quit immediately", "[startup_media]") {
    StartupFlow flow{StartupState::OpeningLogo};
    StartupMediaController media;
    const auto bytes = one_frame_video();
    REQUIRE(media.begin("synthetic", bytes).has_value() == false);

    media.release();
    const StartupTransition transition = flow.dispatch(StartupAction::Quit);

    REQUIRE_FALSE(media.active());
    REQUIRE(transition.to == StartupState::Quit);
    REQUIRE(transition.effect == StartupEffect::QuitApplication);
}

TEST_CASE("finished and skipped reels take the same startup transition", "[startup_media]") {
    StartupFlow natural{StartupState::OpeningLogo};
    StartupFlow keyboard_skip{StartupState::OpeningLogo};
    StartupFlow mouse_skip{StartupState::OpeningLogo};

    REQUIRE(natural.dispatch(startup_action_for_media_outcome(StartupMediaOutcome::Finished)).to ==
            StartupState::OpeningIntro);
    REQUIRE(
        keyboard_skip.dispatch(startup_action_for_media_outcome(StartupMediaOutcome::Skipped)).to ==
        StartupState::OpeningIntro);
    REQUIRE(
        mouse_skip.dispatch(startup_action_for_media_outcome(StartupMediaOutcome::Skipped)).to ==
        StartupState::OpeningIntro);
}

TEST_CASE("decode failure advances an optional reel without entering play", "[startup_media]") {
    StartupFlow flow{StartupState::OpeningIntro};

    const auto transition =
        flow.dispatch(startup_action_for_media_outcome(StartupMediaOutcome::DecodeFailed));

    REQUIRE(transition.to == StartupState::Title);
    REQUIRE_FALSE(flow.world_active());
}
