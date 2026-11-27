#include "KeyTips.hpp"

#include <QAction>
#include <QObject>
#include <QVariant>

namespace keytips {

namespace {
const char* const shownKeyProperty = "zubShownKey";
const char* const baseTipProperty = "zubBaseTip";
const char* const tipWithKeyProperty = "zubTipWithKey";
} // namespace

QString withKey(const QString& text, const QKeySequence& key) {
    if (key.isEmpty())
        return text;
    return QStringLiteral("%1 (%2)").arg(text, key.toString(QKeySequence::NativeText));
}

void setShownKey(QAction* action, const QKeySequence& key) {
    action->setProperty(shownKeyProperty, QVariant::fromValue(key));
    QString text = action->text();
    if (const auto tab = text.indexOf(QLatin1Char('\t')); tab >= 0)
        text.truncate(tab);
    // A toolbar shows the icon text: the name without the key (and without the mnemonic).
    QString iconText = text;
    iconText.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    iconText.remove(QLatin1Char('&'));
    iconText.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    action->setIconText(iconText);
    if (!key.isEmpty())
        text += QLatin1Char('\t') + key.toString(QKeySequence::NativeText);
    action->setText(text);
}

void apply(QObject* root) {
    for (QAction* action : root->findChildren<QAction*>()) {
        QKeySequence key = action->shortcut();
        if (key.isEmpty())
            key = action->property(shownKeyProperty).value<QKeySequence>();
        if (key.isEmpty())
            continue;
        // The tooltip without a key: the one stored, unless the code set another since.
        QString base = action->toolTip();
        if (base == action->property(tipWithKeyProperty).toString())
            base = action->property(baseTipProperty).toString();
        if (const auto tab = base.indexOf(QLatin1Char('\t')); tab >= 0)
            base.truncate(tab); // a default tooltip is the text, with its shown key
        const QString tip = withKey(base, key);
        action->setProperty(baseTipProperty, base);
        action->setProperty(tipWithKeyProperty, tip);
        action->setToolTip(tip);
    }
}

} // namespace keytips
