#pragma once

#include <QKeySequence>
#include <QString>

class QAction;
class QObject;

// Keys in tooltips, written from the keys themselves (never typed by hand): "Save the song
// (Ctrl+S)" in the platform's notation, so a key that differs between platforms (Redo) or that
// changes is never shown wrong.
namespace keytips {

// "text (key)"; `text` alone for an empty key.
QString withKey(const QString& text, const QKeySequence& key);

// A key some other code handles (the text editor's Undo), to show in `action`'s tooltip and menu
// entry without binding it: a menu shows the text after a tab as the key.
void setShownKey(QAction* action, const QKeySequence& key);

// Every QAction under `root` (its children's too) with a shortcut or a shown key gets it at the end
// of its tooltip. Again after a tooltip or key changed: the key is added once.
void apply(QObject* root);

} // namespace keytips
