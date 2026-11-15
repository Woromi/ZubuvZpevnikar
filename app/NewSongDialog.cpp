#include "NewSongDialog.hpp"

#include "convert.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <zub/core/songimport.hpp>

NewSongDialog::NewSongDialog(const std::map<QString, QString>& artistFolders,
                             const QStringList& languages, QWidget* parent)
    : QDialog(parent), artistFolders_(artistFolders), title_(new QLineEdit), artist_(new QComboBox),
      language_(new QComboBox), file_(new QLabel),
      buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel)) {
    setWindowTitle(tr("New Song"));
    title_->setObjectName(QStringLiteral("newSongTitle"));
    artist_->setObjectName(QStringLiteral("newSongArtist"));
    language_->setObjectName(QStringLiteral("newSongLanguage"));

    artist_->setEditable(true);
    artist_->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& [name, folder] : artistFolders_)
        artist_->addItem(name);
    artist_->setCurrentIndex(-1);
    artist_->setToolTip(tr("An artist of the songbook keeps their folder; a new one gets a folder "
                           "named after them"));

    language_->setEditable(true);
    QStringList offered = languages;
    for (const QString& common :
         {QStringLiteral("czech"), QStringLiteral("slovak"), QStringLiteral("english")})
        if (!offered.contains(common))
            offered << common;
    language_->addItems(offered);
    language_->setCurrentText(QStringLiteral("czech"));
    language_->setToolTip(tr("The song's lang: chord and note names, index order. Leave it empty "
                             "to fill it in later."));

    file_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    file_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(tr("&Title:"), title_);
    form->addRow(tr("&Artist:"), artist_);
    form->addRow(tr("&Language:"), language_);
    form->addRow(tr("File:"), file_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(title_, &QLineEdit::textChanged, this, &NewSongDialog::update);
    connect(artist_, &QComboBox::currentTextChanged, this, &NewSongDialog::update);
    update();
    resize(480, sizeHint().height());
}

QString NewSongDialog::title() const {
    return title_->text().trimmed();
}

QString NewSongDialog::artist() const {
    return artist_->currentText().trimmed();
}

QString NewSongDialog::language() const {
    return language_->currentText().trimmed();
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
    file_->setText(name.isEmpty()
                       ? tr("(type a title and an artist)")
                       : QStringLiteral("songs/chords/") + name + QStringLiteral(".tex"));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!name.isEmpty());
}
