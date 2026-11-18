#pragma once

#include "FindMatches.hpp"

#include <QList>
#include <QTextEdit>
#include <QWidget>

class CodeTextEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QToolButton;

// The find & replace bar of one CodeTextEdit (one bar per editor, each with its own state): sits
// below the editor, hidden until open(). Marks all matches (a colour of its own, apart from the
// editor's marks of the selection's other occurrences), selects the current one, wraps around.
// Replace All is one undo step; the replace controls are disabled while the editor is read-only.
// Enter / F3: next, Shift+Enter / Shift+F3: previous, Escape closes the bar and focuses the editor.
class FindBar : public QWidget {
    Q_OBJECT

public:
    // `edit` is the editor searched; the bar belongs next to it (the owner puts it in a layout).
    explicit FindBar(CodeTextEdit* edit, QWidget* parent = nullptr);

    // Shows the bar (with the replace row in replace mode), takes a one-line selection of the
    // editor as the search text, focuses the find field with its text selected.
    void open(bool replaceMode);
    // Hides the bar, drops the marks and focuses the editor.
    void dismiss();

    void findNext();
    void findPrevious();
    // Replaces the current match (the selection, when it is a match) and moves to the next one;
    // else only moves to the next match.
    void replaceCurrent();
    // Replaces every match, as one undo step.
    void replaceAll();

    bool replaceMode() const;
    QString countText() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend struct AppTestAccess;
    findmatches::Options options() const;
    // Searches again (the text, the needle or an option changed) and updates marks and count.
    void refresh();
    // The match under the selection, or -1.
    int currentIndex() const;
    void select(const findmatches::Match& match);
    // Incremental search while typing: the first match at or after the selection's start.
    void selectFromStart();
    void updateMarks();
    void updateCount();
    void updateEnabled();

    CodeTextEdit* edit_;
    QLineEdit* find_;
    QLineEdit* replace_;
    QWidget* replaceRow_;
    QToolButton* matchCase_;
    QToolButton* wholeWord_;
    QToolButton* regex_;
    QLabel* count_;
    QToolButton* previous_;
    QToolButton* next_;
    QPushButton* replaceButton_;
    QPushButton* replaceAllButton_;
    findmatches::Result result_;
    QString replacedNote_; // "N replaced" until the next change
    QTimer* pending_ = nullptr;
    bool textChanged_ = false; // the text changed since the last search
    bool busy_ = false;        // our own edits: refresh once at the end
};
