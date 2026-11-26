#include <catch2/catch_test_macros.hpp>
#include <zub/core/package.hpp>

#include <filesystem>
#include <fstream>
#include "Unique.hpp"

namespace fs = std::filesystem;

TEST_CASE("a package checkout has the style file and scripts/build.sh", "[package]") {
    const fs::path root = fs::temp_directory_path() / ("zub-package-" + zubtest::uniqueSuffix());
    fs::remove_all(root);
    const fs::path own = root / "zubuvzpevnik";
    fs::create_directories(own / "scripts");
    CHECK_FALSE(zub::package::isPackage(own)); // empty: an unchecked-out submodule
    std::ofstream(own / "zubuvzpevnik.sty") << "x";
    CHECK_FALSE(zub::package::isPackage(own)); // the layout before the package had its own repo
    std::ofstream(own / "scripts" / "build.sh") << "x";
    CHECK(zub::package::isPackage(own));
    CHECK_FALSE(zub::package::isPackage(root / "nope"));
    fs::remove_all(root);
}

TEST_CASE("the package defines \\transposesong in its style or a feature file", "[package]") {
    const fs::path dir = fs::temp_directory_path() / ("zub-package-tr-" + zubtest::uniqueSuffix());
    fs::remove_all(dir);
    fs::create_directories(dir / "feature");
    CHECK_FALSE(zub::package::definesTransposesong(dir));
    std::ofstream(dir / "zubuvzpevnik.sty") << "% \\transposesong is coming\n\\newcommand\\other{}\n";
    CHECK_FALSE(zub::package::definesTransposesong(dir)); // a comment is not a definition
    std::ofstream(dir / "feature" / "transpozice.cfg")
        << "\\newcommand\\transposesong[2][]{%\n}\n";
    CHECK(zub::package::definesTransposesong(dir));
    fs::remove_all(dir);
}
