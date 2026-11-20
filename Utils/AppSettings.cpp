#include "AppSettings.h"

#include <QSettings>

namespace AppSettings {

QSettings& settings()
{
    // Function-local static: constructed on first use, after main() has set the
    // organisation/application names.
    static QSettings s;
    return s;
}

QVariant value(const QString& key, const QVariant& defaultValue)
{
    return settings().value(key, defaultValue);
}

void setValue(const QString& key, const QVariant& v)
{
    settings().setValue(key, v);
}

bool contains(const QString& key)
{
    return settings().contains(key);
}

void remove(const QString& key)
{
    settings().remove(key);
}

void sync()
{
    settings().sync();
}

// ---------------------------------------------------------------------------
// Key builders
// ---------------------------------------------------------------------------

QString moduleKey(const QString& serial, const QString& leaf)
{
    // A serial should never contain '/', but sanitise anyway — a stray
    // separator would silently create a nested settings group.
    QString safe = serial;
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
    if (safe.isEmpty())
        safe = QStringLiteral("unknown");
    return QStringLiteral("Modules/%1/%2").arg(safe, leaf);
}

QString windowKey(const QString& windowName, const QString& leaf)
{
    return QStringLiteral("Windows/%1/%2").arg(windowName, leaf);
}

} // namespace AppSettings
