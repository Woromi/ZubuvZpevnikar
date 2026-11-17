#include "SongActions.hpp"

#include "ConverterRunner.hpp"
#include "ConverterVersion.hpp"
#include "ShrinkDialog.hpp"
#include "SongEditor.hpp"
#include "convert.hpp"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QPointer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <memory>
#include <zub/core/latex.hpp>
#include <zub/core/linediff.hpp>
#include <zub/core/preview.hpp>
#include <zub/core/score.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songheader.hpp>
#include <zub/core/songoutline.hpp>
#include <zub/core/songsuggest.hpp>
#include <zub/core/songtidy.hpp>
#include <zub/core/textfile.hpp>

SongActions::SongActions(QWidget* window, Hooks hooks)
    : QObject(window), window_(window), hooks_(std::move(hooks)) {
    converterRunner_ = new ConverterRunner(this);
    connect(converterRunner_, &ConverterRunner::finished, this, &SongActions::onConverterFinished);

    tidyAction_ = new QAction(QIcon::fromTheme(QStringLiteral("tools-wizard")), tr("Tidy"), this);
    tidyAction_->setToolTip(tr(
        "Tidy the song with chords_converter: repeated choruses, chord positions (one undo step)"));
    connect(tidyAction_, &QAction::triggered, this, [this] { startTidy(); });

    makeSmallerAction_ =
        new QAction(QIcon::fromTheme(QStringLiteral("zoom-out")), tr("Make Smaller"), this);
    makeSmallerAction_->setToolTip(tr("Ask chords_converter for changes that make the song print "
                                      "smaller; apply one (one undo step), then ask again"));
    connect(makeSmallerAction_, &QAction::triggered, this,
            [this] { startProposals(SongAction::Shrink); });

    joinLinesAction_ = new QAction(QIcon::fromTheme(QStringLiteral("format-justify-fill")),
                                   tr("Join Lines"), this);
    joinLinesAction_->setToolTip(
        tr("Join every two lines of a kind of part into one, where they still fit the column "
           "(typesets the song on A4 and A5: a few seconds)"));
    connect(joinLinesAction_, &QAction::triggered, this,
            [this] { startProposals(SongAction::Join); });

    checkAction_ = new QAction(QIcon::fromTheme(QStringLiteral("tools-check-spelling")),
                               tr("Check Song"), this);
    checkAction_->setToolTip(
        tr("Look for mistakes: typos where a line's copies differ, apostrophes typed two ways, "
           "a chorus typed as a verse"));
    connect(checkAction_, &QAction::triggered, this, [this] { startProposals(SongAction::Check); });
}

void SongActions::setSong(SongEditor* song) {
    song_ = song;
}

void SongActions::setSongbook(const std::filesystem::path& root,
                              const std::filesystem::path& packageDir) {
    songbookRoot_ = root;
    packageDir_ = packageDir;
}

void SongActions::cancelRun() {
    if (converterRunner_->isRunning())
        emit statusCleared(); // "Tidying…"; cancel() reports nothing
    converterRunner_->cancel();
    runEditor_.clear();
}

void SongActions::shutdown() {
    converterRunner_->cancel();
    disconnect(converterRunner_, nullptr, this, nullptr);
    song_ = nullptr;
}

void SongActions::showStatus(const QString& text, int timeoutMs) {
    emit statusMessage(text, timeoutMs);
}

bool SongActions::approveBuild(SongEditor* editor, const std::string& content, bool askUser,
                               const QString& yesText, const QString& noText) {
    return hooks_.approveBuild(editor, content, askUser, yesText, noText);
}

void SongActions::updateEnabled(bool songTab) {
    if (!song_) {
        for (QAction* action : {tidyAction_, makeSmallerAction_, joinLinesAction_, checkAction_})
            action->setEnabled(false);
        return;
    }
    // The song actions work on the song's text.
    // The background re-measure does not block them: starting one cancels it.
    const bool busy = converterRunner_->isRunning() && runKind_ != RunKind::Measure;
    const bool converterFree =
        songTab && !song_->textEdit()->isReadOnly() && !busy && !hooks_.findConverter().empty();
    tidyAction_->setEnabled(converterFree);
    for (QAction* action : {makeSmallerAction_, joinLinesAction_})
        action->setEnabled(converterFree && !songbookRoot_.empty());
    checkAction_->setEnabled(converterFree); // check needs no songbook
}

