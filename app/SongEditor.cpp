#include "SongEditor.hpp"

#include "CodeTextEdit.hpp"
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
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUtf8StringView>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <zub/core/fretboard.hpp>
#include <zub/core/scoreregion.hpp>
#include <zub/core/textfile.hpp>

SongEditor::SongEditor(std::filesystem::path path, KSyntaxHighlighting::Repository& repository,
                       QWidget* parent, Kind kind)
    : QWidget(parent), path_(std::move(path)), kind_(kind) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    banner_ = new QLabel(this);
    banner_->setWordWrap(true);
    banner_->setTextFormat(Qt::PlainText);
    banner_->setStyleSheet(
        QStringLiteral("background-color: #fff3cd; color: #664d03; padding: 6px;"));
    banner_->hide();
    layout->addWidget(banner_);

    auto* codeEdit = new CodeTextEdit(this);
    edit_ = codeEdit;
    layout->addWidget(edit_);

    const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    edit_->setFont(font);
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4);

    highlighter_ = new KSyntaxHighlighting::SyntaxHighlighter(edit_->document());
    KSyntaxHighlighting::Definition definition;
    if (kind_ == Kind::Score) {
        // KSyntaxHighlighting ships a LilyPond definition; none if it is somehow missing.
        definition = repository.definitionForName(QStringLiteral("LilyPond"));
        if (!definition.isValid())
            definition = repository.definitionForFileName(QStringLiteral("x.ly"));
    } else {
        // Bundled as a resource (app/syntax/); plain LaTeX only if it is somehow missing.
        definition = repository.definitionForName(QStringLiteral("ZubuvZpevnik Song"));
        if (!definition.isValid())
            definition = repository.definitionForName(QStringLiteral("LaTeX"));
    }
    highlighter_->setDefinition(definition);
    const auto theme = repository.themeForPalette(palette());
    highlighter_->setTheme(theme);
    using Theme = KSyntaxHighlighting::Theme;
    codeEdit->setLineNumberColors(QColor::fromRgba(theme.editorColor(Theme::IconBorder)),
                                  QColor::fromRgba(theme.editorColor(Theme::LineNumbers)),
                                  QColor::fromRgba(theme.editorColor(Theme::CurrentLineNumber)));

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

void SongEditor::openNew(const std::string& content) {
    setContent(content); // also refuses content that would not round-trip
    diskContent_.clear();
    isNew_ = true;
    newContent_ = currentContent(); // as the editor holds it, so an untouched draft diffs empty
    edit_->document()->setModified(true);
}

