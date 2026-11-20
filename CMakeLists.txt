cmake_minimum_required(VERSION 3.21)

# CMAKE_CXX_STANDARD must be set before project() so that MSVC feature
# detection under the Visual Studio 18 2026 generator picks up C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(VNA_Tool LANGUAGES CXX)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo" CACHE STRING "" FORCE)

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE        "/O2 /Ob2 /Oi /Ot /GL /DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /INCREMENTAL:NO"         CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Ob1 /Oi /Zi /DNDEBUG"     CACHE STRING "" FORCE)
    add_compile_options(/permissive- /utf-8 /MP /W4)
    # C4127: conditional expression is constant - noisy inside Qt headers.
    add_compile_options(/wd4127)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

# ---------------------------------------------------------------------------
# vna_core - headless engine.
#
# ARCHITECTURAL RULE: nothing under Scpi/, Instrument/, Sim/, Dsp/, Data/ or
# Utils/ may include a header from UI/. vna_core links Qt Core + Gui + Network
# but NOT Qt Widgets, so VnaBench runs with no display. (Gui is needed only
# for QColor in the trace model.)
# ---------------------------------------------------------------------------
set(CORE_SOURCES
    Utils/LogRouter.h       Utils/LogRouter.cpp
    Utils/AppSettings.h     Utils/AppSettings.cpp

    Scpi/ScpiTypes.h        Scpi/ScpiTypes.cpp
    Scpi/ScpiParse.h        Scpi/ScpiParse.cpp
    Scpi/ScpiTransport.h
    Scpi/ScpiTcpTransport.h Scpi/ScpiTcpTransport.cpp
    Scpi/ScpiSimTransport.h Scpi/ScpiSimTransport.cpp
    Scpi/ScpiSession.h      Scpi/ScpiSession.cpp
    Scpi/ScpiCommandCatalog.h Scpi/ScpiCommandCatalog.cpp

    Instrument/VnaTypes.h   Instrument/VnaTypes.cpp
    Instrument/VnaFrame.h   Instrument/VnaFrame.cpp
    Instrument/VnaDevice.h  Instrument/VnaDevice.cpp

    Sim/SimVna.h            Sim/SimVna.cpp

    Dsp/Fft.h               Dsp/Fft.cpp
    Dsp/Formats.h           Dsp/Formats.cpp
    Dsp/TimeDomain.h        Dsp/TimeDomain.cpp
    Dsp/MarkerSearch.h      Dsp/MarkerSearch.cpp

    Data/TraceModel.h       Data/TraceModel.cpp
    Data/TracePipeline.h    Data/TracePipeline.cpp
    Data/Touchstone.h       Data/Touchstone.cpp
    Data/Workspace.h        Data/Workspace.cpp
)

add_library(vna_core STATIC ${CORE_SOURCES})
set_target_properties(vna_core PROPERTIES AUTOMOC ON)
target_include_directories(vna_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vna_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

# ---------------------------------------------------------------------------
# Bench - headless regression executable. Exit 0 = pass, 1 = failures,
# 2 = usage error; --out <json> writes machine-readable results.
# ---------------------------------------------------------------------------
add_executable(VnaBench Bench/VnaBench.cpp)
target_link_libraries(VnaBench PRIVATE vna_core)
set_target_properties(VnaBench PROPERTIES WIN32_EXECUTABLE FALSE)

option(VNA_BUILD_GUI "Build the GUI application" ON)
if(NOT VNA_BUILD_GUI)
    return()
endif()

# ---------------------------------------------------------------------------
# GUI application
# ---------------------------------------------------------------------------
set(UI_SOURCES
    UI/Theme.h                  UI/Theme.cpp
    UI/UiSettings.h             UI/UiSettings.cpp
    UI/LogWidget.h              UI/LogWidget.cpp
    UI/LenientDoubleSpinBox.h   UI/LenientDoubleSpinBox.cpp
    UI/EngEdit.h                UI/EngEdit.cpp
    UI/PlotWidget.h             UI/PlotWidget.cpp
    UI/PlotArea.h               UI/PlotArea.cpp
    UI/StimulusPanel.h          UI/StimulusPanel.cpp
    UI/TracePanel.h             UI/TracePanel.cpp
    UI/MarkerPanel.h            UI/MarkerPanel.cpp
    UI/LimitPanel.h             UI/LimitPanel.cpp
    UI/DevicePanel.h            UI/DevicePanel.cpp
    UI/ScpiConsoleWidget.h      UI/ScpiConsoleWidget.cpp
    UI/ConnectionDialog.h       UI/ConnectionDialog.cpp
    UI/MainWindow.h             UI/MainWindow.cpp
)

# The application icon, wired in twice because Windows and Qt need it from
# different places: Resources/app.rc gives the EXECUTABLE its icon (Explorer,
# task bar, Alt-Tab), Resources/resources.qrc is what
# QApplication::setWindowIcon() uses. Both point at Resources/vna.ico;
# regenerate it with Resources/make_icon.ps1.
set(APP_RESOURCES Resources/resources.qrc)
if(WIN32)
    list(APPEND APP_RESOURCES Resources/app.rc)
endif()

qt_add_executable(VNA_Tool main.cpp ${UI_SOURCES} ${APP_RESOURCES})
set_target_properties(VNA_Tool PROPERTIES WIN32_EXECUTABLE TRUE AUTORCC ON)
target_link_libraries(VNA_Tool PRIVATE vna_core Qt6::Gui Qt6::Widgets)

# ---------------------------------------------------------------------------
# Deployment - windeployqt, re-run only when an expected Qt file is missing.
# Any newly linked Qt module must be added to _wdqt_modules.
# ---------------------------------------------------------------------------
find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${CMAKE_PREFIX_PATH}/bin" ENV QTDIR PATH_SUFFIXES bin)

if(WINDEPLOYQT_EXECUTABLE)
    set(_wdqt_modules Core Gui Widgets Network)
    set(_wdqt_expect "$<TARGET_FILE_DIR:VNA_Tool>/platforms/qwindows$<$<CONFIG:Debug>:d>.dll")
    foreach(_mod IN LISTS _wdqt_modules)
        string(APPEND _wdqt_expect
            "|$<TARGET_FILE_DIR:VNA_Tool>/Qt6${_mod}$<$<CONFIG:Debug>:d>.dll")
    endforeach()

    add_custom_command(TARGET VNA_Tool POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DWINDEPLOYQT=${WINDEPLOYQT_EXECUTABLE}
            -DEXE=$<TARGET_FILE:VNA_Tool>
            -DEXPECTED=${_wdqt_expect}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windeployqt.cmake"
        COMMENT "Running windeployqt (skipped if already deployed)"
        VERBATIM)
else()
    message(WARNING "windeployqt not found - the built exe will not be deployable in place.")
endif()
