#pragma once

class QString;
class QToolBar;
class QWidget;

// The heading bar at the top of a page (Songs, Songbooks): the page's name in bold, then the
// page's actions. Every page makes it here, so they all look the same.
QToolBar* createPageHeader(const QString& title, QWidget* parent);
