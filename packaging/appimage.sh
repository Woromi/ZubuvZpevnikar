#!/bin/bash
# Packs a release build of Zpěvníkář into an AppImage: the app, its converter and the bundled
# package (cmake --install), with the Qt and KF6 libraries it needs. lualatex, LilyPond,
# fluidsynth, python3 and git are not bundled: the user installs them (see the README).
#
# Usage: packaging/appimage.sh <build dir> <output dir>
#   QMAKE                the qmake of the Qt to bundle (default: qmake6, else qmake on PATH)
#   LINUXDEPLOY          linuxdeploy (default: linuxdeploy-x86_64.AppImage on PATH); the
#                        linuxdeploy-plugin-qt AppImage must be on PATH too
#   LD_LIBRARY_PATH      must reach KF6 SyntaxHighlighting when it is not a system library
# The AppImage is <output dir>/Zpevnikar-<version>-x86_64.AppImage.
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 <build dir> <output dir>" >&2
    exit 2
fi
build=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")

QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
export QMAKE
LINUXDEPLOY=${LINUXDEPLOY:-linuxdeploy-x86_64.AppImage}
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")
if [ -z "$version" ]; then
    echo "$build is not a configured build dir" >&2
    exit 1
fi

# A fresh folder each time: nothing of an earlier run gets into the AppImage.
work=$(mktemp -d)
appdir=$work/AppDir
DESTDIR=$appdir cmake --install "$build" --prefix /usr
# The build skips the converter without cargo and the package without its submodule: a release
# must have both.
for file in usr/bin/chords_converter usr/share/zpevnikar/zubuvzpevnik/zubuvzpevnik.sty; do
    if [ ! -f "$appdir/$file" ]; then
        echo "$file is missing: the build had no cargo or no submodules" >&2
        exit 1
    fi
done

# Wayland as well as X11 (the plugin names differ between Qt versions), and offscreen for
# running it headless (the release workflow's start check).
plugins=$("$QMAKE" -query QT_INSTALL_PLUGINS)
extra=$(cd "$plugins/platforms" && ls libqwayland*.so libqoffscreen.so 2>/dev/null | paste -sd ';' || true)
export EXTRA_PLATFORM_PLUGINS=$extra
# The SVG icon engine for themed icons.
export EXTRA_QT_MODULES="svg"

cd "$work"
export OUTPUT="Zpevnikar-$version-x86_64.AppImage"
"$LINUXDEPLOY" --appdir "$appdir" \
    --executable "$appdir/usr/bin/zpevnikar" \
    --executable "$appdir/usr/bin/chords_converter" \
    --desktop-file "$appdir/usr/share/applications/zpevnikar.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/zpevnikar.png" \
    --plugin qt --output appimage
mv "$work/$OUTPUT" "$out/"
echo "Built $out/$OUTPUT"
