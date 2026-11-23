#include "BookPreview.hpp"

#include "SongbookExporter.hpp"
#include "convert.hpp"

#include <QBuffer>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPdfDocument>
#include "PdfLinkView.hpp"
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

BookPreview::BookPreview(SongbookExporter* exporter, QWidget* parent)
    : QWidget(parent), exporter_(exporter) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* row = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    build_ = new QPushButton(tr("Build Preview"), this);
    build_->setToolTip(tr("Build the whole songbook as a PDF (this can take minutes)"));
    cancel_ = new QPushButton(tr("Cancel"), this);
    row->addWidget(status_, 1);
    row->addWidget(build_);
    row->addWidget(cancel_);
    layout->addLayout(row);
    view_ = new PdfLinkView(this);
    document_ = new QPdfDocument(this);
    view_->setDocument(document_);
    view_->setPageMode(QPdfView::PageMode::MultiPage);
    view_->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    layout->addWidget(view_, 1);
    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setMaximumBlockCount(20000);
    log_->hide();
    layout->addWidget(log_, 1);
    connect(build_, &QPushButton::clicked, this, [this] { build(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (process_) {
            cancelled_ = true;
            SongbookExporter::stopBuild(process_);
        }
    });
    updateView();
}

BookPreview::~BookPreview() {
    if (process_) {
        process_->disconnect(this);
        SongbookExporter::stopBuild(process_);
    }
}

void BookPreview::setBook(const QString& id, const std::filesystem::path& file,
                          const std::string& text) {
    id_ = id;
    file_ = file;
    text_ = text;
    updateView(); // the same PDF stays loaded (and scrolled): only the status line changes
}

void BookPreview::setVariant(const QString& variant, const std::string& signature) {
    if (variant_ == variant && signature_ == signature)
        return;
    variant_ = variant;
    signature_ = signature;
    updateView();
}

bool BookPreview::building() const {
    return process_ != nullptr;
}

bool BookPreview::upToDate() const {
    const auto it = results_.find(id_);
    return it != results_.end() && !it->second.pdf.isEmpty() && it->second.text == text_ &&
           it->second.signature == signature_;
}

QByteArray BookPreview::pdf() const {
    const auto it = results_.find(id_);
    return it == results_.end() ? QByteArray() : it->second.pdf;
}

QString BookPreview::report() const {
    const auto it = results_.find(id_);
    return it == results_.end() ? QString() : it->second.report;
}

bool BookPreview::build() {
    if (process_ || id_.isEmpty() || text_.empty())
        return false;
    std::optional<BuildInput> input = BuildInput{text_, {}};
    if (composer_)
        input = composer_(text_);
    if (!input)
        return false;
    const auto command = exporter_->prepareRepoBuild(file_, input->text, input->songs);
    if (!command)
        return false;
    buildingId_ = id_;
    buildingText_ = text_;
    buildingVariant_ = variant_;
    buildingSignature_ = signature_;
    buildingPdf_ = command->pdf;
    buildingLog_ = command->log;
    output_.clear();
    cancelled_ = false;
    log_->clear();
    process_ = SongbookExporter::createBuildProcess(*command, this);
    connect(process_, &QProcess::readyRead, this, [this] {
        const QByteArray bytes = process_->readAll();
        output_ += bytes;
        if (buildingId_ == id_) {
            log_->moveCursor(QTextCursor::End);
            log_->insertPlainText(QString::fromUtf8(bytes));
            log_->moveCursor(QTextCursor::End);
        }
    });
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        onFinished(code, status != QProcess::NormalExit);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onFinished(-1, true);
    });
    SongbookExporter::startBuild(process_, *command);
    updateView();
    return true;
}

void BookPreview::onFinished(int exitCode, bool crashed) {
    if (!process_)
        return;
    output_ += process_->readAll();
    process_->deleteLater();
    process_ = nullptr;
    Result& result = results_[buildingId_];
    result.cancelled = cancelled_;
    QFile pdf(toQString(buildingPdf_));
    const bool ok = !crashed && exitCode == 0 && pdf.open(QIODevice::ReadOnly);
    if (ok) {
        result.pdf = pdf.readAll();
        result.text = buildingText_;
        result.variant = buildingVariant_;
        result.signature = buildingSignature_;
        result.log.clear();
        result.report = SongbookExporter::buildReport(output_, buildingLog_);
    } else if (cancelled_) {
        result.log.clear(); // nothing failed
    } else {
        // The last good PDF stays (marked out of date by its text); the log says why.
        result.log = QString::fromUtf8(output_);
        if (result.log.isEmpty())
            result.log = tr("The build did not start: is bash installed?");
    }
    const QString id = buildingId_;
    buildingId_.clear();
    updateView();
    emit built(id, ok && !result.pdf.isEmpty());
}

void BookPreview::updateView() {
    const bool shown = !id_.isEmpty() && !text_.empty();
    const bool here = process_ && buildingId_ == id_;
    const auto it = results_.find(id_);
    build_->setEnabled(shown && !process_);
    build_->setText(it != results_.end() && !it->second.pdf.isEmpty() ? tr("Rebuild Preview")
                                                                      : tr("Build Preview"));
    cancel_->setVisible(here);
    const QByteArray pdf = it == results_.end() ? QByteArray() : it->second.pdf;
    // The document reads from a buffer: the next build may replace the file.
    if (!buffer_ || buffer_->data() != pdf) {
        document_->close();
        delete buffer_;
        buffer_ = nullptr;
        if (!pdf.isEmpty()) {
            buffer_ = new QBuffer(this);
            buffer_->setData(pdf);
            buffer_->open(QIODevice::ReadOnly);
            document_->load(buffer_);
        }
    }
    const bool failed = it != results_.end() && !it->second.log.isEmpty();
    log_->setVisible(here || failed);
    if (here)
        log_->setPlainText(QString::fromUtf8(output_)); // this build's output only
    else if (failed)
        log_->setPlainText(it->second.log);
    view_->setVisible(!pdf.isEmpty() && !here);
    QString status;
    if (!shown)
        status = QString();
    else if (here)
        status = tr("Building… (a whole songbook takes a minute or more)");
    else if (process_)
        status = tr("Another songbook is being built.");
    else if (failed)
        status = tr("The build failed; its output is below.");
    else if (it != results_.end() && it->second.cancelled && it->second.pdf.isEmpty())
        status = tr("The build was cancelled.");
    else if (pdf.isEmpty())
        status = tr("Not built yet: Build Preview typesets the whole songbook.");
    else if (it->second.text != text_)
        status =
            tr("Out of date: the songbook changed since this preview. %1").arg(it->second.report);
    else if (it->second.signature != signature_)
        status = tr("Out of date: built %1. %2")
                     .arg(it->second.variant.isEmpty() ? tr("without transposing")
                                                       : it->second.variant,
                          it->second.report);
    else if (!variant_.isEmpty())
        status = variant_ + QStringLiteral(". ") + it->second.report;
    else
        status = it->second.report;
    status_->setText(status);
}
