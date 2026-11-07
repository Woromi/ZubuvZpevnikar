#include "SongEditor.hpp"

#include "convert.hpp"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#include <QAbstractButton>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUtf8StringView>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <zub/core/textfile.hpp>

SongEditor::SongEditor(std::filesystem::path path, KSyntaxHighlighting::Repository& repository,
                       QWidget* parent)
    : QWidget(parent), path_(std::move(path)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    banner_ = new QLabel(this);
    banner_->setWordWrap(true);
    banner_->setTextFormat(Qt::PlainText);
    banner_->setStyleSheet(
        QStringLiteral("background-color: #fff3cd; color: #664d03; padding: 6px;"));
    banner_->hide();
    layout->addWidget(banner_);

    edit_ = new QPlainTextEdit(this);
    layout->addWidget(edit_);

    const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    edit_->setFont(font);
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4);

    highlighter_ = new KSyntaxHighlighting::SyntaxHighlighter(edit_->document());
    // Bundled as a resource (app/syntax/); plain LaTeX only if it is somehow missing.
    auto definition = repository.definitionForName(QStringLiteral("ZubuvZpevnik Song"));
    if (!definition.isValid())
        definition = repository.definitionForName(QStringLiteral("LaTeX"));
    highlighter_->setDefinition(definition);
    highlighter_->setTheme(repository.themeForPalette(palette()));

    watcher_ = new QFileSystemWatcher(this);
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this] { onFileChanged(); });
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this] { onDirectoryChanged(); });
    connect(edit_->document(), &QTextDocument::modificationChanged, this,
            &SongEditor::modificationChanged);
}

SongEditor::~SongEditor() = default;

bool SongEditor::isModified() const {
    return edit_->document()->isModified();
}

std::expected<void, QString> SongEditor::load() {
    auto bytes = zub::readTextFile(path_);
    if (!bytes) {
        return std::unexpected(toQString(bytes.error()));
    }
    setContent(*bytes);
    diskContent_ = *bytes;
    edit_->document()->setModified(false);
    watch();
    return {};
}

std::expected<SongEditor::SaveResult, QString> SongEditor::save() {
    if (lossy_) {
        return std::unexpected(
            tr("%1 is open read-only because it can't be edited safely here (%2).")
                .arg(toQString(path_.filename()), lossyReason_));
    }

    if (auto onDisk = zub::readTextFile(path_); onDisk && *onDisk != diskContent_) {
        QMessageBox box(QMessageBox::Warning, tr("Song changed on disk"),
                        tr("%1 was changed outside the app since you opened it.")
                            .arg(toQString(path_.filename())),
                        {}, this);
        box.setInformativeText(tr("Overwrite it with your version?"));
        QPushButton* overwrite = box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
        QPushButton* cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        box.setDefaultButton(cancel);
        const bool wasAsking = std::exchange(askingAboutReload_, true);
        box.exec();
        askingAboutReload_ = wasAsking;
        if (box.clickedButton() != overwrite) {
            return SaveResult::Cancelled;
        }
    }

    const std::string bytes = currentContent();
    auto written = zub::writeTextFileAtomic(path_, bytes);
    if (!written) {
        return std::unexpected(toQString(written.error()));
    }
    diskContent_ = bytes;
    if (missing_) {
        missing_ = false;
        updateBanner();
        if (!watcher_->directories().isEmpty()) {
            watcher_->removePaths(watcher_->directories());
        }
    }
    edit_->document()->setModified(false);
    watch();
    emit saved(bytes);
    return SaveResult::Saved;
}

void SongEditor::stopWatching() {
    stopped_ = true;
    watcher_->blockSignals(true);
    if (!watcher_->files().isEmpty()) {
        watcher_->removePaths(watcher_->files());
    }
    if (!watcher_->directories().isEmpty()) {
        watcher_->removePaths(watcher_->directories());
    }
}

void SongEditor::checkLossless(const std::string& bytes) {
    lossy_ = currentContent() != bytes;
    if (lossy_) {
        if (bytes.starts_with("\xEF\xBB\xBF")) {
            lossyReason_ = tr("it starts with a byte-order mark");
        } else if (!QUtf8StringView(bytes.data(), static_cast<qsizetype>(bytes.size()))
                        .isValidUtf8()) {
            lossyReason_ = tr("it is not valid UTF-8");
        } else if (bytes.find('\r') != std::string::npos) {
            lossyReason_ = tr("it has mixed or unusual line endings");
        } else {
            lossyReason_ = tr("it contains characters the editor cannot keep (e.g. Unicode "
                              "line separators)");
        }
    } else {
        lossyReason_.clear();
    }
    edit_->setReadOnly(lossy_);
    updateBanner();
}

void SongEditor::updateBanner() {
    const QString name = toQString(path_.filename());
    if (lossy_) {
        banner_->setText(tr("%1 can't be edited safely here: %2. It is open read-only; saving "
                            "would change parts of the file you did not edit.")
                             .arg(name, lossyReason_));
        banner_->show();
    } else if (missing_) {
        banner_->setText(
            tr("%1 was deleted or moved on disk. Saving will create it again.").arg(name));
        banner_->show();
    } else {
        banner_->hide();
    }
}

