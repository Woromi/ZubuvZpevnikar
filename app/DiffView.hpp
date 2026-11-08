#pragma once

#include <QPlainTextEdit>
#include <string>

// A read-only view of the differences between two song texts: the whole new text with the
// removed lines interleaved. Added lines are green, removed lines red and struck out, and a
// gutter marker (+, -, space) starts every line.
class DiffView : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit DiffView(QWidget* parent = nullptr);

    // Shows what changed from `before` (the saved file) to `after` (the editor text).
    // `noChanges` is shown when the texts are equal (e.g. "No changes since the import").
    void setTexts(const std::string& before, const std::string& after, const QString& noChanges);
};
