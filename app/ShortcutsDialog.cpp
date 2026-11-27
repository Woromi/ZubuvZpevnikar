#include "ShortcutsDialog.hpp"

#include "KeyTips.hpp"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QLineEdit>
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

// Keys in the platform's notation, joined; words (a gesture) can stand among them.
QString keys(std::initializer_list<QKeyCombination> combinations,
             const QStringList& words = {}) {
    QStringList parts;
    for (const QKeyCombination& combination : combinations)
        parts << QKeySequence(combination).toString(QKeySequence::NativeText);
    parts << words;
    return parts.join(QStringLiteral(" / "));
}

} // namespace

ShortcutsDialog::ShortcutsDialog(const QMenuBar* menus, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(640, 640);
    auto* layout = new QVBoxLayout(this);
    auto* filter = new QLineEdit(this);
    filter->setObjectName(QStringLiteral("shortcutsFilter"));
    filter->setPlaceholderText(tr("Filter"));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);
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
         {tr("Insert a part at the cursor (then its number)"), keys({Qt::CTRL | Qt::Key_P})},
         {tr("Add a cursor above / below"),
          keys({Qt::ALT | Qt::SHIFT | Qt::Key_Up, Qt::ALT | Qt::SHIFT | Qt::Key_Down})},
         {tr("New line below / above"),
          keys({Qt::CTRL | Qt::Key_Enter, Qt::CTRL | Qt::SHIFT | Qt::Key_Enter})},
         {tr("Scroll by a line"), keys({Qt::CTRL | Qt::Key_Up, Qt::CTRL | Qt::Key_Down})},
         {tr("Find next / previous"), keys({Qt::Key_F3, Qt::SHIFT | Qt::Key_F3})}},
        {{tr("Score tab"), {}},
         {tr("Note length (6 is a sixteenth; on the score or the fretboard)"),
          keys({Qt::Key_1, Qt::Key_2, Qt::Key_4, Qt::Key_8, Qt::Key_6})},
         {tr("Dotted note"), keys({Qt::Key_Period})},
         {tr("Rest / bar check"), keys({Qt::Key_R, Qt::Key_Bar})},
         {tr("Insert the notes picked on the fretboard"), keys({Qt::ALT | Qt::Key_N})},
         {tr("Drop the notes picked on the fretboard"), keys({Qt::Key_Escape})},
         {tr("Pick and insert a note at once"), tr("Double-click a fret")},
         {tr("Move between notes, select"),
          keys({Qt::Key_Left, Qt::Key_Right, Qt::SHIFT | Qt::Key_Left, Qt::SHIFT | Qt::Key_Right,
                Qt::Key_Home, Qt::Key_End})},
         {tr("Delete notes"), keys({Qt::Key_Backspace, Qt::Key_Delete})},
         {tr("Actions on the selected notes"), tr("Right-click the score")}},
        {{tr("Songs and songbooks"), {}},
         {tr("Back"), keys({Qt::ALT | Qt::Key_Left}, {tr("the mouse's back button")})},
         {tr("Favourite in the song list"), tr("Click the ★ column")},
         {tr("Add songs to a songbook"), tr("Drag them from Add Songs onto the songbook")},
         {tr("Remove songs from a songbook (Ctrl+Z brings them back)"), keys({Qt::Key_Delete})},
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
    // The filter matches an action's or its keys' text; a heading stays while a row below it does.
    connect(filter, &QLineEdit::textChanged, tree, [tree](const QString& text) {
        QTreeWidgetItem* lastHeading = nullptr;
        bool any = false;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = tree->topLevelItem(i);
            if (item->text(1).isEmpty() && item->isFirstColumnSpanned()) {
                if (lastHeading)
                    lastHeading->setHidden(!any && !text.isEmpty());
                lastHeading = item;
                any = false;
                continue;
            }
            const bool match = text.isEmpty() ||
                               item->text(0).contains(text, Qt::CaseInsensitive) ||
                               item->text(1).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match);
            any = any || match;
        }
        if (lastHeading)
            lastHeading->setHidden(!any && !text.isEmpty());
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
