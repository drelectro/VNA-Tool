#include "LogWidget.h"

#include <QTextBrowser>
#include <QToolBar>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QAction>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextBlock>

LogWidget::LogWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    _toolbar = new QToolBar(this);
    _toolbar->setIconSize(QSize(16, 16));
    auto* clearAction = _toolbar->addAction("Clear");
    connect(clearAction, &QAction::triggered, this, &LogWidget::clear);
    layout->addWidget(_toolbar);

    _browser = new QTextBrowser(this);
    _browser->setReadOnly(true);
    _browser->setOpenLinks(false);
    _browser->setUndoRedoEnabled(false);
    // Use a monospace font so decoder output aligns nicely.
    QFont f("Consolas", 9);
    f.setStyleHint(QFont::Monospace);
    _browser->setFont(f);
    layout->addWidget(_browser);
}

void LogWidget::appendMessage(const QString& text)
{
    QScrollBar* sb = _browser->verticalScrollBar();
    const bool atBottom = (sb->value() >= sb->maximum() - 4);

    // The batch may contain multiple newline-separated messages from LogRouter.
    // append() inserts each call as a new paragraph, so split and iterate.
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (QString line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;

        if (_liveMessageActive && line == _liveMessageText) {
            finishLiveMessage();
            continue;
        }
        finishLiveMessage();
        appendHtmlLine(toHtml(line));
    }

    if (atBottom)
        sb->setValue(sb->maximum());
}

void LogWidget::updateLiveMessage(const QString& text)
{
    if (text.isEmpty()) return;

    QScrollBar* sb = _browser->verticalScrollBar();
    const bool atBottom = (sb->value() >= sb->maximum() - 4);
    const QString html = toHtml(text);

    if (!_liveMessageActive) {
        _liveMessageBlockNumber = appendHtmlLine(html);
    } else {
        const QTextBlock block = _browser->document()->findBlockByNumber(_liveMessageBlockNumber);
        if (!block.isValid()) {
            _liveMessageBlockNumber = appendHtmlLine(html);
        } else {
            QTextCursor cursor(block);
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.insertHtml(html);
        }
    }

    _liveMessageActive = true;
    _liveMessageText = text;

    if (atBottom)
        sb->setValue(sb->maximum());
}

void LogWidget::finishLiveMessage()
{
    _liveMessageActive = false;
    _liveMessageText.clear();
    _liveMessageBlockNumber = -1;
}

void LogWidget::clear()
{
    _browser->clear();
    finishLiveMessage();
}

int LogWidget::appendHtmlLine(const QString& html)
{
    QTextCursor cursor(_browser->document());
    cursor.movePosition(QTextCursor::End);
    if (!_browser->document()->isEmpty())
        cursor.insertBlock();

    const int blockNumber = cursor.blockNumber();
    cursor.insertHtml(html);
    return blockNumber;
}

// ---------------------------------------------------------------------------
// Markup parser
// ---------------------------------------------------------------------------
// Converts a string with optional [b], [i], [color=X] tags to an HTML
// fragment suitable for insertion into a QTextBrowser document.
// Plain text segments are HTML-escaped; tags are translated to inline HTML.
// ---------------------------------------------------------------------------

static QString htmlEscape(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        switch (c.unicode()) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

QString LogWidget::toHtml(const QString& text)
{
    static const QRegularExpression tagRe(
        R"(\[(/?)(\w+)(?:=([^\]]*))?\])",
        QRegularExpression::CaseInsensitiveOption
    );

    QString html;
    html.reserve(text.size() * 2);

    int pos = 0;
    auto it = tagRe.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        // Append escaped plain text before this tag — escape first, then convert newlines.
        QString plain = text.mid(pos, m.capturedStart() - pos);
        html += htmlEscape(plain).replace("\r\n", "<br>").replace('\r', "<br>").replace('\n', "<br>");
        pos = m.capturedEnd();

        const bool closing = (m.captured(1) == "/");
        const QString tag  = m.captured(2).toLower();
        const QString arg  = m.captured(3);

        if (tag == "b") {
            html += closing ? "</b>" : "<b>";
        } else if (tag == "i") {
            html += closing ? "</i>" : "<i>";
        } else if (tag == "color" || tag == "colour") {
            if (closing) {
                html += "</span>";
            } else if (!arg.isEmpty()) {
                html += "<span style='color:" + htmlEscape(arg) + "'>";
            }
        } else {
            // Unknown tag — pass through as escaped text.
            html += htmlEscape(m.captured(0));
        }
    }

    // Append any remaining plain text after the last tag.
    html += htmlEscape(text.mid(pos)).replace("\r\n", "<br>").replace('\r', "<br>").replace('\n', "<br>");

    // Each message is already its own paragraph via append() — no wrapper needed.
    return html;
}

QString LogWidget::plainText() const
{
    // DIVERGENCE from the XMC_SDRv2 original: added so the SCPI console can
    // save its transcript. A bench session that discovers something is worth
    // very little if it cannot be written down.
    return _browser ? _browser->toPlainText() : QString();
}
