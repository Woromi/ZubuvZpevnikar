#include "EditorFont.hpp"

#include "CodeTextEdit.hpp"
#include "DiffView.hpp"

#include <QFontDatabase>
#include <QSettings>
#include <QWidget>
#include <algorithm>

namespace editorfont {

namespace {
const auto stepKey = QStringLiteral("editor/sizeStep");
} // namespace

int sizeStep() {
    return QSettings{}.value(stepKey, 0).toInt();
}

QFont font() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const qreal base = font.pointSizeF() > 0 ? font.pointSizeF() : 10.0;
    font.setPointSizeF(std::max(4.0, base + sizeStep()));
    return font;
}

void setSizeStep(int step, QWidget* root) {
    step = std::clamp(step, -6, 12);
    if (step == 0)
        QSettings{}.remove(stepKey);
    else
        QSettings{}.setValue(stepKey, step);
    const QFont newFont = font();
    for (auto* edit : root->findChildren<CodeTextEdit*>())
        edit->setFont(newFont);
    for (auto* diff : root->findChildren<DiffView*>())
        diff->setFont(newFont);
}

} // namespace editorfont
