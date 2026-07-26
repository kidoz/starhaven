#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/random.hpp"

using starhaven::Mm6Random;

TEST_CASE("MM6 random advances the shared 32-bit LCG", "[random]") {
    Mm6Random random(1);

    REQUIRE(random.next() == 41);
    REQUIRE(random.next() == 18467);
    REQUIRE(random.next() == 6334);
    REQUIRE(random.next() == 26500);
    REQUIRE(random.next() == 19169);
    REQUIRE(random.state() == UINT32_C(0xCAE1DF84));
}

TEST_CASE("MM6 random arithmetic wraps at 32 bits", "[random]") {
    Mm6Random random(UINT32_MAX);

    REQUIRE(random.next() == 35);
    REQUIRE(random.state() == UINT32_C(0x00235AC6));
}