std::expected<SongEditor::SaveResult, QString> SongEditor::save() {
    if (lossy_) {
        return std::unexpected(
            tr("%1 is open read-only because it can't be edited safely here (%2).")
                .arg(toQString(path_.filename()), lossyReason_));
    }

    std::error_code existsError;
    const auto onDisk = zub::readTextFile(path_);
    // A new song asks whenever something is at its path, even an empty or unreadable file.
    const bool conflict = isNew_ ? std::filesystem::exists(path_, existsError) || bool(existsError)
                                 : onDisk && *onDisk != diskContent_;
    if (conflict) {
        const QString name = toQString(path_.filename());
        QMessageBox box(QMessageBox::Warning,
                        kind_ == Kind::Score
                            ? (isNew_ ? tr("Score already exists") : tr("Score changed on disk"))
                            : (isNew_ ? tr("Song already exists") : tr("Song changed on disk")),
                        isNew_
                            ? tr("%1 already exists.").arg(name)
                            : tr("%1 was changed outside the app since you opened it.").arg(name),
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

    if (isNew_) {
        // A new artist gets its folder with the first song.
        std::error_code ec;
        std::filesystem::create_directories(path_.parent_path(), ec);
        if (ec)
            return std::unexpected(toQString(path_.parent_path()) + QStringLiteral(": ") +
                                   QString::fromStdString(ec.message()));
    }
    const std::string bytes = currentContent();
    auto written = zub::writeTextFileAtomic(path_, bytes);
    if (!written) {
        return std::unexpected(toQString(written.error()));
    }
    diskContent_ = bytes;
    isNew_ = false;
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

namespace {

// CRLF only if every line ends that way; mixed files are caught by checkLossless().
bool usesCrlf(const std::string& bytes) {
    const auto lfCount = std::ranges::count(bytes, '\n');
    std::size_t crlfPairs = 0;
    for (std::size_t pos = bytes.find("\r\n"); pos != std::string::npos;
         pos = bytes.find("\r\n", pos + 2)) {
        ++crlfPairs;
    }
    return crlfPairs > 0 && static_cast<std::ptrdiff_t>(crlfPairs) == lfCount;
}

// File bytes -> editor text (LF line ends).
QString toEditorText(const std::string& bytes, bool crlf) {
    QString text = toQString(bytes);
    if (crlf) {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }
    return text;
}

} // namespace

bool SongEditor::roundTrips(const std::string& bytes) {
    const bool crlf = usesCrlf(bytes);
    QTextDocument document;
    document.setPlainText(toEditorText(bytes, crlf));
    QString text = document.toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    if (crlf)
        text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    return toStdString(text) == bytes;
}

void SongEditor::replaceContent(const std::string& bytes) {
    if (lossy_) {
        return;
    }
    replaceDocumentText(toEditorText(bytes, crlf_));
}

void SongEditor::replaceDocumentText(const QString& text) {
    QTextDocument* document = edit_->document();
    // The raw text has one U+2029 per line break, one character each like the document positions.
    const QString oldText = document->toRawText();
    QString newText = text;
    newText.replace(QLatin1Char('\n'), QChar::ParagraphSeparator);
    if (oldText == newText) {
        return;
    }
    qsizetype prefix = 0;
    const qsizetype limit = std::min(oldText.size(), newText.size());
    while (prefix < limit && oldText[prefix] == newText[prefix]) {
        ++prefix;
    }
    // Do not split a surrogate pair.
    if (prefix > 0 && oldText[prefix - 1].isHighSurrogate()) {
        --prefix;
    }
    qsizetype suffix = 0;
    while (suffix < limit - prefix &&
           oldText[oldText.size() - 1 - suffix] == newText[newText.size() - 1 - suffix]) {
        ++suffix;
    }
    if (suffix > 0 && newText[newText.size() - suffix].isLowSurrogate()) {
        --suffix;
    }
    QString middle = newText.mid(prefix, newText.size() - prefix - suffix);
    middle.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.setPosition(static_cast<int>(prefix));
    cursor.setPosition(static_cast<int>(oldText.size() - suffix), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    if (!middle.isEmpty()) {
        cursor.insertText(middle);
    }
    cursor.endEditBlock();
}

void SongEditor::setContent(const std::string& bytes) {
    crlf_ = usesCrlf(bytes);
    const QString text = toEditorText(bytes, crlf_);

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
        QMessageBox::Warning,
        kind_ == Kind::Score ? tr("Score changed on disk") : tr("Song changed on disk"),
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

void SongEditor::goToLine(int line) {
    // A block is one line of the file (no wrapping), so block number = line - 1.
    const QTextBlock block = edit_->document()->findBlockByNumber(std::max(0, line - 1));
    if (!block.isValid())
        return;
    edit_->setTextCursor(QTextCursor(block));
    edit_->centerCursor();
    edit_->setFocus();
}

void SongEditor::goToLineColumn(int line, int column) {
    const QTextBlock block = edit_->document()->findBlockByNumber(std::max(0, line - 1));
    if (!block.isValid())
        return;
    const QString text = block.text();
    int position = 0;
    for (int i = 0; i < column && position < text.size(); ++i)
        position += text.at(position).isHighSurrogate() && position + 1 < text.size() ? 2 : 1;
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + position);
    edit_->setTextCursor(cursor);
    edit_->centerCursor();
    edit_->setFocus();
}

void SongEditor::setCursorByteOffset(std::size_t byteOffset) {
    QString text = edit_->document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    const QByteArray utf8 = text.toUtf8();
    const auto offset = std::min(static_cast<qsizetype>(byteOffset), utf8.size());
    QTextCursor cursor(edit_->document());
    cursor.setPosition(static_cast<int>(QString::fromUtf8(utf8.constData(), offset).size()));
    edit_->setTextCursor(cursor);
    edit_->centerCursor();
    edit_->setFocus();
}

namespace {

// A block's text as UTF-8 and conversions between a position in the block (UTF-16 units) and a
// byte offset in that UTF-8. Positions must not split a surrogate pair (a cursor never does).
std::size_t byteOffset(const QString& text, int utf16Pos) {
    return static_cast<std::size_t>(text.left(utf16Pos).toUtf8().size());
}

// A position in a whole document's raw text -> byte offset in its UTF-8 (U+2029 is one byte, as
// the line feed it stands for).
std::size_t utf16ToByteOffset(const QString& raw, int utf16Pos) {
    QString head = raw.left(utf16Pos);
    head.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return static_cast<std::size_t>(head.toUtf8().size());
}

int utf16Position(const std::string& utf8, std::size_t byteOffset) {
    return static_cast<int>(
        QString::fromUtf8(utf8.data(), static_cast<qsizetype>(byteOffset)).size());
}

// Replaces the text of `block` (not its line break) and puts the cursor at `utf16Pos` in it.
void replaceBlockText(QPlainTextEdit* edit, const QTextBlock& block, const QString& text,
                      int utf16Pos) {
    const int number = block.blockNumber();
    QTextCursor cursor(block);
    cursor.beginEditBlock();
    if (block.text() != text) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.insertText(text);
    }
    cursor.endEditBlock();
    const QTextBlock after = edit->document()->findBlockByNumber(number);
    QTextCursor placed(after);
    placed.setPosition(after.position() + utf16Pos);
    edit->setTextCursor(placed);
}

} // namespace

bool SongEditor::moveLines(int direction) {
    if (edit_->isReadOnly())
        return false;
    const QTextCursor cursor = edit_->textCursor();
    QTextDocument* document = edit_->document();
    const QTextBlock first = document->findBlock(cursor.selectionStart());
    QTextBlock last = document->findBlock(cursor.selectionEnd());
    // A selection that ends at the start of a line does not take that line along.
    if (cursor.hasSelection() && last != first && cursor.selectionEnd() == last.position())
        last = last.previous();
    const QTextBlock neighbour = direction < 0 ? first.previous() : last.next();
    if (!neighbour.isValid())
        return false;

    QStringList moved;
    for (QTextBlock block = first;; block = block.next()) {
        moved << block.text();
        if (block == last)
            break;
    }
    const QString other = neighbour.text();
    const QTextBlock top = direction < 0 ? neighbour : first;
    const QTextBlock bottom = direction < 0 ? last : neighbour;
    const int topPosition = top.position();
    const int bottomEnd = bottom.position() + bottom.length() - 1; // before its line break
    const int anchorOffset = cursor.anchor() - first.position();
    const int positionOffset = cursor.position() - first.position();

    // One undo step: the lines and their neighbour are written again in the new order.
    QTextCursor writer(document);
    writer.beginEditBlock();
    writer.setPosition(topPosition);
    writer.setPosition(bottomEnd, QTextCursor::KeepAnchor);
    const QString lines = moved.join(QLatin1Char('\n'));
    writer.insertText(direction < 0 ? lines + QLatin1Char('\n') + other
                                    : other + QLatin1Char('\n') + lines);
    writer.endEditBlock();

    const int movedStart = direction < 0 ? topPosition : topPosition + int(other.size()) + 1;
    QTextCursor result(document);
    result.setPosition(movedStart + anchorOffset);
    result.setPosition(movedStart + positionOffset, QTextCursor::KeepAnchor);
    edit_->setTextCursor(result);
    return true;
}

namespace {

// The cursor's line and the cursor and selection in it (byte offsets); a selection over several
// lines is ignored (selStart == selEnd == cursor).
struct LineState {
    QTextBlock block;
    QString text;
    std::string line;
    std::size_t cursor;
    std::size_t selStart;
    std::size_t selEnd;
};

LineState lineState(const QTextCursor& cursor) {
    LineState state;
    state.block = cursor.block();
    state.text = state.block.text();
    state.line = state.text.toUtf8().toStdString();
    const int start = state.block.position();
    int selStart = cursor.position() - start;
    int selEnd = selStart;
    if (cursor.hasSelection() && cursor.selectionStart() >= start &&
        cursor.selectionEnd() <= start + state.text.size()) {
        selStart = cursor.selectionStart() - start;
        selEnd = cursor.selectionEnd() - start;
    }
    state.cursor = byteOffset(state.text, cursor.position() - start);
    state.selStart = byteOffset(state.text, selStart);
    state.selEnd = byteOffset(state.text, selEnd);
    return state;
}

void applyChange(QPlainTextEdit* edit, const LineState& state,
                 const zub::songedit::Change& change) {
    const QString updated =
        QString::fromUtf8(change.text.data(), static_cast<qsizetype>(change.text.size()));
    replaceBlockText(edit, state.block, updated, utf16Position(change.text, change.cursor));
}

} // namespace

std::string SongEditor::editorText() const {
    // Raw text: toPlainText() would turn non-breaking spaces into plain ones (other byte count).
    QString text = edit_->document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text.toUtf8().toStdString();
}

std::size_t SongEditor::cursorByteOffset() const {
    return utf16ToByteOffset(edit_->document()->toRawText(), edit_->textCursor().position());
}

// The duration in force where an inserted token lands: a cursor inside a note goes behind it.
std::string SongEditor::effectiveDurationAtCursor() const {
    const auto state = lineState(edit_->textCursor());
    const std::size_t lineStart = cursorByteOffset() - state.cursor;
    return zub::fretboard::effectiveDuration(
        editorText(), lineStart + zub::fretboard::insertionPoint(state.line, state.cursor));
}

namespace {

// `duration` unless it is the one in force (then nothing needs to be written).
std::string durationToWrite(const std::string& duration, const std::string& effective) {
    return duration == effective ? std::string() : duration;
}

} // namespace

// The note or rest that follows the inserted token on a LATER line has no duration of its own
// either when it carried the old one; the line edit cannot see it. Part of the same undo step.
void SongEditor::keepCarriedDuration(const std::string& written, const std::string& effective) {
    if (written.empty() || written == effective)
        return;
    const std::string text = editorText();
    const auto fix = zub::fretboard::carriedDurationFix(text, cursorByteOffset(), effective);
    if (!fix)
        return;
    QTextCursor cursor(edit_->document());
    cursor.setPosition(utf16Position(text, fix->offset));
    cursor.insertText(toQString(fix->text));
}

bool SongEditor::cursorInScoreComment() const {
    const auto state = lineState(edit_->textCursor());
    return zub::fretboard::inCommentOrString(state.line, state.cursor);
}

void SongEditor::insertScoreNote(const std::string& token, const std::string& duration) {
    if (edit_->isReadOnly() || kind_ != Kind::Score)
        return;
    const auto state = lineState(edit_->textCursor());
    const std::string effective = effectiveDurationAtCursor();
    const std::string written = durationToWrite(duration, effective);
    QTextCursor outer(edit_->document());
    outer.beginEditBlock();
    applyChange(edit_, state,
                zub::fretboard::insertNote(state.line, state.cursor, token, written, effective));
    keepCarriedDuration(written, effective);
    outer.endEditBlock();
}

void SongEditor::addScoreNoteToChord(const std::string& token) {
    if (edit_->isReadOnly() || kind_ != Kind::Score)
        return;
    const auto state = lineState(edit_->textCursor());
    applyChange(edit_, state, zub::fretboard::addToChord(state.line, state.cursor, token));
}

void SongEditor::insertScoreRest(const std::string& duration) {
    if (edit_->isReadOnly() || kind_ != Kind::Score)
        return;
    const auto state = lineState(edit_->textCursor());
    const std::string effective = effectiveDurationAtCursor();
    const std::string written = durationToWrite(duration, effective);
    QTextCursor outer(edit_->document());
    outer.beginEditBlock();
    applyChange(edit_, state,
                zub::fretboard::insertRest(state.line, state.cursor, written, effective));
    keepCarriedDuration(written, effective);
    outer.endEditBlock();
}

void SongEditor::insertScoreBarCheck() {
    if (edit_->isReadOnly() || kind_ != Kind::Score)
        return;
    const auto state = lineState(edit_->textCursor());
    applyChange(edit_, state, zub::fretboard::insertBarCheck(state.line, state.cursor));
}

void SongEditor::insertChord() {
    if (edit_->isReadOnly() || kind_ != Kind::Song)
        return;
    const auto state = lineState(edit_->textCursor());
    applyChange(edit_, state,
                zub::songedit::insertChord(state.line, state.cursor, state.selStart,
                                           state.selEnd));
}

void SongEditor::insertMultivoice() {
    if (edit_->isReadOnly() || kind_ != Kind::Song)
        return;
    const auto state = lineState(edit_->textCursor());
    applyChange(edit_, state,
                zub::songedit::insertMultivoice(state.line, state.cursor, state.selStart,
                                                state.selEnd));
}

void SongEditor::insertRole() {
    if (edit_->isReadOnly() || kind_ != Kind::Song)
        return;
    const auto state = lineState(edit_->textCursor());
    applyChange(edit_, state,
                zub::songedit::insertRole(state.line, state.cursor, state.selStart,
                                          state.selEnd));
}

bool SongEditor::removeElement() {
    if (edit_->isReadOnly() || kind_ != Kind::Song)
        return false;
    const auto state = lineState(edit_->textCursor());
    const auto change = zub::songedit::removeElementAt(state.line, state.cursor);
    if (!change)
        return false;
    applyChange(edit_, state, *change);
    return true;
}

bool SongEditor::moveChord(int direction, zub::songedit::Step step) {
    if (edit_->isReadOnly() || kind_ != Kind::Song)
        return false;
    const QTextCursor cursor = edit_->textCursor();
    const QTextBlock block = cursor.block();
    const QString text = block.text();
    const std::string line = text.toUtf8().toStdString();
    const auto change = zub::songedit::moveChord(
        line, byteOffset(text, cursor.position() - block.position()), direction, step);
    if (!change)
        return false;
    const QString updated =
        QString::fromUtf8(change->text.data(), static_cast<qsizetype>(change->text.size()));
    replaceBlockText(edit_, block, updated, utf16Position(change->text, change->cursor));
    return true;
}

void SongEditor::insertPart(zub::songedit::Part part) {
    if (edit_->isReadOnly() || kind_ != Kind::Song)
        return;
    QTextDocument* document = edit_->document();
    const QTextCursor cursor = edit_->textCursor();
    QTextBlock first = cursor.block();
    QTextBlock last = first;
    const bool selection = cursor.hasSelection();
    if (selection) {
        first = document->findBlock(cursor.selectionStart());
        last = document->findBlock(cursor.selectionEnd());
        // A selection that ends at the start of a line does not include that line.
        if (last != first && cursor.selectionEnd() == last.position())
            last = last.previous();
    }

    QString selected;
    if (selection) {
        for (QTextBlock block = first;; block = block.next()) {
            selected += block.text();
            if (block == last)
                break;
            selected += QLatin1Char('\n');
        }
    }
    const auto snippet = zub::songedit::partSnippet(part, selected.toUtf8().toStdString());
    // The snippet ends with a line break, which the text after the insertion point already has.
    std::string body = snippet.text;
    body.pop_back();

    QTextCursor edit(document);
    edit.beginEditBlock();
    int cursorPos = 0;
    if (selection) {
        edit.setPosition(first.position());
        edit.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        edit.insertText(QString::fromUtf8(body.data(), static_cast<qsizetype>(body.size())));
        cursorPos = first.position() + utf16Position(snippet.text, snippet.cursor);
    } else if (first.text().trimmed().isEmpty()) {
        edit.setPosition(first.position());
        edit.insertText(
            QString::fromUtf8(snippet.text.data(), static_cast<qsizetype>(snippet.text.size())));
        cursorPos = first.position() + utf16Position(snippet.text, snippet.cursor);
    } else {
        const int end = first.position() + first.length() - 1;
        edit.setPosition(end);
        edit.insertText(QLatin1Char('\n') +
                        QString::fromUtf8(body.data(), static_cast<qsizetype>(body.size())));
        cursorPos = end + 1 + utf16Position(snippet.text, snippet.cursor);
    }
    edit.endEditBlock();
    QTextCursor placed(document);
    placed.setPosition(std::min(cursorPos, document->characterCount() - 1));
    edit_->setTextCursor(placed);
    edit_->centerCursor();
    edit_->setFocus();
}

bool SongEditor::hasSongHeader() const {
    return !edit_->document()->find(QStringLiteral("\\beginsong")).isNull();
}

std::optional<std::pair<std::size_t, std::size_t>> SongEditor::selectedByteRange() const {
    const QTextCursor cursor = edit_->textCursor();
    if (!cursor.hasSelection())
        return std::nullopt;
    const QString raw = edit_->document()->toRawText();
    return std::pair{utf16ToByteOffset(raw, cursor.selectionStart()),
                     utf16ToByteOffset(raw, cursor.selectionEnd())};
}

void SongEditor::selectByteRange(std::size_t begin, std::size_t end) {
    const std::string text = editorText();
    begin = std::min(begin, text.size());
    end = std::min(end, text.size());
    QTextCursor cursor(edit_->document());
    cursor.setPosition(utf16Position(text, begin));
    cursor.setPosition(utf16Position(text, end), QTextCursor::KeepAnchor);
    edit_->setTextCursor(cursor);
    edit_->ensureCursorVisible();
    edit_->setFocus();
}

bool SongEditor::applyRegionAction(zub::scoreregion::Action action,
                                   zub::scoreregion::Refusal* why) {
    if (edit_->isReadOnly() || kind_ != Kind::Score)
        return false;
    const auto range = selectedByteRange();
    if (!range)
        return false;
    const std::string text = editorText();
    const auto result = zub::scoreregion::apply(action, text, {range->first, range->second}, why);
    if (!result)
        return false;
    const bool changed = result->text != text;
    if (changed)
        replaceDocumentText(toQString(result->text));
    selectByteRange(result->selection.begin, result->selection.end);
    return changed;
}

void SongEditor::setRegionMenuActions(const QList<QAction*>& actions) {
    static_cast<CodeTextEdit*>(edit_)->setExtraMenuActions(actions);
}
