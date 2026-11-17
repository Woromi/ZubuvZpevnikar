#include "Support.hpp"

#include <QAbstractButton>
#include <QDialog>
#include <QInputDialog>
#include <QTimer>
#include <fstream>
#include <sstream>

namespace apptest {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

Fixture::Fixture(Extras extras) : dir_() {
    namespace fs = std::filesystem;
    root_ = toPath(dir_.path()) / "songbook";
    const fs::path data = ZUB_TEST_DATA_DIR;
    fs::copy(data / "songbook", root_, fs::copy_options::recursive);
    if (extras == Extras::Yes)
        fs::copy(fs::path(ZUB_APP_TEST_DATA_DIR) / "extras", root_,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

bool waitUntil(const std::function<bool()>& condition, int timeoutMs) {
    for (int waited = 0; waited <= timeoutMs; waited += 20) {
        if (condition())
            return true;
        QTest::qWait(20);
    }
    return condition();
}

Responder::Responder(std::function<bool(QWidget*)> matches, std::function<void(QWidget*)> answer)
    : state_(std::make_shared<State>()) {
    auto* timer = new QTimer(QApplication::instance());
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, timer,
                     [state = state_, timer, matches = std::move(matches),
                      answer = std::move(answer)] {
                         if (state->fired)
                             return;
                         for (QWidget* top : QApplication::topLevelWidgets()) {
                             if (!top->isVisible() || !matches(top))
                                 continue;
                             state->fired = true;
                             timer->stop();
                             answer(top);
                             return;
                         }
                     });
    timer->start();
    timer_ = timer;
}

Responder::~Responder() {
    delete timer_;
}

Responder answerMessageBox(QMessageBox::StandardButton button) {
    return Responder([](QWidget* w) { return qobject_cast<QMessageBox*>(w) != nullptr; },
                     [button](QWidget* w) { static_cast<QMessageBox*>(w)->button(button)->click(); });
}

Responder answerMessageBoxText(const QString& buttonText) {
    return Responder([](QWidget* w) { return qobject_cast<QMessageBox*>(w) != nullptr; },
                     [buttonText](QWidget* w) {
                         for (auto* b : static_cast<QMessageBox*>(w)->buttons())
                             if (b->text() == buttonText) {
                                 b->click();
                                 return;
                             }
                     });
}

Responder answerInputDialog(const QString& text) {
    return Responder([](QWidget* w) { return qobject_cast<QInputDialog*>(w) != nullptr; },
                     [text](QWidget* w) {
                         auto* d = static_cast<QInputDialog*>(w);
                         d->setTextValue(text);
                         d->accept();
                     });
}

Responder answerTitledDialog(const QString& title, std::function<void(QDialog*)> answer) {
    return Responder(
        [title](QWidget* w) {
            auto* d = qobject_cast<QDialog*>(w);
            return d && d->windowTitle() == title;
        },
        [answer = std::move(answer)](QWidget* w) { answer(static_cast<QDialog*>(w)); });
}

} // namespace apptest
