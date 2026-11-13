#include "SongbookScan.hpp"

#include "ConverterRunner.hpp"
#include "convert.hpp"

#include <QTimer>
#include <zub/core/safety.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/songtidy.hpp>
#include <zub/core/textfile.hpp>

SongbookScan::SongbookScan(ScanOptions options, std::vector<Song> songs, QObject* parent)
    : QObject(parent), options_(std::move(options)), songs_(std::move(songs)),
      runner_(new ConverterRunner(this)) {
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
    // From the event loop: the caller connects first.
    QTimer::singleShot(0, this, &SongbookScan::next);
}

void SongbookScan::cancel() {
    if (cancelled_)
        return;
    cancelled_ = true;
    runner_->cancel();
    QTimer::singleShot(0, this, [this] { emit finished(true); });
}

void SongbookScan::next() {
    while (!cancelled_) {
        if (current_ >= steps_.size()) {
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
            command = [converter, paper, root](const std::filesystem::path& file) {
                return zub::songsuggest::shrinkCommand(converter, file, paper, true, root);
            };
            break;
        }
        }
        emit progress(static_cast<int>(current_), static_cast<int>(steps_.size()), what);
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
    if (finding_.any())
        emit songScanned(toQString(songs_[song].path), finding_);
    finding_ = {};
    content_.clear();
}
