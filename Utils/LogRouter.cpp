#include "LogRouter.h"
#include <QTimer>
#include <QMutexLocker>

LogRouter* LogRouter::s_instance = nullptr;

LogRouter::LogRouter(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(50); // flush at most every 50 ms
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &LogRouter::flush);
    m_timer->start();
}

LogRouter* LogRouter::instance()
{
    if (!s_instance)
        s_instance = new LogRouter();
    return s_instance;
}

void LogRouter::post(const char* text)
{
    if (text && *text)
        post(QString::fromUtf8(text));
}

void LogRouter::post(const QString& text)
{
    if (text.isEmpty())
        return;
    QMutexLocker lock(&instance()->m_mutex);
    instance()->m_buffer += text + '\n';
}

void LogRouter::flush()
{
    QString batch;
    {
        QMutexLocker lock(&m_mutex);
        if (m_buffer.isEmpty())
            return;
        batch.swap(m_buffer);
    }
    emit message(batch);
}
