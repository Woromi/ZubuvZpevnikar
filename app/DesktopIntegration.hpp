#pragma once

#include <QList>
#include <QPair>
#include <QString>

// Run as an AppImage, the app puts itself into the desktop's application menu, as an installed
// app would be: on every start its launcher entry (zub::desktopentry::forAppImage, Exec = the
// AppImage, so a moved or updated AppImage is followed) and its icons are written into
// ~/.local/share (applications/, icons/hicolor/), only where something changed. An entry the app
// did not write (e.g. from cmake --install) is never replaced. Settings → "Show in the
// application menu" turns it off and removes the entry (`desktop/menuEntry`, default on).
namespace desktopintegration {

// The AppImage file the app runs from ($APPIMAGE, when it is an existing file), else "".
QString appImage();

bool enabled();
void setEnabled(bool on);

// What is copied: the app's .desktop file and its icons (size, file).
struct Source {
    QString desktopFile;
    QList<QPair<int, QString>> icons;
};
// The ones inside the running AppImage (usr/share next to usr/bin).
Source bundled();

// <GenericDataLocation>/applications/zpevnikar.desktop
QString entryPath();

enum class Result {
    Unchanged, // already there as it should be
    Added,     // there was none
    Updated,   // ours, for another AppImage path or an older entry
    NotOurs,   // an entry installed otherwise is there: left alone
    Failed,    // the source cannot be read or a file cannot be written
};
Result install(const QString& appImage, const Source& source);

// Removes the entry if the app wrote it (the icons stay: an installed copy may use them too).
bool remove();

} // namespace desktopintegration
