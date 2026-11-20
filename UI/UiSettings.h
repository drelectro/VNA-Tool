#pragma once

#include <QString>

class QWidget;
class QMainWindow;

// Widget geometry and dock-layout persistence.
//
// Deliberately separate from Utils/AppSettings: that header is part of mt_core,
// which links Qt Core + Network only so the Bench/ executables stay headless.
// Anything touching QWidget belongs here, in the GUI target.
//
// Keys are produced by AppSettings::windowKey(), so both layers agree on the
// settings namespace.

namespace UiSettings {

// Save/restore a window's outer geometry.
// restoreGeometry() returns false when nothing was stored (first run), letting
// the caller fall back to a sensible default size.
void saveGeometry(const QString& windowName, const QWidget* w);
bool restoreGeometry(const QString& windowName, QWidget* w);

// Save/restore a QMainWindow's dock and toolbar layout.
//
// Every dock and toolbar must have a unique objectName() or Qt cannot restore
// the layout and prints a warning at save time.
void saveWindowState(const QString& windowName, const QMainWindow* w);
bool restoreWindowState(const QString& windowName, QMainWindow* w);

} // namespace UiSettings
