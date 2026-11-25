#include "SongbookScan.hpp"

#include "ConverterRunner.hpp"
#include "ExternalTools.hpp"
#include "convert.hpp"

#include <QProcess>
#include <QTimer>
#include <zub/core/library.hpp>
#include <zub/core/score.hpp>
#include <zub/core/song.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/songtidy.hpp>
#include <zub/core/textfile.hpp>

SongbookScan::SongbookScan(ScanOptions options, std::vector<Song> songs, QObject* parent)
    : QObject(parent), options_(std::move(options)), songs_(std::move(songs)),
      runner_(new ConverterRunner(this)), process_(new QProcess(this)) {
    if (options_.keysMissing || options_.keysStated)
        scripts_.push_back(Script::Keys);
    if (options_.metadata)
        scripts_.push_back(Script::Metadata);
    connect(process_, &QProcess::finished, this, &SongbookScan::onScriptFinished);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onScriptFinished();
    });
    for (std::size_t i = 0; i < songs_.size(); ++i) {
        if (options_.tidy)
            steps_.push_back({i, Kind::Tidy});
        if (options_.check)
            steps_.push_back({i, Kind::Check});
        if (options_.shrink)
            steps_.push_back({i, Kind::Shrink});
    }
    connect(runner_, &ConverterRunner::finished, this,
            [this](const std::expected<ConverterOutput, QString>& result) {
                if (result)
                    onRunFinished({}, result->exitCode, result->out, result->err);
                else
                    onRunFinished(result.error(), -1, {}, {});
            });
}

void SongbookScan::start() {
    current_ = 0;
    scriptAt_ = 0;
    // From the event loop: the caller connects first.
    QTimer::singleShot(0, this, &SongbookScan::nextScript);
}

void SongbookScan::nextScript() {
    if (cancelled_)
        return;
    if (scriptAt_ >= scripts_.size()) {
        next();
        return;
    }
    const Script script = scripts_[scriptAt_];
    emit progress(static_cast<int>(scriptAt_), stepCount(),
                  script == Script::Keys ? tr("Keys of all songs (checkkeys.py)")
                                         : tr("Metadata of all songs (checksongs.py)"));
    const auto command =
        script == Script::Keys
            ? zub::songcheck::scriptCommand(options_.packageDir, "checkkeys.py",
                                            options_.songbookRoot, {"--all"})
            : zub::songcheck::scriptCommand(options_.packageDir, "checksongs.py",
                                            options_.songbookRoot, {"--no-orphans"});
    QStringList arguments;
    for (const auto& argument : command.arguments)
        arguments << toQString(argument);
    // python3, or on Windows the Python found there (none: it fails to start, as without one).
    const QString python = externaltools::python();
    process_->start(python.isEmpty() ? toQString(command.program) : python, arguments);
}

