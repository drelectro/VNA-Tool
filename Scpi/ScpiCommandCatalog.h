#pragma once

#include <QList>
#include <QString>

// The instrument's command set, for the console's completer and tooltips.
//
// Every entry carries its provenance, because the vendor manual documents only
// part of what *LST? reports and says nothing about how several commands
// behave. See docs/DEVICE_NOTES.md.

enum class ScpiProvenance : quint8 {
    Observed,   // exercised on the real instrument; behaviour recorded
    Listed,     // reported by *LST? but not exercised
    Manual,     // documented, behaviour as documented
};

struct ScpiCatalogEntry
{
    QString        syntax;      // long form with short-form capitals, incl. '?' and argument hint
    QString        summary;
    ScpiProvenance provenance = ScpiProvenance::Manual;
};

namespace ScpiCommandCatalog {
const QList<ScpiCatalogEntry>& entries();
QString provenanceName(ScpiProvenance p);
}
