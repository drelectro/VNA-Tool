#pragma once

#include "Data/TraceModel.h"

#include <QWidget>

class EngEdit;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

// Markers of the active trace, searches, and the bandwidth search.
//
// Tracking markers and the bandwidth search are re-evaluated on every sweep,
// so their readings stay current without pressing Search again.
class MarkerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MarkerPanel(TraceModel* model, QWidget* parent = nullptr);

    // Adds a marker to the active trace at the peak, or at `x` when given.
    void addMarker(bool atX = false, double x = 0);

private slots:
    void rebuild();
    void updateValues();

private:
    int  selectedNumber() const;
    void search(TraceModel::Search kind);

    TraceModel* _model = nullptr;
    bool _refreshing = false;

    QTableWidget* _table = nullptr;
    QComboBox* _deltaRef = nullptr;
    QCheckBox* _coupled = nullptr;
    QDoubleSpinBox* _target = nullptr;
    QDoubleSpinBox* _prominence = nullptr;

    QCheckBox* _bwEnabled = nullptr;
    QDoubleSpinBox* _bwLevel = nullptr;
    QCheckBox* _bwNotch = nullptr;
    QCheckBox* _bwFromMarker = nullptr;
    QLabel* _bwResult = nullptr;
};
