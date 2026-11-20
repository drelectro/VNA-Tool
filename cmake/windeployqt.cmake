# windeployqt.cmake — called by the POST_BUILD step in CMakeLists.txt.
# Deploys the Qt runtime next to the executable, and skips the (slow) run when
# everything the exe needs is already there.
#
# DIVERGENCE from XMC_SDRv2: that copy takes a single SENTINEL file and skips
# whenever it exists. That is wrong once a target gains a new Qt module — the
# sentinel is still present, nothing re-deploys, and the exe fails to start on
# a missing DLL. See the comment at the call site in CMakeLists.txt for the
# Qt6OpenGL.dll post-mortem. This copy takes the full expected file list
# instead and re-runs if any one of them is missing.
#
# Expected variables (passed via -D on the cmake command line):
#   WINDEPLOYQT  — full path to windeployqt.exe
#   EXE          — full path to the built executable
#   EXPECTED     — `|`-separated list of files whose presence means "already
#                  deployed"; typically the platform plugin plus every Qt DLL
#                  the exe imports. `|` rather than `;` because a `;` list is
#                  split into separate arguments on the command line.

string(REPLACE "|" ";" _expected_list "${EXPECTED}")

set(_missing "")
foreach(_file IN LISTS _expected_list)
    if(NOT EXISTS "${_file}")
        list(APPEND _missing "${_file}")
    endif()
endforeach()

if(_missing)
    # Name what was missing: it is the only clue that a newly linked Qt module
    # needs adding to _wdqt_modules in CMakeLists.txt.
    foreach(_file IN LISTS _missing)
        get_filename_component(_name "${_file}" NAME)
        message(STATUS "windeployqt: ${_name} not deployed")
    endforeach()
    message(STATUS "windeployqt: deploying Qt files alongside ${EXE}")
    execute_process(
        COMMAND "${WINDEPLOYQT}" --no-compiler-runtime "${EXE}"
        RESULT_VARIABLE _wdqt_result
    )
    if(NOT _wdqt_result EQUAL 0)
        message(WARNING "windeployqt exited with code ${_wdqt_result}")
    endif()
else()
    message(STATUS "windeployqt: Qt already deployed, skipping")
endif()
