#include "ShrinkDialog.hpp"

#include "convert.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QString measuredText(const QString& paper, const zub::songsuggest::Measured& measured) {
    QString text = paper + QStringLiteral(": ");
    if (measured.fits)
        text += ShrinkDialog::tr("fits (%n page(s))", nullptr, measured.pages);
    else
        text += ShrinkDialog::tr("%n page(s), doesn't fit yet", nullptr, measured.pages);
    QStringList wide;
    for (const int line : measured.wrapLines)
        wide << QString::number(line);
    if (measured.chorusWraps > 0)
        wide << ShrinkDialog::tr("%n chorus line(s)", nullptr, measured.chorusWraps);
    if (!wide.isEmpty())
        text += QStringLiteral("\n") +
                ShrinkDialog::tr("Lines too wide for the column: %1").arg(wide.join(QStringLiteral(", ")));
    return text;
}

// "no chords on small paper, chords stop part way"
QString partState(const zub::songoutline::Part& part) {
    using zub::songoutline::Chords;
    using zub::songoutline::Repeat;
    QStringList state;
    if (part.repeat == Repeat::Full)
        state << ShrinkDialog::tr("repeated in full");
    else if (part.repeat == Repeat::Reminder)
        state << ShrinkDialog::tr("first line only");
    if (part.repeat != Repeat::Reminder) {
        if (part.chords == Chords::Off)
            state << ShrinkDialog::tr("no chords");
        else if (part.chords == Chords::OffOnSmallPage)
            state << ShrinkDialog::tr("no chords on small paper");
    }
    if (part.chordsOffPartWay)
        state << ShrinkDialog::tr("some lines without chords");
    return state.join(QStringLiteral(", "));
}

// The names of the parts a proposal changes, "Verse 3", or "whole song".
QString changedParts(const zub::songoutline::Preview& preview) {
    QStringList names;
    if (preview.songChanged)
        names << ShrinkDialog::tr("Whole song");
    for (std::size_t i = 0; i < preview.after.parts.size(); ++i) {
        if (preview.changed[i])
            names << toQString(preview.after.parts[i].name);
    }
    return names.join(QStringLiteral(", "));
}

} // namespace

QString ShrinkDialog::summary(const zub::songsuggest::Suggestions& suggestions) {
    QStringList parts;
    if (suggestions.a4)
        parts << measuredText(QStringLiteral("A4"), *suggestions.a4);
    if (suggestions.a5)
        parts << measuredText(QStringLiteral("A5"), *suggestions.a5);
    if (!suggestions.note.empty())
        parts << toQString(suggestions.note);
    return parts.join(QStringLiteral("\n"));
}

ShrinkDialog::ShrinkDialog(const zub::songsuggest::Suggestions& suggestions,
                           const std::string& songText, QWidget* parent)
    : QDialog(parent), suggestions_(suggestions) {
    for (const auto& proposal : suggestions_.proposals) {
        auto preview = zub::songoutline::preview(songText, proposal.edits);
        previews_.push_back(preview ? std::optional(std::move(*preview)) : std::nullopt);
    }
    setWindowTitle(tr("Make Smaller"));
    resize(960, 520);
    setSizeGripEnabled(true);

    auto* layout = new QVBoxLayout(this);
    const QString header = summary(suggestions_);
    if (!header.isEmpty()) {
        auto* label = new QLabel(header, this);
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    layout->addWidget(new QLabel(
        tr("Pick one change, most readable first (score 0–100: how readable the song stays):"),
        this));

    list_ = new QListWidget(this);
    for (std::size_t i = 0; i < suggestions_.proposals.size(); ++i) {
        const auto& proposal = suggestions_.proposals[i];
        // "57 fair: Verse 3: the verse repeats ... — saves 1 line (small paper only)"
        QString where;
        if (previews_[i])
            where = changedParts(*previews_[i]);
        if (!where.isEmpty())
            where += QStringLiteral(": ");
        // "57 fair: the pre-chorus repeats ... — saves 1 line (small paper only)"
        QString text = tr("%1 %2: ").arg(proposal.score).arg(toQString(proposal.grade)) + where +
                       toQString(proposal.description) + QStringLiteral(" — ") +
                       tr("saves %n line(s)", nullptr, proposal.gainLines);
        if (proposal.stage == 2)
            text += tr(" (small paper only)");
        auto* item = new QListWidgetItem(text, list_);
        if (proposal.line > 0)
            item->setToolTip(tr("Line %1").arg(proposal.line));
    }
    list_->setWordWrap(true);
    outline_ = new QTextBrowser(this);
    outline_->setOpenLinks(false);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(list_);
    splitter->addWidget(outline_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* apply = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    apply->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    if (list_->count() > 0)
        list_->setCurrentRow(0); // nobody listens yet: the caller shows the first line itself
    showOutline(list_->currentRow());
    connect(list_, &QListWidget::currentRowChanged, this, [this, apply](int row) {
        apply->setEnabled(row >= 0);
        showOutline(row);
        if (row >= 0) {
            const int line = suggestions_.proposals.at(static_cast<std::size_t>(row)).line;
            if (line > 0)
                emit proposalSelected(line);
        }
    });
}

const zub::songsuggest::Proposal& ShrinkDialog::selected() const {
    const int row = list_->currentRow();
    return suggestions_.proposals.at(static_cast<std::size_t>(row < 0 ? 0 : row));
}

void ShrinkDialog::showOutline(int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= previews_.size()) {
        outline_->clear();
        return;
    }
    const auto& preview = previews_[static_cast<std::size_t>(row)];
    if (!preview) {
        outline_->setPlainText(tr("The proposal does not fit the song text."));
        return;
    }
    const QColor mark = palette().color(QPalette::Highlight);
    const QString markStyle = QStringLiteral("background-color:%1; color:%2; font-weight:bold;")
                                  .arg(mark.name(), palette().color(QPalette::HighlightedText).name());
    const QString dim = palette().color(QPalette::PlaceholderText).name();

    QString html = QStringLiteral("<p><b>%1</b></p><table cellspacing=0 cellpadding=3 width=100%>")
                       .arg(tr("The song after this change:").toHtmlEscaped());
    const auto row_ = [&](const QString& name, const QString& state, int line, bool changed) {
        html += QStringLiteral("<tr style=\"%1\"><td>%2</td><td>%3</td><td align=right style=\"color:%4\">%5</td></tr>")
                    .arg(changed ? markStyle : QString(), name.toHtmlEscaped(), state.toHtmlEscaped(),
                         changed ? QString() : dim,
                         line > 0 ? tr("line %1").arg(line) : QString());
    };
    if (preview->songChanged || preview->after.smallerOnSmallPage) {
        QStringList state;
        if (preview->after.smallerOnSmallPage)
            state << tr("smaller type on small paper");
        else if (preview->smallerChanged)
            state << tr("normal type size");
        if (preview->songChanged && !preview->smallerChanged)
            state << tr("changed between the parts");
        row_(tr("Whole song"), state.join(QStringLiteral(", ")), 0, preview->songChanged);
    }
    for (std::size_t i = 0; i < preview->after.parts.size(); ++i) {
        const auto& part = preview->after.parts[i];
        row_(toQString(part.name), partState(part), part.line, preview->changed[i]);
    }
    html += QStringLiteral("</table>");
    outline_->setHtml(html);
}
