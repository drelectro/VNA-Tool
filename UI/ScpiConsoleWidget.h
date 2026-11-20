#pragma once

#include "Scpi/ScpiTypes.h"

#include <QStringList>
#include <QWidget>

class LogWidget;
class QCheckBox;
class QLineEdit;
class VnaDevice;

// A raw SCPI console on the same session the application uses.
//
// Typed lines go through the session queue at User priority, so they never
// interleave with a sweep in progress. Set commands are followed by *OPC?
// like everything else (see ScpiSession), which is why a successful one shows
// "ok" rather than nothing.
//
// The completer is fed from the command catalogue; each entry's tooltip says
// whether its behaviour was observed on the instrument or only documented.
class ScpiConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScpiConsoleWidget(VnaDevice* device, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void send();
    void onTraffic(const ScpiTrafficRecord& record);
    void onUserReply(quint64 id, const ScpiResult& result);

private:
    VnaDevice* _device = nullptr;
    LogWidget* _log = nullptr;
    QLineEdit* _input = nullptr;
    QCheckBox* _showApp = nullptr;
    QCheckBox* _showPolling = nullptr;
    QStringList _history;
    int _historyAt = 0;
};
