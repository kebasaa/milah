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

    foreach(test_name project_storage osis consensus apparatus)
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
