#pragma once

#include <QString>
#include <QWidget>

struct ScoreResult;
class QLabel;
class QListWidget;
class QScrollArea;
class QToolButton;
class SvgCanvas;

// Shows the engraved SVG of the latest successful score build, the build status, and the errors
// of a failed build. A failed build keeps the last good image on screen.
class ScorePreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit ScorePreviewPane(QWidget* parent = nullptr);

    void showBuilding();
    void showResult(const ScoreResult& result);
    // The score is not built (e.g. not approved): `message` is shown in place of the errors.
    void showBlocked(const QString& message);
    void clear(); // no score: empty view, no status, no errors

signals:
    void rebuildRequested();
    // An error located in the score was double-clicked (1-based line and column).
    void errorActivated(int line, int column);

private:
    void showLog();

    QToolButton* rebuild_ = nullptr;
    QToolButton* logButton_ = nullptr;
    QLabel* status_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    SvgCanvas* canvas_ = nullptr;
    QListWidget* errors_ = nullptr;
    QString log_;
};
