#pragma once

#include <QDialog>
#include <QString>
#include <optional>
#include <string>
#include <vector>
#include <zub/core/songoutline.hpp>
#include <zub/core/songsuggest.hpp>

class QListWidget;
class QPushButton;
class QTextBrowser;

// Make Smaller, Join Lines and Check Song: shows what chords_converter shrink/join/check found
// (the measured pages and the proposals) and lets the user pick one or several to apply, or none.
// Next to the list, a sketch of the song with the selected proposals made, the parts they change
// marked.
class ShrinkDialog : public QDialog {
    Q_OBJECT

public:
    // `songText` is the text the proposals were listed for (the proposals' edits point into it). `title` and
    // `heading` name the action (Make Smaller, Restructure, Join Lines).
    ShrinkDialog(const zub::songsuggest::Suggestions& suggestions, const std::string& songText,
                 const QString& title, const QString& heading, QWidget* parent = nullptr);

    // The selected proposals in list order (Ctrl/Shift+click picks several); valid after the
    // dialog was accepted, never empty then.
    std::vector<zub::songsuggest::Proposal> selected() const;

    // The measurement and the converter's note as text ("A5: 2 pages, doesn't fit yet" ...).
    // Empty when nothing was measured and there is no note.
    static QString summary(const zub::songsuggest::Suggestions& suggestions);

signals:
    // The user moved to a proposal at this line of the song (not for the initial selection).
    void proposalSelected(int line);

private:
    zub::songsuggest::Suggestions suggestions_;
    std::string songText_;
    QListWidget* list_ = nullptr;
    QTextBrowser* outline_ = nullptr;
    QPushButton* apply_ = nullptr;

    std::vector<int> selectedRows() const; // sorted
    // The sketch of the song with all selected proposals made; Apply only when they fit together.
    void showOutline();
};