void SongActions::startTidy(const QString& note) {
    SongEditor* editor = song_;
    cancelMeasure();
    if (!editor || editor->textEdit()->isReadOnly() || converterRunner_->isRunning())
        return;
    const std::filesystem::path converter = hooks_.findConverter();
    if (converter.empty()) {
        showStatus(
            tr("Tidy needs chords_converter, which was not found: choose it in Settings (Ctrl+,)."),
            8000);
        return;
    }
    if (!converterUsable(converter, tr("Tidy")))
        return;
    runKind_ = RunKind::Tidy;
    runEditor_ = editor;
    runInput_ = editor->content();
    tidyNote_ = note;
    // Before start(): a failure to start may be reported from inside it, and the result
    // handler then replaces this message.
    showStatus(tr("Tidying…"));
    if (!converterRunner_->start(
            [converter](const std::filesystem::path& file) {
                return zub::songtidy::prepare(converter, file);
            },
            runInput_, toQString(editor->path().stem())))
        return;
    emit stateChanged();
}

void SongActions::onConverterFinished(const std::expected<ConverterOutput, QString>& output) {
    switch (runKind_) {
    case RunKind::Tidy:
        onTidyFinished(output);
        break;
    case RunKind::Suggest:
        onSuggestFinished(output);
        break;
    case RunKind::Apply:
        onApplyFinished(output);
        break;
    case RunKind::Measure:
        onMeasureFinished(output);
        break;
    }
}

bool SongActions::converterUsable(const std::filesystem::path& converter, const QString& action) {
    const QString problem = converterProblem(converter);
    if (problem.isEmpty())
        return true;
    QMessageBox::warning(window_, action, problem);
    return false;
}

SongEditor* SongActions::takeRun(std::string& input, const QString& what) {
    SongEditor* editor = runEditor_.data();
    input = std::move(runInput_);
    runEditor_.clear();
    runInput_.clear();
    if (!editor || editor != song_ || editor->content() != input) {
        // The song was closed, switched or edited while the converter ran: its result no longer
        // fits the text.
        showStatus(tr("%1 discarded: the song changed while it ran.").arg(what), 8000);
        return nullptr;
    }
    return editor;
}

void SongActions::onTidyFinished(const std::expected<ConverterOutput, QString>& output) {
    std::string input;
    const QString note = std::move(tidyNote_);
    tidyNote_.clear();
    const auto finishUp = [this] { emit stateChanged(); };

    SongEditor* editor = takeRun(input, tr("Tidy"));
    if (!editor) {
        finishUp();
        return;
    }
    std::expected<zub::songtidy::Tidied, QString> result = std::unexpected(output.error_or({}));
    if (output) {
        auto interpreted = zub::songtidy::interpret(output->exitCode, output->out, output->err);
        if (interpreted)
            result = std::move(*interpreted);
        else
            result = std::unexpected(toQString(interpreted.error()));
    }
    if (!result) {
        emit statusCleared();
        QMessageBox::warning(window_, tr("Tidy failed"), result.error());
        finishUp();
        return;
    }

    const zub::songtidy::Tidied& tidied = *result;
    editor->replaceContent(tidied.content);
    // Against what the editor now holds, not the raw output: a CRLF song keeps CRLF even where
    // the converter wrote a lone LF.
    const std::string after = editor->content();
    const auto lines = zub::diff::lines(input, after);

    std::vector<std::pair<int, QString>> listed;
    for (const auto& change : tidied.changes) {
        const int line = zub::diff::mapOldLine(lines, change.line);
        listed.emplace_back(line, tr("line %1: %2").arg(line).arg(toQString(change.description)));
    }
    emit tidyChangesListed(listed);

    QString message;
    if (tidied.changes.empty())
        message = tr("Tidy: nothing to change");
    else
        message = tr("Tidy: %n change(s)", nullptr, static_cast<int>(tidied.changes.size()));
    // tidy moves chords to their syllable's start only for these languages (it hyphenates), and
    // skips that rule silently otherwise: say so, or "nothing to change" looks like "all fine".
    std::string lang;
    if (const auto meta = zub::parseSong(after)) {
        if (const auto it = meta->keys.find("lang"); it != meta->keys.end())
            lang = zub::mainLanguage(it->second); // lang={czech, hebrew}: the first counts
    }
    if (lang.empty())
        message += tr(" (chord positions not checked: set lang in the song header)");
    else if (lang != "czech" && lang != "slovak" && lang != "english")
        message += tr(" (chord positions are only checked for lang czech, slovak or english)");
    if (!note.isEmpty())
        message += QStringLiteral(" — ") + note;
    showStatus(message, 10000);
    finishUp();
    emit previewRequested();
}

