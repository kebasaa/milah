cmake_minimum_required(VERSION 3.24)

project(Milah VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

option(MILAH_BUILD_TESTS "Build Milah's test suite" OFF)
option(MILAH_DEPLOY_QT "Run windeployqt after a Windows build" OFF)

# Used by the apparatus test to find the repository's OSIS corpus. The test
# skips itself when this points nowhere useful, so the suite stays runnable
# from a standalone copy of app/.
set(MILAH_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH
    "Repository root containing the data/ corpus")

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(QuaZip-Qt6 CONFIG REQUIRED)

qt_standard_project_setup()

# --------------------------------------------------------------------------
# MilahCore: OSIS parsing, alignment, consensus, serialisation and storage.
# No widgets, so the tests can link it directly.
# --------------------------------------------------------------------------

add_library(MilahCore STATIC
    src/core/types.cpp
    src/core/types.h
    src/core/books.cpp
    src/core/books.h
    src/core/tokenize.cpp
    src/core/tokenize.h
    src/core/diff.cpp
    src/core/diff.h
    src/core/data_paths.cpp
    src/core/data_paths.h
    src/core/lexicon.cpp
    src/core/lexicon.h
    src/core/suggestions.cpp
    src/core/suggestions.h
    src/core/osis.cpp
    src/core/osis.h
    src/core/alignment.cpp
    src/core/alignment.h
    src/core/coverage.cpp
    src/core/coverage.h
    src/core/serialize.cpp
    src/core/serialize.h
    src/core/project.cpp
    src/core/project.h
    src/project_storage.cpp
    src/project_storage.h
)

target_include_directories(MilahCore PUBLIC src)

# --------------------------------------------------------------------------
# Data files
#
# Read from disk rather than compiled in, so a regenerated lexicon or an edited
# rule table can replace the shipped one without rebuilding Milah. They are
# staged beside the binaries here so the application and the tests both find
# them the same way they would in an installed copy; packaging has to carry the
# directory too. See data/README.md for sources, licences and the search order.
# --------------------------------------------------------------------------

set(MILAH_DATA_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/data/hebrew_lexicon.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/hebrew_phrase_rules.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/rabbinic.words.txt"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/data/.staged"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/data"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${MILAH_DATA_FILES} "${CMAKE_CURRENT_BINARY_DIR}/data"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/data/.staged"
    DEPENDS ${MILAH_DATA_FILES}
    COMMENT "Staging the Hebrew data files beside the executables"
    VERBATIM
)
add_custom_target(MilahData ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/data/.staged"
)

target_link_libraries(MilahCore PUBLIC
    Qt6::Core
    QuaZip::QuaZip
)

# --------------------------------------------------------------------------
# Milah: the Qt Widgets application.
# --------------------------------------------------------------------------

qt_add_executable(Milah WIN32
    src/main.cpp
    src/app_controller.cpp
    src/app_controller.h
    src/main_window.cpp
    src/main_window.h
    src/ui/icons.cpp
    src/ui/icons.h
    src/ui/source_settings_widget.cpp
    src/ui/source_settings_widget.h
    src/ui/verse_grid_widget.cpp
    src/ui/verse_grid_widget.h
)

target_link_libraries(Milah PRIVATE
    MilahCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

add_dependencies(Milah MilahData)

# Runtime window / taskbar icon plus the toolbar artwork, embedded so they ship
# inside the binary and are reachable as ":/img/milah.png" (see main.cpp) and
# ":/img/icons/<name>.svg" (see ui/icons.cpp). The SVGs are single-colour and get
# recoloured to the palette on load, so one file serves light and dark.
qt_add_resources(Milah "milah_icons"
    PREFIX "/"
    FILES
        img/milah.png
        img/icons/document-open.svg
        img/icons/document-save.svg
        img/icons/document-save-as.svg
        img/icons/edit-redo.svg
        img/icons/edit-undo.svg
        img/icons/go-next.svg
        img/icons/go-previous.svg
        img/icons/load-manuscript.svg
        img/icons/load-translation.svg
        img/icons/view-refresh.svg
)

# Windows executable file icon (what Explorer shows). The .rc is compiled by the
# RC toolchain, which only exists on Windows builds.
if(WIN32)
    target_sources(Milah PRIVATE img/milah.rc)
endif()

target_compile_definitions(Milah PRIVATE
    MILAH_VERSION="${PROJECT_VERSION}"
)

if(WIN32 AND MILAH_DEPLOY_QT)
    add_custom_command(TARGET Milah POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DMILAH_EXECUTABLE=$<TARGET_FILE:Milah>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deploy_windows.cmake"
        VERBATIM
    )
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------

if(MILAH_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

    foreach(test_name
            project_storage osis consensus apparatus diff acronym lexicon suggestions)
        # Plain add_executable, not qt_add_executable: the latter finalises the
        # target as a Windows GUI application, and a GUI-subsystem binary
        # discards the stdout QTest reports through, leaving a failing test
        # with nothing to show for it.
        add_executable(Milah_${test_name}_test
            tests/${test_name}_test.cpp
            tests/test_data.cpp
            tests/test_data.h
        )
        target_include_directories(Milah_${test_name}_test PRIVATE tests)
        # The data files are read from beside the executable, so the tests
        # exercise exactly the path an installed copy uses.
        add_dependencies(Milah_${test_name}_test MilahData)
        target_link_libraries(Milah_${test_name}_test PRIVATE
            MilahCore
            Qt6::Core
            Qt6::Test
        )
        target_compile_definitions(Milah_${test_name}_test PRIVATE
            MILAH_REPO_ROOT="${MILAH_REPO_ROOT}"
        )
        add_test(NAME Milah_${test_name}_test COMMAND Milah_${test_name}_test)
    endforeach()
endif()
