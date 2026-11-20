#include "ScpiConsoleWidget.h"

#include "Instrument/VnaDevice.h"
#include "LogWidget.h"
#include "Scpi/ScpiCommandCatalog.h"
#include "Scpi/ScpiParse.h"
#include "Theme.h"

#include <QCheckBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

ScpiConsoleWidget::ScpiConsoleWidget(VnaDevice* device, QWidget* parent)
    : QWidget(parent)
    , _device(device)
{
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(2, 2, 2, 2);

    _log = new LogWidget;
    col->addWidget(_log, 1);

    auto* row = new QHBoxLayout;
    _input = new QLineEdit;
    _input->setPlaceholderText(tr("SCPI command, e.g. VNA:ACQ:POINTS?   (Up/Down for history)"));
    _input->installEventFilter(this);
    auto* sendButton = new QPushButton(tr("Send"));
    row->addWidget(_input, 1);
    row->addWidget(sendButton);
    col->addLayout(row);

    auto* options = new QHBoxLayout;
    _showApp = new QCheckBox(tr("Show application traffic"));
    _showPolling = new QCheckBox(tr("Show sweep and polling traffic"));
    options->addWidget(_showApp);
    options->addWidget(_showPolling);
    options->addStretch(1);
    col->addLayout(options);

    // Completer: the command without its argument hint; the tooltip carries
    // the summary and where the knowledge came from.
    auto* model = new QStandardItemModel(this);
    for (const ScpiCatalogEntry& e : ScpiCommandCatalog::entries()) {
        QString text = e.syntax;
        const qsizetype hint = text.indexOf(QLatin1String(" <"));
        if (hint >= 0)
            text.truncate(hint + 1);
        auto* item = new QStandardItem(text);
        item->setToolTip(QStringLiteral("%1\n%2\n(%3)")
            .arg(e.syntax, e.summary, ScpiCommandCatalog::provenanceName(e.provenance)));
        model->appendRow(item);
    }
    auto* completer = new QCompleter(model, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    _input->setCompleter(completer);

    connect(_input, &QLineEdit::returnPressed, this, &ScpiConsoleWidget::send);
    connect(sendButton, &QPushButton::clicked, this, &ScpiConsoleWidget::send);
    connect(_device, &VnaDevice::traffic, this, &ScpiConsoleWidget::onTraffic);
    connect(_device, &VnaDevice::userReply, this, &ScpiConsoleWidget::onUserReply);
}

void ScpiConsoleWidget::send()
{
    const QString line = _input->text().trimmed();
    if (line.isEmpty())
        return;
    if (_history.isEmpty() || _history.last() != line)
        _history.append(line);
    _historyAt = int(_history.size());
    _input->clear();

    if (!_device->isReady()) {
        _log->appendMessage(Theme::error(tr("Not connected")));
        return;
    }
    _device->sendRaw(line.toLatin1());
}

void ScpiConsoleWidget::onUserReply(quint64, const ScpiResult& result)
{
    if (result.cancelled) {
        _log->appendMessage(Theme::muted(tr("(not sent)")));
        return;
    }
    // Replies themselves arrive through the traffic stream; a silent success
    // would otherwise leave nothing on screen.
    for (const ScpiReply& r : result.replies)
        if (r.ok && !r.isBlock && r.text.isEmpty())
            _log->appendMessage(Theme::ok(tr("ok")));
}

void ScpiConsoleWidget::onTraffic(const ScpiTrafficRecord& rec)
{
    const bool error = rec.direction == ScpiTrafficDirection::Error;
    if (!rec.fromUser && !error) {
        if (rec.background && !_showPolling->isChecked())
            return;
        if (!rec.background && !_showApp->isChecked())
            return;
    }
    // A failed sweep poll is already reported in the log; do not flood here.
    if (error && rec.background && !_showPolling->isChecked())
        return;

    QString text = QString::fromLatin1(rec.text);
    if (text.size() > 400)
        text = text.left(300) + QStringLiteral(" … (%1 characters)").arg(rec.text.size());

    switch (rec.direction) {
    case ScpiTrafficDirection::Tx:
        _log->appendMessage(Theme::info(QStringLiteral("> ") + text));
        break;
    case ScpiTrafficDirection::Rx:
        _log->appendMessage(QStringLiteral("< %1  %2").arg(text,
            Theme::muted(QStringLiteral("%1 ms").arg(rec.elapsedUs / 1000.0, 0, 'f', 1))));
        break;
    case ScpiTrafficDirection::Error:
        _log->appendMessage(Theme::error(QStringLiteral("! ") + text));
        break;
    case ScpiTrafficDirection::Info:
        _log->appendMessage(Theme::muted(text));
        break;
    }
}

bool ScpiConsoleWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _input && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Up && !_history.isEmpty()) {
            _historyAt = std::max(0, _historyAt - 1);
            _input->setText(_history[_historyAt]);
            return true;
        }
        if (key->key() == Qt::Key_Down && !_history.isEmpty()) {
            _historyAt = std::min(int(_history.size()), _historyAt + 1);
            _input->setText(_historyAt < _history.size() ? _history[_historyAt] : QString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