QString SongActions::actionName(SongAction action) const {
    switch (action) {
    case SongAction::Shrink:
        return tr("Make Smaller");
    case SongAction::Check:
        return tr("Check Song");
    case SongAction::Join:
        return tr("Join Lines");
    }
    return {};
}

void SongActions::startProposals(SongAction action) {
    SongEditor* editor = song_;
    cancelMeasure();
    if (!editor || editor->textEdit()->isReadOnly() || converterRunner_->isRunning() ||
        songbookRoot_.empty())
        return;
    const QString name = actionName(action);
    const std::filesystem::path converter = hooks_.findConverter();
    if (converter.empty()) {
        showStatus(
            tr("%1 needs chords_converter, which was not found: choose it in Settings (Ctrl+,).")
                .arg(name),
            8000);
        return;
    }
    if (!converterUsable(converter, name))
        return;
    const std::string content = editor->content();
    const std::filesystem::path root = songbookRoot_;
    std::function<zub::converter::Command(const std::filesystem::path&)> command;
    QString status;
    measuredRun_ = false;
    switch (action) {
    case SongAction::Shrink: {
        // --measure builds the song with lualatex -shell-escape: the same rule as for a preview.
        // Declined, the proposals are still useful, just without page counts.
        // Without a package nothing can be typeset: the proposals come without page counts.
        const bool measure =
            !packageDir_.empty() &&
            approveBuild(editor, content, true, tr("Measure"), tr("Suggest Without Measuring"));
        measuredRun_ = measure;
        if (measure)
            emit
            previewRequested(); // approved now: the pane's "Not built automatically" message goes
        // No paper known (a class without a paper option): no --fit, the converter measures both.
        const auto paper = zub::preview::paperOf(packageDir_, hooks_.documentClass());
        command = [converter, paper, measure, root,
                   package = packageDir_](const std::filesystem::path& file) {
            return zub::songsuggest::shrinkCommand(converter, file, paper, measure, root, package);
        };
        status = tr("Looking for ways to make the song smaller…");
        break;
    }
    case SongAction::Check:
        // check builds nothing: no approval needed
        command = [converter](const std::filesystem::path& file) {
            return zub::songsuggest::checkCommand(converter, file);
        };
        status = tr("Checking the song…");
        break;
    case SongAction::Join:
        // join always typesets the song: without a package or the approval it can't run.
        if (packageDir_.empty()) {
            QMessageBox::information(window_, name, hooks_.noPackageText());
            return;
        }
        if (!approveBuild(editor, content, true, tr("Build"), tr("Cancel")))
            return;
        emit previewRequested();
        measuredRun_ = true;
        command = [converter, root, package = packageDir_](const std::filesystem::path& file) {
            return zub::songsuggest::joinCommand(converter, file, root, package);
        };
        status = tr("Checking which lines can be joined (typesetting the song, a few seconds)…");
        break;
    }

    runKind_ = RunKind::Suggest;
    songAction_ = action;
    runEditor_ = editor;
    runInput_ = content;
    showStatus(status);
    if (!converterRunner_->start(command, content, toQString(editor->path().stem())))
        return;
    emit stateChanged();
}

