#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace {

// ---------------------------------------------------------------------------
// Style constants (harvested from XMC_SDRv2 UI/VSAControlPanel.cpp:16-100,
// with the VSAControlPanel type selector replaced by an objectName selector so
// the sheet is not tied to one widget class).
// ---------------------------------------------------------------------------

const char* kSidePanelStyle = R"(
    QWidget#instrumentSidePanel {
        background: #1a1a22;
    }
)";

const char* kRunButtonStyle = R"(
    QAbstractButton {
        background: #1f3a1f;
        color: #80e880;
        border: 1px solid #2a5a2a;
        border-radius: 3px;
        text-align: center;
        padding: 3px 6px;
        font-weight: bold;
        min-height: 26px;
    }
    QAbstractButton:hover { background: #254525; }
    QAbstractButton:disabled { background: #202028; color: #707080; border-color: #303040; }
)";

const char* kStopButtonStyle = R"(
    QAbstractButton {
        background: #3a1f1f;
        color: #e88080;
        border: 1px solid #5a2a2a;
        border-radius: 3px;
        text-align: center;
        padding: 3px 6px;
        font-weight: bold;
        min-height: 26px;
    }
    QAbstractButton:hover { background: #452525; }
    QAbstractButton:disabled { background: #202028; color: #707080; border-color: #303040; }
)";

// #8290c0 is Colour::kSection — see the contrast note on it in Theme.h.
const char* kSectionLabelStyle = R"(
    QLabel {
        color: #8290c0;
        font-size: 7pt;
        font-weight: bold;
        padding-left: 4px;
        padding-top: 2px;
    }
)";

// Application-wide sheet. Deliberately small — the Fusion palette does most of
// the work; this only covers the widgets whose Fusion defaults look wrong on a
// dark background (dock titles, tabs, group boxes, tooltips).
const char* kGlobalStyle = R"(
    QMainWindow, QDialog { background: #1a1a22; }

    QDockWidget {
        color: #c4cede;
        titlebar-close-icon: none;
        titlebar-normal-icon: none;
    }
    QDockWidget::title {
        background: #252535;
        padding: 4px 8px;
        border: 1px solid #3c3c58;
        text-align: left;
    }

    QTabBar::tab {
        background: #202030;
        color: #c4cede;
        border: 1px solid #3c3c58;
        padding: 5px 12px;
    }
    QTabBar::tab:selected { background: #2e2e46; border-bottom-color: #6060a0; }
    QTabBar::tab:hover:!selected { background: #282840; }
    QTabWidget::pane { border: 1px solid #3c3c58; }

    QGroupBox {
        border: 1px solid #3c3c58;
        border-radius: 3px;
        margin-top: 10px;
        padding-top: 6px;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 8px;
        padding: 0 4px;
        color: #8290c0;                 /* Colour::kSection */
        font-weight: bold;
    }

    QToolBar { background: #202030; border-bottom: 1px solid #3c3c58; spacing: 4px; }
    QStatusBar { background: #202030; border-top: 1px solid #3c3c58; }
    QStatusBar::item { border: none; }

    QToolTip {
        background: #252535;
        color: #c4cede;
        border: 1px solid #6060a0;
        padding: 3px;
    }

    QLineEdit, QPlainTextEdit, QTextBrowser, QSpinBox, QDoubleSpinBox, QComboBox {
        background: #14141c;
        color: #c4cede;
        border: 1px solid #3c3c58;
        border-radius: 2px;
        selection-background-color: #305090;
    }
    QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
        border-color: #6060a0;
    }

    QPushButton, QToolButton {
        background: #252535;
        color: #c4cede;
        border: 1px solid #3c3c58;
        border-radius: 3px;
        padding: 4px 10px;
    }
    QPushButton:hover, QToolButton:hover { background: #2e2e46; border-color: #6060a0; }
    QPushButton:pressed, QToolButton:pressed { background: #1a1a2c; }
    QPushButton:disabled, QToolButton:disabled { color: #707080; border-color: #303040; }
)";

QString wrap(const QColor& c, const QString& text)
{
    return QStringLiteral("[color=%1]%2[/color]").arg(c.name(), text);
}

} // namespace

namespace Theme {

QString ok(const QString& text)    { return wrap(Colour::kOk,    text); }
QString warn(const QString& text)  { return wrap(Colour::kWarn,  text); }
QString error(const QString& text) { return wrap(Colour::kError, text); }
QString info(const QString& text)  { return wrap(Colour::kInfo,  text); }
QString muted(const QString& text) { return wrap(Colour::kMuted, text); }

const char* sidePanelStyle()    { return kSidePanelStyle; }
const char* runButtonStyle()    { return kRunButtonStyle; }
const char* stopButtonStyle()   { return kStopButtonStyle; }
const char* sectionLabelStyle() { return kSectionLabelStyle; }

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window,          Colour::kWindow);
    p.setColor(QPalette::WindowText,      Colour::kText);
    p.setColor(QPalette::Base,            Colour::kBase);
    p.setColor(QPalette::AlternateBase,   Colour::kAltBase);
    p.setColor(QPalette::ToolTipBase,     Colour::kButton);
    p.setColor(QPalette::ToolTipText,     Colour::kText);
    p.setColor(QPalette::Text,            Colour::kText);
    p.setColor(QPalette::Button,          Colour::kButton);
    p.setColor(QPalette::ButtonText,      Colour::kText);
    p.setColor(QPalette::BrightText,      Colour::kBrightText);
    p.setColor(QPalette::Link,            Colour::kInfo);
    p.setColor(QPalette::Highlight,       Colour::kHighlight);
    p.setColor(QPalette::HighlightedText, Colour::kBrightText);
    p.setColor(QPalette::Mid,             Colour::kBorder);

    p.setColor(QPalette::Disabled, QPalette::Text,        Colour::kDisabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,  Colour::kDisabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText,  Colour::kDisabled);

    app.setPalette(p);
    app.setStyleSheet(QString::fromLatin1(kGlobalStyle));
}

} // namespace Theme
