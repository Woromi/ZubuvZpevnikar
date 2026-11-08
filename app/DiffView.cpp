#include "DiffView.hpp"

#include "convert.hpp"

#include <QFontDatabase>
#include <QPalette>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <algorithm>
#include <string_view>
#include <zub/core/linediff.hpp>

namespace {

// `tint` over `base`, so the colours work in light and dark palettes.
QColor blend(const QColor& base, const QColor& tint, qreal amount) {
    const auto mix = [amount](float from, float to) {
        return static_cast<float>(static_cast<qreal>(from) * (1 - amount) +
                                  static_cast<qreal>(to) * amount);
    };
    return QColor::fromRgbF(mix(base.redF(), tint.redF()), mix(base.greenF(), tint.greenF()),
                            mix(base.blueF(), tint.blueF()));
}

} // namespace

DiffView::DiffView(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

void DiffView::setTexts(const std::string& before, const std::string& after,
                        const QString& noChanges) {
    clear();
    const auto lines = zub::diff::lines(before, after);
    const bool changed = std::ranges::any_of(
        lines, [](const zub::diff::Line& line) { return line.op != zub::diff::Op::Same; });
    if (!changed) {
        // The line diff ignores a missing final newline; say so when that is the only change.
        setPlainText(before == after ? noChanges
                                     : tr("Only the newline at the end of the file changed."));
        return;
    }

    const QColor base = palette().base().color();
    QTextBlockFormat addedBlock;
    addedBlock.setBackground(blend(base, QColor(0, 170, 0), 0.25));
    QTextBlockFormat removedBlock;
    removedBlock.setBackground(blend(base, QColor(220, 0, 0), 0.25));
    QTextCharFormat removedChars;
    removedChars.setFontStrikeOut(true);

    QTextCursor cursor(document());
    bool first = true;
    for (const auto& line : lines) {
        if (!first)
            cursor.insertBlock();
        first = false;
        std::string_view text = line.text;
        if (text.ends_with('\r'))
            text.remove_suffix(1);
        const QChar marker = line.op == zub::diff::Op::Added     ? QLatin1Char('+')
                             : line.op == zub::diff::Op::Removed ? QChar(0x2212)
                                                                 : QLatin1Char(' ');
        // The block format first: the char format must not leak into the next block.
        cursor.setBlockFormat(line.op == zub::diff::Op::Added     ? addedBlock
                              : line.op == zub::diff::Op::Removed ? removedBlock
                                                                  : QTextBlockFormat());
        cursor.setBlockCharFormat(QTextCharFormat());
        cursor.insertText(QString(marker) + QLatin1Char(' '), QTextCharFormat());
        cursor.insertText(toQString(text),
                          line.op == zub::diff::Op::Removed ? removedChars : QTextCharFormat());
    }
    moveCursor(QTextCursor::Start);
}
