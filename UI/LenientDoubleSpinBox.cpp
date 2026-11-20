#include "LenientDoubleSpinBox.h"

#include <QLocale>
#include <QRegularExpression>

QString LenientDoubleSpinBox::coreText(const QString& text) const
{
    QString core = text;
    if (!prefix().isEmpty() && core.startsWith(prefix()))
        core.remove(0, prefix().size());
    if (!suffix().isEmpty() && core.endsWith(suffix()))
        core.chop(suffix().size());
    return core.trimmed();
}

QValidator::State LenientDoubleSpinBox::validate(QString& text, int& pos) const
{
    const QValidator::State base = QDoubleSpinBox::validate(text, pos);
    if (base != QValidator::Invalid)
        return base;

    // Qt said no. Allow it anyway if it is still the shape of a number: the
    // range check happens on commit, in fixup().
    const QString core = coreText(text);
    const QString point = QRegularExpression::escape(locale().decimalPoint());
    const QRegularExpression shape(
        QStringLiteral("^[+-]?\\d*(?:(?:%1|\\.)\\d{0,%2})?$").arg(point).arg(decimals()));
    return shape.match(core).hasMatch() ? QValidator::Intermediate : QValidator::Invalid;
}

void LenientDoubleSpinBox::fixup(QString& text) const
{
    QDoubleSpinBox::fixup(text);

    // Called on commit when the text is not Acceptable. Leave the text alone
    // so the base class reverts to the previous value, but remember what was
    // refused so the owner can explain instead of the edit just vanishing.
    const QString core = coreText(text);
    bool ok = false;
    double v = locale().toDouble(core, &ok);
    if (!ok)
        v = core.toDouble(&ok);
    if (ok && (v < minimum() || v > maximum()))
        _refused = v;
}

std::optional<double> LenientDoubleSpinBox::takeRefusedValue()
{
    const std::optional<double> r = _refused;
    _refused.reset();
    return r;
}
