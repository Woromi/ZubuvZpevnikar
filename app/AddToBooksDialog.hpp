#pragma once

#include "BookAdder.hpp"

#include <QDialog>
#include <filesystem>
#include <string>
#include <vector>

class QTreeWidget;

// Add to Songbooks…: the song library's books with checkboxes, one per song list (a book's
// sections, or the book itself when it has one list). Sections that have the song are checked and
// disabled; locked books, read-only lists and the book with unsaved LaTeX edits on the Songbooks
// page are disabled with the reason. targets() are the newly checked ones.
class AddToBooksDialog : public QDialog {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    AddToBooksDialog(const std::filesystem::path& root, const std::string& inputName,
                     const QString& songTitle, const QString& busyBookId,
                     QWidget* parent = nullptr);

    std::vector<BookAdder::Target> targets() const;
    bool anyAvailable() const { return available_ > 0; } // something can be checked

private:
    QTreeWidget* tree_ = nullptr;
    std::vector<BookAdder::Target> all_; // indexed by the items' data
    int available_ = 0;
};
