#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <string>
#include <vector>

class QCheckBox;
class QLineEdit;

// Edits the labels of one or several songs: the favourite checkbox, a checkbox for every known
// label (checked = all the songs have it, partly checked = some do, left alone unless clicked)
// and a line for new labels, separated by commas.
class LabelsDialog : public QDialog {
    Q_OBJECT

public:
    // `known`: every label of the library (not "fav"); `songs`: the tags of each selected song.
    LabelsDialog(const QStringList& known, const std::vector<std::vector<std::string>>& songs,
                 QWidget* parent = nullptr);

    // The tags of a song that had `current` after the dialog's choices: unchecked ones removed,
    // checked and new ones added at the end, the rest (and partly checked ones) unchanged.
    std::vector<std::string> apply(const std::vector<std::string>& current) const;

    void accept() override; // refuses invalid new labels

private:
    struct Item {
        std::string tag;
        QCheckBox* box;
    };
    std::vector<std::string> newLabels() const; // trimmed, valid ones only
    static void applyBox(std::vector<std::string>& tags, const std::string& tag,
                         const QCheckBox* box);

    QCheckBox* favourite_ = nullptr;
    std::vector<Item> items_;
    QLineEdit* newLabels_ = nullptr;
};
