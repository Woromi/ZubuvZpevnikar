#pragma once

#include <QString>
#include <QTreeWidget>
#include <string>
#include <utility>
#include <vector>

// The sections of a songbook: the songs outside any section are top-level items (first), then one
// bold item per named section with its songs below. Dragging songs (inside the tree, or from the
// picker, see SongPickerModel::songsMimeType) only reports where they were dropped; the page
// changes the songbook and fills the tree again.
class SongbookTree : public QTreeWidget {
    Q_OBJECT

public:
    explicit SongbookTree(QWidget* parent = nullptr);

    // Items are added by the page. The root part has no item of its own.
    void setRootUid(quint64 uid) { rootUid_ = uid; }
    QTreeWidgetItem* addSection(quint64 uid, const QString& text);
    // `section` null: a song outside any section (top level).
    QTreeWidgetItem* addSong(QTreeWidgetItem* section, quint64 uid, const std::string& inputName,
                             const QString& text, bool missing);

    bool isSection(const QTreeWidgetItem* item) const;
    static quint64 uidOf(const QTreeWidgetItem* item); // the section (of a song: the one holding it)
    // The selected songs: (section uid, input name), in tree order.
    std::vector<std::pair<quint64, std::string>> selectedSongs() const;

signals:
    // fromSectionUid 0: from the picker. `index` is where in the target the drop was.
    void songsDropped(std::vector<std::string> names, quint64 fromSectionUid, quint64 toSectionUid,
                      int index);
    void sortSectionRequested(quint64 uid); // the root part's uid for the songs outside sections
    void renameSectionRequested(quint64 uid);
    void removeSectionRequested(quint64 uid);
    void removeSelectedRequested();

protected:
    QStringList mimeTypes() const override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Target {
        quint64 uid = 0;
        int index = 0;
    };
    static bool ours(const QMimeData* mime);
    Target dropTarget(const QPoint& position) const;
    int rootSongCount() const;
    void showMenu(const QPoint& position);

    quint64 rootUid_ = 0;
};