void SongActions::onSuggestFinished(const std::expected<ConverterOutput, QString>& output) {
    const SongAction action = songAction_;
    const QString name = actionName(action);
    std::string input;
    QPointer<SongEditor> editor = takeRun(input, name);
    if (!editor) {
        emit stateChanged();
        return;
    }
    std::expected<zub::songsuggest::Suggestions, QString> result =
        std::unexpected(output.error_or({}));
    if (output) {
        auto interpreted =
            zub::songsuggest::interpretProposals(output->exitCode, output->out, output->err);
        if (interpreted)
            result = std::move(*interpreted);
        else
            result = std::unexpected(toQString(interpreted.error()));
    }
    emit stateChanged();
    emit statusCleared();
    if (!result) {
        QMessageBox::warning(window_, tr("%1 failed").arg(name), result.error());
        return;
    }
    const zub::songsuggest::Suggestions& suggestions = *result;
    if (action != SongAction::Check)
        offerPapers(editor, input, suggestions);
    if (action == SongAction::Check) {
        // The song's key too (the package's checkkeys.py, as Check Songs runs it): its finding
        // goes to the bar above the song, and "No mistakes" is only said when there is none.
        const bool none = suggestions.proposals.empty();
        checkKey(editor, input,
                 [this, editor, input, none, name](std::optional<zub::songcheck::KeyFinding> key) {
                     if (!editor)
                         return;
                     if (key)
                         offerKey(editor);
                     if (none)
                         QMessageBox::information(
                             window_, name,
                             key ? keyFindingText(*key, input) + QStringLiteral("\n\n") +
                                       tr("Nothing else found. Write or keep the key in the bar "
                                          "above the song.")
                                 : tr("No mistakes found."));
                 });
        if (none)
            return;
    }
    if (suggestions.proposals.empty()) {
        const QString summary = ShrinkDialog::summary(suggestions);
        QString text;
        switch (action) {
        case SongAction::Shrink:
            text = tr("No more changes to suggest.");
            if (suggestions.fits == true) {
                const bool a5 = suggestions.a5.has_value();
                text = tr("The song fits on %1 already.").arg(a5 ? tr("A5") : tr("A4")) +
                       QStringLiteral(" ") + text;
            }
            break;
        case SongAction::Check:
            text = tr("No mistakes found.");
            break;
        case SongAction::Join:
            // The converter says why (0.11.0, listed in the summary); the rule in general
            // otherwise.
            text = suggestions.notJoined.empty()
                       ? tr("No lines can be joined: the parts of a kind need the same number of "
                            "lines, and the joined lines must still fit the column on A4 and A5.")
                       : tr("No lines can be joined.");
            break;
        }
        if (!summary.isEmpty() && action != SongAction::Check)
            text += QStringLiteral("\n\n") + summary;
        QMessageBox::information(window_, name, text);
        return;
    }
    QString heading;
    switch (action) {
    case SongAction::Shrink:
        heading = tr("Pick a change, most readable first (score 0–100: how readable the song "
                     "stays):");
        break;
    case SongAction::Check:
        heading = tr("Possible mistakes, in song order. A part with a chorus's words: only you can "
                     "tell whether it is the chorus sung again (the melody may differ); made a "
                     "chorus, Tidy runs after it to shorten the copies.");
        break;
    case SongAction::Join:
        heading = tr("Pick the parts whose lines to join in pairs (checked: the joined lines fit "
                     "the column on A4 and A5):");
        break;
    }
    ShrinkDialog dialog(suggestions, input, name, heading, window_, action == SongAction::Check);
    // Show where each proposal is (moving the cursor does not change the text).
    connect(&dialog, &ShrinkDialog::proposalSelected, this, &SongActions::lineRequested);
    if (const int line = suggestions.proposals.front().line; line > 0)
        emit lineRequested(line);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // The same text the suggestions were computed for (the dialog is modal, but the editor may
    // have been closed or changed by other means).
    if (!editor || editor != song_ || editor->content() != input) {
        showStatus(tr("%1 discarded: the song changed.").arg(name), 8000);
        return;
    }
    const auto chosen = dialog.selected();
    if (chosen.empty())
        return;
    if (dialog.dismissed()) {
        // Mark as Correct: the "% nocheck" lines go in locally, as one undo step.
        std::vector<zub::songsuggest::Edit> marks;
        for (const auto& proposal : chosen)
            if (proposal.dismiss)
                marks.push_back(*proposal.dismiss);
        const auto marked = zub::songoutline::applyEdits(input, marks);
        if (!marked) {
            QMessageBox::warning(window_, tr("%1 failed").arg(name), toQString(marked.error()));
            return;
        }
        editor->replaceContent(*marked); // one undo step; nothing is saved
        showStatus(
            chosen.size() == 1
                ? tr("Marked as correct: %1").arg(toQString(chosen.front().description))
                : tr("Marked as correct: %n finding(s)", nullptr, static_cast<int>(chosen.size())),
            10000);
        emit stateChanged();
        emit previewRequested();
        return;
    }
    std::vector<std::string> ids;
    for (const auto& proposal : chosen)
        ids.push_back(proposal.id);
    const QString description = chosen.size() == 1
                                    ? toQString(chosen.front().description)
                                    : tr("%n changes", nullptr, static_cast<int>(chosen.size()));
    runEditor_ = editor.data();
    runInput_ = input;
    songAction_ = action;
    applyRestructures_ = std::ranges::any_of(
        chosen, [](const auto& proposal) { return proposal.category == "restructure"; });
    measureAfterApply_ = measuredRun_ && action != SongAction::Check;
    startApply(std::move(ids), description);
}

