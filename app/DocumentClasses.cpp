#include "DocumentClasses.hpp"

#include <QCoreApplication>

namespace documentclasses {

// The context stays "SongbooksPage", where these texts were first translated.
static QString tr(const char* text) {
    return QCoreApplication::translate("SongbooksPage", text);
}

QList<Known> known() {
    return {
        {QStringLiteral("velkyzpevnik"), tr("Velký zpěvník (A4)")},
        {QStringLiteral("malyzpevnik"), tr("Malý zpěvník (A5 book)")},
        {QStringLiteral("taborovyzpevnik"), tr("Táborový zpěvník (A5 camp booklet)")},
    };
}

QString displayName(const QString& cls) {
    for (const Known& entry : known())
        if (entry.name == cls)
            return entry.display;
    return cls;
}

} // namespace documentclasses
