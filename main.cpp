#include "UI/MainWindow.h"
#include "UI/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>

// VNA Tool - remote front panel for the Harogic PNB-60.
//
//   VNA_Tool                              start disconnected
//   VNA_Tool --sim                        start on the built-in simulator
//   VNA_Tool --connect 192.168.0.155:5973 connect at startup
//   VNA_Tool --workspace file.vnaws       load a saved layout
//   VNA_Tool --preset                     start with the four-plot preset
//   VNA_Tool --sim --snapshot out.png     save a window image after a few
//                                         sweeps and exit (for scripted checks)

int main(int argc, char** argv)
{
    // Makes qDebug/qWarning lines clickable in the Visual Studio output pane.
    qSetMessagePattern(QStringLiteral("%{file}(%{line}): %{message}"));

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VK2XMC"));
    QCoreApplication::setApplicationName(QStringLiteral("VNA Tool"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Remote front panel for the Harogic PNB-60 VNA"));
    parser.addHelpOption();
    const QCommandLineOption simOpt(QStringLiteral("sim"), QStringLiteral("Connect to the built-in simulator."));
    const QCommandLineOption connectOpt(QStringLiteral("connect"),
        QStringLiteral("Connect to host[:port] at startup."), QStringLiteral("host:port"));
    const QCommandLineOption workspaceOpt(QStringLiteral("workspace"),
        QStringLiteral("Load a workspace file."), QStringLiteral("file"));
    const QCommandLineOption snapshotOpt(QStringLiteral("snapshot"),
        QStringLiteral("Save a window image to <file> after a few sweeps, then exit."), QStringLiteral("file"));
    const QCommandLineOption snapshotSweepsOpt(QStringLiteral("snapshot-sweeps"),
        QStringLiteral("Sweeps to wait for before --snapshot (default 4)."), QStringLiteral("n"), QStringLiteral("4"));
    const QCommandLineOption dockOpt(QStringLiteral("show-dock"),
        QStringLiteral("Bring the dock with this title to the front (e.g. Traces, \"SCPI Console\")."),
        QStringLiteral("title"));
    const QCommandLineOption presetOpt(QStringLiteral("preset"),
        QStringLiteral("Start with the four-plot display preset."));
    parser.addOptions({simOpt, connectOpt, workspaceOpt, snapshotOpt, snapshotSweepsOpt, dockOpt, presetOpt});
    parser.process(app);

    Theme::apply(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/Resources/vna.ico")));

    MainWindow window;
    if (parser.isSet(workspaceOpt))
        window.loadWorkspaceFile(parser.value(workspaceOpt));
    if (parser.isSet(presetOpt))
        window.applyFourPlotPreset();
    window.show();
    for (const QString& title : parser.values(dockOpt))
        QTimer::singleShot(0, &window, [&window, title] { window.raiseDock(title); });

    if (parser.isSet(snapshotOpt))
        window.snapshotAndQuit(parser.value(snapshotOpt), parser.value(snapshotSweepsOpt).toInt());

    if (parser.isSet(simOpt)) {
        ScpiEndpoint ep;
        ep.simulated = true;
        ep.host = QStringLiteral("sim");
        QTimer::singleShot(0, &window, [&window, ep] { window.connectTo(ep); });
    } else if (parser.isSet(connectOpt)) {
        const QString target = parser.value(connectOpt);
        ScpiEndpoint ep;
        const qsizetype colon = target.lastIndexOf(QLatin1Char(':'));
        ep.host = colon > 0 ? target.left(colon) : target;
        if (colon > 0)
            ep.port = target.mid(colon + 1).toUShort();
        QTimer::singleShot(0, &window, [&window, ep] { window.connectTo(ep); });
    }

    return app.exec();
}