void SongActions::offerPapers(SongEditor* editor, const std::string& measuredText,
                              const zub::songsuggest::Suggestions& suggestions) {
    if (!editor || editor != song_ || editor->textEdit()->isReadOnly() ||
        editor->kind() != SongEditor::Kind::Song || editor->content() != measuredText ||
        (!suggestions.a4 && !suggestions.a5))
        return;
    const std::string current = zub::songheader::option(measuredText, "pap").value_or("");
    const std::string wanted =
        zub::songheader::measuredPapers(current, suggestions.a4, suggestions.a5);
    if (wanted == current)
        return;
    const auto parts = [](const std::string& list) {
        std::vector<std::string> out;
        for (std::size_t start = 0; start <= list.size();) {
            const auto comma = list.find(',', start);
            const auto end = comma == std::string::npos ? list.size() : comma;
            if (end > start)
                out.push_back(list.substr(start, end - start));
            start = end + 1;
        }
        return out;
    };
    const auto before = parts(current);
    const auto after = parts(wanted);
    // The measured papers the song fits one page of / no longer fits (and was marked for).
    QStringList fitting;
    QStringList notFitting;
    bool added = false;
    const std::pair<const char*, const std::optional<zub::songsuggest::Measured>*> measured[] = {
        {"a4", &suggestions.a4}, {"a5", &suggestions.a5}};
    for (const auto& [paper, m] : measured) {
        if (!*m)
            continue;
        const bool had = std::ranges::find(before, paper) != before.end();
        const bool has = std::ranges::find(after, paper) != after.end();
        const QString label = QString::fromLatin1(paper).toUpper();
        if ((*m)->fits && (*m)->pages == 1)
            fitting << label;
        if (has && !had)
            added = true;
        if (had && !has)
            notFitting << label;
    }
    QString message;
    if (!fitting.isEmpty())
        message = tr("Measured: the song fits one page of %1.").arg(fitting.join(tr(" and ")));
    if (!notFitting.isEmpty()) {
        if (!message.isEmpty())
            message += QLatin1Char(' ');
        message += tr("Measured: the song no longer fits one page of %1.")
                       .arg(notFitting.join(tr(" and ")));
    }
    const auto shown = [this](const std::string& list) {
        return list.empty() ? tr("none") : toQString(list);
    };
    const QString detail = tr("pap: %1 \u2192 %2").arg(shown(current), shown(wanted));
    QPointer<SongEditor> guard = editor;
    editor->showSuggestion(
        message, detail, added ? tr("Mark") : tr("Update"), [this, guard, wanted] {
            if (!guard || guard != song_ || guard->textEdit()->isReadOnly())
                return;
            emit songTextReplaceRequested(zub::songheader::setPap(guard->content(), wanted));
            showStatus(tr("Paper marks updated. Save to keep them."), 8000);
        });
}

