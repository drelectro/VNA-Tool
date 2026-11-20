#include "UiSettings.h"

#include "../Utils/AppSettings.h"

#include <QByteArray>
#include <QMainWindow>
#include <QWidget>

namespace UiSettings {

void saveGeometry(const QString& windowName, const QWidget* w)
{
    if (!w)
        return;
    AppSettings::setValue(AppSettings::windowKey(windowName, QStringLiteral("geometry")),
                          w->saveGeometry());
}

bool restoreGeometry(const QString& windowName, QWidget* w)
{
    if (!w)
        return false;
    const QByteArray blob =
        AppSettings::value(AppSettings::windowKey(windowName, QStringLiteral("geometry")))
            .toByteArray();
    if (blob.isEmpty())
        return false;
    return w->restoreGeometry(blob);
}

void saveWindowState(const QString& windowName, const QMainWindow* w)
{
    if (!w)
        return;
    AppSettings::setValue(AppSettings::windowKey(windowName, QStringLiteral("state")),
                          w->saveState());
}

bool restoreWindowState(const QString& windowName, QMainWindow* w)
{
    if (!w)
        return false;
    const QByteArray blob =
        AppSettings::value(AppSettings::windowKey(windowName, QStringLiteral("state")))
            .toByteArray();
    if (blob.isEmpty())
        return false;
    return w->restoreState(blob);
}

} // namespace UiSettings
