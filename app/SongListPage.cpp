#include "SongListPage.hpp"

#include "LabelFilterBox.hpp"
#include "PageHeader.hpp"
#include "SongFilterProxy.hpp"
#include "SongTableModel.hpp"

#include <QAction>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <set>

SongListPage::SongListPage(SongTableModel* model, SongFilterProxy* proxy, QWidget* parent)
    : QWidget(parent), model_(model), proxy_(proxy) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The heading and the actions on songs.
    header_ = createPageHeader(tr("Songs"), this);

    // The filters.
    auto* filters = new QToolBar(tr("Filter"), this);
    filters->setMovable(false);
    search_ = new QLineEdit(filters);
    search_->setPlaceholderText(tr("Search title, artist, key… (Ctrl+F)"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(320);
    connect(search_, &QLineEdit::textChanged, proxy_, &SongFilterProxy::setSearchText);
    filters->addWidget(search_);

    language_ = new QComboBox(filters);
    language_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(language_, &QComboBox::currentIndexChanged, this,
            [this] { proxy_->setLanguage(language_->currentData().toString()); });
    filters->addWidget(language_);

    labelFilter_ = new LabelFilterBox(model_, filters);
    connect(labelFilter_, &LabelFilterBox::labelChanged, proxy_, &SongFilterProxy::setLabel);
    filters->addWidget(labelFilter_);

    // One choice instead of a checkbox per filter (they were never combined).
    show_ = new QComboBox(filters);
    show_->setObjectName(QStringLiteral("songListShow"));
    show_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    show_->addItem(tr("All songs"), Show::All);
    show_->addItem(tr("★ Favourites"), Show::Favourites);
    show_->addItem(tr("With tabs"), Show::Tabs);
    show_->setItemData(2, tr("Songs that include a score (\\lilypondfile)"), Qt::ToolTipRole);
    show_->addItem(tr("With TODOs"), Show::Todo);
    show_->setItemData(3, tr("Songs with a TODO comment, e.g. imported songs not checked yet"),
                       Qt::ToolTipRole);
    connect(show_, &QComboBox::currentIndexChanged, this, &SongListPage::applyShow);
    filters->addWidget(show_);
    filters->layout()->setSpacing(8);
    filters->setContentsMargins(6, 4, 6, 4);

    table_ = new QTableView(this);
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->sortByColumn(SongTableModel::Title, Qt::AscendingOrder);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    QHeaderView* columns = table_->horizontalHeader();
    columns->setSectionResizeMode(SongTableModel::Title, QHeaderView::Stretch);
    columns->setSectionResizeMode(SongTableModel::Artist, QHeaderView::Stretch);
    columns->setSectionResizeMode(SongTableModel::Todo, QHeaderView::Stretch);
    columns->setSectionResizeMode(SongTableModel::Changes, QHeaderView::Stretch);
    columns->setSectionResizeMode(SongTableModel::Favourite, QHeaderView::ResizeToContents);
    columns->setSectionResizeMode(SongTableModel::Tabs, QHeaderView::ResizeToContents);
    columns->setMinimumSectionSize(28);                     // the ★ and Tabs columns stay narrow
    table_->setColumnHidden(SongTableModel::Changes, true); // until Check Songs
    // Click the ★ to toggle the favourite; right-click for the labels.
    connect(table_, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (index.column() != SongTableModel::Favourite)
            return;
        if (const auto* song = model_->song(proxy_->mapToSource(index).row()))
            emit favouriteClicked(song->path);
    });
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this,
            &SongListPage::contextMenuRequested);
    connect(table_, &QAbstractItemView::activated, this, &SongListPage::songActivated);

    layout->addWidget(header_);
    layout->addWidget(filters);
    layout->addWidget(table_);
    updateLanguages();
}

void SongListPage::addHeaderAction(QAction* action) {
    header_->addAction(action);
}

void SongListPage::focusSearch() {
    search_->setFocus();
    search_->selectAll();
}

void SongListPage::updateLanguages() {
    const QString current = language_->currentData().toString();
    std::set<QString> languages;
    for (int row = 0; row < model_->rowCount(); ++row) {
        // Every language of every song: also the ones a song is only tagged with.
        for (const QString& lang :
             model_->index(row, 0).data(SongTableModel::LanguageRole).toStringList())
            languages.insert(lang);
    }
    const QSignalBlocker blocker(language_);
    language_->clear();
    language_->addItem(tr("All languages"), QString{});
    for (const QString& lang : languages)
        language_->addItem(lang, lang);
    const int index = language_->findData(current);
    language_->setCurrentIndex(index >= 0 ? index : 0);
    proxy_->setLanguage(language_->currentData().toString());
}

void SongListPage::showScanResults(bool found) {
    table_->setColumnHidden(SongTableModel::Changes, false);
    if (show_->findData(Show::Changes) < 0) {
        show_->addItem(tr("With possible changes"), Show::Changes);
        show_->setItemData(show_->count() - 1,
                           tr("Songs the last Check Songs found something in (this session)"),
                           Qt::ToolTipRole);
    }
    if (found)
        show_->setCurrentIndex(show_->findData(Show::Changes));
}

void SongListPage::selectSong(const std::filesystem::path& path) {
    const int row = model_->rowOf(path);
    if (row >= 0) {
        const QModelIndex index = proxy_->mapFromSource(model_->index(row, 0));
        if (index.isValid()) {
            table_->selectRow(index.row());
            table_->setCurrentIndex(index);
            table_->scrollTo(index);
        }
    }
    table_->setFocus();
}

void SongListPage::applyShow() {
    const int show = show_->currentData().toInt();
    proxy_->setFavouritesOnly(show == Show::Favourites);
    proxy_->setTabsOnly(show == Show::Tabs);
    proxy_->setTodoOnly(show == Show::Todo);
    proxy_->setChangesOnly(show == Show::Changes);
}
