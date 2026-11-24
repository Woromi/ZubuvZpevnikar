#pragma once

#include <string>
#include <string_view>

// The app's launcher entry (a freedesktop.org .desktop file) for the menu, when it runs as an
// AppImage: the app writes it into ~/.local/share/applications itself (DesktopIntegration).
namespace zub::desktopentry {

// The marker line of an entry the app wrote for an AppImage (only such an entry is replaced or
// removed; one installed otherwise, e.g. by cmake --install, is left alone).
inline constexpr std::string_view kMarker = "X-Zpevnikar-AppImage=true";

// `entry` (the app's own .desktop text) with Exec running `appImage` and TryExec naming it (menus
// hide the entry when the file is gone), and the marker; any old Exec, TryExec and marker lines
// of its [Desktop Entry] group go. `appImage` is UTF-8.
std::string forAppImage(std::string_view entry, std::string_view appImage);

// The entry has the marker.
bool writtenForAppImage(std::string_view entry);

// `path` as one argument of an Exec line: double-quoted with `"`, `` ` ``, `$` and `\` escaped,
// then the string escape (`\` doubled) and `%` doubled, as the Desktop Entry specification wants.
std::string execArgument(std::string_view path);

} // namespace zub::desktopentry
