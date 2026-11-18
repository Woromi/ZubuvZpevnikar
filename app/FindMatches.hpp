#pragma once

// Finding text for the find bar: a pure helper (no widgets), positions are UTF-16 code units like
// QTextDocument's, so a match can be selected as it is.
#include <QList>
#include <QString>
#include <QStringList>

namespace findmatches {

struct Options {
    bool matchCase = false;
    // Not preceded or followed by a letter, digit, mark or underscore (any script: Czech letters
    // count).
    bool wholeWord = false;
    bool regex = false; // the needle is a QRegularExpression pattern (Unicode properties on)
};

struct Match {
    int start = 0;
    int length = 0;
    int end() const { return start + length; }
    QStringList captures; // regex only: group 0 (the match) then the groups
};

struct Result {
    QList<Match> matches;
    QString error; // an invalid regular expression: why; no matches then
    bool truncated = false;
};

// All non-overlapping matches of `needle` in `text` (none for an empty needle; empty regex matches
// are skipped), at most `limit`.
Result findAll(const QString& text, const QString& needle, const Options& options,
               int limit = 100000);

// What replaces `match`: `replacement` as it is, or for a regex its $0..$9 filled from the
// captures ($$ is a dollar sign).
QString replacementFor(const Match& match, const QString& replacement, const Options& options);

} // namespace findmatches
