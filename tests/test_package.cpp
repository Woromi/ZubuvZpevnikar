#include <catch2/catch_test_macros.hpp>
#include <zub/core/package.hpp>

#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

TEST_CASE("a package checkout has the style file and scripts/build.sh", "[package]") {
    const fs::path root = fs::temp_directory_path() / ("zub-package-" + std::to_string(::getpid()));
    fs::remove_all(root);
    const fs::path own = root / "zubuvzpevnik";
    fs::create_directories(own / "scripts");
    CHECK_FALSE(zub::package::ofSongbook(root)); // empty: an unchecked-out submodule
    std::ofstream(own / "zubuvzpevnik.sty") << "x";
    CHECK_FALSE(zub::package::isPackage(own)); // the layout before the package had its own repo
    CHECK_FALSE(zub::package::ofSongbook(root));
    std::ofstream(own / "scripts" / "build.sh") << "x";
    CHECK(zub::package::isPackage(own));
    CHECK(zub::package::ofSongbook(root) == own);
    CHECK_FALSE(zub::package::ofSongbook(root / "nope"));
    fs::remove_all(root);
}
