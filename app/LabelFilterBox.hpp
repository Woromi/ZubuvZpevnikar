#pragma once

#include <QComboBox>
#include <QString>

class SongTableModel;

// Combo box of the labels in the song table: "All labels" and every label of any song, sorted for
// the locale. Refreshes itself when the table changes and keeps the selection while the label
// exists.
class LabelFilterBox : public QComboBox {
    Q_OBJECT

public:
    explicit LabelFilterBox(SongTableModel* model, QWidget* parent = nullptr);

    // The chosen label, empty for "All labels".
    QString label() const { return currentData().toString(); }

signals:
    void labelChanged(const QString& label);

private:
    void refresh();

    SongTableModel* model_;
};
