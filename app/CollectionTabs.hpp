#pragma once

#include <QString>
#include <QTabBar>
#include <string>
#include <vector>

// The song collections (the folders under songs/: Chords, Tin whistle, …) as tabs, with "All"
// first. Hidden while there is only one collection. The chosen tab is remembered under
// `settingsKey`.
class CollectionTabs : public QTabBar {
    Q_OBJECT

public:
    CollectionTabs(const QString& settingsKey, QWidget* parent = nullptr);

    void setCollections(const std::vector<std::string>& collections);
    QString current() const; // the chosen collection, "" for All

    // "Chords", "Tin whistle", else the folder's own name.
    static QString displayName(const QString& collection);

signals:
    void collectionChanged(const QString& collection); // "" for All

private:
    QString settingsKey_;
    bool filling_ = false;
};
