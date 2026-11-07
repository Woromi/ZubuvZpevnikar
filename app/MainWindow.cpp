#include "MainWindow.hpp"

#include <QApplication>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QApplication::applicationDisplayName() + QLatin1Char(' ') +
                   QApplication::applicationVersion());
    resize(1200, 800);
    statusBar()->showMessage(tr("Ready"));
}
