#include "MainWindow.h"

#include "ConnectionDialog.h"
#include "Data/Touchstone.h"
#include "Data/Workspace.h"
#include "DevicePanel.h"
#include "LimitPanel.h"
#include "LogWidget.h"
#include "MarkerPanel.h"
#include "PlotArea.h"
#include "ScpiConsoleWidget.h"
#include "StimulusPanel.h"
#include "Theme.h"
#include "TracePanel.h"
#include "UiSettings.h"
#include "Utils/AppSettings.h"
#include "Utils/LogRouter.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace {
const QString kWindowName = QStringLiteral("MainWindow");

int columnsFor(int plots)
{
    return plots <= 2 ? 1 : plots <= 4 ? 2 : plots <= 9 ? 3 : 4;
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("VNA Tool"));
    setDockNestingEnabled(true);

    // Start from the layout the application was closed with.
    Workspace ws;
    QString err;
    if (!WorkspaceIo::load(autosavePath(), ws, err) || ws.traces.isEmpty())
        ws = WorkspaceIo::defaultWorkspace();
    _model.setWorkspace(ws);

    _plots = new PlotArea(&_model, this);
    setCentralWidget(_plots);

    buildDocks();
    buildMenus();

    _statusConnection = new QLabel;
    _statusSweep = new QLabel;
    _statusSim = new QLabel(tr("  SIMULATED  "));
    _statusSim->setStyleSheet(QStringLiteral("background: %1; color: #101018; font-weight: bold;")
                                  .arg(Theme::Colour::kWarn.name()));
    _statusSim->hide();
    statusBar()->addWidget(_statusConnection);
    statusBar()->addWidget(_statusSweep, 1);
    statusBar()->addPermanentWidget(_statusSim);

    connect(&_device, &VnaDevice::stateChanged, this, &MainWindow::onDeviceState);
    connect(&_device, &VnaDevice::sweepReady, this, &MainWindow::onSweep);
    connect(&_device, &VnaDevice::message, this, &MainWindow::log);
    connect(&_device, &VnaDevice::runModeChanged, this, &MainWindow::updateStatus);
    connect(&_device, &VnaDevice::healthChanged, this, &MainWindow::updateStatus);
    connect(LogRouter::instance(), &LogRouter::message, _log, &LogWidget::appendMessage);

    connect(_plots, &PlotArea::stimulusZoomRequested, &_device, &VnaDevice::setStartStop);
    connect(&_model, &TraceModel::structureChanged, this, [this] { _device.setNeededMask(_model.neededMask()); });
    _device.setNeededMask(_model.neededMask());
    connect(_plots, &PlotArea::contextMenuRequested, this, &MainWindow::showPlotMenu);

    if (!UiSettings::restoreGeometry(kWindowName, this))
        resize(1500, 900);
    UiSettings::restoreWindowState(kWindowName, this);

    updateStatus();
    log(tr("Ready. Use File → Connect, or the simulator, to begin."));
}

MainWindow::~MainWindow() = default;

void MainWindow::log(const QString& markup)
{
    _log->appendMessage(markup);
}

QString MainWindow::autosavePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/last.vnaws");
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::buildDocks()
{
    auto addDock = [this](const QString& title, const QString& name, QWidget* w, Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(name);      // needed for saveState()
        dock->setWidget(w);
        addDockWidget(area, dock);
        _dockActions.append(dock->toggleViewAction());
        return dock;
    };

    auto* stimulus = addDock(tr("Stimulus"), QStringLiteral("dockStimulus"),
                             new StimulusPanel(&_device), Qt::RightDockWidgetArea);
    auto* traces = addDock(tr("Traces"), QStringLiteral("dockTraces"),
                           new TracePanel(&_model), Qt::RightDockWidgetArea);
    _markers = new MarkerPanel(&_model);
    auto* markers = addDock(tr("Markers"), QStringLiteral("dockMarkers"), _markers, Qt::RightDockWidgetArea);
    auto* limits = addDock(tr("Limits"), QStringLiteral("dockLimits"),
                           new LimitPanel(&_model), Qt::RightDockWidgetArea);
    auto* device = addDock(tr("Instrument"), QStringLiteral("dockDevice"),
                           new DevicePanel(&_device), Qt::RightDockWidgetArea);
    tabifyDockWidget(stimulus, traces);
    tabifyDockWidget(traces, markers);
    tabifyDockWidget(markers, limits);
    tabifyDockWidget(limits, device);
    stimulus->raise();

    _log = new LogWidget;
    auto* logDock = addDock(tr("Log"), QStringLiteral("dockLog"), _log, Qt::BottomDockWidgetArea);
    auto* console = addDock(tr("SCPI Console"), QStringLiteral("dockConsole"),
                            new ScpiConsoleWidget(&_device), Qt::BottomDockWidgetArea);
    tabifyDockWidget(logDock, console);
    logDock->raise();

    // resizeDocks() only takes effect once the window has been laid out.
    QTimer::singleShot(0, this, [this, stimulus, logDock] {
        resizeDocks({stimulus}, {360}, Qt::Horizontal);
        resizeDocks({logDock}, {150}, Qt::Vertical);
    });
}

