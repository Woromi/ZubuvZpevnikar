#pragma once

#include <QIdentityProxyModel>
#include <QString>
#include <QStringList>
#include <map>
#include <set>
#include <string>

// The song list as the songbook assembler's picker: the Title column has a check box that is
// checked for the songs of the current section; songs that are only in other sections are grey
// with a tooltip naming them. Sits over a SongFilterProxy (or the song table itself); changing
// the check state is up to the page (a click on the check box toggles), not to setData. Dragging
// rows gives the MIME type songsMimeType: the input names, one per line.
class SongPickerModel : public QIdentityProxyModel {
    Q_OBJECT

public:
    explicit SongPickerModel(QObject* parent = nullptr);

    static QString songsMimeType() { return QStringLiteral("application/x-zubuvzpevnik-songs"); }

    // Input names ("Artist/Title") in the current section.
    void setCurrent(std::set<std::string> inSection);
    // For songs that are in other sections: the names of those sections, already joined.
    void setOtherSections(std::map<std::string, QString> namesOfOtherSections);

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

private:
    void refresh();
    QString inputNameOf(const QModelIndex& index) const;

    std::set<std::string> current_;
    std::map<std::string, QString> others_;
};
