#include "DesktopIntegration.hpp"

#include <QApplication>
#include <optional>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <zub/core/desktopentry.hpp>

namespace desktopintegration {

namespace {

const auto enabledKey = QStringLiteral("desktop/menuEntry");

QString dataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

std::optional<QByteArray> readAll(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

// Writes `bytes` to `path` unless it already holds them. False when it cannot be written.
bool writeIfChanged(const QString& path, const QByteArray& bytes, bool* changed = nullptr) {
    if (readAll(path) == bytes)
        return true;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return false;
    if (changed)
        *changed = true;
    return true;
}

} // namespace

QString appImage() {
    const QString path = qEnvironmentVariable("APPIMAGE");
    return !path.isEmpty() && QFileInfo(path).isFile() ? path : QString();
}

bool enabled() {
    return QSettings{}.value(enabledKey, true).toBool();
}

void setEnabled(bool on) {
    if (on)
        QSettings{}.remove(enabledKey);
    else
        QSettings{}.setValue(enabledKey, false);
}

Source bundled() {
    const QDir share(QApplication::applicationDirPath() + QStringLiteral("/../share"));
    Source source;
    source.desktopFile = share.filePath(QStringLiteral("applications/zpevnikar.desktop"));
    const QDir hicolor(share.filePath(QStringLiteral("icons/hicolor")));
    for (const QString& dir : hicolor.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString icon = hicolor.filePath(dir + QStringLiteral("/apps/zpevnikar.png"));
        const int size = dir.section(QLatin1Char('x'), 0, 0).toInt();
        if (size > 0 && QFileInfo(icon).isFile())
            source.icons.append({size, icon});
    }
    return source;
}

QString entryPath() {
    return dataDir() + QStringLiteral("/applications/zpevnikar.desktop");
}

Result install(const QString& image, const Source& source) {
    if (image.isEmpty() || dataDir().isEmpty())
        return Result::Failed;
    const auto own = readAll(source.desktopFile);
    if (!own)
        return Result::Failed;
    const auto existing = readAll(entryPath());
    if (existing && !zub::desktopentry::writtenForAppImage(existing->toStdString()))
        return Result::NotOurs;
    const std::string entry =
        zub::desktopentry::forAppImage(own->toStdString(), image.toStdString());
    bool changed = false;
    if (!writeIfChanged(entryPath(), QByteArray::fromStdString(entry), &changed))
        return Result::Failed;
    for (const auto& [size, file] : source.icons) {
        const auto icon = readAll(file);
        const QString target =
            dataDir() + QStringLiteral("/icons/hicolor/%1x%1/apps/zpevnikar.png").arg(size);
        if (!icon || !writeIfChanged(target, *icon))
            return Result::Failed;
    }
    if (!existing)
        return Result::Added;
    return changed ? Result::Updated : Result::Unchanged;
}

bool remove() {
    const auto existing = readAll(entryPath());
    if (!existing)
        return true;
    if (!zub::desktopentry::writtenForAppImage(existing->toStdString()))
        return false;
    return QFile::remove(entryPath());
}

} // namespace desktopintegration
