#include "ScpiTypes.h"

QString scpiSessionStateName(ScpiSessionState s)
{
    switch (s) {
    case ScpiSessionState::Disconnected: return QStringLiteral("Disconnected");
    case ScpiSessionState::Connecting:   return QStringLiteral("Connecting");
    case ScpiSessionState::Ready:        return QStringLiteral("Ready");
    case ScpiSessionState::Closing:      return QStringLiteral("Closing");
    }
    return QStringLiteral("?");
}
