#include "ConverterVersion.hpp"

#include "convert.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <map>
#include <utility>

namespace {

struct CacheEntry {
    qint64 modified = 0;
    qint64 size = 0;
    ConverterVersionResult result;
};

ConverterVersionResult askVersion(const QString& path) {
    using State = ConverterVersionResult::State;
    QProcess process;
    process.setProgram(path);
    process.setArguments({QStringLiteral("--version")});
    process.setWorkingDirectory(QDir::tempPath());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(2000))
        return {State::NoAnswer, std::nullopt, process.error() == QProcess::FailedToStart};
    if (!process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(1000);
        return {State::NoAnswer, std::nullopt, false};
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        if (const auto version =
                zub::converter::parseVersion(process.readAllStandardOutput().toStdString()))
            return {State::Known, version, false};
    }
    return {State::Unparseable, std::nullopt, false};
}

} // namespace

ConverterVersionResult converterVersion(const std::filesystem::path& converter) {
    static std::map<std::filesystem::path, CacheEntry> cache;
    const QString path = toQString(converter);
    const QFileInfo info(path);
    const qint64 modified = info.lastModified().toMSecsSinceEpoch();
    const qint64 size = info.size();
    if (const auto it = cache.find(converter);
        it != cache.end() && it->second.modified == modified && it->second.size == size)
        return it->second.result;
    const auto result = askVersion(path);
    if (!result.failedToStart) // ask again next time
        cache[converter] = {modified, size, result};
    return result;
}

QString converterProblem(const std::filesystem::path& converter) {
    const QString required = QString::fromStdString(zub::converter::toString(zub::converter::kRequired));
    const auto answer = converterVersion(converter);
    if (answer.state == ConverterVersionResult::State::NoAnswer) {
        return QCoreApplication::translate("ConverterVersion",
                                           "This chords_converter did not answer --version within "
                                           "2 s (or could not be started): %1")
            .arg(toQString(converter));
    }
    const auto& version = answer.version;
    if (!version) {
        return QCoreApplication::translate(
                   "ConverterVersion",
                   "This chords_converter is too old or not a chords_converter (it does not "
                   "report a version; %1 is needed): %2\nUse the built-in one.")
            .arg(required, toQString(converter));
    }
    if (*version < zub::converter::kRequired) {
        return QCoreApplication::translate(
                   "ConverterVersion",
                   "This chords_converter is too old (%1; needs %2): %3\nUse the built-in one.")
            .arg(QString::fromStdString(zub::converter::toString(*version)), required,
                 toQString(converter));
    }
    return {};
}
