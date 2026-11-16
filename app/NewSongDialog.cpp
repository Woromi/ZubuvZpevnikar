#include "NewSongDialog.hpp"

#include "CollectionTabs.hpp"
#include "convert.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <zub/core/songimport.hpp>

NewSongDialog::NewSongDialog(const std::map<QString, QString>& artistFolders,
                             const std::vector<std::string>& collections, QWidget* parent)
    : QDialog(parent), artistFolders_(artistFolders), title_(new QLineEdit), artist_(new QComboBox),
      collection_(new QComboBox), file_(new QLineEdit),
      buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel)) {
    setWindowTitle(tr("New Song"));
    title_->setObjectName(QStringLiteral("newSongTitle"));
    artist_->setObjectName(QStringLiteral("newSongArtist"));
    file_->setObjectName(QStringLiteral("newSongFile"));

    artist_->setEditable(true);
    artist_->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& [name, folder] : artistFolders_)
        artist_->addItem(name);
    artist_->setCurrentIndex(-1);
    artist_->setToolTip(tr("An artist of the songbook keeps their folder; a new one gets a folder "
                           "named after them"));

    // Read-only and frameless: the whole path, however long (a label would wrap and be cut off).
    file_->setReadOnly(true);
    file_->setFrame(false);
    file_->setFocusPolicy(Qt::NoFocus);
    file_->setPlaceholderText(tr("(type a title and an artist)"));

    collection_->setObjectName(QStringLiteral("newSongCollection"));
    for (const std::string& name : collections)
        collection_->addItem(CollectionTabs::displayName(toQString(name)), toQString(name));
    if (collection_->count() == 0)
        collection_->addItem(CollectionTabs::displayName(QStringLiteral("chords")),
                             QStringLiteral("chords"));

    auto* form = new QFormLayout;
    form->addRow(tr("&Title:"), title_);
    form->addRow(tr("&Artist:"), artist_);
    form->addRow(tr("&Collection:"), collection_);
    form->setRowVisible(collection_, collection_->count() > 1);
    form->addRow(tr("File:"), file_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(title_, &QLineEdit::textChanged, this, &NewSongDialog::update);
    connect(artist_, &QComboBox::currentTextChanged, this, &NewSongDialog::update);
    connect(collection_, &QComboBox::currentIndexChanged, this, &NewSongDialog::update);
    update();
    resize(520, sizeHint().height());
}

QString NewSongDialog::title() const {
    return title_->text().trimmed();
}

QString NewSongDialog::collection() const {
    return collection_->currentData().toString();
}

QString NewSongDialog::artist() const {
    return artist_->currentText().trimmed();
}

QString NewSongDialog::inputName() const {
    const auto known = artistFolders_.find(artist());
    const QString folder = known != artistFolders_.end()
                               ? known->second
                               : toQString(zub::songimport::fileNameOf(toStdString(artist())));
    const QString file = toQString(zub::songimport::fileNameOf(toStdString(title())));
    if (folder.isEmpty() || file.isEmpty())
        return {};
    return folder + QLatin1Char('/') + file;
}

void NewSongDialog::update() {
    const QString name = inputName();
    file_->setText(name.isEmpty() ? QString()
                                  : QStringLiteral("songs/") + collection() + QLatin1Char('/') +
                                        name + QStringLiteral(".tex"));
    file_->setCursorPosition(0);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!name.isEmpty());
}
