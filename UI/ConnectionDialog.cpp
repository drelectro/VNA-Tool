#include "ConnectionDialog.h"

#include "Theme.h"
#include "Utils/AppSettings.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ConnectionDialog::ConnectionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Connect to instrument"));

    auto* col = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("On the instrument, enable <b>System → SCPI</b> in VNStudio and note "
                               "the address and port shown there. VNStudio must stay running."));
    hint->setWordWrap(true);
    col->addWidget(hint);

    const ScpiEndpoint last = lastEndpoint();
    auto* form = new QFormLayout;
    _host = new QLineEdit(last.host);
    _host->setPlaceholderText(QStringLiteral("192.168.0.155"));
    _port = new QSpinBox;
    _port->setRange(1, 65535);
    _port->setValue(last.isValid() ? last.port : kDefaultScpiPort);
    form->addRow(tr("Address"), _host);
    form->addRow(tr("Port"), _port);
    col->addLayout(form);

    auto* buttons = new QDialogButtonBox;
    auto* connectButton = buttons->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
    auto* simButton = buttons->addButton(tr("Use simulator"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    simButton->setToolTip(tr("A built-in stand-in for the instrument, measuring a simulated band-pass filter"));
    col->addWidget(buttons);

    connect(connectButton, &QPushButton::clicked, this, [this] {
        if (_host->text().trimmed().isEmpty()) {
            _host->setFocus();
            return;
        }
        _endpoint = ScpiEndpoint{};
        _endpoint.host = _host->text().trimmed();
        _endpoint.port = static_cast<quint16>(_port->value());
        accept();
    });
    connect(simButton, &QPushButton::clicked, this, [this] {
        _endpoint = ScpiEndpoint{};
        _endpoint.simulated = true;
        _endpoint.host = QStringLiteral("sim");
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connectButton->setDefault(true);
}

ScpiEndpoint ConnectionDialog::lastEndpoint()
{
    ScpiEndpoint e;
    e.host = AppSettings::value(QStringLiteral("Connect/host")).toString();
    e.port = static_cast<quint16>(AppSettings::value(QStringLiteral("Connect/port"), kDefaultScpiPort).toUInt());
    return e;
}

void ConnectionDialog::rememberEndpoint(const ScpiEndpoint& endpoint)
{
    if (endpoint.simulated)
        return;
    AppSettings::setValue(QStringLiteral("Connect/host"), endpoint.host);
    AppSettings::setValue(QStringLiteral("Connect/port"), endpoint.port);
}
