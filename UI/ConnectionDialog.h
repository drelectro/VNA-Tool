#pragma once

#include "Scpi/ScpiTypes.h"

#include <QDialog>

class QLineEdit;
class QSpinBox;

// Host and port of VNStudio's SCPI server, or the built-in simulator.
// The last address used is remembered.
class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget* parent = nullptr);

    ScpiEndpoint endpoint() const { return _endpoint; }

    // The remembered endpoint, for "Reconnect". Invalid if none was saved.
    static ScpiEndpoint lastEndpoint();
    static void rememberEndpoint(const ScpiEndpoint& endpoint);

private:
    QLineEdit* _host = nullptr;
    QSpinBox*  _port = nullptr;
    ScpiEndpoint _endpoint;
};
