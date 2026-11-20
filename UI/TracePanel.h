#pragma once

#include "Data/TraceModel.h"

#include <QWidget>

#include <functional>

class EngEdit;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;

// The trace list and every per-trace setting: measurement, format, scale,
// memory and math, smoothing, time domain and gating. Port extension and Z0,
// which apply to every trace, sit at the bottom.
class TracePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TracePanel(TraceModel* model, QWidget* parent = nullptr);

private slots:
    void refresh();

private:
    // Applies `fn` to the active trace and recomputes.
    void edit(const std::function<void(TraceConfig&)>& fn, bool structural = false);

    TraceModel* _model = nullptr;
    bool _refreshing = false;

    QTableWidget* _list = nullptr;
    QComboBox* _source = nullptr;
    QComboBox* _param = nullptr;
    QComboBox* _format = nullptr;
    QSpinBox*  _plot = nullptr;
    EngEdit*   _perDiv = nullptr;
    EngEdit*   _refValue = nullptr;
    QSpinBox*  _refPos = nullptr;
    QComboBox* _show = nullptr;
    QComboBox* _math = nullptr;
    QCheckBox* _smoothing = nullptr;
    QDoubleSpinBox* _smoothPercent = nullptr;
    QDoubleSpinBox* _aperture = nullptr;

    QCheckBox* _tdEnabled = nullptr;
    QComboBox* _tdMode = nullptr;
    QComboBox* _tdWindow = nullptr;
    EngEdit*   _tdStart = nullptr;
    EngEdit*   _tdStop = nullptr;
    QCheckBox* _tdDistance = nullptr;
    QDoubleSpinBox* _tdVf = nullptr;
    QLabel*    _tdInfo = nullptr;

    QCheckBox* _gateEnabled = nullptr;
    EngEdit*   _gateStart = nullptr;
    EngEdit*   _gateStop = nullptr;
    QCheckBox* _gateNotch = nullptr;

    QCheckBox* _extEnabled[2] = {nullptr, nullptr};
    EngEdit*   _extDelay[2] = {nullptr, nullptr};
    QDoubleSpinBox* _extLoss[2] = {nullptr, nullptr};
    QDoubleSpinBox* _z0 = nullptr;
};
