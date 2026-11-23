#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

// The Home view's panel while no songbook is open: at the first start (Open Songbook Folder…,
// New Song Library…), or when the remembered or given folder cannot be opened (where it was and
// why, with Try Again and Choose Another…).
class WelcomePage : public QWidget {
    Q_OBJECT

public:
    explicit WelcomePage(QWidget* parent = nullptr);

    void showFirstStart();
    // `path` could not be opened as a songbook: `reason` says why.
    void showMissing(const QString& path, const QString& reason);
    QString path() const { return path_; } // the folder Try Again opens ("" at the first start)

signals:
    void chooseRequested();                   // Open Songbook Folder… / Choose Another…
    void retryRequested(const QString& path); // Try Again
    void newLibraryRequested();               // New Song Library…

private:
    QString path_;
    QLabel* message_ = nullptr;
    QPushButton* choose_ = nullptr;
    QPushButton* retry_ = nullptr;
    QPushButton* library_ = nullptr;
};
