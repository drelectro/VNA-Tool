#include "VnaTypes.h"

QString sParamName(SParam p)
{
    switch (p) {
    case SParam::S11: return QStringLiteral("S11");
    case SParam::S21: return QStringLiteral("S21");
    case SParam::S12: return QStringLiteral("S12");
    case SParam::S22: return QStringLiteral("S22");
    }
    return QString();
}

bool sParamFromName(const QString& name, SParam& out)
{
    const QString n = name.trimmed().toUpper();
    if (n == QLatin1String("S11")) { out = SParam::S11; return true; }
    if (n == QLatin1String("S21")) { out = SParam::S21; return true; }
    if (n == QLatin1String("S12")) { out = SParam::S12; return true; }
    if (n == QLatin1String("S22")) { out = SParam::S22; return true; }
    return false;
}

QString sweepModeName(SweepMode m)
{
    switch (m) {
    case SweepMode::LinFreq: return QStringLiteral("Linear frequency");
    case SweepMode::LogFreq: return QStringLiteral("Log frequency");
    case SweepMode::Power:   return QStringLiteral("Power");
    case SweepMode::CW:      return QStringLiteral("CW time");
    }
    return QString();
}
