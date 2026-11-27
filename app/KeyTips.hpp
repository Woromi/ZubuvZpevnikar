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

// Keys that an action and another widget's tooltip both name: written once, here (the action
// gets it from here, the tooltip too).
namespace appkeys {
inline QKeySequence home() {
    return QKeySequence(Qt::ALT | Qt::Key_Home);
}
inline QKeySequence songs() {
    return QKeySequence(Qt::CTRL | Qt::Key_1);
}
inline QKeySequence songEditor() {
    return QKeySequence(Qt::CTRL | Qt::Key_2);
}
inline QKeySequence songbooks() {
    return QKeySequence(Qt::CTRL | Qt::Key_3);
}
inline QKeySequence settings() {
    return QKeySequence(Qt::CTRL | Qt::Key_Comma);
}
inline QKeySequence rebuildPreview() {
    return QKeySequence(Qt::CTRL | Qt::Key_R);
}
inline QKeySequence replace() {
    return QKeySequence(Qt::CTRL | Qt::Key_H);
}
} // namespace appkeys