void SongActions::checkKey(SongEditor* editor, const std::string& content,
                           std::function<void(std::optional<zub::songcheck::KeyFinding>)> done) {
    auto dir = std::make_shared<QTemporaryDir>();
    const std::filesystem::path file = toPath(dir->filePath(QStringLiteral("song.tex")));
    if (packageDir_.empty() || songbookRoot_.empty() || !dir->isValid() ||
        !zub::writeTextFileAtomic(file, content)) {
        done(std::nullopt);
        return;
    }
    const auto command = zub::songcheck::scriptCommand(packageDir_, "checkkeys.py", songbookRoot_,
                                                       {"--all", toStdString(toQString(file))});
    auto* process = new QProcess(this);
    const QPointer<SongEditor> guard = editor;
    const std::filesystem::path songPath = editor->path();
    auto finish = [this, process, dir, guard, songPath, content,
                   done = std::move(done)](bool ran) mutable {
        std::optional<zub::songcheck::KeyFinding> found;
        if (ran) {
            const auto findings =
                zub::songcheck::parseCheckKeys(process->readAllStandardOutput().toStdString());
            if (findings) {
                for (const auto& finding : *findings) {
                    const bool missing = finding.kind == "propose";
                    const bool differs =
                        finding.kind == "mismatch" && !zub::songcheck::keyConfirmed(content);
                    if (missing || differs)
                        found = finding;
                }
                // Fresh from this check: it replaces what Check Songs found.
                if (found)
                    keyFindings_[songPath] = *found;
                else
                    keyFindings_.erase(songPath);
            }
        }
        process->deleteLater();
        dir.reset();
        done(guard ? found : std::nullopt);
    };
    connect(process, &QProcess::finished, this, [finish]() mutable { finish(true); });
    connect(process, &QProcess::errorOccurred, this,
            [finish](QProcess::ProcessError error) mutable {
                if (error == QProcess::FailedToStart)
                    finish(false);
            });
    // It takes a fraction of a second; a hung one is stopped (finish then reports nothing).
    QTimer::singleShot(15000, process, [process] { process->kill(); });
    connect(qApp, &QCoreApplication::aboutToQuit, process, [process] { process->kill(); });
    QStringList arguments;
    for (const auto& argument : command.arguments)
        arguments << toQString(argument);
    process->start(toQString(command.program), arguments);
}

QString SongActions::keyFindingText(const zub::songcheck::KeyFinding& finding,
                                    const std::string& content) const {
    const auto meta = zub::parseSong(content);
    const bool german = meta && zub::score::germanNames(meta->value("lang"));
    const QString detected =
        toQString(zub::latex::toDisplayText(zub::songcheck::keyValue(finding.detected, german)));
    if (finding.kind == "propose")
        return tr("Key: none written; the chords say %1.").arg(detected);
    const auto stated = zub::songheader::option(content, "key");
    return tr("Key: written %1, the chords say %2.")
        .arg(toQString(zub::latex::toDisplayText(stated.value_or(""))), detected);
}

void SongActions::offerKey(SongEditor* editor) {
    const auto found = keyFindings_.find(editor->path());
    if (found == keyFindings_.end() || editor->textEdit()->isReadOnly())
        return;
    const zub::songcheck::KeyFinding finding = found->second;
    const std::string content = editor->content();
    const auto meta = zub::parseSong(content);
    const bool german = meta && zub::score::germanNames(meta->value("lang"));
    // The per-part suggestion when checkkeys has one (already in the song's naming).
    const std::string detected = zub::songcheck::keyValue(finding.detected, german);
    const std::string wanted = finding.suggest.empty() ? detected : finding.suggest;
    if (wanted.empty())
        return;
    const QString shown = toQString(zub::latex::toDisplayText(wanted));
    const QString margin = QString::number(finding.margin, 'f', 1);
    const QPointer<SongEditor> guard = editor;
    const auto write = [this, guard, wanted] {
        if (!guard || guard != song_ || guard->textEdit()->isReadOnly())
            return;
        // setOption writes the value as given: the braces are ours, as in every song.
        emit songTextReplaceRequested(
            zub::songheader::setOption(guard->content(), "key", "{" + wanted + "}"));
        keyFindings_.erase(guard->path());
        showStatus(tr("Key written. Save to keep it."), 8000);
    };
    if (finding.kind == "propose") {
        if (zub::songheader::option(content, "key"))
            return; // written since the check
        editor->showSuggestion(
            tr("Key:"), tr("none written; the chords say %1 (margin %2).").arg(shown, margin),
            tr("Write %1").arg(shown), write);
        return;
    }
    if (zub::songcheck::keyConfirmed(content))
        return;
    const auto stated = zub::songheader::option(content, "key");
    editor->showSuggestion(
        tr("Key:"),
        tr("written %1, the chords say %2 (margin %3).")
            .arg(toQString(zub::latex::toDisplayText(stated.value_or(""))), shown, margin),
        tr("Write %1").arg(shown), write, tr("Keep the Written Key"), [this, guard] {
            if (!guard || guard != song_ || guard->textEdit()->isReadOnly())
                return;
            if (const auto confirmed = zub::songcheck::confirmKey(guard->content()))
                emit songTextReplaceRequested(*confirmed);
            keyFindings_.erase(guard->path());
            showStatus(tr("The written key is confirmed (% nocheck key). Save to keep it."), 8000);
        });
}

