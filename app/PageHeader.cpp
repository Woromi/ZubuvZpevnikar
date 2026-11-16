#include "PageHeader.hpp"

#include <QFont>
#include <QLabel>
#include <QLayout>
#include <QToolBar>

QToolBar* createPageHeader(const QString& title, QWidget* parent) {
    auto* header = new QToolBar(title, parent);
    header->setMovable(false);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* heading = new QLabel(title, header);
    QFont font = heading->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.15);
    heading->setFont(font);
    heading->setContentsMargins(4, 0, 12, 0);
    header->addWidget(heading);
    header->layout()->setSpacing(6);
    header->setContentsMargins(6, 4, 6, 0);
    return header;
}
