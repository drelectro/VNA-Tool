#pragma once

#include <QColor>
#include <QString>

class QApplication;

// Dark instrument theme.
//
// The palette is harvested from XMC_SDRv2's VSAControlPanel, which gives the
// application the same "bench instrument" look as its sibling without pulling
// in SARibbon. Unlike XMC_SDRv2 — where the dark look came from SARibbon's own
// theme plus stylesheets built at runtime from the ribbon palette — everything
// here is centralised: Theme::apply() sets a Fusion dark QPalette plus one
// application-wide stylesheet, and the per-widget QSS constants below are for
// the few places that need a distinct look (run/stop buttons, side panels).

namespace Theme {

// --- Palette --------------------------------------------------------------

namespace Colour {
inline const QColor kWindow      {0x1a, 0x1a, 0x22};   // panel background
inline const QColor kBase        {0x14, 0x14, 0x1c};   // text/entry background
inline const QColor kAltBase     {0x1e, 0x1e, 0x2a};   // alternating rows
inline const QColor kButton      {0x25, 0x25, 0x35};
inline const QColor kText        {0xc4, 0xce, 0xde};
inline const QColor kBrightText  {0xf0, 0xf0, 0xff};
inline const QColor kDisabled    {0x70, 0x70, 0x80};
inline const QColor kBorder      {0x3c, 0x3c, 0x58};
inline const QColor kHighlight   {0x30, 0x50, 0x90};
// Section captions, group-box titles and the explanatory hint labels. Was
// #6070a0, which measures only 3.56:1 against kWindow — under the 4.5:1 WCAG AA
// floor, and it is used at 7pt bold, so it is small text with no large-text
// exemption. #8290c0 is 5.52:1 and still reads as clearly secondary to kText
// (10.89:1). The same hex is repeated in kSectionLabelStyle and
// QGroupBox::title in Theme.cpp — change all three together.
inline const QColor kSection     {0x82, 0x90, 0xc0};   // small section captions

// Semantic — used by status badges, the log, and instrument indicators.
inline const QColor kOk          {0x80, 0xe8, 0x80};   // levelled / pass / RF on
inline const QColor kWarn        {0xe8, 0xc0, 0x60};   // unlevelled / drift / simulated
inline const QColor kError       {0xe8, 0x80, 0x80};   // SCPI error / fail
inline const QColor kInfo        {0x6d, 0xa3, 0xff};   // transmitted commands
inline const QColor kMuted       {0x70, 0x70, 0x80};   // timing / meta
} // namespace Colour

// --- Log markup helpers ---------------------------------------------------
// LogWidget accepts a BBCode-like markup; these wrap text in the right colour
// so call sites do not hard-code hex values.
QString ok(const QString& text);
QString warn(const QString& text);
QString error(const QString& text);
QString info(const QString& text);
QString muted(const QString& text);

// --- Widget stylesheets ---------------------------------------------------
// Applied per-widget where the global sheet is not enough.

// A fixed-width dark side strip. Apply to a widget with
// objectName "instrumentSidePanel".
const char* sidePanelStyle();

// Large green "running" / red "stopped" toggles — used for RF ON/OFF.
const char* runButtonStyle();
const char* stopButtonStyle();

// Small uppercase caption above a group of controls.
const char* sectionLabelStyle();

// --- Application setup ----------------------------------------------------

// Installs the Fusion style, the dark palette and the global stylesheet.
// Call once from main() after constructing QApplication.
void apply(QApplication& app);

} // namespace Theme
