#include "ShortcutsDialog.hpp"

#include "KeyTips.hpp"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QString plainText(QString text) {
    if (const auto tab = text.indexOf(QLatin1Char('\t')); tab >= 0)
        text.truncate(tab);
    text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    return text;
}

QString keyText(const QAction* action) {
    QStringList keys;
    for (const QKeySequence& key : action->shortcuts())
        keys << key.toString(QKeySequence::NativeText);
    if (keys.isEmpty()) {
        const QString text = action->text();
        if (const auto tab = text.indexOf(QLatin1Char('\t')); tab >= 0)
            keys << text.mid(tab + 1);
    }
    return keys.join(QStringLiteral(", "));
}

} // namespace

ShortcutsDialog::ShortcutsDialog(const QMenuBar* menus, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(640, 640);
    auto* layout = new QVBoxLayout(this);
    auto* tree = new QTreeWidget(this);
    tree->setObjectName(QStringLiteral("shortcutsTree"));
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Action"), tr("Keys")});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    const auto heading = [tree](const QString& text) {
        auto* item = new QTreeWidgetItem(tree, {text});
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        item->setFirstColumnSpanned(true);
    };
    for (const QAction* top : menus->actions()) {
        const QMenu* menu = top->menu();
        if (!menu)
            continue;
        QList<QPair<QString, QString>> rows;
        for (const QAction* action : menu->actions()) {
            const QString keys = keyText(action);
            if (!action->isSeparator() && !action->menu() && !keys.isEmpty())
                rows.append({plainText(action->text()), keys});
        }
        if (rows.isEmpty())
            continue;
        heading(plainText(menu->title()));
        for (const auto& [text, keys] : rows)
            new QTreeWidgetItem(tree, {text, keys});
    }
    // Keys without a menu entry: handled by the widgets themselves.
    const QList<QPair<QString, QString>> sections[] = {
        {{tr("Text editor"), {}},
         {tr("Add a cursor above / below"), tr("Alt+Shift+Up / Down")},
         {tr("New line below / above"), tr("Ctrl+Enter / Ctrl+Shift+Enter")},
         {tr("Scroll by a line"), tr("Ctrl+Up / Down")},
         {tr("Find next / previous"), tr("F3 / Shift+F3")}},
        {{tr("Score tab"), {}},
         {tr("Note length (on the score or the fretboard)"), tr("1, 2, 4, 8, 6 (16)")},
         {tr("Dotted note"), QStringLiteral(".")},
         {tr("Rest / bar check"), tr("R / |")},
         {tr("Insert the notes picked on the fretboard"), tr("Ctrl+Enter")},
         {tr("Drop the notes picked on the fretboard"), tr("Escape")},
         {tr("Pick and insert a note at once"), tr("Double-click a fret")},
         {tr("Move between notes, select"), tr("Left / Right, Shift+Left / Right, Home / End")},
         {tr("Delete notes"), tr("Backspace / Delete")},
         {tr("Actions on the selected notes"), tr("Right-click the score")},
         {tr("Play / stop"), tr("Ctrl+Space")},
         {tr("Rebuild the preview"), tr("Ctrl+R")}},
        {{tr("Songs and songbooks"), {}},
         {tr("Back"), tr("Alt+Left, the mouse's back button")},
         {tr("Favourite in the song list"), tr("Click the ★ column")},
         {tr("Add songs to a songbook"), tr("Drag them from Add Songs onto the songbook")},
         {tr("Remove songs from a songbook"), tr("Delete (Ctrl+Z brings them back)")},
         {tr("Go to a change in Tidy's list"), tr("Double-click it")}},
    };
    for (const auto& section : sections) {
        heading(section.front().first);
        for (qsizetype i = 1; i < section.size(); ++i)
            new QTreeWidgetItem(tree, {section[i].first, section[i].second});
    }
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setStretchLastSection(false);
    layout->addWidget(tree);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
