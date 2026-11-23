#pragma once

#include <QWidget>

class QButtonGroup;
class QToolButton;

// The narrow bar at the window's left edge, like a code editor's activity bar: one button per
// view (the song list, the open song, the songbooks) and Settings at the bottom.
class ActivityBar : public QWidget {
    Q_OBJECT

public:
    enum class View { Home, Songs, Song, Songbooks };

    explicit ActivityBar(QWidget* parent = nullptr);

    void setCurrent(View view);           // shows it as the current one; emits nothing
    void setSongEnabled(bool open);       // the Song button works while a song is open
    QToolButton* button(View view) const; // for tests

signals:
    void viewRequested(ActivityBar::View view);
    void settingsRequested();

private:
    QToolButton* addButton(View view, const QIcon& icon, const QString& toolTip);

    QButtonGroup* group_ = nullptr;
    QToolButton* settings_ = nullptr;
};