void SongbookScan::onScriptFinished() {
    if (cancelled_)
        return;
    const Script script = scripts_[scriptAt_++];
    const QString name = script == Script::Keys ? QStringLiteral("checkkeys.py")
                                                : QStringLiteral("checksongs.py");
    const std::string out = process_->readAllStandardOutput().toStdString();
    const QString err = QString::fromUtf8(process_->readAllStandardError()).trimmed();
    if (process_->error() == QProcess::FailedToStart) {
        emit problem(tr("%1 could not run: python3 was not found.").arg(name));
        QTimer::singleShot(0, this, &SongbookScan::nextScript);
        return;
    }
    // The songs of this check, by path.
    std::map<std::string, std::size_t> wanted;
    for (std::size_t i = 0; i < songs_.size(); ++i)
        wanted.emplace(songs_[i].path.lexically_normal().string(), i);
    const auto songOf = [&](const std::string& file) -> const Song* {
        const auto it =
            wanted.find((options_.songbookRoot / toPath(toQString(file))).lexically_normal().string());
        return it == wanted.end() ? nullptr : &songs_[it->second];
    };
    if (script == Script::Keys) {
        const auto findings = zub::songcheck::parseCheckKeys(out);
        if (!findings) {
            emit problem(tr("%1 failed: %2")
                             .arg(name, err.isEmpty() ? toQString(findings.error()) : err));
        } else {
            for (const auto& finding : *findings) {
                const Song* song = songOf(finding.file);
                const bool missing = finding.kind == "propose" && options_.keysMissing;
                const bool differs = finding.kind == "mismatch" && options_.keysStated;
                if (!song || (!missing && !differs))
                    continue;
                const auto content = zub::readTextFile(song->path);
                if (!content || (differs && zub::songcheck::keyConfirmed(*content)))
                    continue; // confirmed by hand: % nocheck key
                const auto meta = zub::parseSong(*content);
                const bool german = meta && zub::score::germanNames(meta->value("lang"));
                const QString detected =
                    toQString(zub::songcheck::keyValue(finding.detected, german));
                const QString margin = QString::number(finding.margin, 'f', 1);
                ScanFinding& found = scriptFindings_[song->path.lexically_normal().string()];
                found.keys << (missing ? tr("none written, the chords say %1 (margin %2)")
                                             .arg(detected, margin)
                                       : tr("written %1, the chords say %2 (margin %3)")
                                             .arg(toQString(zub::songcheck::keyValue(
                                                      finding.stated, german)),
                                                  detected, margin));
                emit keyFound(toQString(song->path), finding);
            }
        }
    } else {
        const auto issues = zub::songcheck::parseCheckSongs(out);
        if (!issues) {
            emit problem(
                tr("%1 failed: %2").arg(name, err.isEmpty() ? toQString(issues.error()) : err));
        } else {
            for (const auto& [file, list] : *issues) {
                const Song* song = songOf(file);
                if (!song)
                    continue;
                ScanFinding& found = scriptFindings_[song->path.lexically_normal().string()];
                for (const auto& issue : list)
                    found.metadata << (issue.line ? tr("line %1: %2")
                                                        .arg(*issue.line)
                                                        .arg(toQString(issue.message))
                                                  : toQString(issue.message));
            }
        }
    }
    QTimer::singleShot(0, this, &SongbookScan::nextScript);
}

void SongbookScan::takeScriptFindings(std::size_t song) {
    const auto it = scriptFindings_.find(songs_[song].path.lexically_normal().string());
    if (it == scriptFindings_.end())
        return;
    finding_.keys = it->second.keys;
    finding_.metadata = it->second.metadata;
    scriptFindings_.erase(it);
}

void SongbookScan::cancel() {
    if (cancelled_)
        return;
    cancelled_ = true;
    runner_->cancel();
    if (process_->state() != QProcess::NotRunning)
        process_->kill();
    QTimer::singleShot(0, this, [this] { emit finished(true); });
}

void SongbookScan::next() {
    while (!cancelled_) {
        if (current_ >= steps_.size()) {
            // The songs only the scripts found something in.
            for (std::size_t i = 0; i < songs_.size() && !scriptFindings_.empty(); ++i) {
                finding_ = {};
                takeScriptFindings(i);
                if (finding_.any())
                    emit songScanned(toQString(songs_[i].path), finding_);
            }
            finding_ = {};
            emit finished(false);
            return;
        }
        const Step& step = steps_[current_];
        const Song& song = songs_[step.song];
        const bool firstOfSong = current_ == 0 || steps_[current_ - 1].song != step.song;
        if (firstOfSong) {
            finding_ = {};
            auto content = zub::readTextFile(song.path);
            if (!content) {
                finding_.errors << tr("cannot read the file");
                // skip the song's other steps
                while (current_ < steps_.size() && steps_[current_].song == step.song)
                    ++current_;
                endSong(step.song);
                continue;
            }
            content_ = std::move(*content);
        }

        const QString name = toQString(song.inputName);
        QString what;
        ConverterRunner::CommandFactory command;
        const auto converter = options_.converter;
        const auto root = options_.songbookRoot;
        const auto package = options_.packageDir;
        switch (step.kind) {
        case Kind::Tidy:
            what = tr("Tidy: %1").arg(name);
            command = [converter](const std::filesystem::path& file) {
                return zub::songtidy::prepare(converter, file);
            };
            break;
        case Kind::Check:
            what = tr("Check: %1").arg(name);
            command = [converter](const std::filesystem::path& file) {
                return zub::songsuggest::checkCommand(converter, file);
            };
            break;
        case Kind::Shrink: {
            // The package's checkpaper.py measures songs of songs/chords and songs/tinwhistle.
            if (const auto collection = zub::collectionOf(options_.songbookRoot, song.path);
                collection && *collection != "chords" && *collection != "tinwhistle") {
                finding_.errors << tr("not measured: the package measures only Chords and Tin "
                                      "whistle songs");
                ++current_;
                if (current_ >= steps_.size() || steps_[current_].song != step.song)
                    endSong(step.song);
                continue;
            }
            // Measuring builds the song: never one with commands outside the song format.
            if (!zub::safety::unknownConstructs(content_).empty()) {
                finding_.errors << tr("not measured: commands outside the song format");
                ++current_;
                if (current_ >= steps_.size() || steps_[current_].song != step.song)
                    endSong(step.song);
                continue;
            }
            // ... and neither one whose scores run code in LilyPond.
            if (const auto risks =
                    zub::songbook::scoreRisks(options_.songbookRoot, song.path, content_);
                !risks.empty()) {
                finding_.errors << tr("not measured: %1").arg(toQString(risks.front()));
                ++current_;
                if (current_ >= steps_.size() || steps_[current_].song != step.song)
                    endSong(step.song);
                continue;
            }
            what = tr("Measuring: %1").arg(name);
            const auto paper = options_.paper;
            command = [converter, paper, root, package](const std::filesystem::path& file) {
                return zub::songsuggest::shrinkCommand(converter, file, paper, true, root, package);
            };
            break;
        }
        }
        emit progress(static_cast<int>(scripts_.size() + current_), stepCount(), what);
        if (runner_->start(command, content_, toQString(song.path.stem())))
            return; // onRunFinished goes on
        finding_.errors << tr("the converter could not start");
        ++current_;
        if (current_ >= steps_.size() || steps_[current_].song != step.song)
            endSong(step.song);
    }
}

