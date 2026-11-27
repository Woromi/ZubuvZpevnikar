#pragma once

#include <QList>
#include <QString>

// One name per document class everywhere (preview combo, Songbooks page, New Songbook).
namespace documentclasses {

struct Known {
    QString name;    // the class as stored (settings, \documentclass)
    QString display; // translated
};

QList<Known> known();

// The translated name of a known class, else the raw name.
QString displayName(const QString& cls);

} // namespace documentclasses
