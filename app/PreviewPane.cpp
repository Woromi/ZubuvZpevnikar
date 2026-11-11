#include "PreviewPane.hpp"

#include "PreviewRunner.hpp"
#include "convert.hpp"

#include <QBuffer>
#include <QComboBox>
#include <QDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

PreviewPane::PreviewPane(QWidget* parent)
    : QWidget(parent), classes_(new QComboBox), rebuild_(new QToolButton),
      logButton_(new QToolButton), status_(new QLabel), view_(new QPdfView),
      document_(new QPdfDocument(this)), errors_(new QListWidget) {
    classes_->setToolTip(tr("Document class used for the preview"));

    rebuild_->setText(tr("Rebuild"));
    if (QIcon::hasThemeIcon(QStringLiteral("view-refresh"))) {
        rebuild_->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
        rebuild_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    rebuild_->setToolTip(tr("Rebuild the preview (Ctrl+R)"));

    logButton_->setText(tr("Log"));
    logButton_->setEnabled(false);

    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setMinimumWidth(0);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 4, 4, 0);
    toolbar->addWidget(classes_);
    toolbar->addWidget(rebuild_);
    toolbar->addWidget(status_, 1);
    toolbar->addWidget(logButton_);

    view_->setDocument(document_);
    view_->setPageMode(QPdfView::PageMode::MultiPage);
    view_->setZoomMode(QPdfView::ZoomMode::FitToWidth);

    errors_->setWordWrap(true);
    errors_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    errors_->setMaximumHeight(errors_->fontMetrics().lineSpacing() * 8 + 8);
    errors_->hide();

    // A plain layout, not a splitter: a splitter keeps the zero height the hidden error list
    // had, so showing it later can leave it invisible.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(toolbar);
    layout->addWidget(view_, 1);
    layout->addWidget(errors_);

    auto* shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, &PreviewPane::rebuildRequested);

    connect(classes_, &QComboBox::currentTextChanged, this, &PreviewPane::documentClassChanged);
    connect(rebuild_, &QToolButton::clicked, this, &PreviewPane::rebuildRequested);
    connect(logButton_, &QToolButton::clicked, this, [this] { showLog(); });
    connect(errors_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const QVariant line = item->data(Qt::UserRole);
        if (line.isValid())
            emit errorActivated(errorInputName_, line.toInt());
    });
}

void PreviewPane::setDocumentClasses(const std::vector<std::string>& classes,
                                     const QString& current) {
    const QSignalBlocker blocker(classes_);
    classes_->clear();
    for (const std::string& name : classes)
        classes_->addItem(toQString(name));
    const int index = classes_->findText(current);
    classes_->setCurrentIndex(index >= 0 ? index : (classes_->count() > 0 ? 0 : -1));
}

QString PreviewPane::documentClass() const {
    return classes_->currentText();
}

void PreviewPane::showBuilding() {
    status_->setStyleSheet({});
    status_->setText(tr("Building…"));
    errors_->clear();
    errors_->hide();
}

void PreviewPane::showResult(const PreviewResult& builtResult) {
    PreviewResult result = builtResult;
    log_ = result.log;
    logButton_->setEnabled(!log_.isEmpty());

    if (result.success) {
        // Validate the bytes before they replace the shown document.
        QByteArray bytes = result.pdf;
        QBuffer probeBuffer(&bytes);
        probeBuffer.open(QIODevice::ReadOnly);
        QPdfDocument probe;
        probe.load(&probeBuffer);
        if (probe.status() != QPdfDocument::Status::Ready || probe.pageCount() == 0) {
            result.success = false;
            result.error.reset();
            result.message = tr("The build produced a PDF that cannot be displayed.");
        }
        probe.close();
    }

    if (result.success) {
        const int page = view_->pageNavigator()->currentPage();
        const QPointF location = view_->pageNavigator()->currentLocation();

        auto* buffer = new QBuffer(this);
        buffer->setData(result.pdf);
        buffer->open(QIODevice::ReadOnly);
        document_->load(buffer);
        delete pdfBuffer_;
        pdfBuffer_ = buffer;

        const int pages = document_->pageCount();
        if (pages > 0 && page > 0)
            view_->pageNavigator()->jump(std::min(page, pages - 1), location);

        status_->setStyleSheet({});
        status_->setText(tr("Built in %1 s · %n page(s)", nullptr, pages)
                             .arg(static_cast<double>(result.elapsedMs) / 1000.0, 0, 'f', 1));
        errors_->clear();
        errors_->hide();
        return;
    }

    QString text = tr("Build failed");
    if (document_->status() == QPdfDocument::Status::Ready)
        text += tr(" — showing the last successful build");
    status_->setText(text);
    status_->setStyleSheet(QStringLiteral("color: #c0392b;"));

    errors_->clear();
    errorInputName_ = QString::fromStdString(result.inputName);
    if (result.error) {
        const zub::preview::Error& error = *result.error;
        const QString message = toQString(error.message);
        QString head;
        const bool located = error.inSong && error.line;
        if (located)
            head = tr("Line %1: %2").arg(*error.line).arg(message);
        else if (!error.file.empty() && error.line)
            head = QStringLiteral("%1:%2: %3")
                       .arg(toQString(error.file))
                       .arg(*error.line)
                       .arg(message);
        else
            head = message;

        auto* item = new QListWidgetItem(head, errors_);
        if (located) {
            item->setData(Qt::UserRole, *error.line);
            item->setToolTip(tr("Double-click to jump to the line"));
        }
        if (!error.context.empty()) {
            QStringList lines;
            for (const std::string& line : error.context)
                lines << toQString(line);
            auto* context = new QListWidgetItem(lines.join(u'\n'), errors_);
            context->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            if (located) {
                context->setData(Qt::UserRole, *error.line);
                context->setToolTip(tr("Double-click to jump to the line"));
            }
        }
    } else {
        new QListWidgetItem(result.message, errors_);
    }
    errors_->show();
}

void PreviewPane::clear() {
    document_->close();
    delete pdfBuffer_;
    pdfBuffer_ = nullptr;
    status_->setStyleSheet({});
    status_->clear();
    errors_->clear();
    errors_->hide();
    log_.clear();
    logButton_->setEnabled(false);
}

void PreviewPane::showLog() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Build log"));
    dialog->resize(800, 600);

    auto* text = new QPlainTextEdit;
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(log_);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(text);

    dialog->show();
    text->moveCursor(QTextCursor::End);
    text->ensureCursorVisible();
}
