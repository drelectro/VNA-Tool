#pragma once

#include <QWidget>
#include <QString>

class QTextBrowser;
class QToolBar;

// Scrolling rich-text log widget.
//
// Accepts plain text or a lightweight BBCode-like markup:
//   [b]bold[/b]          [i]italic[/i]
//   [color=red]…[/color]  [color=#RRGGBB]…[/color]
//
// Plain text is HTML-escaped automatically.
// Newlines in the input become line breaks.
//
// Auto-scroll: the view tracks the bottom of the log unless the user has
// manually scrolled up, in which case it stays in place until the user
// scrolls back to the bottom.

class LogWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LogWidget(QWidget* parent = nullptr);

    // DIVERGENCE from the XMC_SDRv2 original: the SCPI console needs to export
    // its transcript to a file, which the source copy had no way to do.
    QString plainText() const;

    // Connect this to LogRouter::instance()->message() to display log output.
public slots:
    void appendMessage(const QString& text);
    void updateLiveMessage(const QString& text);
    void finishLiveMessage();
    void clear();

private:
    // Converts markup + escapes plain text to an HTML fragment.
    static QString toHtml(const QString& text);
    int appendHtmlLine(const QString& html);

    QTextBrowser* _browser = nullptr;
    QToolBar*     _toolbar = nullptr;
    bool          _liveMessageActive = false;
    QString       _liveMessageText;
    int           _liveMessageBlockNumber = -1;
};
