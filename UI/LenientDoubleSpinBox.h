#pragma once

#include <QDoubleSpinBox>

#include <optional>

// A QDoubleSpinBox that lets you TYPE freely and judges the value on commit.
//
// The stock validator rejects keystrokes, not values. With a range of -130 to
// -10 dBm it refuses "2" the moment it is typed (positive and above the
// maximum), so select-all-and-type "20" silently does nothing, and deleting
// the minus sign to retype it is impossible. Operators read that as a dead
// control.
//
// Here any text that could still become a number — optional sign, digits, one
// decimal point, at most decimals() fraction digits — is accepted as
// Intermediate while it is being edited. On Enter or focus-out the value is
// checked against the range as before: inside it, it is applied; outside it,
// the edit is REFUSED and the box reverts to its previous value, rather than
// clamped. Clamping would turn a forgotten minus sign ("20" for -20 dBm) into
// the maximum output level, which is exactly the mistake a DUT does not want
// made for it. The refused value is kept so the owner can say what happened.

class LenientDoubleSpinBox : public QDoubleSpinBox
{
    Q_OBJECT

public:
    using QDoubleSpinBox::QDoubleSpinBox;

    QValidator::State validate(QString& text, int& pos) const override;
    void fixup(QString& text) const override;

    // The value the last commit refused as out of range, if any. Cleared by
    // the call, so check it from an editingFinished() handler.
    std::optional<double> takeRefusedValue();

private:
    // The text between prefix and suffix, trimmed.
    QString coreText(const QString& text) const;

    mutable std::optional<double> _refused;
};
