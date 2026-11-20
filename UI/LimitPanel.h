#pragma once

#include "Data/TraceModel.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QTableWidget;

// Limit lines for the active trace: a table of straight segments, each an
// upper or a lower bound, tested on every sweep.
class LimitPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LimitPanel(TraceModel* model, QWidget* parent = nullptr);

private slots:
    void rebuild();
    void updateVerdict();

private:
    void commitTable();

    TraceModel* _model = nullptr;
    bool _refreshing = false;
    QCheckBox* _enabled = nullptr;
    QTableWidget* _table = nullptr;
    QLabel* _verdict = nullptr;
};
