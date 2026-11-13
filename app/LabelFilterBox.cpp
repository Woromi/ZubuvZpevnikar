#include "LabelFilterBox.hpp"

#include "SongTableModel.hpp"

#include <QSignalBlocker>

LabelFilterBox::LabelFilterBox(SongTableModel* model, QWidget* parent)
    : QComboBox(parent), model_(model) {
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
    setToolTip(tr("Show only songs with this label"));
    connect(this, &QComboBox::currentIndexChanged, this, [this] { emit labelChanged(label()); });
    connect(model_, &QAbstractItemModel::modelReset, this, &LabelFilterBox::refresh);
    connect(model_, &QAbstractItemModel::rowsInserted, this, &LabelFilterBox::refresh);
    connect(model_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                if (topLeft.column() <= SongTableModel::Labels &&
                    bottomRight.column() >= SongTableModel::Labels)
                    refresh();
            });
    refresh();
}

void LabelFilterBox::refresh() {
    const QStringList labels = model_->labels();
    QStringList shown;
    for (int i = 1; i < count(); ++i)
        shown << itemText(i);
    if (count() > 0 && shown == labels)
        return;
    const QString current = label();
    {
        const QSignalBlocker blocker(this);
        clear();
        addItem(tr("All labels"), QString{});
        for (const QString& name : labels)
            addItem(name, name);
        const int index = findData(current);
        setCurrentIndex(index < 0 ? 0 : index);
    }
    if (label() != current) // the chosen label is gone
        emit labelChanged(label());
}
