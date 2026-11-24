#include <catch2/catch_test_macros.hpp>
#include <zub/core/desktopentry.hpp>

using namespace zub::desktopentry;

namespace {
constexpr std::string_view kEntry = "[Desktop Entry]\n"
                                    "Type=Application\n"
                                    "Name=Zpěvníkář\n"
                                    "Exec=zpevnikar\n"
                                    "Icon=zpevnikar\n"
                                    "StartupWMClass=zpevnikar\n";
} // namespace

TEST_CASE("desktop entry: Exec and TryExec name the AppImage, the rest stays", "[desktopentry]") {
    const std::string out = forAppImage(kEntry, "/home/me/Apps/Zpevnikar-0.1.0-x86_64.AppImage");
    CHECK(out == "[Desktop Entry]\n"
                 "Type=Application\n"
                 "Name=Zpěvníkář\n"
                 "Exec=\"/home/me/Apps/Zpevnikar-0.1.0-x86_64.AppImage\"\n"
                 "TryExec=/home/me/Apps/Zpevnikar-0.1.0-x86_64.AppImage\n"
                 "X-Zpevnikar-AppImage=true\n"
                 "Icon=zpevnikar\n"
                 "StartupWMClass=zpevnikar\n");
    CHECK(writtenForAppImage(out));
    CHECK_FALSE(writtenForAppImage(kEntry));
    // Written again for a moved AppImage: one Exec, one TryExec, one marker.
    const std::string moved = forAppImage(out, "/opt/Z.AppImage");
    CHECK(moved == forAppImage(kEntry, "/opt/Z.AppImage"));
}

TEST_CASE("desktop entry: an Exec argument is quoted and escaped as the spec says",
          "[desktopentry]") {
    CHECK(execArgument("/a b/c") == "\"/a b/c\"");
    CHECK(execArgument("/50%/x") == "\"/50%%/x\"");
    CHECK(execArgument("/a\"b") == "\"/a\\\\\"b\""); // \" , then the backslash doubled
    CHECK(execArgument("/a$b`c") == "\"/a\\\\$b\\\\`c\"");
    CHECK(execArgument("/a\\b") == "\"/a\\\\\\\\b\""); // four backslashes for one
}

TEST_CASE("desktop entry: other groups and CR LF lines", "[desktopentry]") {
    const std::string out =
        forAppImage("[Desktop Entry]\r\nName=Z\r\n[Desktop Action new]\r\nExec=zpevnikar --new\r\n",
                    "/x/Z.AppImage");
    // The main group gets the Exec at its end; another group's Exec is not ours to change.
    CHECK(out == "[Desktop Entry]\nName=Z\nExec=\"/x/Z.AppImage\"\nTryExec=/x/Z.AppImage\n"
                 "X-Zpevnikar-AppImage=true\n[Desktop Action new]\nExec=zpevnikar --new\n");
    CHECK(forAppImage("", "/x/Z") ==
          "[Desktop Entry]\nExec=\"/x/Z\"\nTryExec=/x/Z\nX-Zpevnikar-AppImage=true\n");
}
