#pragma once

#include <QString>
#include <QVariant>

class QSettings;

// Thin, typed facade over QSettings.
//
// The application relies on QCoreApplication::setOrganizationName() /
// setApplicationName() being called in main() before anything here is used;
// on Windows that puts the store in the registry under
// HKCU\Software\VK2XMC\VNA Tool.
//
// Keys are namespaced strings, e.g. "Window/geometry", "Modules/<serial>/host".
// Prefer the key-building helpers below over hand-written strings so that the
// per-module namespace stays consistent as the instrument layer grows.
//
// This header is part of mt_core and therefore must not reference Qt Widgets.
// Widget geometry and dock-layout persistence lives in UI/UiSettings.h, which
// is built into the GUI target and uses the key builders below.
//
// Threading: main thread only. QSettings is reentrant but not thread-safe,
// and every caller is UI or startup code.

namespace AppSettings {

// The shared QSettings instance. Lives for the life of the process.
QSettings& settings();

QVariant value(const QString& key, const QVariant& defaultValue = {});
void     setValue(const QString& key, const QVariant& v);
bool     contains(const QString& key);
void     remove(const QString& key);
void     sync();

// --- Key builders ---------------------------------------------------------

// Per-module settings are keyed by instrument serial rather than by IP or slot,
// so that a module keeps its layout and preferences if it is moved to another
// slot or re-addressed.
QString moduleKey(const QString& serial, const QString& leaf);
QString windowKey(const QString& windowName, const QString& leaf);

} // namespace AppSettings
