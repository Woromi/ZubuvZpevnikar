#include "CollectionTabs.hpp"

#include "convert.hpp"

#include <QSettings>

CollectionTabs::CollectionTabs(const QString& settingsKey, QWidget* parent)
    : QTabBar(parent), settingsKey_(settingsKey) {
    setDocumentMode(true);
    setDrawBase(false);
    setExpanding(false);
    hide();
    connect(this, &QTabBar::currentChanged, this, [this] {
        if (filling_)
            return;
        QSettings{}.setValue(settingsKey_, current());
        emit collectionChanged(current());
    });
}

QString CollectionTabs::displayName(const QString& collection) {
    if (collection == QLatin1String("chords"))
        return tr("Chords", "song collection");
    if (collection == QLatin1String("tinwhistle"))
        return tr("Tin whistle", "song collection");
    return collection;
}

void CollectionTabs::setCollections(const std::vector<std::string>& collections) {
    const QString wanted = count() > 0 ? current() : QSettings{}.value(settingsKey_).toString();
    filling_ = true;
    while (count() > 0)
        removeTab(0);
    addTab(tr("All", "song collections"));
    setTabData(0, QString());
    int select = 0;
    for (const std::string& name : collections) {
        const QString collection = toQString(name);
        const int index = addTab(displayName(collection));
        setTabData(index, collection);
        if (collection == wanted)
            select = index;
    }
    setCurrentIndex(select);
    filling_ = false;
    setVisible(collections.size() > 1);
    emit collectionChanged(current());
}

QString CollectionTabs::current() const {
    return currentIndex() < 0 ? QString() : tabData(currentIndex()).toString();
}