void SongEditor::setContent(const std::string& bytes) {
    // CRLF only if every line ends that way; mixed files are caught by checkLossless().
    const auto crlfCount = std::ranges::count(bytes, '\n');
    std::size_t crlfPairs = 0;
    for (std::size_t pos = bytes.find("\r\n"); pos != std::string::npos;
         pos = bytes.find("\r\n", pos + 2)) {
        ++crlfPairs;
    }
    crlf_ = crlfPairs > 0 && static_cast<std::ptrdiff_t>(crlfPairs) == crlfCount;
    QString text = toQString(bytes);
    if (crlf_) {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }

    // A reload keeps the cursor and the scroll position (clamped to the new text).
    const bool reload = !edit_->document()->isEmpty();
    const int cursorPos = edit_->textCursor().position();
    const int vScroll = edit_->verticalScrollBar()->value();
    const int hScroll = edit_->horizontalScrollBar()->value();

    edit_->setPlainText(text);

    if (reload) {
        QTextCursor cursor(edit_->document());
        cursor.setPosition(
            std::min(cursorPos, std::max(0, edit_->document()->characterCount() - 1)));
        edit_->setTextCursor(cursor);
        edit_->verticalScrollBar()->setValue(
            std::min(vScroll, edit_->verticalScrollBar()->maximum()));
        edit_->horizontalScrollBar()->setValue(
            std::min(hScroll, edit_->horizontalScrollBar()->maximum()));
    }
    edit_->document()->setModified(false);
    checkLossless(bytes);
}

std::string SongEditor::currentContent() const {
    // toRawText, not toPlainText: the latter turns U+00A0 into a plain space.
    QString text = edit_->document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    if (crlf_) {
        text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    }
    return toStdString(text);
}

void SongEditor::watch() {
    if (stopped_) {
        return;
    }
    const QString file = toQString(path_);
    if (!watcher_->files().contains(file)) {
        watcher_->addPath(file);
    }
}

void SongEditor::onDirectoryChanged() {
    std::error_code ec;
    if (stopped_ || !std::filesystem::exists(path_, ec)) {
        return;
    }
    if (!watcher_->directories().isEmpty()) {
        watcher_->removePaths(watcher_->directories());
    }
    onFileChanged();
}

void SongEditor::onFileChanged() {
    if (stopped_) {
        return;
    }
    // The path may have been replaced (new inode), which drops the watch.
    watch();

    auto bytes = zub::readTextFile(path_);
    if (!bytes) {
        // Deleted, or another editor is in the middle of a rename: try once more shortly.
        QTimer::singleShot(300, this, [this] {
            if (stopped_) {
                return;
            }
            if (zub::readTextFile(path_)) {
                onFileChanged();
                return;
            }
            if (!missing_) {
                missing_ = true;
                modifiedBeforeMissing_ = isModified();
                edit_->document()->setModified(true); // closing must ask
                updateBanner();
            }
            // Wait for the file to come back.
            std::filesystem::path dir = path_.parent_path();
            if (dir.empty()) {
                dir = ".";
            }
            const QString dirString = toQString(dir);
            if (!watcher_->directories().contains(dirString)) {
                watcher_->addPath(dirString);
            }
        });
        return;
    }
    if (missing_) {
        missing_ = false;
        edit_->document()->setModified(modifiedBeforeMissing_);
        updateBanner();
        if (!watcher_->directories().isEmpty()) {
            watcher_->removePaths(watcher_->directories());
        }
    }
    if (*bytes == diskContent_) {
        return; // our own save, or a touch without change
    }

    if (!isModified()) {
        setContent(*bytes);
        diskContent_ = *bytes;
        emit reloadedFromDisk();
        return;
    }

    if (askingAboutReload_) {
        return;
    }
    askingAboutReload_ = true;
    QPointer<SongEditor> self(this);
    // On the heap: if this editor is destroyed while the dialog runs, its parent-owned
    // child must not be destroyed a second time.
    QPointer<QMessageBox> box = new QMessageBox(
        QMessageBox::Warning, tr("Song changed on disk"),
        tr("%1 was changed outside the app.").arg(toQString(path_.filename())), {}, this);
    box->setInformativeText(tr("Reload it and lose your unsaved changes, or keep your version?"));
    QPushButton* reloadButton = box->addButton(tr("Reload"), QMessageBox::DestructiveRole);
    QPushButton* keepButton = box->addButton(tr("Keep Mine"), QMessageBox::RejectRole);
    box->setDefaultButton(keepButton);
    box->exec();
    if (!self) {
        return;
    }
    const bool reload = box && box->clickedButton() == reloadButton;
    if (box) {
        box->deleteLater();
    }
    if (reload) {
        // The file may have changed again while the dialog was open: load the newest version.
        if (auto latest = zub::readTextFile(path_))
            bytes = std::move(latest);
        setContent(*bytes);
        diskContent_ = *bytes;
        emit reloadedFromDisk();
    } else {
        diskContent_ = *bytes; // the same change must not ask again
    }
    askingAboutReload_ = false;

    // A change made while the dialog was open must not be lost.
    auto again = zub::readTextFile(path_);
    if (!again || *again != diskContent_) {
        QTimer::singleShot(0, this, [this] { onFileChanged(); });
    }
}
