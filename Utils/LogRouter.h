#pragma once

#include <QObject>
#include <QString>
#include <QMutex>

class QTimer;

// Thread-safe log message router with 50 ms batching.
//
// Any thread (including DSP/dsdcc threads) can call LogRouter::post().
// Messages are accumulated in a mutex-protected buffer and flushed to
// the message(QString) signal on the main thread every 50 ms.
// This prevents flooding the Qt event loop when dsdcc emits text at a
// high rate (e.g. one call per std::endl during active decoding).
//
// Usage:
//   // From any thread:
//   LogRouter::post("Hello [b]world[/b]");
//
//   // In UI setup (main thread):
//   connect(LogRouter::instance(), &LogRouter::message, logWidget, &LogWidget::appendMessage);

class LogRouter : public QObject
{
    Q_OBJECT

public:
    static LogRouter* instance();

    // Thread-safe — safe to call from DSP or dsdcc threads.
    static void post(const char* text);
    static void post(const QString& text);

signals:
    void message(const QString& text);

private slots:
    void flush();

private:
    explicit LogRouter(QObject* parent = nullptr);

    static LogRouter* s_instance;
    QMutex  m_mutex;
    QString m_buffer;
    QTimer* m_timer = nullptr;
};
