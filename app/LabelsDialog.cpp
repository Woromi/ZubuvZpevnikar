#include "LabelsDialog.hpp"

#include "convert.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
#include <zub/core/songtags.hpp>

namespace {

// All, some or none of the songs have `tag`.
Qt::CheckState stateFor(const std::string& tag,
                        const std::vector<std::vector<std::string>>& songs) {
    std::size_t with = 0;
    for (const auto& tags : songs)
        if (std::ranges::find(tags, tag) != tags.end())
            ++with;
    if (with == 0)
        return Qt::Unchecked;
    return with == songs.size() ? Qt::Checked : Qt::PartiallyChecked;
}

QCheckBox* makeBox(const QString& text, Qt::CheckState state, QWidget* parent) {
    auto* box = new QCheckBox(text, parent);
    // Only a mixed state can be clicked through all three states.
    box->setTristate(state == Qt::PartiallyChecked);
    box->setCheckState(state);
    return box;
}

} // namespace

LabelsDialog::LabelsDialog(const QStringList& known,
                           const std::vector<std::vector<std::string>>& songs, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Labels"));
    auto* layout = new QVBoxLayout(this);

    favourite_ =
        makeBox(tr("★ Favourite"), stateFor(std::string{zub::tags::kFavourite}, songs), this);
    layout->addWidget(favourite_);

    auto* host = new QWidget;
    auto* hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    for (const QString& label : known) {
        const std::string tag = toStdString(label);
        auto* box = makeBox(label, stateFor(tag, songs), host);
        hostLayout->addWidget(box);
        items_.push_back({tag, box});
    }
    hostLayout->addStretch(1);
    if (known.isEmpty())
        hostLayout->insertWidget(0, new QLabel(tr("No labels yet."), host));
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(120);
    layout->addWidget(scroll, 1);

    layout->addWidget(new QLabel(tr("New labels (separated by commas):"), this));
    newLabels_ = new QLineEdit(this);
    layout->addWidget(newLabels_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &LabelsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

std::vector<std::string> LabelsDialog::newLabels() const {
    std::vector<std::string> result;
    for (const QString& part : newLabels_->text().split(QLatin1Char(','))) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty())
            continue;
        const std::string tag = toStdString(trimmed);
        if (!zub::tags::invalidTag(tag) && std::ranges::find(result, tag) == result.end())
            result.push_back(tag);
    }
    return result;
}

void LabelsDialog::accept() {
    for (const QString& part : newLabels_->text().split(QLatin1Char(','))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty() && zub::tags::invalidTag(toStdString(trimmed))) {
            QMessageBox::warning(
                this, windowTitle(),
                tr("\"%1\" cannot be a label: it must not contain { } \\ % # or a line break.")
                    .arg(trimmed));
            newLabels_->setFocus();
            return;
        }
    }
    QDialog::accept();
}

void LabelsDialog::applyBox(std::vector<std::string>& tags, const std::string& tag,
                            const QCheckBox* box) {
    const auto it = std::ranges::find(tags, tag);
    if (box->checkState() == Qt::Checked && it == tags.end())
        tags.push_back(tag);
    else if (box->checkState() == Qt::Unchecked && it != tags.end())
        tags.erase(it);
}

std::vector<std::string> LabelsDialog::apply(const std::vector<std::string>& current) const {
    std::vector<std::string> tags = current;
    applyBox(tags, std::string{zub::tags::kFavourite}, favourite_);
    for (const Item& item : items_)
        applyBox(tags, item.tag, item.box);
    for (const std::string& tag : newLabels()) {
        if (std::ranges::find(tags, tag) == tags.end())
            tags.push_back(tag);
    }
    return tags;
}
