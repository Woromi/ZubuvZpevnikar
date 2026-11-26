#include "AddToBooksDialog.hpp"

#include "convert.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <zub/core/bookfile.hpp>
#include <zub/core/textfile.hpp>

namespace {
constexpr int targetRole = Qt::UserRole; // index into all_, -1: not a target
} // namespace

AddToBooksDialog::AddToBooksDialog(const std::filesystem::path& root, const std::string& inputName,
                                   const QString& songTitle, const QString& busyBookId,
                                   QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Add to Songbooks"));
    resize(520, 480);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Which songbooks should %1 be in? It goes where the index puts it "
                                "(by title); unchecking a list takes it out of that one.")
                                 .arg(songTitle),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("addToBooksTree"));
    tree_->setHeaderHidden(true);
    layout->addWidget(tree_, 1);

    const QBrush grey = palette().brush(QPalette::Disabled, QPalette::Text);
    const auto disable = [&grey](QTreeWidgetItem* item, const QString& why) {
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setText(0, item->text(0) + QStringLiteral("  (") + why + QLatin1Char(')'));
        item->setForeground(0, grey);
    };
    for (const auto& relative : zub::bookfile::findBooks(root)) {
        const QString id = toQString(relative);
        const auto text = zub::readTextFile(root / relative);
        if (!text)
            continue;
        const auto book = zub::bookfile::parse(*text);
        const auto settings = zub::bookfile::readSettings(*text);
        const QString title =
            settings.title.empty() ? toQString(relative.stem()) : toQString(settings.title);
        auto* bookItem = new QTreeWidgetItem(tree_, {title + QStringLiteral("  ·  ") + id});
        bookItem->setData(0, targetRole, -1);
        bookItem->setToolTip(0, id);
        if (!book) {
            disable(bookItem, toQString(book.error()));
            continue;
        }
        const bool locked = settings.locked;
        const bool busy = id == busyBookId;
        const bool single = book->environments.size() == 1;
        for (std::size_t e = 0; e < book->environments.size(); ++e) {
            const auto& environment = book->environments[e];
            QTreeWidgetItem* item = bookItem;
            if (!single)
                item = new QTreeWidgetItem(bookItem, {environment.section.empty()
                                                          ? tr("Songs before the first section")
                                                          : toQString(environment.section)});
            const QString label =
                single || environment.section.empty()
                    ? title
                    : title + QStringLiteral(" › ") + toQString(environment.section);
            item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsAutoTristate);
            const bool has = std::ranges::count(environment.songs(), inputName) > 0;
            item->setCheckState(0, has ? Qt::Checked : Qt::Unchecked);
            item->setData(0, targetRole, -1);
            if (locked) {
                disable(item, tr("locked"));
            } else if (busy) {
                disable(item, tr("unsaved LaTeX edits on the Songbooks page"));
            } else if (!environment.readOnly.empty()) {
                disable(item, tr("read-only: %1").arg(toQString(environment.readOnly)));
            } else {
                item->setData(0, targetRole, static_cast<int>(all_.size()));
                all_.push_back({root / relative, label, e, environment.section});
                had_.push_back(has);
                ++available_;
            }
        }
        bookItem->setExpanded(true);
    }
    if (tree_->topLevelItemCount() == 0) {
        auto* none =
            new QTreeWidgetItem(tree_, {tr("No songbooks yet: make one on the Songbooks page.")});
        none->setData(0, targetRole, -1);
    }

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* add = buttons->addButton(
        tr("Apply", "add the song to the checked lists, take it out of the unchecked ones"),
        QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Not Now"), QDialogButtonBox::RejectRole);
    add->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

std::vector<BookAdder::Target> AddToBooksDialog::targets() const {
    std::vector<BookAdder::Target> out;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        const int index = (*it)->data(0, targetRole).toInt();
        if (index >= 0 && (*it)->checkState(0) == Qt::Checked && (*it)->flags() & Qt::ItemIsEnabled &&
            !had_[static_cast<std::size_t>(index)])
            out.push_back(all_[static_cast<std::size_t>(index)]);
    }
    return out;
}

std::vector<BookAdder::Target> AddToBooksDialog::removals() const {
    std::vector<BookAdder::Target> out;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        const int index = (*it)->data(0, targetRole).toInt();
        if (index >= 0 && (*it)->checkState(0) != Qt::Checked &&
            (*it)->flags() & Qt::ItemIsEnabled && had_[static_cast<std::size_t>(index)])
            out.push_back(all_[static_cast<std::size_t>(index)]);
    }
    return out;
}