void MainWindow::buildMenus()
{
    // --- File ---
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* actConnect = file->addAction(tr("&Connect..."), QKeySequence(Qt::CTRL | Qt::Key_O),
                                          this, &MainWindow::showConnectDialog);
    QAction* actReconnect = file->addAction(tr("&Reconnect last"), QKeySequence(Qt::CTRL | Qt::Key_R),
                                            this, &MainWindow::reconnectLast);
    _actDisconnect = file->addAction(tr("&Disconnect"), &_device, &VnaDevice::disconnectFrom);
    file->addSeparator();
    file->addAction(tr("Open workspace..."), this, &MainWindow::openWorkspace);
    file->addAction(tr("Save workspace..."), QKeySequence(Qt::CTRL | Qt::Key_S), this, &MainWindow::saveWorkspace);
    file->addAction(tr("Reset workspace (one plot)"), this, [this] {
        _model.setWorkspace(WorkspaceIo::defaultWorkspace());
    });
    file->addSeparator();
    file->addAction(tr("Open Touchstone file..."), this, &MainWindow::openTouchstone);
    QMenu* exportMenu = file->addMenu(tr("Export"));
    exportMenu->addAction(tr("Touchstone .s2p..."), this, &MainWindow::exportS2p);
    exportMenu->addAction(tr("Touchstone .s1p (S11)..."), this, [this] { exportS1p(SParam::S11); });
    exportMenu->addAction(tr("Touchstone .s1p (S22)..."), this, [this] { exportS1p(SParam::S22); });
    exportMenu->addAction(tr("CSV..."), this, &MainWindow::exportCsv);
    exportMenu->addSeparator();
    exportMenu->addAction(tr("Image of all plots..."), this, [this] { saveImage(-1); });
    exportMenu->addAction(tr("Copy image of all plots"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                          this, [this] { copyImage(-1); });
    file->addSeparator();
    file->addAction(tr("E&xit"), this, &QWidget::close);

    // --- Sweep ---
    QMenu* sweep = menuBar()->addMenu(tr("&Sweep"));
    auto* runGroup = new QActionGroup(this);
    _actContinuous = sweep->addAction(tr("&Continuous"), QKeySequence(Qt::Key_F5), this, [this] {
        _device.setRunMode(VnaDevice::RunMode::Continuous);
    });
    _actHold = sweep->addAction(tr("&Hold"), QKeySequence(Qt::Key_F6), this, [this] {
        _device.setRunMode(VnaDevice::RunMode::Hold);
    });
    for (QAction* a : {_actContinuous, _actHold}) {
        a->setCheckable(true);
        runGroup->addAction(a);
    }
    _actContinuous->setChecked(true);
    QAction* actSingle = sweep->addAction(tr("&Single"), QKeySequence(Qt::Key_F7), &_device, &VnaDevice::single);
    sweep->addSeparator();
    sweep->addAction(tr("Restart averaging"), &_device, &VnaDevice::restartAveraging);
    sweep->addAction(tr("Re-read instrument settings"), &_device, &VnaDevice::refreshStimulus);

    // --- Trace ---
    QMenu* trace = menuBar()->addMenu(tr("&Trace"));
    QMenu* newTrace = trace->addMenu(tr("New trace"));
    for (int k = 0; k < kSParamCount; ++k) {
        const SParam p = static_cast<SParam>(k);
        newTrace->addAction(sParamName(p), this, [this, p] {
            const TraceConfig* a = _model.activeTrace();
            _model.addTrace(p, TraceFormat::LogMag, a ? a->plot : 0);
        });
    }
    trace->addAction(tr("Remove active trace"), this, [this] { _model.removeTrace(_model.activeIndex()); });
    trace->addSeparator();
    QAction* actAutoAll = trace->addAction(tr("Auto scale all"), QKeySequence(Qt::CTRL | Qt::Key_A),
                                           &_model, &TraceModel::autoScaleAll);
    trace->addAction(tr("Auto scale active"), this, [this] { _model.autoScale(_model.activeIndex()); });
    trace->addAction(tr("Data → Memory"), this, [this] { _model.dataToMemory(_model.activeIndex()); });

    // --- Marker ---
    QMenu* marker = menuBar()->addMenu(tr("&Marker"));
    QAction* actMarker = marker->addAction(tr("Add marker at peak"), QKeySequence(Qt::CTRL | Qt::Key_M),
                                           this, [this] { _markers->addMarker(); });
    marker->addAction(tr("Clear markers on active trace"), this, [this] {
        _model.clearMarkers(_model.activeIndex());
        _model.notifyStructureChanged();
    });
    marker->addAction(tr("Clear all markers"), this, [this] {
        _model.clearMarkers();
        _model.notifyStructureChanged();
    });

    // --- Display ---
    QMenu* display = menuBar()->addMenu(tr("&Display"));
    QAction* actPreset = display->addAction(tr("Preset: four plots"), QKeySequence(Qt::CTRL | Qt::Key_P),
                                            this, &MainWindow::applyFourPlotPreset);
    actPreset->setIconText(tr("Preset"));
    actPreset->setToolTip(tr("Reset the display to four plots: S11 and S21 log magnitude, "
                             "S11 Smith, S11 SWR, S22 log magnitude"));
    display->addSeparator();
    QMenu* plots = display->addMenu(tr("Number of plots"));
    for (const int n : {1, 2, 3, 4, 6, 8, 9, 12})
        plots->addAction(QString::number(n), this, [this, n] { setPlotCount(n); });
    display->addAction(tr("One plot per trace"), this, [this] {
        // Assign first: reducing the plot count removes traces left in the
        // plots that go.
        const int n = std::clamp(_model.traceCount(), 1, 16);
        for (int i = 0; i < _model.traceCount(); ++i)
            _model.edit().traces[i].plot = std::min(i, n - 1);
        _model.edit().plotCount = std::max(_model.workspace().plotCount, n);
        setPlotCount(n);
    });
    display->addSeparator();
    for (QAction* a : _dockActions)
        display->addAction(a);

    // --- Help ---
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Mouse and keyboard"), this, [this] {
        QMessageBox::information(this, tr("Mouse and keyboard"),
            tr("<b>On a plot</b><br>"
               "Click a legend line: make that trace active<br>"
               "Drag a marker: move it<br>"
               "Wheel: scale per division &nbsp; Shift+wheel: reference value<br>"
               "Shift+drag: zoom the sweep (or the time-domain window) to the dragged span<br>"
               "Double-click: maximise or restore the plot<br>"
               "Right-click: menu<br><br>"
               "<b>Entry fields</b> accept 2.4G, 100M, 10k, 3n, 1.5e9<br><br>"
               "F5 continuous &nbsp; F6 hold &nbsp; F7 single &nbsp; Ctrl+M marker &nbsp; Ctrl+A auto scale all"));
    });
    help->addAction(tr("About"), this, [this] {
        QMessageBox::about(this, tr("About VNA Tool"),
            tr("<b>VNA Tool</b><br>Remote front panel for the Harogic PNB-60 vector network analyzer.<br><br>"
               "The instrument supplies calibrated S-parameters over SCPI; formats, markers, limits, "
               "time domain and everything else are computed here."));
    });

    // --- Toolbar ---
    QToolBar* bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("toolbarMain"));
    bar->setMovable(false);
    bar->addAction(actConnect);
    bar->addAction(actReconnect);
    bar->addSeparator();
    bar->addAction(_actContinuous);
    bar->addAction(_actHold);
    bar->addAction(actSingle);
    bar->addSeparator();
    bar->addAction(actPreset);
    bar->addAction(actAutoAll);
    bar->addAction(actMarker);
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void MainWindow::showConnectDialog()
{
    ConnectionDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
        connectTo(dlg.endpoint());
}