void SongbookScan::onRunFinished(const QString& error, int exitCode, const std::string& out,
                                 const std::string& err) {
    if (cancelled_)
        return;
    const Step step = steps_[current_];
    const auto failed = [this](const QString& what, const QString& message) {
        finding_.errors << tr("%1 failed: %2").arg(what, message);
    };
    if (!error.isEmpty()) {
        failed(step.kind == Kind::Tidy    ? tr("Tidy")
               : step.kind == Kind::Check ? tr("Check")
                                          : tr("Measuring"),
               error);
    } else {
        switch (step.kind) {
        case Kind::Tidy: {
            const auto tidied = zub::songtidy::interpret(exitCode, out, err);
            if (!tidied)
                failed(tr("Tidy"), toQString(tidied.error()));
            else
                for (const auto& change : tidied->changes)
                    finding_.tidy
                        << tr("line %1: %2").arg(change.line).arg(toQString(change.description));
            break;
        }
        case Kind::Check: {
            const auto found = zub::songsuggest::interpretProposals(exitCode, out, err);
            if (!found)
                failed(tr("Check"), toQString(found.error()));
            else
                for (const auto& proposal : found->proposals)
                    finding_.check << toQString(proposal.description);
            break;
        }
        case Kind::Shrink: {
            const auto found = zub::songsuggest::interpretProposals(exitCode, out, err);
            if (!found) {
                failed(tr("Measuring"), toQString(found.error()));
            } else if (found->fits == false) {
                const auto& measured = found->a5 ? found->a5 : found->a4;
                if (measured) {
                    const QString paper = found->a5 ? QStringLiteral("A5") : QStringLiteral("A4");
                    // one page that still doesn't fit: lines too wide for the column
                    finding_.pages = measured->pages > 1
                                         ? tr("%1: %n pages", nullptr, measured->pages).arg(paper)
                                         : tr("%1: lines too wide").arg(paper);
                }
                for (const auto& proposal : found->proposals)
                    finding_.shrink << toQString(proposal.description);
                if (finding_.shrink.isEmpty())
                    finding_.shrink << tr("does not fit, and nothing more to suggest");
            }
            break;
        }
        }
    }
    ++current_;
    if (current_ >= steps_.size() || steps_[current_].song != step.song)
        endSong(step.song);
    next();
}

void SongbookScan::endSong(std::size_t song) {
    takeScriptFindings(song);
    if (finding_.any())
        emit songScanned(toQString(songs_[song].path), finding_);
    finding_ = {};
    content_.clear();
}
