#include "ShrinkDialog.hpp"

#include "convert.hpp"

#include <QDialogButtonBox>
#include <algorithm>
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
                           const std::string& songText, const QString& title,
                           const QString& heading, QWidget* parent)
    : QDialog(parent), suggestions_(suggestions) {
    for (const auto& proposal : suggestions_.proposals) {
        auto preview = zub::songoutline::preview(songText, proposal.edits, proposal.sources);
        previews_.push_back(preview ? std::optional(std::move(*preview)) : std::nullopt);
    }
    setWindowTitle(title);
    resize(1180, 600);
    setSizeGripEnabled(true);

    auto* layout = new QVBoxLayout(this);
    const QString header = summary(suggestions_);
    if (!header.isEmpty()) {
        auto* label = new QLabel(header, this);
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    auto* headingLabel = new QLabel(heading, this);
    headingLabel->setWordWrap(true);
    layout->addWidget(headingLabel);

    list_ = new QListWidget(this);
    for (std::size_t i = 0; i < suggestions_.proposals.size(); ++i) {
        const auto& proposal = suggestions_.proposals[i];
        // "57 fair: the pre-chorus repeats ... — saves 1 line (small paper only)"
        QString text = tr("%1 %2: ").arg(proposal.score).arg(toQString(proposal.grade)) +
                       toQString(proposal.description);
        if (proposal.gainLines > 0) // a restructuring saves nothing by itself
            text += QStringLiteral(" — ") + tr("saves %n line(s)", nullptr, proposal.gainLines);
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
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
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

namespace {

QColor mix(const QColor& a, const QColor& b, double share) {
    return QColor::fromRgbF(static_cast<float>(a.redF() * share + b.redF() * (1 - share)),
                            static_cast<float>(a.greenF() * share + b.greenF() * (1 - share)),
                            static_cast<float>(a.blueF() * share + b.blueF() * (1 - share)));
}

QString span(const QString& text, const QString& style) {
    return QStringLiteral("<span style=\"%1\">%2</span>").arg(style, text.toHtmlEscaped());
}

} // namespace

void ShrinkDialog::showOutline(int row) {
    using zub::songoutline::Chords;
    using zub::songoutline::Repeat;
    if (row < 0 || static_cast<std::size_t>(row) >= previews_.size()) {
        outline_->clear();
        return;
    }
    const auto& preview = previews_[static_cast<std::size_t>(row)];
    if (!preview) {
        outline_->setPlainText(tr("The proposal does not fit the song text."));
        return;
    }
    const QPalette& pal = palette();
    const QString sourceTint =
        mix(pal.color(QPalette::Link), pal.color(QPalette::Base), 0.12).name();
    const QString marked = mix(pal.color(QPalette::Highlight), pal.color(QPalette::Base), 0.3).name();
    const QString dim = QStringLiteral("color:%1").arg(pal.color(QPalette::PlaceholderText).name());
    const QString chordStyle =
        QStringLiteral("color:%1; font-weight:bold").arg(pal.color(QPalette::Link).name());
    const QString offStyle = dim + QStringLiteral("; text-decoration:line-through");

    // A sketch of the printed song: labels in a column, choruses indented, each part's first
    // lines with their chords; the parts the change touches on a highlighted background.
    QString html = QStringLiteral("<p><b>%1</b></p><table cellspacing=0 cellpadding=4 width=100%>")
                       .arg(tr("The song after this change:").toHtmlEscaped());
    bool anchored = false;
    bool firstRow = true;
    bool scroll = false; // not when the first row is the marked one: the heading stays visible
    const auto addRow = [&](const QString& sketch, const QString& note, bool changed,
                            bool source = false) {
        // scrolled into view: the first changed part, or the source above it
        const bool shown = changed || source;
        const QString anchor = shown && !anchored ? QStringLiteral("<a name=\"changed\"></a>")
                                                   : QString();
        if (shown && !anchored)
            scroll = !firstRow;
        anchored = anchored || shown;
        firstRow = false;
        html += QStringLiteral("<tr%1><td>%2<pre style=\"margin:0\">%3</pre></td>"
                               "<td align=right valign=top style=\"%4\">%5</td></tr>")
                    .arg(changed  ? QStringLiteral(" bgcolor=\"%1\"").arg(marked)
                         : source ? QStringLiteral(" bgcolor=\"%1\"").arg(sourceTint)
                                  : QString(),
                         anchor, sketch, dim, note);
    };

    if (preview->songChanged || preview->after.smallerOnSmallPage) {
        QStringList state;
        if (preview->after.smallerOnSmallPage)
            state << tr("smaller type on small paper");
        else if (preview->smallerChanged)
            state << tr("normal type size");
        if (preview->songChanged && !preview->smallerChanged)
            state << tr("changed between the parts");
        addRow(span(tr("Whole song: %1").arg(state.join(QStringLiteral(", "))), QString()),
               QString(), preview->songChanged);
    }

    for (std::size_t i = 0; i < preview->after.parts.size(); ++i) {
        const auto& part = preview->after.parts[i];
        const bool chorus = part.kind == "chorus";
        const QString indent = chorus ? QStringLiteral("    ") : QString();
        const QString label = toQString(part.label).leftJustified(4, QLatin1Char(' '));
        const QString blank(label.size(), QLatin1Char(' '));
        const bool showChords = part.repeat != Repeat::Reminder && part.chords != Chords::Off;
        const QString chordsLook = part.chords == Chords::OffOnSmallPage ? offStyle : chordStyle;

        QStringList lines;
        for (std::size_t n = 0; n < part.shown.size(); ++n) {
            const auto& shown = part.shown[n];
            if (showChords && !shown.chords.empty())
                lines << (indent + blank).toHtmlEscaped() + span(toQString(shown.chords), chordsLook);
            QString text = toQString(shown.text);
            // the part goes on: "…" at the end of its last shown lyrics
            const bool goesOn = part.repeat == Repeat::Full ||
                                (part.repeat == Repeat::None &&
                                 part.lines > static_cast<int>(part.shown.size()));
            const bool lyrics = std::ranges::any_of(text, [](QChar c) { return c.isLetter(); });
            if (n + 1 == part.shown.size() && goesOn && lyrics &&
                !text.endsWith(QStringLiteral("…")) && !text.endsWith(QStringLiteral("..."))) {
                // like the songbook's reminder line: trailing punctuation goes, then the dots
                while (!text.isEmpty() && QStringLiteral(",.;:!?").contains(text.back()))
                    text.chop(1);
                text += QStringLiteral("…");
            }
            if (!text.isEmpty() || shown.chords.empty() || !showChords)
                lines << (indent + (n == 0 ? label : blank) + text).toHtmlEscaped();
        }
        if (part.shown.empty())
            lines << (indent + label).toHtmlEscaped() + span(toQString(part.name), dim);

        QStringList note;
        const QString state = partState(part);
        if (!state.isEmpty())
            note << state.toHtmlEscaped();
        const int linesBefore = preview->linesBefore.at(i);
        if (preview->changed[i] && part.repeat == Repeat::None && linesBefore > 0 &&
            linesBefore != part.lines)
            note << tr("%1 → %2 lines").arg(linesBefore).arg(part.lines).toHtmlEscaped();
        if (preview->source.at(i))
            note << tr("the change relies on this part").toHtmlEscaped();
        note << tr("line %1").arg(part.line).toHtmlEscaped();
        addRow(lines.join(QStringLiteral("\n")), note.join(QStringLiteral("<br>")), preview->changed[i],
               preview->source.at(i));
    }
    html += QStringLiteral("</table>");
    outline_->setHtml(html);
    if (scroll)
        outline_->scrollToAnchor(QStringLiteral("changed"));
}
