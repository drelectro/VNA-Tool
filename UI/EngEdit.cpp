#include "EngEdit.h"

#include "Dsp/Formats.h"

#include <QFocusEvent>

EngEdit::EngEdit(const QString& unit, QWidget* parent)
    : QLineEdit(parent)
    , _unit(unit)
{
    _mIsMega = unit == QLatin1String("Hz");
    connect(this, &QLineEdit::returnPressed, this, [this] {
        commit();
        selectAll();
    });
    showValue();
}

void EngEdit::setValue(double v)
{
    _value = v;
    // Leave alone text that is being typed; it is committed or reverted soon.
    if (hasFocus() && isModified())
        return;
    showValue();
}

void EngEdit::showValue()
{
    setText(Formats::engineering(_value, _unit, 10));
    setModified(false);
}

bool EngEdit::parse(const QString& textIn, const QString& unit, bool mIsMega, double& out)
{
    QString t = textIn.trimmed();
    if (t.isEmpty())
        return false;

    // Drop the unit, if typed.
    if (!unit.isEmpty() && t.endsWith(unit, Qt::CaseInsensitive))
        t.chop(unit.size());
    t = t.trimmed();
    if (t.isEmpty())
        return false;

    double scale = 1.0;
    const QChar last = t.back();
    if (last.isLetter() || last == QChar(0x00B5)) {
        // An exponent ("1e9") ends in a digit, so a trailing letter is a prefix.
        switch (last.unicode()) {
        case 'T': scale = 1e12; break;
        case 'G': case 'g': scale = 1e9; break;
        case 'M': scale = 1e6; break;
        case 'm': scale = mIsMega ? 1e6 : 1e-3; break;
        case 'k': case 'K': scale = 1e3; break;
        case 'u': case 0x00B5: scale = 1e-6; break;
        case 'n': scale = 1e-9; break;
        case 'p': scale = 1e-12; break;
        default: return false;
        }
        t.chop(1);
        t = t.trimmed();
    }

    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok)
        return false;
    out = v * scale;
    return true;
}

void EngEdit::commit()
{
    if (!isModified())
        return;
    double v = 0;
    if (parse(text(), _unit, _mIsMega, v) && v != _value) {
        _value = v;
        showValue();
        emit valueEdited(v);
    } else {
        showValue();
    }
}

void EngEdit::focusOutEvent(QFocusEvent* e)
{
    commit();
    QLineEdit::focusOutEvent(e);
}
