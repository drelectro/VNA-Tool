#pragma once

#include "Data/TraceModel.h"
#include "Instrument/VnaDevice.h"

#include <QMainWindow>

class LogWidget;
class MarkerPanel;
class PlotArea;
class QAction;
class QLabel;

// The application window: plots in the centre, settings in docks around them.
//
// Owns the two long-lived objects everything else hangs off - the VnaDevice
// (the instrument) and the TraceModel (what the PC makes of its data) - and
// wires sweeps from the one into the other.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void connectTo(const ScpiEndpoint& endpoint);
    bool loadWorkspaceFile(const QString& path);

    // Saves an image of the whole window after `sweeps` sweeps have arrived,
    // then quits. Used to check the UI from a script.
    void snapshotAndQuit(const QString& path, int sweeps);
    // Brings the dock with this title to the front of its tab group.
    void raiseDock(const QString& title);

public slots:
    // Resets the display to four plots: S11 and S21 log magnitude, S11 Smith,
    // S11 SWR, S22 log magnitude.
    void applyFourPlotPreset();

public:

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void showConnectDialog();
    void reconnectLast();
    void onDeviceState(VnaDevice::State state, const QString& detail);
    void onSweep(const SweepData& sweep);
    void showPlotMenu(int plotIndex, const QPoint& globalPos, double x, bool xValid,
                      int markerTrace, int markerNumber);

    void openWorkspace();
    void saveWorkspace();
    void openTouchstone();
    void exportS2p();
    void exportS1p(SParam param);
    void exportCsv();
    void saveImage(int plotIndex);
    void copyImage(int plotIndex);
    void setPlotCount(int count);

private:
    void buildDocks();
    void buildMenus();
    void updateStatus();
    void log(const QString& markup);
    QString autosavePath() const;
    QString pickSavePath(const QString& title, const QString& filter, const QString& suffix);

    VnaDevice  _device;
    TraceModel _model;

    PlotArea*    _plots = nullptr;
    LogWidget*   _log = nullptr;
    MarkerPanel* _markers = nullptr;

    QAction* _actContinuous = nullptr;
    QAction* _actHold = nullptr;
    QAction* _actDisconnect = nullptr;
    QList<QAction*> _dockActions;

    QLabel* _statusConnection = nullptr;
    QLabel* _statusSweep = nullptr;
    QLabel* _statusSim = nullptr;

    quint64 _sweepCount = 0;
    QString _snapshotPath;
    int     _snapshotAfter = 0;
};
