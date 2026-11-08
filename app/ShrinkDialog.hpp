#pragma once

#include <QDialog>
#include <QString>
#include <optional>
#include <string>
#include <vector>
#include <zub/core/songoutline.hpp>
#include <zub/core/songsuggest.hpp>

class QListWidget;
class QTextBrowser;

// "Make Smaller": shows what chords_converter suggest found (the measured pages and the
// proposals, rated, most readable first) and lets the user pick one to apply, or none. Next to
// the list, the song's parts as an outline with the parts the selected proposal changes marked.
class ShrinkDialog : public QDialog {
    Q_OBJECT

public:
    // `songText` is the text suggest ran on (the proposals' edits point into it).
    ShrinkDialog(const zub::songsuggest::Suggestions& suggestions, const std::string& songText,
                 QWidget* parent = nullptr);

    // Valid after the dialog was accepted.
    const zub::songsuggest::Proposal& selected() const;

    // The measurement and the converter's note as text ("A5: 2 pages, doesn't fit yet" ...).
    // Empty when nothing was measured and there is no note.
    static QString summary(const zub::songsuggest::Suggestions& suggestions);

signals:
    // The user selected a proposal at this line of the song (not for the initial selection).
    void proposalSelected(int line);

private:
    zub::songsuggest::Suggestions suggestions_;
    // per proposal; nullopt when its edits don't fit the text
    std::vector<std::optional<zub::songoutline::Preview>> previews_;
    QListWidget* list_ = nullptr;
    QTextBrowser* outline_ = nullptr;

    void showOutline(int row);
};
