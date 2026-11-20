#pragma once

#include "TraceModel.h"

#include <QJsonObject>
#include <QString>

// Saving and loading a Workspace as JSON: plots, traces, scales, markers,
// limit lines, memory traces and loaded files. Instrument settings are not
// part of it; those are saved on the instrument (CLIENT:SETUP:SAVE).

namespace WorkspaceIo {

QJsonObject toJson(const Workspace& ws);
bool fromJson(const QJsonObject& json, Workspace& ws, QString& err);

bool save(const QString& path, const Workspace& ws, QString& err);
bool load(const QString& path, Workspace& ws, QString& err);

// The layout a fresh start gets: one plot, S11 and S21 in log magnitude.
Workspace defaultWorkspace();

// The Preset button's layout: four plots -
//   1: S11 and S21 log magnitude   2: S11 Smith
//   3: S11 SWR                     4: S22 log magnitude
Workspace fourPlotPreset();

} // namespace WorkspaceIo
