#include "ScorePreviewPane.hpp"

#include "ScoreRunner.hpp"
#include "convert.hpp"

#include <QDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QEvent>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

// The SVG, as wide as the view (but at most twice its own size, so a short score is not blown
// up), with the height that keeps its proportions.
class SvgCanvas : public QWidget {
public:
    explicit SvgCanvas(QWidget* parent = nullptr) : QWidget(parent), renderer_(this) {
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    bool load(const QByteArray& svg) {
        QSvgRenderer probe;
        if (!probe.load(svg) || !probe.isValid() || probe.defaultSize().isEmpty())
            return false;
        svg_ = svg;
        renderer_.load(recolored(svg_));
        updateGeometry();
        update();
        return true;
    }
    void clear() {
        svg_.clear();
        renderer_.load(QByteArray());
        updateGeometry();
        update();
    }
    bool hasImage() const { return renderer_.isValid() && !renderer_.defaultSize().isEmpty(); }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (!hasImage())
            return 0;
        const QSize size = renderer_.defaultSize();
        return shownWidth(width) * size.height() / size.width();
    }
    QSize sizeHint() const override { return hasImage() ? renderer_.defaultSize() : QSize(0, 0); }

protected:
    void changeEvent(QEvent* event) override {
        // A new colour scheme: the score follows it.
        if (event->type() == QEvent::PaletteChange && !svg_.isEmpty()) {
            renderer_.load(recolored(svg_));
            update();
        }
        QWidget::changeEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        if (!hasImage())
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int width = shownWidth(this->width());
        renderer_.render(&painter, QRectF(0, 0, width, heightForWidth(this->width())));
    }

private:
    int shownWidth(int available) const {
        return std::max(1, std::min(available, renderer_.defaultSize().width() * 2));
    }

    // The score in the colours of the pane: LilyPond draws everything in currentColor (black by
    // default) except the white patches it puts behind tablature fret numbers so the staff line
    // does not cross them. Those get the background colour and currentColor the text colour, so
    // a dark colour scheme shows a light score without white squares.
    QByteArray recolored(const QByteArray& svg) const {
        const QByteArray ink = palette().color(QPalette::WindowText).name().toLatin1();
        const QByteArray paper = palette().color(QPalette::Window).name().toLatin1();
        QByteArray out = svg;
        for (const char* white : {"color=\"rgb(100.0000%, 100.0000%, 100.0000%)\"",
                                  "color=\"#ffffff\"", "color=\"white\""})
            out.replace(white, "color=\"" + paper + "\"");
        if (const auto root = out.indexOf("<svg"); root >= 0)
            out.insert(root + 4, " color=\"" + ink + "\"");
        return out;
    }

    QByteArray svg_; // as LilyPond wrote it
    QSvgRenderer renderer_;
};

ScorePreviewPane::ScorePreviewPane(QWidget* parent)
    : QWidget(parent), rebuild_(new QToolButton), logButton_(new QToolButton), status_(new QLabel),
      scroll_(new QScrollArea), canvas_(new SvgCanvas), errors_(new QListWidget) {
    rebuild_->setText(tr("Rebuild"));
    if (QIcon::hasThemeIcon(QStringLiteral("view-refresh"))) {
        rebuild_->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
        rebuild_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    rebuild_->setToolTip(tr("Rebuild the score (Ctrl+R)"));
    rebuild_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));

    logButton_->setText(tr("Log"));
    logButton_->setEnabled(false);

    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setMinimumWidth(0);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 4, 4, 0);
    toolbar->addWidget(rebuild_);
    toolbar->addWidget(status_, 1);
    toolbar->addWidget(logButton_);

    scroll_->setWidget(canvas_);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);

    errors_->setWordWrap(true);
    errors_->setMaximumHeight(errors_->fontMetrics().lineSpacing() * 6 + 8);
    errors_->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(toolbar);
    layout->addWidget(scroll_, 1);
    layout->addWidget(errors_);

    connect(rebuild_, &QToolButton::clicked, this, &ScorePreviewPane::rebuildRequested);
    connect(logButton_, &QToolButton::clicked, this, [this] { showLog(); });
    connect(errors_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const QVariant position = item->data(Qt::UserRole);
        if (position.isValid())
            emit errorActivated(position.toPoint().y(), position.toPoint().x());
    });
}

void ScorePreviewPane::showBuilding() {
    status_->setStyleSheet({});
    status_->setText(tr("Building…"));
    errors_->clear();
    errors_->hide();
}

void ScorePreviewPane::showResult(const ScoreResult& result) {
    log_ = result.log;
    logButton_->setEnabled(!log_.isEmpty());

    bool success = result.success;
    QString message = result.message;
    if (success && !canvas_->load(result.svg)) {
        success = false;
        message = tr("The build produced an image that cannot be displayed.");
    }
    if (success) {
        status_->setStyleSheet({});
        status_->setText(
            tr("Built in %1 s").arg(static_cast<double>(result.elapsedMs) / 1000.0, 0, 'f', 1));
        errors_->clear();
        errors_->hide();
        return;
    }

    QString text = tr("Build failed");
    if (canvas_->hasImage())
        text += tr(" — showing the last successful build");
    status_->setText(text);
    status_->setStyleSheet(QStringLiteral("color: #c0392b;"));

    errors_->clear();
    for (const zub::score::Error& error : result.errors) {
        const bool located = error.file == result.scoreCopy;
        const QString head = located
                                 ? tr("Line %1: %2").arg(error.line).arg(toQString(error.message))
                                 : QStringLiteral("%1:%2: %3")
                                       .arg(toQString(error.file))
                                       .arg(error.line)
                                       .arg(toQString(error.message));
        auto* item = new QListWidgetItem(head, errors_);
        if (located) {
            item->setData(Qt::UserRole, QPoint(error.column, error.line));
            item->setToolTip(tr("Double-click to jump to the line"));
        }
    }
    if (errors_->count() == 0)
        new QListWidgetItem(message, errors_);
    errors_->show();
}

void ScorePreviewPane::showBlocked(const QString& message) {
    canvas_->clear();
    log_.clear();
    logButton_->setEnabled(false);
    status_->setStyleSheet({});
    status_->setText(tr("Not built"));
    errors_->clear();
    new QListWidgetItem(message, errors_);
    errors_->show();
}

void ScorePreviewPane::clear() {
    canvas_->clear();
    status_->setStyleSheet({});
    status_->clear();
    errors_->clear();
    errors_->hide();
    log_.clear();
    logButton_->setEnabled(false);
}

void ScorePreviewPane::showLog() {
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
}
