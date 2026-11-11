#include "SongbookSorter.hpp"

#include "convert.hpp"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>

SongbookSorter::SongbookSorter(QObject* parent) : QObject(parent) {
    startLocaleDetection();
}

SongbookSorter::~SongbookSorter() {
    if (localeProcess_) {
        localeProcess_->disconnect(this);
        localeProcess_->kill();
        localeProcess_->waitForFinished(500);
    }
    stopRun();
}

void SongbookSorter::stopRun() {
    if (!process_)
        return;
    process_->disconnect(this);
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(2000);
    }
    delete process_;
    process_ = nullptr;
}

void SongbookSorter::setTools(const std::filesystem::path& songbookRoot, const QString& texlua) {
    root_ = songbookRoot;
    texlua_ = texlua;
}

void SongbookSorter::startLocaleDetection() {
    if (localeDone_ || localeProcess_)
        return;
    localeProcess_ = new QProcess(this);
    connect(localeProcess_, &QProcess::finished, this, [this] {
        finishLocale(QString::fromLocal8Bit(localeProcess_->readAllStandardOutput()), true);
    });
    connect(localeProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishLocale({}, false);
    });
    QTimer::singleShot(2000, this, [this] { finishLocale({}, false); });
    localeProcess_->start(QStringLiteral("locale"), {QStringLiteral("-a")});
}

void SongbookSorter::finishLocale(const QString& output, bool ran) {
    if (localeDone_)
        return;
    localeDone_ = true;
    if (localeProcess_) {
        localeProcess_->disconnect(this);
        if (localeProcess_->state() != QProcess::NotRunning)
            localeProcess_->kill();
        localeProcess_->deleteLater();
        localeProcess_ = nullptr;
    }
    if (ran) {
        const QStringList available = output.split(QLatin1Char('\n'));
        for (const QString& wanted : {QStringLiteral("cs_CZ.utf8"), QStringLiteral("cs_CZ.UTF-8"),
                                      QStringLiteral("cs_CZ")}) {
            for (const QString& line : available) {
                if (locale_.isEmpty() && line.trimmed().compare(wanted, Qt::CaseInsensitive) == 0)
                    locale_ = line.trimmed();
            }
        }
        localeUnavailable_ = locale_.isEmpty();
    }
    startNext();
}

void SongbookSorter::sort(const QString& key, std::vector<std::string> inputNames,
                          std::vector<zub::songbook::SortEntry> entries) {
    // A newer request replaces the older ones for the same key.
    std::erase_if(pending_, [&](const Request& r) { return r.key == key; });
    if (running_ && running_->key == key)
        runningStale_ = true;

    if (inputNames.size() <= 1) {
        QTimer::singleShot(0, this, [this, key, names = std::move(inputNames)] {
            emit sorted(key, names);
        });
        return;
    }
    pending_.push_back({key, std::move(inputNames), std::move(entries)});
    if (!running_)
        startNext();
}

void SongbookSorter::startNext() {
    // Requests wait for the locale.
    if (running_ || pending_.empty() || !localeDone_)
        return;
    running_ = std::move(pending_.front());
    pending_.pop_front();
    runningStale_ = false;
    const QString key = running_->key;

    const auto failNow = [this, key](const QString& message) {
        running_.reset();
        QTimer::singleShot(0, this, [this, key, message] {
            emit failed(key, message);
            startNext();
        });
    };
    if (texlua_.isEmpty()) {
        failNow(tr("texlua was not found (it comes with TeX Live)."));
        return;
    }
    if (localeUnavailable_ && !localeWarned_) {
        localeWarned_ = true;
        QTimer::singleShot(0, this, [this] {
            emit warning(tr("No Czech locale (cs_CZ.UTF-8) is installed: the songs are sorted in "
                            "the default locale's order, which may differ from the songbook's "
                            "index."));
        });
    }
    dir_ = std::make_unique<QTemporaryDir>();
    if (!dir_->isValid()) {
        failNow(tr("Cannot create a temporary directory: %1").arg(dir_->errorString()));
        return;
    }
    const std::string input = zub::songbook::indexOrderInput(running_->entries);
    QFile in(dir_->filePath(QStringLiteral("in.sxd")));
    if (!in.open(QIODevice::WriteOnly) ||
        in.write(input.data(), static_cast<qint64>(input.size())) != static_cast<qint64>(input.size())) {
        failNow(tr("Cannot write %1: %2").arg(in.fileName(), in.errorString()));
        return;
    }
    in.close();

    const zub::converter::Command command = zub::songbook::sortCommand(
        toPath(texlua_), root_ / "scripts" / "songidx.lua", toStdString(locale_),
        toPath(in.fileName()), toPath(dir_->filePath(QStringLiteral("out.sbx"))));
    QStringList arguments;
    for (const std::string& argument : command.arguments)
        arguments << toQString(argument);

    output_.clear();
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->setWorkingDirectory(dir_->path());
    connect(process_, &QProcess::readyRead, this, [this] { output_ += process_->readAll(); });
    connect(process_, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
                output_ += process_->readAll();
                finishRun(code, status == QProcess::CrashExit, {});
            });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishRun(-1, false, process_->errorString());
    });
    process_->start(toQString(command.program), arguments);
}

void SongbookSorter::finishRun(int exitCode, bool crashed, const QString& processError) {
    Request request = std::move(*running_);
    running_.reset();
    const bool stale = runningStale_;
    process_->disconnect(this);
    process_->deleteLater();
    process_ = nullptr;
    const QString outFile = dir_->filePath(QStringLiteral("out.sbx"));

    std::optional<std::vector<std::string>> result;
    QString error;
    if (!processError.isEmpty()) {
        error = tr("Cannot run texlua: %1").arg(processError);
    } else if (crashed || exitCode != 0) {
        error = tr("texlua failed (exit %1): %2")
                    .arg(exitCode)
                    .arg(QString::fromUtf8(output_.right(400)).trimmed());
    } else {
        QFile out(outFile);
        if (!out.open(QIODevice::ReadOnly)) {
            error = tr("Cannot read the sorted index: %1").arg(out.errorString());
        } else {
            const QByteArray bytes = out.readAll();
            const auto order = zub::songbook::parseIndexOrder(
                std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                request.names.size());
            if (!order) {
                error = toQString(order.error());
            } else {
                std::vector<std::string> names;
                for (const int position : *order)
                    names.push_back(request.names[static_cast<std::size_t>(position - 1)]);
                result = std::move(names);
            }
        }
    }
    dir_.reset();

    if (!stale) {
        if (result)
            emit sorted(request.key, std::move(*result));
        else
            emit failed(request.key, error);
    }
    startNext();
}
