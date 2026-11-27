#pragma once

#include <QDialog>

class QMenuBar;

// Help > Keyboard Shortcuts: every menu's actions with their keys (read from the actions, so the
// list cannot go stale), then the keys and gestures that have no menu entry.
class ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutsDialog(const QMenuBar* menus, QWidget* parent = nullptr);
};
