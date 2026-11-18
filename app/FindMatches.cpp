#include "FindMatches.hpp"

#include <QRegularExpression>
#include <QStringView>

namespace findmatches {

namespace {

bool isWordCode(char32_t code) {
    return code == U'_' || QChar::isLetterOrNumber(code) || QChar::isMark(code);
}

bool wordBefore(QStringView text, int index) {
    if (index <= 0)
        return false;
    const QChar last = text[index - 1];
    if (last.isLowSurrogate() && index >= 2 && text[index - 2].isHighSurrogate())
        return isWordCode(QChar::surrogateToUcs4(text[index - 2], last));
    return isWordCode(last.unicode());
}

bool wordAfter(QStringView text, int index) {
    if (index >= text.size())
        return false;
    const QChar first = text[index];
    if (first.isHighSurrogate() && index + 1 < text.size() && text[index + 1].isLowSurrogate())
        return isWordCode(QChar::surrogateToUcs4(first, text[index + 1]));
    return isWordCode(first.unicode());
}

} // namespace

Result findAll(const QString& text, const QString& needle, const Options& options, int limit) {
    Result result;
    if (needle.isEmpty())
        return result;
    if (options.regex) {
        QString pattern = needle;
        if (options.wholeWord)
            // \\E ends a \\Q the needle may leave open (harmless without one).
            pattern = QStringLiteral("(?<![\\p{L}\\p{N}\\p{M}_])(?:") + needle +
                      QStringLiteral("\\E)(?![\\p{L}\\p{N}\\p{M}_])");
        QRegularExpression::PatternOptions flags =
            QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::MultilineOption;
        if (!options.matchCase)
            flags |= QRegularExpression::CaseInsensitiveOption;
        const QRegularExpression regex(pattern, flags);
        if (!regex.isValid()) {
            result.error = regex.errorString();
            return result;
        }
        auto it = regex.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch found = it.next();
            if (found.capturedLength() == 0)
                continue;
            if (result.matches.size() >= limit) {
                result.truncated = true;
                break;
            }
            result.matches.append({static_cast<int>(found.capturedStart()),
                                   static_cast<int>(found.capturedLength()),
                                   found.capturedTexts()});
        }
        return result;
    }
    const Qt::CaseSensitivity sensitivity =
        options.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int from = 0;
    while (true) {
        const int at = static_cast<int>(text.indexOf(needle, from, sensitivity));
        if (at < 0)
            break;
        const int end = at + static_cast<int>(needle.size());
        from = at + 1;
        if (options.wholeWord && (wordBefore(text, at) || wordAfter(text, end)))
            continue;
        if (result.matches.size() >= limit) {
            result.truncated = true;
            break;
        }
        result.matches.append({at, static_cast<int>(needle.size()), {}});
        from = end;
    }
    return result;
}

QString replacementFor(const Match& match, const QString& replacement, const Options& options) {
    if (!options.regex)
        return replacement;
    QString out;
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        const QChar c = replacement[i];
        if (c == QLatin1Char('$') && i + 1 < replacement.size()) {
            const QChar next = replacement[i + 1];
            if (next == QLatin1Char('$')) {
                out += QLatin1Char('$');
                ++i;
                continue;
            }
            if (next >= QLatin1Char('0') && next <= QLatin1Char('9')) {
                const qsizetype group = next.unicode() - u'0';
                if (group < match.captures.size())
                    out += match.captures[group];
                ++i;
                continue;
            }
        }
        out += c;
    }
    return out;
}

} // namespace findmatches