void MainWindow::reconnectLast()
{
    const ScpiEndpoint last = ConnectionDialog::lastEndpoint();
    if (last.isValid())
        connectTo(last);
    else
        showConnectDialog();
}

void MainWindow::connectTo(const ScpiEndpoint& endpoint)
{
    _sweepCount = 0;
    // The simulator answers instantly; a little latency makes it behave, and
    // look, like the real thing.
    _device.setSimLatencyMs(endpoint.simulated ? 4 : 0);
    log(tr("Connecting to %1...").arg(endpoint.displayName()));
    _device.connectTo(endpoint);
}

void MainWindow::onDeviceState(VnaDevice::State state, const QString& detail)
{
    if (state == VnaDevice::State::Ready)
        ConnectionDialog::rememberEndpoint(_device.endpoint());
    else if (state == VnaDevice::State::Disconnected && !detail.isEmpty()
             && detail != QLatin1String("disconnected") && detail != QLatin1String("closed"))
        log(Theme::error(detail));
    updateStatus();
}

void MainWindow::updateStatus()
{
    QString text;
    switch (_device.state()) {
    case VnaDevice::State::Disconnected: text = tr("Not connected"); break;
    case VnaDevice::State::Connecting:   text = tr("Connecting..."); break;
    case VnaDevice::State::Identifying:  text = tr("Reading instrument..."); break;
    case VnaDevice::State::Ready:
        text = tr("%1 %2  %3").arg(_device.info().model, _device.info().serial, _device.endpoint().displayName());
        break;
    }
    _statusConnection->setText(QStringLiteral("  %1  ").arg(text));
    _statusSim->setVisible(_device.isReady() && _device.isSimulated());
    _actDisconnect->setEnabled(_device.state() != VnaDevice::State::Disconnected);

    const bool continuous = _device.runMode() == VnaDevice::RunMode::Continuous;
    _actContinuous->setChecked(continuous);
    _actHold->setChecked(!continuous);

    if (!_device.isReady()) {
        _statusSweep->setText(QString());
        return;
    }
    QString sweep = tr("%1   sweep %2").arg(continuous ? tr("Continuous") : tr("Hold")).arg(_sweepCount);
    if (_device.averaging() > 1)
        sweep += tr("   avg %1/%2").arg(_model.liveSweep().averages).arg(_device.averaging());
    const Health& h = _device.health();
    if (h.unlocked || h.adcOverload || h.unlevelled)
        sweep += tr("   CHECK INSTRUMENT STATUS");
    _statusSweep->setText(sweep);
}