void SongActions::startApply(std::vector<std::string> ids, const QString& description) {
    const std::filesystem::path converter = hooks_.findConverter();
    if (converter.empty() || !runEditor_) {
        runEditor_.clear();
        runInput_.clear();
        return;
    }
    runKind_ = RunKind::Apply;
    applyDescription_ = description;
    showStatus(tr("Applying…"));
    if (!converterRunner_->start(
            [converter, ids = std::move(ids)](const std::filesystem::path& file) {
                return zub::songsuggest::applyCommand(converter, file, ids);
            },
            runInput_, toQString(runEditor_->path().stem())))
        return;
    emit stateChanged();
}

void SongActions::onApplyFinished(const std::expected<ConverterOutput, QString>& output) {
    std::string input;
    const QString description = std::move(applyDescription_);
    applyDescription_.clear();
    const SongAction action = songAction_;
    const bool restructured = std::exchange(applyRestructures_, false);
    const bool measureAgain = std::exchange(measureAfterApply_, false);
    SongEditor* editor = takeRun(input, actionName(action));
    if (!editor) {
        emit stateChanged();
        return;
    }
    std::expected<std::string, QString> result = std::unexpected(output.error_or({}));
    if (output) {
        auto interpreted =
            zub::songsuggest::interpretApply(output->exitCode, output->out, output->err);
        if (interpreted)
            result = std::move(*interpreted);
        else
            result = std::unexpected(toQString(interpreted.error()));
    }
    emit stateChanged();
    if (!result) {
        emit statusCleared();
        QMessageBox::warning(window_, tr("%1 failed").arg(actionName(action)), result.error());
        return;
    }
    editor->replaceContent(*result); // one undo step; nothing is saved
    showStatus(tr("Applied: %1").arg(description), 10000);
    emit stateChanged();
    emit previewRequested();
    // The saving comes after a restructuring: tidy makes the chorus's copies \insertchorus
    // (its own undo step).
    if (restructured && editor == song_)
        startTidy(tr(" (after making a part a chorus)"));
    else if (measureAgain && editor == song_)
        startMeasure(editor);
}

void SongActions::startMeasure(SongEditor* editor) {
    const std::filesystem::path converter = hooks_.findConverter();
    if (!editor || editor->textEdit()->isReadOnly() || converterRunner_->isRunning() ||
        songbookRoot_.empty() || packageDir_.empty() || converter.empty() ||
        !converterProblem(converter).isEmpty())
        return;
    const std::string content = editor->content();
    // Measuring builds the song: only when it is approved already, never a new question.
    if (!approveBuild(editor, content, false))
        return;
    const auto paper = zub::preview::paperOf(packageDir_, hooks_.documentClass());
    const std::filesystem::path root = songbookRoot_;
    const std::filesystem::path package = packageDir_;
    runKind_ = RunKind::Measure;
    runEditor_ = editor;
    runInput_ = content;
    showStatus(tr("Measuring…"));
    if (!converterRunner_->start(
            [converter, paper, root, package](const std::filesystem::path& file) {
                return zub::songsuggest::shrinkCommand(converter, file, paper, true, root, package);
            },
            content, toQString(editor->path().stem())))
        return;
    emit stateChanged();
}

void SongActions::onMeasureFinished(const std::expected<ConverterOutput, QString>& output) {
    SongEditor* editor = runEditor_.data();
    const std::string input = std::move(runInput_);
    runEditor_.clear();
    runInput_.clear();
    emit stateChanged();
    emit statusCleared();
    // Quiet: a failure or a changed text just means no offer.
    if (!editor || editor != song_ || editor->content() != input || !output)
        return;
    if (auto interpreted =
            zub::songsuggest::interpretProposals(output->exitCode, output->out, output->err))
        offerPapers(editor, input, *interpreted);
}

void SongActions::cancelMeasure() {
    if (runKind_ != RunKind::Measure || !converterRunner_->isRunning())
        return;
    converterRunner_->cancel(); // reports nothing
    runEditor_.clear();
    runInput_.clear();
    emit statusCleared();
    emit stateChanged();
}
