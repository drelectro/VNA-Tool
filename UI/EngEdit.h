#pragma once

#include <QLineEdit>

// A line edit for engineering values: type "2.4G", "100k", "10 MHz", "3n",
// "1.5e9". Shows the committed value as "2.4 GHz".
//
// The value is judged on commit (Enter or focus-out), never per keystroke,
// and an unparseable entry reverts to the last value. setValue() does not
// disturb text the operator is in the middle of typing.

class EngEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit EngEdit(const QString& unit, QWidget* parent = nullptr);

    double value() const { return _value; }
    void   setValue(double v);

    // Frequency entry treats "m" as mega: nobody types millihertz into a VNA.
    void setLowercaseMIsMega(bool on) { _mIsMega = on; }

    static bool parse(const QString& text, const QString& unit, bool mIsMega, double& out);

signals:
    void valueEdited(double value);

protected:
    void focusOutEvent(QFocusEvent* e) override;

private:
    void commit();
    void showValue();

    QString _unit;
    double  _value = 0;
    bool    _mIsMega = false;
};
