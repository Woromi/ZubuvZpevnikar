#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;

namespace {

// Fresh unique directory under the system temp dir, removed on destruction.
struct TempDir {
    fs::path path;

    TempDir() {
        std::random_device device;
        path = fs::temp_directory_path() /
               ("zub-textfile-test-" + std::to_string(device()) + "-" + std::to_string(device()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::vector<std::string> entries(const fs::path& dir) {
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(dir))
        names.push_back(e.path().filename().string());
    std::ranges::sort(names);
    return names;
}

} // namespace

TEST_CASE("writeTextFileAtomic round-trips bytes exactly", "[textfile]") {
    const std::string content = GENERATE(std::string("no final newline"),
                                         std::string("a\xC2\xA0"
                                                     "b"),
                                         std::string("a\tb\t\tc\n"), std::string("a\r\nb\r\n"),
                                         std::string("\xC5\xBDlu\xC5\xA5ou\xC4\x8D"
                                                     "k\xC3\xBD k\xC5\xAF\xC5\x88"),
                                         std::string());
    TempDir dir;
    const auto file = dir.path / "song.tex";

    REQUIRE(zub::writeTextFileAtomic(file, content));

    const auto read = zub::readTextFile(file);
    REQUIRE(read);
    CHECK(*read == content);
    CHECK(slurp(file) == content);
}

TEST_CASE("writeTextFileAtomic replaces existing content fully", "[textfile]") {
    TempDir dir;
    const auto file = dir.path / "song.tex";
    REQUIRE(zub::writeTextFileAtomic(file, "a much longer original content\nline 2\n"));
    REQUIRE(zub::writeTextFileAtomic(file, "short"));
    CHECK(slurp(file) == "short");
}

TEST_CASE("writeTextFileAtomic keeps permissions", "[textfile]") {
#ifdef _WIN32
    SKIP("POSIX permissions only");
#else
    TempDir dir;
    const auto file = dir.path / "song.tex";
    {
        std::ofstream out(file, std::ios::binary);
        out << "old";
    }
    const auto perms = fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read;
    fs::permissions(file, perms, fs::perm_options::replace);

    REQUIRE(zub::writeTextFileAtomic(file, "new"));

    CHECK(fs::status(file).permissions() == perms);
    CHECK(slurp(file) == "new");
#endif
}

TEST_CASE("textfile errors name the file", "[textfile]") {
    TempDir dir;

    SECTION("write into a missing parent directory") {
        const auto file = dir.path / "missing-dir" / "song.tex";
        const auto result = zub::writeTextFileAtomic(file, "x");
        REQUIRE_FALSE(result);
        CHECK_FALSE(result.error().empty());
        CHECK(result.error().find("song.tex") != std::string::npos);
        CHECK_FALSE(fs::exists(dir.path / "missing-dir"));
    }
    SECTION("read a missing file") {
        const auto result = zub::readTextFile(dir.path / "nope.tex");
        REQUIRE_FALSE(result);
        CHECK(result.error().find("nope.tex") != std::string::npos);
        CHECK(result.error().size() > std::string("nope.tex: ").size());
    }
    SECTION("read a directory") {
        fs::create_directory(dir.path / "sub");
        const auto result = zub::readTextFile(dir.path / "sub");
        REQUIRE_FALSE(result);
        CHECK(result.error().find("sub") != std::string::npos);
    }
}

TEST_CASE("writeTextFileAtomic leaves no temporary file", "[textfile]") {
    TempDir dir;

    SECTION("after success") {
        REQUIRE(zub::writeTextFileAtomic(dir.path / "a.tex", "one"));
        REQUIRE(zub::writeTextFileAtomic(dir.path / "a.tex", "two"));
        CHECK(entries(dir.path) == std::vector<std::string>{"a.tex"});
    }
    SECTION("after a failed rename") {
        // Renaming a regular file over a non-empty directory fails.
        const auto target = dir.path / "target";
        fs::create_directory(target);
        std::ofstream(target / "inner.txt") << "x";

        const auto result = zub::writeTextFileAtomic(target, "x");
        REQUIRE_FALSE(result);
        CHECK(result.error().find("target") != std::string::npos);
        CHECK(entries(dir.path) == std::vector<std::string>{"target"});
        CHECK(entries(target) == std::vector<std::string>{"inner.txt"});
    }
}

TEST_CASE("writeTextFileAtomic writes through a symlink", "[textfile]") {
#ifdef _WIN32
    SKIP("POSIX symlinks only");
#else
    TempDir dir;
    const auto target = dir.path / "real.tex";
    const auto link = dir.path / "link.tex";
    REQUIRE(zub::writeTextFileAtomic(target, "old"));
    fs::create_symlink(target.filename(), link);

    REQUIRE(zub::writeTextFileAtomic(link, "new"));

    CHECK(fs::is_symlink(link));
    CHECK(slurp(target) == "new");
    CHECK(slurp(link) == "new");
    CHECK(entries(dir.path) == std::vector<std::string>{"link.tex", "real.tex"});
#endif
}

TEST_CASE("writeTextFileAtomic fails cleanly in a read-only directory", "[textfile]") {
#ifdef _WIN32
    SKIP("POSIX permissions only");
#else
    TempDir dir;
    const auto sub = dir.path / "ro";
    fs::create_directory(sub);
    const auto file = sub / "song.tex";
    REQUIRE(zub::writeTextFileAtomic(file, "original"));
    fs::permissions(sub, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);

    // Running as root ignores directory permissions: detect that and skip.
    {
        std::ofstream probe(sub / "probe");
        if (probe) {
            probe.close();
            fs::permissions(sub, fs::perms::owner_all, fs::perm_options::replace);
            SKIP("directory permissions are not enforced (running as root?)");
        }
    }

    const auto result = zub::writeTextFileAtomic(file, "changed");
    fs::permissions(sub, fs::perms::owner_all, fs::perm_options::replace);

    REQUIRE_FALSE(result);
    CHECK(result.error().find("song.tex") != std::string::npos);
    CHECK(slurp(file) == "original");
    CHECK(entries(sub) == std::vector<std::string>{"song.tex"});
#endif
}
