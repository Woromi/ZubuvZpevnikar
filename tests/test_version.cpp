#include <catch2/catch_test_macros.hpp>
#include <zub/core/version.hpp>

TEST_CASE("version matches the CMake project version", "[version]") {
    REQUIRE(zub::version() == ZUB_EXPECTED_VERSION);
}