void MainWindow::onSweep(const SweepData& sweep)
{
    ++_sweepCount;
    _model.setLiveSweep(sweep);
    updateStatus();

    if (!_snapshotPath.isEmpty() && int(_sweepCount) >= _snapshotAfter) {
        const QString path = _snapshotPath;
        _snapshotPath.clear();
        // Let the last paint land before grabbing.
        QTimer::singleShot(200, this, [this, path] {
            grab().save(path);
            qApp->quit();
        });
    }
}

void MainWindow::raiseDock(const QString& title)
{
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        if (dock->windowTitle().compare(title, Qt::CaseInsensitive) == 0) {
            dock->show();
            dock->raise();
        }
    }
}

void MainWindow::snapshotAndQuit(const QString& path, int sweeps)
{
    _snapshotPath = path;
    _snapshotAfter = std::max(1, sweeps);
}

// ---------------------------------------------------------------------------
// Plot context menu
// ---------------------------------------------------------------------------

void MainWindow::applyFourPlotPreset()
{
    // Keeps loaded files and fixture settings out of it: this is a reset.
    _model.setWorkspace(WorkspaceIo::fourPlotPreset());
    log(tr("Display preset: four plots"));
}

void MainWindow::showPlotMenu(int plotIndex, const QPoint& globalPos, double x, bool xValid,
                              int markerTrace, int markerNumber)
{
    QMenu menu(this);
    const int active = _model.activeIndex();
    const TraceConfig* t = _model.activeTrace();
    const bool inPlot = t && t->plot == plotIndex;

    // Right-clicking a marker offers to delete that one, first in the menu.
    if (markerTrace >= 0) {
        menu.addAction(tr("Delete marker M%1").arg(markerNumber), this, [this, markerTrace, markerNumber] {
            _model.removeMarker(markerTrace, markerNumber);
            _model.notifyStructureChanged();
        });
        menu.addSeparator();
    }

    if (inPlot) {
        if (!t->markers.isEmpty()) {
            QMenu* del = menu.addMenu(tr("Delete marker"));
            for (const Marker& m : t->markers) {
                const int number = m.number;
                del->addAction(QStringLiteral("M%1").arg(number), this, [this, active, number] {
                    _model.removeMarker(active, number);
                    _model.notifyStructureChanged();
                });
            }
        }
        if (xValid)
            menu.addAction(tr("Add marker here"), this, [this, x] { _markers->addMarker(true, x); });
        menu.addAction(tr("Add marker at peak"), this, [this] { _markers->addMarker(); });
        menu.addAction(tr("Clear markers"), this, [this, active] {
            _model.clearMarkers(active);
            _model.notifyStructureChanged();
        });
        menu.addSeparator();
        menu.addAction(tr("Auto scale"), this, [this, active] { _model.autoScale(active); });
    }
    menu.addAction(tr("Auto scale all"), &_model, &TraceModel::autoScaleAll);

    if (inPlot) {
        QMenu* format = menu.addMenu(tr("Format"));
        for (int f = 0; f < kTraceFormatCount; ++f) {
            const TraceFormat tf = static_cast<TraceFormat>(f);
            QAction* a = format->addAction(Formats::name(tf), this, [this, active, tf] {
                _model.setFormat(active, tf);
                _model.notifyStructureChanged();
            });
            a->setCheckable(true);
            a->setChecked(t->format == tf);
        }
        menu.addAction(tr("Data → Memory"), this, [this, active] { _model.dataToMemory(active); });
    }

    QMenu* newTrace = menu.addMenu(tr("New trace here"));
    for (int k = 0; k < kSParamCount; ++k) {
        const SParam p = static_cast<SParam>(k);
        newTrace->addAction(sParamName(p), this, [this, p, plotIndex] {
            _model.addTrace(p, TraceFormat::LogMag, plotIndex);
        });
    }
    if (inPlot)
        menu.addAction(tr("Remove %1").arg(t->name()), this, [this, active] { _model.removeTrace(active); });

    menu.addSeparator();
    if (_model.workspace().plotCount > 1) {
        menu.addAction(tr("Maximise / restore"), this, [this, plotIndex] { _plots->toggleMaximise(plotIndex); });
        menu.addAction(tr("Close this plot and its traces"), this, [this, plotIndex] {
            _model.edit().plotColumns = columnsFor(_model.workspace().plotCount - 1);
            _model.removePlot(plotIndex);
        });
    }
    menu.addAction(tr("Save plot image..."), this, [this, plotIndex] { saveImage(plotIndex); });
    menu.addAction(tr("Copy plot image"), this, [this, plotIndex] { copyImage(plotIndex); });
    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

QString MainWindow::pickSavePath(const QString& title, const QString& filter, const QString& suffix)
{
    const QString dir = AppSettings::value(QStringLiteral("Files/lastDir"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    QString path = QFileDialog::getSaveFileName(this, title, dir, filter);
    if (path.isEmpty())
        return path;
    if (QFileInfo(path).suffix().isEmpty())
        path += QLatin1Char('.') + suffix;
    AppSettings::setValue(QStringLiteral("Files/lastDir"), QFileInfo(path).absolutePath());
    return path;
}

bool MainWindow::loadWorkspaceFile(const QString& path)
{
    Workspace ws;
    QString err;
    if (!WorkspaceIo::load(path, ws, err)) {
        log(Theme::error(err));
        return false;
    }
    _model.setWorkspace(ws);
    log(tr("Loaded workspace %1").arg(path));
    return true;
}

void MainWindow::openWorkspace()
{
    const QString dir = AppSettings::value(QStringLiteral("Files/lastDir")).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open workspace"), dir,
                                                      tr("VNA Tool workspace (*.vnaws)"));
    if (path.isEmpty())
        return;
    AppSettings::setValue(QStringLiteral("Files/lastDir"), QFileInfo(path).absolutePath());
    if (!loadWorkspaceFile(path))
        QMessageBox::warning(this, tr("Open workspace"), tr("Could not open %1. See the log.").arg(path));
}

void MainWindow::saveWorkspace()
{
    const QString path = pickSavePath(tr("Save workspace"), tr("VNA Tool workspace (*.vnaws)"),
                                      QStringLiteral("vnaws"));
    if (path.isEmpty())
        return;
    QString err;
    if (WorkspaceIo::save(path, _model.workspace(), err))
        log(tr("Saved workspace %1").arg(path));
    else
        log(Theme::error(err));
}

void MainWindow::openTouchstone()
{
    const QString dir = AppSettings::value(QStringLiteral("Files/lastDir")).toString();
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open Touchstone file"), dir,
                                                            tr("Touchstone (*.s1p *.s2p)"));
    for (const QString& path : paths) {
        AppSettings::setValue(QStringLiteral("Files/lastDir"), QFileInfo(path).absolutePath());
        SweepData sweep;
        double z0 = 50;
        QString err;
        if (!Touchstone::read(path, sweep, z0, err)) {
            log(Theme::error(err));
            continue;
        }
        const int file = _model.addFile(sweep);
        const TraceConfig* a = _model.activeTrace();
        const int plot = a ? a->plot : 0;
        // Mirror the traces already on screen where the file has the data;
        // a 1-port file only ever yields S11.
        for (int k = 0; k < kSParamCount; ++k) {
            const SParam p = static_cast<SParam>(k);
            if (!sweep.has(p))
                continue;
            if (sweep.mask == 0x0F && !(_model.neededMask() & (1u << k)))
                continue;
            _model.addTrace(p, TraceFormat::LogMag, plot, file);
        }
        log(tr("Loaded %1 (%2 points)").arg(sweep.label).arg(sweep.points()));
    }
}

void MainWindow::exportS2p()
{
    const QString path = pickSavePath(tr("Export .s2p"), tr("Touchstone 2-port (*.s2p)"), QStringLiteral("s2p"));
    if (path.isEmpty())
        return;
    QString err;
    if (Touchstone::writeS2p(path, _model.liveSweep(), _model.workspace().z0, err))
        log(tr("Saved %1").arg(path));
    else
        log(Theme::error(err));
}

void MainWindow::exportS1p(SParam param)
{
    const QString path = pickSavePath(tr("Export .s1p (%1)").arg(sParamName(param)),
                                      tr("Touchstone 1-port (*.s1p)"), QStringLiteral("s1p"));
    if (path.isEmpty())
        return;
    QString err;
    if (Touchstone::writeS1p(path, _model.liveSweep(), param, _model.workspace().z0, err))
        log(tr("Saved %1").arg(path));
    else
        log(Theme::error(err));
}

void MainWindow::exportCsv()
{
    const QString path = pickSavePath(tr("Export CSV"), tr("CSV (*.csv)"), QStringLiteral("csv"));
    if (path.isEmpty())
        return;
    QString err;
    if (Touchstone::writeCsv(path, _model.liveSweep(), err))
        log(tr("Saved %1").arg(path));
    else
        log(Theme::error(err));
}

void MainWindow::saveImage(int plotIndex)
{
    const QString path = pickSavePath(tr("Save image"), tr("PNG image (*.png)"), QStringLiteral("png"));
    if (path.isEmpty())
        return;
    if (_plots->grabPlot(plotIndex).save(path))
        log(tr("Saved %1").arg(path));
    else
        log(Theme::error(tr("Could not write %1").arg(path)));
}

void MainWindow::copyImage(int plotIndex)
{
    QApplication::clipboard()->setPixmap(_plots->grabPlot(plotIndex));
    log(tr("Image copied to the clipboard"));
}

void MainWindow::setPlotCount(int count)
{
    _model.edit().plotColumns = columnsFor(count);
    _model.setPlotCount(count);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    UiSettings::saveGeometry(kWindowName, this);
    UiSettings::saveWindowState(kWindowName, this);

    QString err;
    WorkspaceIo::save(autosavePath(), _model.workspace(), err);

    _device.disconnectFrom();
    e->accept();
}
